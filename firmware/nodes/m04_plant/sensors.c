/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "sensors.h"

#include "clock.h"
#include "cyphal.h"
#include "flatfield.h"
#include "frame.h"
#include "i2c.h"
#include "m24c64.h"
#include "mlx90640.h"
#include "module_id.h"
#include "registers.h"
#include "uart.h"

#include "uavcan/node/Heartbeat_1_0.h"      /* Health constants */
#include "uavcan/node/ExecuteCommand_1_0.h" /* command response status */
#include "uavcan/diagnostic/Severity_1_0.h"
#include "uavcan/file/Read_1_1.h"
#include "uavcan/file/Write_1_1.h"
#include "industryflow/greenhouse/plant/CanopyThermalSummary_1_0.h"

#include <string.h>

/* Default subject-ID, unregulated range. ADR-0005 d7 wants it register-
 * configurable (uavcan.pub.<name>.id) with this as the default; baked for now,
 * as on M05, M01 and M02. M05 holds 4096-4102, M01 4112-4121 and M02
 * 4128-4131, so M04 starts at 4144 and leaves M02's block room to grow.
 *
 * ONE subject: the statistics describe one frame, and split across separate SI
 * subjects they would describe no frame in particular (ADR-0005 d11). The field
 * itself is served, not published (M04 spec 10.2). */
#define SUBJ_CANOPY 4144u

/* The served frame, and the trim that is written back over the same mechanism
 * (M04 spec 6.7, 10.2). Fixed paths: there is one of each. */
#define PATH_FRAME     "/plant/frame.bin"
#define PATH_FLATFIELD "/plant/flatfield.bin"

#define PUBLISH_PERIOD_US  1000000u
#define INTERVAL_PERIOD_US 60000000u
#define REPROBE_PERIOD_US  60000000u

/* DS12 section 12.2.2: the accuracy of M04 spec 6.3 holds only after four
 * minutes of thermal stabilization. Frames inside that window are published
 * with stabilized = false and are not measurements of record (M04 spec 8 T4). */
#define STABILIZE_US 240000000u

/* No transaction before 80 ms plus one refresh interval have elapsed from
 * power-on (DS12 12.2.1, M04 spec 10). 125 ms is the 8 Hz subpage interval. */
#define POWER_ON_DELAY_MS 205u

/* A frame whose Ta moved by more than this from the previous frame is not
 * accumulated (M04 spec 10): the compensation is Ta-referenced throughout, and
 * a die temperature that jumped between two frames 250 ms apart means the frame
 * describes two different thermal states. */
#define TA_JUMP_LIMIT_K 2.0f

/* Event rule (M04 spec 10). Both constants are defaults with no basis in any
 * record: no measurement establishes what canopy excursion is worth a frame
 * (O-98). At most one event frame per interval. */
#define EVENT_HOTSPOTS 8u
#define EVENT_DELTA_K  5.0f

#define FAIL_DEGRADED 3u

/* Vendor ExecuteCommand IDs. Each answers something the published record does
 * not carry and a bench operator needs (M04 spec 11 V3, V12, V16). */
#define CMD_DEVICE_ID  1u
#define CMD_FRAME_STATE 2u
#define CMD_FLATFIELD_STATE 3u

/* Deployment constants, all four from the deployment profile over the register
 * interface and never compiled in (M04 spec 10). They are volatile: no register
 * but uavcan.node.id has a store (ADR-0005 d7), so a commissioned node re-takes
 * them at every restart, and the record publishes the values in force.
 *
 * The uncommissioned state is emissivity 1 and Tr = Ta - 8 K -- placeholders,
 * not values: leaf emissivity and reflected apparent temperature are
 * established by no record (O-87). A reflected temperature of zero means "none
 * supplied", which is what Ta - 8 K then stands in for. */
#define REG_EMISSIVITY  "industryflow.greenhouse.plant.emissivity"
#define REG_REFLECTED   "industryflow.greenhouse.plant.reflected_temperature"
#define REG_HOTSPOT_K   "industryflow.greenhouse.plant.hotspot_k"
#define REG_HOTSPOT_MIN "industryflow.greenhouse.plant.hotspot_delta_min"

static float s_emissivity[1] = {1.0f};
static float s_reflected_k[1] = {0.0f}; /* K; 0 = none supplied */
static float s_hotspot_k[1] = {3.0f};
static float s_hotspot_delta_min[1] = {1.0f};

static bool s_u1, s_u2;          /* imager, store */
static bool s_u1_seen, s_u2_seen;
static bool s_restored;
static uint8_t s_fail;           /* consecutive seconds with no usable frame */
static uint8_t s_class_id_byte = 0xFFu;

static uint8_t s_tid;
static uint64_t s_last_pub, s_last_probe;
/* Interval bookkeeping keeps the two clocks apart: the gateway stamp is what
 * the served header carries, the local one is what the free-running boundary is
 * measured on. Mixing them would make a lost time base look like a 50-year
 * interval. */
static uint64_t s_interval_stamp; /* gateway time base, 0 = unknown */
static uint64_t s_interval_local;
static uint64_t s_boot_us;
static uint64_t s_interval_minute;
static bool s_interval_synced;
static bool s_event_used;

static float s_ta_prev;
static bool s_have_ta_prev;
static uint32_t s_frames_dropped;

static uint8_t s_last_health = uavcan_node_Health_1_0_NOMINAL;

/* --- helpers -------------------------------------------------------------- */

static float emissivity_in_force(void)
{
    const float e = s_emissivity[0];
    return ((e > 0.0f) && (e <= 1.0f)) ? e : 1.0f;
}

/* Reflected apparent temperature in degrees Celsius, for the compensation. A
 * register of zero is the uncommissioned state, where DS12 12.1.2 puts Tr at
 * Ta - 8 K. */
static float reflected_c(float ta_c)
{
    return (s_reflected_k[0] > 0.0f) ? (s_reflected_k[0] - 273.15f) : (ta_c - 8.0f);
}

static float reflected_k_in_force(float ta_c)
{
    return reflected_c(ta_c) + 273.15f;
}

static bool stabilized(void)
{
    return (micros64() - s_boot_us) >= STABILIZE_US;
}

static void on_pixel(uint16_t index, float kelvin, void *ctx)
{
    (void)ctx;
    /* The trim is subtracted after the compensation chain and before the
     * statistics and the accumulation (M04 spec 6.7). Zero when no field is in
     * force, which is the uncorrected path, not a different one. */
    frame_add_pixel(index, kelvin - flatfield_correction(index));
}

/* --- file services (M04 spec 6.7, 10.2) ----------------------------------- */

static bool path_is(const uavcan_file_Path_2_0 *p, const char *want)
{
    const size_t len = strlen(want);
    return (p->path.count == len) && (memcmp(p->path.elements, want, len) == 0);
}

static size_t serve_read(uint8_t from_node_id, const uint8_t *req, size_t req_size,
                         uint8_t *resp, size_t resp_capacity)
{
    (void)from_node_id;
    uavcan_file_Read_Request_1_1 rq;
    size_t in = req_size;
    if (uavcan_file_Read_Request_1_1_deserialize_(&rq, req, &in) < 0) {
        return 0u;
    }
    /* Nunavut stropes the field name: `error` collides in the generated C. */
    uavcan_file_Read_Response_1_1 rs;
    memset(&rs, 0, sizeof(rs));

    if (!path_is(&rq.path, PATH_FRAME)) {
        rs._error.value = uavcan_file_Error_1_0_NOT_FOUND;
    } else if (!frame_available()) {
        /* No interval has completed yet: there is no file, which is not the
         * same as an empty one. */
        rs._error.value = uavcan_file_Error_1_0_NOT_FOUND;
    } else {
        rs.data.value.count =
            frame_read((uint32_t)rq.offset, rs.data.value.elements,
                       (uint16_t)uavcan_primitive_Unstructured_1_0_value_ARRAY_CAPACITY_);
    }

    size_t sz = resp_capacity;
    if (uavcan_file_Read_Response_1_1_serialize_(&rs, resp, &sz) < 0) {
        return 0u;
    }
    return sz;
}

static size_t serve_write(uint8_t from_node_id, const uint8_t *req, size_t req_size,
                          uint8_t *resp, size_t resp_capacity)
{
    (void)from_node_id;
    uavcan_file_Write_Request_1_1 rq;
    size_t in = req_size;
    if (uavcan_file_Write_Request_1_1_deserialize_(&rq, req, &in) < 0) {
        return 0u;
    }
    uavcan_file_Write_Response_1_1 rs;
    memset(&rs, 0, sizeof(rs));

    if (!path_is(&rq.path, PATH_FLATFIELD)) {
        rs._error.value = uavcan_file_Error_1_0_NOT_FOUND;
    } else if (!s_u2) {
        rs._error.value = uavcan_file_Error_1_0_IO_ERROR;
    } else if (flatfield_commit_active()) {
        rs._error.value = uavcan_file_Error_1_0_IO_ERROR;
    } else if (rq.data.value.count == 0u) {
        /* The empty write that ends a transfer: validate what was offered and
         * start writing it to U2. A record that fails validation is refused
         * here, before anything reaches the store (M04 spec 6.7, V16). */
        rs._error.value = (flatfield_commit_begin() == 0)
                             ? uavcan_file_Error_1_0_OK
                             : uavcan_file_Error_1_0_INVALID_VALUE;
        if (rs._error.value != uavcan_file_Error_1_0_OK) {
            cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_WARNING,
                              flatfield_state_str());
        }
    } else if (flatfield_stage((uint32_t)rq.offset, rq.data.value.elements,
                               (uint16_t)rq.data.value.count) != 0) {
        rs._error.value = uavcan_file_Error_1_0_FILE_TOO_LARGE;
    } else {
        rs._error.value = uavcan_file_Error_1_0_OK;
    }

    size_t sz = resp_capacity;
    if (uavcan_file_Write_Response_1_1_serialize_(&rs, resp, &sz) < 0) {
        return 0u;
    }
    return sz;
}

/* --- health --------------------------------------------------------------- */

/* U1 IS the board's purpose: E0005 exists to image the canopy, so losing it, or
 * failing to compensate with it, is CAUTION. U2 holds the trim and nothing
 * else; without it the node publishes uncorrected and says so, which is
 * ADVISORY (ADR-0028 d5).
 *
 * A device that was never there at boot is an absent subject, not a fault
 * (ADR-0005 d8) -- the health only moves for something that was seen and then
 * went. */
static void report_health(void)
{
    const bool u1_fault = (s_u1_seen && !s_u1) || (s_u1 && !s_restored) ||
                          (s_u1 && (s_fail >= FAIL_DEGRADED));
    const bool u2_fault = s_u2_seen && !s_u2;

    uint8_t health = uavcan_node_Health_1_0_NOMINAL;
    if (u2_fault || (s_u2 && !flatfield_applied())) {
        health = uavcan_node_Health_1_0_ADVISORY;
    }
    if (u1_fault) {
        health = uavcan_node_Health_1_0_CAUTION;
    }
    cyphal_set_health(health);

    if (health != s_last_health) {
        s_last_health = health;
        if (health == uavcan_node_Health_1_0_NOMINAL) {
            cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_NOTICE, "M04 imager recovered");
        } else {
            cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_WARNING,
                                  "M04 fault, 1=bad, bitmask U1|restore|U2|trim =",
                                  (uint32_t)((u1_fault ? 1u : 0u) |
                                             (s_restored ? 0u : 2u) |
                                             (u2_fault ? 4u : 0u) |
                                             (flatfield_applied() ? 0u : 8u)));
        }
    }
}

/* --- probe ---------------------------------------------------------------- */

/* U1 first: it is the reason the board exists, and its probe is a device-ID
 * read rather than an address ACK (M04 spec 10, V12). U2 answers at 0x50 with
 * no identity register of its own, so its class-ID byte is read and reported
 * instead -- a store programmed for another module class is exactly the
 * module-swap hazard the trim binding exists to catch. */
static void probe(void)
{
    const bool had_u1 = s_u1;
    mlx90640_id_t id;
    s_u1 = mlx90640_present(&id);
    if (s_u1 && !had_u1) {
        if (mlx90640_configure() != 0) {
            s_u1 = false;
        } else if (!mlx90640_restored()) {
            /* The restore borrows the served-frame buffer. A device that
             * disappeared and came back is restored mid-run, so what was being
             * served is dropped first rather than overwritten under a reader. */
            frame_invalidate();
            s_restored = (mlx90640_restore(frame_scratch(),
                                           MLX90640_RESTORE_SCRATCH_WORDS) == 0);
        }
    }
    if (!s_u1) {
        s_restored = false;
    }

    s_u2 = m24c64_present();
    if (s_u2) {
        (void)m24c64_read(M24C64_CLASS_ID_ADDR, &s_class_id_byte, 1u);
    }

    s_u1_seen = s_u1_seen || s_u1;
    s_u2_seen = s_u2_seen || s_u2;
}

/* --- commands ------------------------------------------------------------- */

static uint8_t m04_command(uint16_t command, const uint8_t *param, size_t param_len)
{
    (void)param;
    (void)param_len;

    switch (command) {
    case CMD_DEVICE_ID: {
        /* The 48-bit per-part identifier. It rides in every served frame
         * header, but a bring-up that has not yet produced one still has to
         * record it (M04 spec 11 V12). */
        if (!s_u1) {
            return uavcan_node_ExecuteCommand_Response_1_0_STATUS_BAD_STATE;
        }
        const mlx90640_id_t *id = mlx90640_id();
        for (unsigned i = 0; i < 3u; i++) {
            cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_NOTICE,
                                  "M04 device-ID word =", (uint32_t)id->word[i]);
        }
        return uavcan_node_ExecuteCommand_Response_1_0_STATUS_SUCCESS;
    }
    case CMD_FRAME_STATE:
        /* Frames accepted against frames dropped is the figure V3 logs over an
         * hour: it is what says whether the chunked read is keeping up with the
         * 8 Hz refresh rate, and it is in no published subject (O-91). */
        cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_NOTICE,
                              "M04 frames this interval =",
                              (uint32_t)frame_interval_frames());
        cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_NOTICE,
                              "M04 frames dropped since boot =", s_frames_dropped);
        cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_NOTICE,
                              "M04 served frame sequence =", frame_seq());
        return uavcan_node_ExecuteCommand_Response_1_0_STATUS_SUCCESS;

    case CMD_FLATFIELD_STATE:
        /* Absent, corrupt and foreign are three different things, and the
         * record's single flat_field flag cannot tell them apart. */
        cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_NOTICE, flatfield_state_str());
        cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_NOTICE,
                              "M04 store class-ID byte =", (uint32_t)s_class_id_byte);
        return uavcan_node_ExecuteCommand_Response_1_0_STATUS_SUCCESS;

    default:
        return uavcan_node_ExecuteCommand_Response_1_0_STATUS_BAD_COMMAND;
    }
}

/* --- publication ---------------------------------------------------------- */

static const uint16_t M04_SUBJECTS[] = {SUBJ_CANOPY};

static void publish_summary(const frame_stats_t *st, float ta_c)
{
    industryflow_greenhouse_plant_CanopyThermalSummary_1_0 m = {0};
    m.timestamp.microsecond = cyphal_timestamp_usec();
    m.t_mean = st->t_mean;
    m.t_min = st->t_min;
    m.t_max = st->t_max;
    m.t_sigma = st->t_sigma;
    m.gradient_x = st->gradient_x;
    m.gradient_y = st->gradient_y;
    memcpy(m.hotspot_mask, st->hotspot_mask, sizeof(m.hotspot_mask));
    m.ta = ta_c + 273.15f;
    m.emissivity = emissivity_in_force();
    m.reflected_temperature = reflected_k_in_force(ta_c);
    m.hotspot_k = s_hotspot_k[0];
    m.hotspot_delta_min = s_hotspot_delta_min[0];
    m.defective_pixels = mlx90640_defective_count();
    m.stabilized = stabilized();
    m.flat_field = flatfield_applied();
    m.frame_seq = frame_seq();
    m.frame_available = frame_available();

    uint8_t b[industryflow_greenhouse_plant_CanopyThermalSummary_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (industryflow_greenhouse_plant_CanopyThermalSummary_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish(SUBJ_CANOPY, &s_tid, b, sz);
    }
}

static void fill_meta(frame_meta_t *meta, uint64_t start_us, uint64_t end_us, bool synced)
{
    meta->start_us = start_us;
    meta->end_us = end_us;
    meta->synchronized = synced;
    meta->stabilized = stabilized();
    meta->flat_field = flatfield_applied();
    meta->emissivity = emissivity_in_force();
    meta->reflected_k = reflected_k_in_force(mlx90640_ta());
    meta->ta_mean_k = frame_interval_ta_mean();
    meta->defective = mlx90640_defective_count();
}

/* --- bring-up ------------------------------------------------------------- */

void m04_sensors_init(void)
{
    cyphal_declare_publishers(M04_SUBJECTS,
                              (uint8_t)(sizeof(M04_SUBJECTS) / sizeof(M04_SUBJECTS[0])));
    cyphal_set_command_handler(m04_command);

    if (!registers_add_real32(REG_EMISSIVITY, s_emissivity, 1u) ||
        !registers_add_real32(REG_REFLECTED, s_reflected_k, 1u) ||
        !registers_add_real32(REG_HOTSPOT_K, s_hotspot_k, 1u) ||
        !registers_add_real32(REG_HOTSPOT_MIN, s_hotspot_delta_min, 1u)) {
        cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_WARNING,
                          "M04 deployment-constant registers not available");
    }

    /* The frame is served, and the trim arrives, over the standard file
     * services (ADR-0005 d11, M04 spec 6.7, 10.2). No type is minted for
     * either. */
    if (!cyphal_serve(uavcan_file_Read_1_1_FIXED_PORT_ID_,
                      uavcan_file_Read_Request_1_1_EXTENT_BYTES_, serve_read) ||
        !cyphal_serve(uavcan_file_Write_1_1_FIXED_PORT_ID_,
                      uavcan_file_Write_Request_1_1_EXTENT_BYTES_, serve_write)) {
        cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_WARNING,
                          "M04 file services not registered: no frame is served");
    }

    i2c_init();
    /* 400 kHz: one subpage read is 37.5 ms there against 150 ms at the platform
     * default, eight times a second. Both U1 and U2 are rated for it, and the
     * STM32F405's I2C tops out at it (M04 spec 5.1). */
    i2c_set_speed(400000u);

    /* No transaction before 80 ms plus one refresh interval from power-on. The
     * clock, CAN self-test and ATECC probe have already taken most of it, so
     * this is a floor against boot getting faster, not a wait. */
    while (millis() < POWER_ON_DELAY_MS) {
    }

    s_boot_us = micros64();
    probe();

    /* The whole calibration block, once, before the watchdog starts (M04 spec
     * 6.2). Publication does not begin without it: a temperature computed from
     * an unrestored device is not a measurement. */
    if (s_u1 && !s_restored) {
        s_restored = (mlx90640_restore(frame_scratch(),
                                       MLX90640_RESTORE_SCRATCH_WORDS) == 0);
    }
    if (s_u2) {
        (void)flatfield_load();
    }

    frame_interval_begin();
    s_last_pub = micros64();
    s_last_probe = s_last_pub;
    s_interval_local = s_last_pub;
    s_interval_stamp = cyphal_timestamp_usec();
    s_interval_minute = cyphal_timestamp_usec() / INTERVAL_PERIOD_US;
    s_interval_synced = cyphal_timestamp_usec() != 0u;

    uart_puts("M04: U1 MLX90640 ");
    uart_puts(s_u1 ? "present" : "ABSENT");
    uart_puts(", calibration ");
    uart_puts(s_restored ? "restored" : "NOT restored");
    uart_puts(", U2 store ");
    uart_puts(s_u2 ? "present" : "absent");
    uart_puts(", flat field ");
    uart_puts(flatfield_state_str());
    uart_puts("\r\n");

    cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_NOTICE,
                          "M04 up, 1=present, bitmask U1|restore|U2|trim =",
                          (uint32_t)((s_u1 ? 1u : 0u) | (s_restored ? 2u : 0u) |
                                     (s_u2 ? 4u : 0u) | (flatfield_applied() ? 8u : 0u)));
    if (s_u2 && (s_class_id_byte != 0xFFu) && (s_class_id_byte != M04_MODULE_ID)) {
        /* The store carries a class this module is not. Said once: it is the
         * signature of a store, or a module, that has been swapped. */
        cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_WARNING,
                              "M04 store carries another module class =",
                              (uint32_t)s_class_id_byte);
    }
    if (s_u1 && !flatfield_applied()) {
        cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_NOTICE,
                          "M04 publishing uncorrected: no flat field in force");
    }
}

/* --- the loop ------------------------------------------------------------- */

/* One accepted frame: the common quantities first, so the frame-validity rule
 * of M04 spec 10 is applied before 768 pixels have been folded into an
 * accumulator that would then have to be unpicked. */
static void take_frame(void)
{
    mlx90640_frame_taken();

    if (mlx90640_frame_common() != 0) {
        s_frames_dropped++;
        return;
    }
    const float ta = mlx90640_ta();
    if (s_have_ta_prev) {
        const float d = ta - s_ta_prev;
        if (((d < 0.0f) ? -d : d) > TA_JUMP_LIMIT_K) {
            s_ta_prev = ta;
            s_frames_dropped++;
            return;
        }
    }
    s_ta_prev = ta;
    s_have_ta_prev = true;

    if (mlx90640_compute(emissivity_in_force(), reflected_c(ta), on_pixel, NULL) != 0) {
        s_frames_dropped++;
        return;
    }
    frame_add_done(ta + 273.15f);
}

static void second_tick(void)
{
    frame_stats_t st;
    if (frame_second_stats(s_hotspot_k[0], s_hotspot_delta_min[0], &st)) {
        s_fail = 0u;
        publish_summary(&st, mlx90640_ta());

        /* Event rule (M04 spec 10), at most one per interval. Arming replaces
         * what is being served with the next complete frame alone. */
        if (!s_event_used &&
            ((st.hotspot_count >= EVENT_HOTSPOTS) ||
             (st.t_max > (st.t_mean + EVENT_DELTA_K)))) {
            s_event_used = true;
            frame_arm_event();
        }
    } else if (s_u1 && s_restored) {
        /* A second that produced no complete frame publishes nothing. The gap
         * is the symptom: a subject that keeps arriving with the previous
         * second's numbers would look healthy while measuring nothing, which is
         * how M01's O-75 hid for 37 minutes. */
        if (s_fail < 255u) {
            s_fail++;
        }
    }
    report_health();
}

static void interval_tick(uint64_t now_us, uint64_t stamp_us, bool synced)
{
    frame_meta_t meta;
    fill_meta(&meta, s_interval_stamp, stamp_us,
              s_interval_synced && synced && (s_interval_stamp != 0u));
    if (frame_interval_frames() > 0u) {
        (void)frame_build_interval(&meta);
    } else {
        /* An interval with no frame produces no file: the previous one stands
         * with its own sequence number, and the gateway sees the sequence stop
         * rather than a frame of nothing. */
        frame_interval_begin();
    }
    s_interval_stamp = stamp_us;
    s_interval_local = now_us;
    s_interval_synced = synced;
    s_event_used = false;
}

void m04_sensors_spin(void)
{
    const uint64_t now = micros64();

    /* A trim commit runs one 32-byte page per pass, waiting out each 5 ms write
     * cycle between them (M04 spec 6.7). It shares the bus with the frame read,
     * and at 49 pages it takes about a quarter of a second. */
    if (flatfield_commit_active()) {
        (void)flatfield_commit_step();
    }

    if (s_u1 && s_restored) {
        if (mlx90640_read_step() == 1) {
            if (mlx90640_frame_complete()) {
                take_frame();
            }
        }
        if (frame_event_captured()) {
            frame_meta_t meta;
            fill_meta(&meta, now, cyphal_timestamp_usec(), cyphal_timestamp_usec() != 0u);
            (void)frame_build_event(&meta);
        }
    }

    if ((now - s_last_pub) >= PUBLISH_PERIOD_US) {
        s_last_pub += PUBLISH_PERIOD_US;
        second_tick();
    }

    /* Interval boundaries are aligned to the minute of the gateway time base
     * while synchronized, and free-running before the first sync pair (M04 spec
     * 6.5). The header's flags say which of the two an interval was. */
    const uint64_t stamp = cyphal_timestamp_usec();
    if (stamp != 0u) {
        const uint64_t minute = stamp / INTERVAL_PERIOD_US;
        if (minute != s_interval_minute) {
            s_interval_minute = minute;
            interval_tick(now, stamp, true);
        }
    } else if ((now - s_interval_local) >= INTERVAL_PERIOD_US) {
        interval_tick(now, 0u, false);
    }

    /* Re-probe on the ADR-0014 d8 interval, and only between reads: a probe
     * issued mid-image costs that image its timing for nothing. */
    if (((now - s_last_probe) >= REPROBE_PERIOD_US) && !mlx90640_busy()) {
        s_last_probe = now;
        probe();
    }
}
