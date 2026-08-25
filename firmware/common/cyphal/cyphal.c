/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*
 * Cyphal node skeleton. Targets the libcanard v3 API (canardInit(alloc,free) +
 * canardTxInit(capacity,mtu)); if a different libcanard major is pulled, the
 * handful of calls here are where the API delta shows up. DSDL types are
 * Nunavut-generated from public_regulated_data_types into the build tree
 * (see firmware/cmake/dsdl.cmake) — generated code is not vendored (ADR-0005 d10).
 */

#include "cyphal.h"

#include "canard.h"
#include "o1heap.h"

#include "uavcan/node/Heartbeat_1_0.h"
#include "uavcan/node/GetInfo_1_0.h"
#include "uavcan/node/ExecuteCommand_1_0.h"
#include "uavcan/_register/Access_1_0.h" /* namespace stropped: register -> _register */
#include "uavcan/_register/List_1_0.h"
#include "uavcan/node/port/List_1_0.h"
#include "uavcan/diagnostic/Record_1_1.h"
#include "uavcan/time/Synchronization_1_0.h"

#include "registers.h"
#include "e0001.h" /* CMSIS: NVIC_SystemReset */
#include "atecc608.h"
#include "clock.h"
#include "can.h"
#include "watchdog.h"

#include <string.h>

/* --- memory: a fixed o1heap arena feeds libcanard's allocator --- */
/* The queue has to hold the largest single transfer, not the busiest second:
 * libcanard pushes a whole transfer or none of it. uavcan.node.port.List is the
 * largest thing this node sends -- two 64-byte service masks are unavoidable
 * even when every list is otherwise empty -- and at 7 payload bytes per classic
 * CAN frame that is ~26 frames. At the previous capacity of 24 the push failed
 * and, since tx_push() discards the result, failed silently. */
#define CYPHAL_HEAP_SIZE 8192u
#define CYPHAL_TX_QUEUE_CAP 40u

static uint8_t s_arena[CYPHAL_HEAP_SIZE] __attribute__((aligned(O1HEAP_ALIGNMENT)));
static O1HeapInstance *s_heap;

static CanardInstance s_canard;
static CanardTxQueue s_txq;
static CanardRxSubscription s_getinfo_sub;
static CanardRxSubscription s_access_sub;
static CanardRxSubscription s_list_sub;
static CanardRxSubscription s_execcmd_sub;
static bool s_pending_reset; /* set by ExecuteCommand RESTART, acted on after TX flush */
static const char *s_node_name = "org.industrygrow.node"; /* set by cyphal_init() */

/* Personality-reported health, and the subjects it publishes. Both are set by
 * the strap-selected personality; the skeleton owns neither. */
static uint8_t s_personality_health; /* uavcan.node.Health value, 0 = NOMINAL */
static const uint16_t *s_pub_subjects;
static uint8_t s_pub_subject_count;
static uint8_t s_portlist_tid;
static uint8_t s_diag_tid;
static cyphal_command_fn s_command_fn;
static uint64_t s_next_portlist_us;

static uint8_t s_hb_tid;       /* heartbeat transfer-id (5-bit, wraps) */
static uint64_t s_start_us;    /* for uptime */
static uint64_t s_next_hb_us;  /* next heartbeat deadline */

/* --- time synchronization slave (ADR-0002 d11) ---------------------------- *
 * The master publishes the transmit timestamp of its PREVIOUS message, so a
 * pair of consecutive messages is needed to learn the offset: the first is
 * held (UPDATE), the second carries the first one's transmit time (ADJUST).
 * That alternation is the algorithm in 7168.Synchronization.1.0, not a
 * simplification of it.
 *
 * The master publishes at least once per second (ADR-0002 d11), so the timeout
 * is the type's 3 x MAX_PUBLICATION_PERIOD outright; deriving it from the
 * measured interval, as the type's text allows, would only reconstruct the same
 * constant. Note that an UPDATE message arrives two periods after the previous
 * UPDATE -- the ADJUST message falls between -- so the timeout must exceed two
 * periods, and 3 s does. */
#define SYNC_MAX_PUBLICATION_PERIOD_US 1000000u
#define SYNC_PUBLISHER_TIMEOUT_US (3u * SYNC_MAX_PUBLICATION_PERIOD_US)

static CanardRxSubscription s_sync_sub;
static int16_t s_sync_master = -1;  /* dominant master's Node-ID, -1 = none yet */
static bool s_sync_adjust;          /* true = STATE_ADJUST, false = STATE_UPDATE */
static uint64_t s_sync_prev_rx_us;  /* local reception time of the held message */
static uint8_t s_sync_prev_tid;
static int64_t s_sync_offset_us;    /* master time - local time */
static bool s_sync_have_offset;
static uint64_t s_sync_last_rx_us;  /* last message from the master; drives staleness */

static void *mem_alloc(CanardInstance *ins, size_t amount)
{
    return o1heapAllocate((O1HeapInstance *)ins->user_reference, amount);
}

static void mem_free(CanardInstance *ins, void *pointer)
{
    o1heapFree((O1HeapInstance *)ins->user_reference, pointer);
}

/* Latched at init: is the node identifying by its ATECC608 anchor, or by the
 * fallback? Sampled once so the answer cannot change between the GetInfo
 * response and the health it is reported through. */
static bool s_identity_anchored;

/* 16-byte Cyphal unique_id, per ADR-0027 d8. The carrier's ATECC608 is the node's
 * hardware-identity anchor (ADR-0007 d5), so its 9-byte serial is the preferred
 * source, left-justified and zero-padded. When the secure element does not answer
 * (bare WeAct / no carrier) this falls back to the STM32F405 96-bit factory UID --
 * which binds to no provisioning record, so the node also reports its identity as
 * unanchored and its health as ADVISORY. atecc608_init() must have run first. */
static void read_unique_id(uint8_t out[16])
{
    memset(out, 0, 16);

    if (atecc608_present()) {
        memcpy(out, atecc608_serial(), ATECC608_SERIAL_LEN);
        return;
    }

    const volatile uint32_t *uid = (const volatile uint32_t *)0x1FFF7A10u;
    for (int i = 0; i < 3; i++) {
        uint32_t w = uid[i];
        out[i * 4 + 0] = (uint8_t)(w);
        out[i * 4 + 1] = (uint8_t)(w >> 8);
        out[i * 4 + 2] = (uint8_t)(w >> 16);
        out[i * 4 + 3] = (uint8_t)(w >> 24);
    }
}

void cyphal_init(uint8_t node_id, const char *node_name, const char *description)
{
    s_heap = o1heapInit(s_arena, sizeof(s_arena));

    s_canard = canardInit(&mem_alloc, &mem_free);
    s_canard.user_reference = s_heap;
    s_canard.node_id = node_id;

    /* atecc608_init() must already have run (main.c probes it before this). */
    s_identity_anchored = atecc608_present();

    s_txq = canardTxInit(CYPHAL_TX_QUEUE_CAP, CANARD_MTU_CAN_CLASSIC);

    (void)canardRxSubscribe(&s_canard,
                            CanardTransferKindRequest,
                            uavcan_node_GetInfo_1_0_FIXED_PORT_ID_,
                            uavcan_node_GetInfo_Request_1_0_EXTENT_BYTES_,
                            CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                            &s_getinfo_sub);
    (void)canardRxSubscribe(&s_canard,
                            CanardTransferKindRequest,
                            uavcan_register_Access_1_0_FIXED_PORT_ID_,
                            uavcan_register_Access_Request_1_0_EXTENT_BYTES_,
                            CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                            &s_access_sub);
    (void)canardRxSubscribe(&s_canard,
                            CanardTransferKindRequest,
                            uavcan_register_List_1_0_FIXED_PORT_ID_,
                            uavcan_register_List_Request_1_0_EXTENT_BYTES_,
                            CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                            &s_list_sub);
    (void)canardRxSubscribe(&s_canard,
                            CanardTransferKindRequest,
                            uavcan_node_ExecuteCommand_1_0_FIXED_PORT_ID_,
                            uavcan_node_ExecuteCommand_Request_1_0_EXTENT_BYTES_,
                            CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                            &s_execcmd_sub);
    (void)canardRxSubscribe(&s_canard,
                            CanardTransferKindMessage,
                            uavcan_time_Synchronization_1_0_FIXED_PORT_ID_,
                            uavcan_time_Synchronization_1_0_EXTENT_BYTES_,
                            CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                            &s_sync_sub);

    s_node_name = node_name;
    registers_init(node_id, description);

    s_start_us = micros64();
    s_next_hb_us = s_start_us + 1000000u;
    s_next_portlist_us = s_start_us + 2000000u; /* offset from the heartbeat tick */
}

static void tx_push(const CanardTransferMetadata *meta, size_t size, const void *payload)
{
    (void)canardTxPush(&s_txq, &s_canard, micros64() + 1000000u, meta, size, payload);
}

void cyphal_publish(uint16_t subject_id, uint8_t *transfer_id,
                    const uint8_t *payload, size_t size)
{
    const CanardTransferMetadata meta = {
        .priority = CanardPriorityNominal,
        .transfer_kind = CanardTransferKindMessage,
        .port_id = (CanardPortID)subject_id,
        .remote_node_id = CANARD_NODE_ID_UNSET,
        .transfer_id = *transfer_id,
    };
    tx_push(&meta, size, payload);
    *transfer_id = (uint8_t)((*transfer_id + 1u) & CANARD_TRANSFER_ID_MAX);
}

/* Hold this message as the first half of a pair and wait for the next one,
 * which will carry this one's transmit timestamp. */
static void sync_update(const CanardRxTransfer *t)
{
    s_sync_prev_rx_us = t->timestamp_usec;
    s_sync_master = (int16_t)t->metadata.remote_node_id;
    s_sync_prev_tid = t->metadata.transfer_id;
    s_sync_adjust = true;
}

static void handle_timesync(const CanardRxTransfer *t)
{
    /* An anonymous publisher cannot be elected: election is by Node-ID and an
     * anonymous transfer has none. */
    if (t->metadata.remote_node_id > CANARD_NODE_ID_MAX) {
        return;
    }
    uavcan_time_Synchronization_1_0 msg;
    size_t sz = t->payload_size;
    if (uavcan_time_Synchronization_1_0_deserialize_(&msg, t->payload, &sz) < 0) {
        return;
    }

    const int16_t src = (int16_t)t->metadata.remote_node_id;
    const uint64_t rx_us = t->timestamp_usec;
    const uint64_t since_prev = rx_us - s_sync_prev_rx_us;

    const bool needs_init = (s_sync_master < 0);
    const bool switch_master = (!needs_init) && (src < s_sync_master);
    const bool timed_out = (!needs_init) && (since_prev > SYNC_PUBLISHER_TIMEOUT_US);

    if (needs_init || switch_master || timed_out) {
        /* A different master, or the same one after a gap, is a different time
         * base until a fresh pair proves otherwise. The old offset does not
         * carry over -- that is exactly the frozen-offset failure ADR-0002 d11
         * rules out. */
        s_sync_have_offset = false;
        sync_update(t);
    } else if (src == s_sync_master) {
        if (s_sync_adjust) {
            const bool msg_invalid =
                (msg.previous_transmission_timestamp_microsecond == 0u);
            const bool wrong_tid =
                (t->metadata.transfer_id !=
                 (uint8_t)((s_sync_prev_tid + 1u) & CANARD_TRANSFER_ID_MAX));
            const bool wrong_timing = (since_prev > SYNC_MAX_PUBLICATION_PERIOD_US);
            if (msg_invalid || wrong_tid || wrong_timing) {
                s_sync_adjust = false; /* the pair is broken; start a new one */
            }
        }
        if (s_sync_adjust) {
            /* The whole measurement, in one line: where the master says it was
             * when we recorded where we were. */
            s_sync_offset_us =
                (int64_t)msg.previous_transmission_timestamp_microsecond -
                (int64_t)s_sync_prev_rx_us;
            s_sync_have_offset = true;
            s_sync_adjust = false;
        } else {
            sync_update(t);
        }
    } else {
        return; /* higher Node-ID than the dominant master: not our time base */
    }
    s_sync_last_rx_us = rx_us;
}

uint64_t cyphal_timestamp_usec(void)
{
    if (!s_sync_have_offset) {
        return 0u; /* the type's own value for "not known" */
    }
    /* The local clock is never stepped -- libcanard's transmission deadlines and
     * transfer-ID timeouts are denominated in it. The offset is applied here and
     * nowhere else. */
    const int64_t t = (int64_t)micros64() + s_sync_offset_us;
    return (t > 0) ? (uint64_t)t : 0u;
}

void cyphal_set_health(uint8_t health)
{
    s_personality_health = health;
}

void cyphal_set_command_handler(cyphal_command_fn fn)
{
    s_command_fn = fn;
}

static size_t copy_text(uint8_t *dst, size_t cap, const char *text)
{
    size_t n = 0;
    while ((text[n] != '\0') && (n < cap)) {
        dst[n] = (uint8_t)text[n];
        n++;
    }
    return n;
}

void cyphal_diagnostic(uint8_t severity, const char *text)
{
    static uint8_t buf[uavcan_diagnostic_Record_1_1_SERIALIZATION_BUFFER_SIZE_BYTES_];
    uavcan_diagnostic_Record_1_1 m;
    memset(&m, 0, sizeof(m));
    m.timestamp.microsecond = cyphal_timestamp_usec();
    m.severity.value = severity;
    m.text.count = copy_text(m.text.elements, sizeof(m.text.elements), text);

    size_t sz = sizeof(buf);
    if (uavcan_diagnostic_Record_1_1_serialize_(&m, buf, &sz) < 0) {
        return;
    }
    const CanardTransferMetadata meta = {
        .priority = CanardPriorityOptional,
        .transfer_kind = CanardTransferKindMessage,
        .port_id = uavcan_diagnostic_Record_1_1_FIXED_PORT_ID_,
        .remote_node_id = CANARD_NODE_ID_UNSET,
        .transfer_id = s_diag_tid,
    };
    tx_push(&meta, sz, buf);
    s_diag_tid = (uint8_t)((s_diag_tid + 1u) & CANARD_TRANSFER_ID_MAX);
}

void cyphal_diagnostic_u32(uint8_t severity, const char *text, uint32_t value)
{
    char line[128];
    size_t n = 0;
    while ((text[n] != '\0') && (n < (sizeof(line) - 14u))) {
        line[n] = text[n];
        n++;
    }
    line[n++] = ' ';
    char digits[10];
    uint8_t d = 0;
    do {
        digits[d++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u);
    while (d > 0u) {
        line[n++] = digits[--d];
    }
    line[n] = '\0';
    cyphal_diagnostic(severity, line);
}

void cyphal_declare_publishers(const uint16_t *subject_ids, uint8_t count)
{
    s_pub_subjects = subject_ids;
    s_pub_subject_count = count;
}

/* uavcan.node.port.List, subject 7510, at least every 10 s at OPTIONAL priority.
 *
 * The buffer is static and large: SubjectIDList reserves 2**15 bits against a
 * future widening of the subject range, and the serialization buffer must hold
 * the reserved extent even though the sparse list actually sent is a few dozen
 * bytes. Stack is the wrong place for it. */
static void publish_port_list(void)
{
    static uint8_t buf[uavcan_node_port_List_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    uavcan_node_port_List_1_0 m;
    memset(&m, 0, sizeof(m));

    /* Publishers: the personality's subjects plus the two the skeleton owns. */
    uavcan_node_port_SubjectIDList_1_0_select_sparse_list_(&m.publishers);
    uint8_t n = 0;
    m.publishers.sparse_list.elements[n++].value = uavcan_node_Heartbeat_1_0_FIXED_PORT_ID_;
    m.publishers.sparse_list.elements[n++].value = uavcan_node_port_List_1_0_FIXED_PORT_ID_;
    m.publishers.sparse_list.elements[n++].value = uavcan_diagnostic_Record_1_1_FIXED_PORT_ID_;
    for (uint8_t i = 0; (i < s_pub_subject_count) && (n < 255u); i++) {
        m.publishers.sparse_list.elements[n++].value = s_pub_subjects[i];
    }
    m.publishers.sparse_list.count = n;

    /* Subscribers: none. The node consumes no subjects, only services. */
    uavcan_node_port_SubjectIDList_1_0_select_sparse_list_(&m.subscribers);
    m.subscribers.sparse_list.count = 0;

    /* Servers: the three the skeleton answers. Clients: none. */
    nunavutSetBit(m.servers.mask_bitpacked_, sizeof(m.servers.mask_bitpacked_),
                  uavcan_node_GetInfo_1_0_FIXED_PORT_ID_, true);
    nunavutSetBit(m.servers.mask_bitpacked_, sizeof(m.servers.mask_bitpacked_),
                  uavcan_register_Access_1_0_FIXED_PORT_ID_, true);
    nunavutSetBit(m.servers.mask_bitpacked_, sizeof(m.servers.mask_bitpacked_),
                  uavcan_node_ExecuteCommand_1_0_FIXED_PORT_ID_, true);

    size_t sz = sizeof(buf);
    if (uavcan_node_port_List_1_0_serialize_(&m, buf, &sz) < 0) {
        return;
    }
    const CanardTransferMetadata meta = {
        .priority = CanardPriorityOptional,
        .transfer_kind = CanardTransferKindMessage,
        .port_id = uavcan_node_port_List_1_0_FIXED_PORT_ID_,
        .remote_node_id = CANARD_NODE_ID_UNSET,
        .transfer_id = s_portlist_tid,
    };
    tx_push(&meta, sz, buf);
    s_portlist_tid = (uint8_t)((s_portlist_tid + 1u) & CANARD_TRANSFER_ID_MAX);
}

static void publish_heartbeat(void)
{
    uavcan_node_Heartbeat_1_0 hb;
    hb.uptime = (uint32_t)((micros64() - s_start_us) / 1000000u);
    /* Without the ATECC608 the node still senses and publishes, but it is
     * identifying by the STM32 factory UID instead of its ADR-0007 anchor --
     * and that substitution is otherwise invisible on the bus, since GetInfo
     * reports a plausible unique_id either way. ADVISORY is the Health value
     * for exactly this: "a minor failure that does not prevent the subsystem
     * from performing any of its real-time functions". */
    const uint8_t identity_health = s_identity_anchored ? uavcan_node_Health_1_0_NOMINAL
                                                        : uavcan_node_Health_1_0_ADVISORY;
    /* The worse of the skeleton's view and the personality's. A node whose
     * sensors have stopped answering is not NOMINAL, whatever its identity. */
    hb.health.value = (s_personality_health > identity_health) ? s_personality_health
                                                              : identity_health;
    hb.mode.value = uavcan_node_Mode_1_0_OPERATIONAL;
    /* Why this node last restarted (RCC_CSR flags, latched at boot). Lets the
     * gateway tell a watchdog recovery from a power cut or a probe-induced
     * reset without anyone attaching a debugger. */
    hb.vendor_specific_status_code = watchdog_reset_cause();

    uint8_t buf[uavcan_node_Heartbeat_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(buf);
    if (uavcan_node_Heartbeat_1_0_serialize_(&hb, buf, &sz) < 0) {
        return;
    }
    const CanardTransferMetadata meta = {
        .priority = CanardPriorityNominal,
        .transfer_kind = CanardTransferKindMessage,
        .port_id = uavcan_node_Heartbeat_1_0_FIXED_PORT_ID_,
        .remote_node_id = CANARD_NODE_ID_UNSET,
        .transfer_id = s_hb_tid,
    };
    tx_push(&meta, sz, buf);
    s_hb_tid = (uint8_t)((s_hb_tid + 1u) & CANARD_TRANSFER_ID_MAX);
}

static void handle_getinfo(const CanardRxTransfer *req)
{
    uavcan_node_GetInfo_Response_1_0 resp;
    memset(&resp, 0, sizeof(resp));

    resp.protocol_version.major = 1; /* Cyphal v1 */
    resp.protocol_version.minor = 0;
    resp.hardware_version.major = 1; /* carrier E0001 */
    resp.hardware_version.minor = 0;
    resp.software_version.major = 0; /* this firmware */
    resp.software_version.minor = 1;
    resp.software_vcs_revision_id = 0u;
    read_unique_id(resp.unique_id);

    /* The node name is the personality's, not the image's: one image serves
     * every module class and the strap picks which (ADR-0017 d16), so this is
     * how the gateway learns what is actually in the socket. Bounded because
     * uavcan.node.GetInfo caps the field at 50 bytes. */
    size_t name_len = strlen(s_node_name);
    if (name_len > uavcan_node_GetInfo_Response_1_0_name_ARRAY_CAPACITY_) {
        name_len = uavcan_node_GetInfo_Response_1_0_name_ARRAY_CAPACITY_;
    }
    resp.name.count = name_len;
    memcpy(resp.name.elements, s_node_name, name_len);

    uint8_t buf[uavcan_node_GetInfo_Response_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(buf);
    if (uavcan_node_GetInfo_Response_1_0_serialize_(&resp, buf, &sz) < 0) {
        return;
    }
    const CanardTransferMetadata meta = {
        .priority = req->metadata.priority,
        .transfer_kind = CanardTransferKindResponse,
        .port_id = uavcan_node_GetInfo_1_0_FIXED_PORT_ID_,
        .remote_node_id = req->metadata.remote_node_id,
        .transfer_id = req->metadata.transfer_id,
    };
    tx_push(&meta, sz, buf);
}

static void respond(const CanardRxTransfer *req, CanardPortID port,
                    const void *buf, size_t sz)
{
    const CanardTransferMetadata meta = {
        .priority = req->metadata.priority,
        .transfer_kind = CanardTransferKindResponse,
        .port_id = port,
        .remote_node_id = req->metadata.remote_node_id,
        .transfer_id = req->metadata.transfer_id,
    };
    tx_push(&meta, sz, buf);
}

static void handle_access(const CanardRxTransfer *req)
{
    uavcan_register_Access_Request_1_0 rq;
    size_t in_sz = req->payload_size;
    if (uavcan_register_Access_Request_1_0_deserialize_(&rq, (const uint8_t *)req->payload, &in_sz) < 0) {
        return;
    }
    uavcan_register_Access_Response_1_0 resp;
    memset(&resp, 0, sizeof(resp));
    bool mut = false, per = false;
    registers_access(&rq.name, &rq.value, &resp.value, &mut, &per);
    resp._mutable = mut; /* nunavut strops the C++ keyword 'mutable' */
    resp.persistent = per;
    resp.timestamp.microsecond = micros64();

    uint8_t buf[uavcan_register_Access_Response_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(buf);
    if (uavcan_register_Access_Response_1_0_serialize_(&resp, buf, &sz) >= 0) {
        respond(req, uavcan_register_Access_1_0_FIXED_PORT_ID_, buf, sz);
    }
}

static void handle_list(const CanardRxTransfer *req)
{
    uavcan_register_List_Request_1_0 rq;
    size_t in_sz = req->payload_size;
    if (uavcan_register_List_Request_1_0_deserialize_(&rq, (const uint8_t *)req->payload, &in_sz) < 0) {
        return;
    }
    uavcan_register_List_Response_1_0 resp;
    memset(&resp, 0, sizeof(resp));
    registers_name_at(rq.index, &resp.name);

    uint8_t buf[uavcan_register_List_Response_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(buf);
    if (uavcan_register_List_Response_1_0_serialize_(&resp, buf, &sz) >= 0) {
        respond(req, uavcan_register_List_1_0_FIXED_PORT_ID_, buf, sz);
    }
}

static void handle_execcmd(const CanardRxTransfer *req)
{
    uavcan_node_ExecuteCommand_Request_1_0 rq;
    size_t in_sz = req->payload_size;
    if (uavcan_node_ExecuteCommand_Request_1_0_deserialize_(&rq, (const uint8_t *)req->payload, &in_sz) < 0) {
        return;
    }
    uavcan_node_ExecuteCommand_Response_1_0 resp;
    memset(&resp, 0, sizeof(resp));
    if (rq.command == uavcan_node_ExecuteCommand_Request_1_0_COMMAND_RESTART) {
        resp.status = uavcan_node_ExecuteCommand_Response_1_0_STATUS_SUCCESS;
        s_pending_reset = true; /* reset after the response is flushed */
    } else if (s_command_fn != NULL) {
        resp.status = s_command_fn(rq.command);
    } else {
        resp.status = uavcan_node_ExecuteCommand_Response_1_0_STATUS_BAD_COMMAND;
    }
    uint8_t buf[uavcan_node_ExecuteCommand_Response_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(buf);
    if (uavcan_node_ExecuteCommand_Response_1_0_serialize_(&resp, buf, &sz) >= 0) {
        respond(req, uavcan_node_ExecuteCommand_1_0_FIXED_PORT_ID_, buf, sz);
    }
}

static void flush_tx(void)
{
    const uint64_t now = micros64();
    for (const CanardTxQueueItem *ti = NULL; (ti = canardTxPeek(&s_txq)) != NULL;) {
        if ((ti->tx_deadline_usec != 0u) && (now > ti->tx_deadline_usec)) {
            s_canard.memory_free(&s_canard, canardTxPop(&s_txq, ti)); /* expired */
            continue;
        }
        if (can_send_ext(ti->frame.extended_can_id,
                         (const uint8_t *)ti->frame.payload,
                         (uint8_t)ti->frame.payload_size) == 0) {
            s_canard.memory_free(&s_canard, canardTxPop(&s_txq, ti));
        } else {
            break; /* all TX mailboxes busy; try again next spin */
        }
    }
}

static void pump_rx(void)
{
    uint32_t eid;
    uint8_t data[8];
    uint8_t len;
    while (can_recv_ext(&eid, data, &len) == 1) {
        const CanardFrame frame = {
            .extended_can_id = eid,
            .payload_size = len,
            .payload = data,
        };
        CanardRxTransfer transfer;
        const int8_t r = canardRxAccept(&s_canard, micros64(), &frame, 0, &transfer, NULL);
        if (r == 1) {
            if (transfer.metadata.transfer_kind == CanardTransferKindRequest) {
                switch (transfer.metadata.port_id) {
                case uavcan_node_GetInfo_1_0_FIXED_PORT_ID_:
                    handle_getinfo(&transfer);
                    break;
                case uavcan_register_Access_1_0_FIXED_PORT_ID_:
                    handle_access(&transfer);
                    break;
                case uavcan_register_List_1_0_FIXED_PORT_ID_:
                    handle_list(&transfer);
                    break;
                case uavcan_node_ExecuteCommand_1_0_FIXED_PORT_ID_:
                    handle_execcmd(&transfer);
                    break;
                default:
                    break;
                }
            } else if (transfer.metadata.transfer_kind == CanardTransferKindMessage) {
                if (transfer.metadata.port_id ==
                    uavcan_time_Synchronization_1_0_FIXED_PORT_ID_) {
                    handle_timesync(&transfer);
                }
            } else {
                /* nothing else is subscribed */
            }
            s_canard.memory_free(&s_canard, transfer.payload);
        }
    }
}

void cyphal_spin(void)
{
    if (micros64() >= s_next_portlist_us) {
        s_next_portlist_us += 10000000u; /* MAX_PUBLICATION_PERIOD */
        publish_port_list();
    }
    if (micros64() >= s_next_hb_us) {
        s_next_hb_us += 1000000u;
        publish_heartbeat();
    }
    flush_tx();
    pump_rx();

    /* A dead master must not leave a frozen offset behind: the drift is silent
     * and a consumer cannot see it. Reverting to UNKNOWN is visible (ADR-0002
     * d11). This has to be checked here and not only on reception -- if the
     * master stops publishing, no reception ever comes to check. */
    if (s_sync_have_offset &&
        ((micros64() - s_sync_last_rx_us) > SYNC_PUBLISHER_TIMEOUT_US)) {
        s_sync_have_offset = false;
    }

    /* Honour an ExecuteCommand RESTART once its response has been flushed. */
    if (s_pending_reset && (canardTxPeek(&s_txq) == NULL)) {
        NVIC_SystemReset();
    }
}
