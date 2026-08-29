/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "update.h"
#include "verify.h"

#include "e0001.h"
#include "atecc608.h"
#include "identity.h"
#include "clock.h"
#include "can.h"
#include "uart.h"
#include "cyphal.h"
#include "flash.h"
#include "image.h"
#include "partition.h"
#include "update_state.h"

#include "uavcan/file/Read_1_1.h"
#include "uavcan/diagnostic/Severity_1_0.h"

#include <string.h>

/* The GetInfo name of the bootloader. Distinct from every personality: a node
 * sitting in its bootloader must not look like a node that is running. */
#define BOOT_NODE_NAME "org.industrygrow.bootloader"

/* uavcan.file.Read returns at most 256 bytes, and a short read means end of
 * file. Requesting in that unit keeps every write offset word-aligned, which
 * is what the flash programmer requires. */
#define READ_BLOCK 256u

/* Per-block budget and how many times a block is asked for again. A gateway
 * that has the file answers in milliseconds; this is generous enough to cross
 * a busy bus and short enough that a dead server does not hold the node. */
#define BLOCK_TIMEOUT_MS 2000u
#define BLOCK_ATTEMPTS 3u

/* Whole-transfer bound. A 384 KB slot at 256 bytes a block cannot legitimately
 * take this long, and without it a server that answers every block with one
 * byte would keep the node in its bootloader indefinitely. */
#define TRANSFER_TIMEOUT_MS 300000u

static struct {
    bool have;
    uint8_t from;
    uint8_t transfer_id;
    uint16_t error;
    uint16_t len;
    uint8_t data[READ_BLOCK] __attribute__((aligned(4)));
} s_rx;

/* "v<major>.<minor>" into `out`, which must hold 12 bytes. The bootloader has
 * no printf and the application does not use one either; the diagnostic of
 * ADR-0029 d11 has to carry the version, so it is composed here. */
static void format_version(char *out, uint16_t major, uint16_t minor)
{
    unsigned i = 0u;
    out[i++] = 'v';
    if (major >= 10u) {
        out[i++] = (char)('0' + (major / 10u) % 10u);
    }
    out[i++] = (char)('0' + (major % 10u));
    out[i++] = '.';
    if (minor >= 10u) {
        out[i++] = (char)('0' + (minor / 10u) % 10u);
    }
    out[i++] = (char)('0' + (minor % 10u));
    out[i] = '\0';
}

static void put_hex32(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uart_putc(hex[(v >> i) & 0xFu]);
    }
}

static void on_read_response(uint8_t from, uint8_t transfer_id,
                             const uint8_t *payload, size_t size)
{
    uavcan_file_Read_Response_1_1 resp;
    size_t sz = size;
    if (uavcan_file_Read_Response_1_1_deserialize_(&resp, payload, &sz) < 0) {
        return;
    }
    s_rx.from = from;
    s_rx.transfer_id = transfer_id;
    s_rx.error = resp._error.value;
    s_rx.len = (uint16_t)resp.data.value.count;
    if (s_rx.len > READ_BLOCK) {
        s_rx.len = READ_BLOCK;
    }
    memcpy(s_rx.data, resp.data.value.elements, s_rx.len);
    s_rx.have = true;
}

/* Bring the node onto the bus. The bootloader identifies itself exactly as the
 * application does -- same unique-id anchor, same Node-ID out of the ADR-0027
 * store (d4) -- so the gateway sees one node in two states, not two nodes. */
static uint8_t bus_up(void)
{
    e0001_led_status(true);
    /* The blue LED on the core board is the only one visible with a module
     * mounted, and from here on it means one thing: this node is in its
     * bootloader doing something, not running. */
    e0001_weact_led(true);
    atecc608_init();
    identity_init();
    const uint8_t node_id = identity_node_id();
    (void)can_init_normal();
    cyphal_init(node_id, BOOT_NODE_NAME, "bootloader");
    (void)cyphal_subscribe_response(uavcan_file_Read_1_1_FIXED_PORT_ID_,
                                    uavcan_file_Read_Response_1_1_EXTENT_BYTES_,
                                    on_read_response);
    return node_id;
}

/* Ask for one block and wait for its answer. Returns the byte count, or -1 on
 * a failure that ends the transfer. */
static int32_t read_block(const update_state_t *req, uint32_t offset, uint8_t *tid)
{
    uavcan_file_Read_Request_1_1 rq;
    memset(&rq, 0, sizeof(rq));
    rq.offset = offset;
    rq.path.path.count = strlen(req->path);
    memcpy(rq.path.path.elements, req->path, rq.path.path.count);

    uint8_t buf[uavcan_file_Read_Request_1_1_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(buf);
    if (uavcan_file_Read_Request_1_1_serialize_(&rq, buf, &sz) < 0) {
        return -1;
    }

    for (unsigned attempt = 0u; attempt < BLOCK_ATTEMPTS; attempt++) {
        s_rx.have = false;
        const uint8_t sent_tid = *tid;
        if (!cyphal_request(uavcan_file_Read_1_1_FIXED_PORT_ID_, req->server_node_id,
                            tid, buf, sz)) {
            cyphal_spin(); /* queue full: let it drain, then ask again */
            continue;
        }
        const uint32_t deadline = millis() + BLOCK_TIMEOUT_MS;
        while (millis() < deadline) {
            cyphal_spin();
            if (!s_rx.have) {
                continue;
            }
            s_rx.have = false;
            /* A response to a request already given up on carries an older
             * transfer-ID; taking it would put the wrong bytes at this offset. */
            if ((s_rx.from != req->server_node_id) || (s_rx.transfer_id != sent_tid)) {
                continue;
            }
            if (s_rx.error != 0u) {
                uart_puts("file.Read error ");
                uart_put_u32(s_rx.error);
                uart_puts("\r\n");
                return -1;
            }
            return (int32_t)s_rx.len;
        }
    }
    uart_puts("no answer from the file server\r\n");
    return -1;
}

/* Write one block into the slot. The last block of a file need not be a whole
 * number of words; it is padded with the erased value so the CRC and the
 * digest still cover a word-aligned region. */
static bool program_block(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint32_t words[READ_BLOCK / 4u];
    const uint32_t word_count = (len + 3u) / 4u;
    memset(words, 0xFF, sizeof(words));
    memcpy(words, data, len);
    return flash_program_words(addr, words, word_count) == 0;
}

static bool erase_slot(igrow_slot_t slot)
{
    for (uint8_t s = partition_slot_first_sector(slot);
         s <= partition_slot_last_sector(slot); s++) {
        if (flash_erase_sector(s) != 0) {
            return false;
        }
    }
    return true;
}

/* Put queued frames on the wire before the bootloader stops spinning. Bounded,
 * and safe on a bus with nobody on it: unsent frames expire on their own
 * deadline and are dropped by the skeleton. */
static void flush_bus(uint32_t ms)
{
    const uint32_t until = millis() + ms;
    while (millis() < until) {
        cyphal_spin();
    }
}

/* Give up on the update, saying why on both channels.
 *
 * The diagnostic is the point: the console is not attached in service, and an
 * update that fails silently looks exactly like one that was never asked for.
 * ADR-0029 d11 requires the verification result to be reported, and a refusal
 * is a result. Flushing before returning is not optional -- the caller boots
 * an image next, and the hand-over discards whatever is still queued. */
static bool abandon(const char *why)
{
    uart_puts(why);
    uart_puts("\r\n");
    cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_ERROR, why);
    flush_bus(300u);
    e0001_weact_led(false);
    return false;
}

bool update_run_request(void)
{
    const update_state_t *req = update_state();
    const igrow_slot_t target = req->request_slot;

    uart_puts("update requested: slot ");
    uart_puts(partition_slot_str(target));
    uart_puts(" from node ");
    uart_put_u32(req->server_node_id);
    uart_puts(", path ");
    uart_puts(req->path);
    uart_puts("\r\n");

    const uint8_t node_id = bus_up();
    uart_puts("bootloader on the bus as node ");
    uart_put_u32(node_id);
    uart_puts("\r\n");

    /* Clear the request FIRST, and before a single sector is erased. A request
     * that survived its own attempt would re-erase the slot on the next boot
     * and keep doing it, so a node that cannot reach its file server would
     * never run again. One attempt per request; the operator retries. */
    update_state_t st = *req;
    st.request_pending = false;
    if (update_state_store(&st) != 0) {
        return abandon("update refused: the request could not be cleared");
    }

    if (!erase_slot(target)) {
        return abandon("update failed: slot erase");
    }

    const uint32_t base = partition_slot_addr(target);
    uint32_t offset = 0u;
    uint32_t total = 0u; /* learnt from the header, once it is written */
    uint8_t tid = 0u;
    const uint32_t started = millis();

    for (;;) {
        if ((millis() - started) > TRANSFER_TIMEOUT_MS) {
            return abandon("update failed: transfer timed out");
        }
        const int32_t n = read_block(req, offset, &tid);
        if (n < 0) {
            return abandon("update failed: download");
        }
        if (n == 0) {
            break; /* end of file */
        }
        if ((offset + (uint32_t)n) > IGROW_SLOT_SIZE) {
            return abandon("update failed: artifact larger than the slot");
        }
        if (!program_block(base + offset, s_rx.data, (uint32_t)n)) {
            uart_puts("flash program failed at ");
            put_hex32(base + offset);
            uart_puts("\r\n");
            return abandon("update failed: flash program");
        }
        offset += (uint32_t)n;
        e0001_weact_led_toggle(); /* one flicker per block: the transfer, visibly */

        /* The header is the first thing written, so after one block the image
         * announces its own length and the transfer knows where it ends. */
        if ((total == 0u) && (offset >= IGROW_IMAGE_HEADER_SIZE)) {
            const igrow_image_header_t *h = image_header(target);
            if ((h->magic != IGROW_IMAGE_MAGIC) ||
                (h->image_length == 0u) ||
                (h->image_length > IGROW_IMAGE_MAX_BODY)) {
                return abandon("update failed: no image header");
            }
            total = IGROW_IMAGE_HEADER_SIZE + h->image_length;
            uart_puts("image length ");
            uart_put_u32(h->image_length);
            uart_puts(" bytes\r\n");
        }
        if ((total != 0u) && (offset >= total)) {
            break;
        }
        if ((uint32_t)n < READ_BLOCK) {
            break; /* a short read is end of file */
        }
    }

    if ((total == 0u) || (offset < total)) {
        return abandon("update failed: artifact ended early");
    }

    /* Steady through the verification, which is the one part of an update
     * that takes a second and produces no traffic to watch. */
    e0001_weact_led(true);
    const image_verify_t result = image_verify_slot(target);
    uart_puts("verification: ");
    uart_puts(image_verify_str(result));
    uart_puts("\r\n");
    if (result != IMAGE_VERIFY_OK) {
        /* d11: the node says why on the channel the gateway is listening to,
         * not only on a UART nobody is attached to. */
        return abandon(image_verify_str(result));
    }

    /* Only now does the slot become bootable, and only on trial: the image has
     * proven that it is ours, not that it runs (d6, d8). */
    const igrow_image_header_t *h = image_header(target);
    st = *update_state();
    st.prev_slot = st.boot_slot;
    st.boot_slot = target;
    st.state = IGROW_BOOT_TRIAL;
    st.attempts = IGROW_UPDATE_TRIAL_ATTEMPTS;
    if (update_state_store(&st) != 0) {
        return abandon("update failed: the slot could not be marked");
    }
    char version[12];
    format_version(version, h->version_major, h->version_minor);
    char notice[48];
    unsigned n = 0u;
    for (const char *t = "image verified, on trial "; *t != 0; t++) {
        notice[n++] = *t;
    }
    for (const char *t = version; *t != 0; t++) {
        notice[n++] = *t;
    }
    notice[n++] = ' ';
    notice[n++] = 'A' + (char)target;
    notice[n] = '\0';
    cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_NOTICE, notice);
    flush_bus(200u);
    uart_puts("slot ");
    uart_puts(partition_slot_str(target));
    uart_puts(" marked bootable on trial\r\n");
    return true;
}

void update_await_image(void)
{
    uart_puts("no bootable image; waiting on the bus for one\r\n");
    const uint8_t node_id = bus_up();
    uart_puts("bootloader on the bus as node ");
    uart_put_u32(node_id);
    uart_puts("\r\n");

    uint32_t last_blink = millis();
    for (;;) {
        cyphal_spin(); /* an ExecuteCommand update request restarts the node */
        if ((millis() - last_blink) >= 250u) {
            last_blink = millis();
            e0001_led_status_toggle();
            e0001_weact_led_toggle();
        }
    }
}
