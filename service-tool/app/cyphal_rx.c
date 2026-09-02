/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "cyphal_rx.h"

#include <string.h>

#include "can_port.h"
#include "canard.h"
#include "model.h"
#include "o1heap.h"
#include "panel_mem.h"
#include "stm32h7xx_hal.h"
#include "subjects.h"

#include "uavcan/node/ExecuteCommand_1_0.h"
#include "uavcan/node/GetInfo_1_0.h"
#include "uavcan/node/Heartbeat_1_0.h"

/* Sessions are allocated per (subject, source node) on first sight and freed
 * when a transfer completes, so the arena has to hold one in-flight buffer per
 * signal the bus is publishing. 21 subjects across a handful of nodes at 128
 * bytes of extent fits inside 48 kB with room to spare. AXI SRAM, not SDRAM:
 * this is touched on every frame. */
#define HEAP_SIZE 49152U
static uint8_t s_arena[HEAP_SIZE] __attribute__((aligned(O1HEAP_ALIGNMENT))) PANEL_AXI_BSS;

/* One extent for every subscription. Larger than any type the panel decodes
 * (the widest is the gas sweep at 96 bytes), so a node that grows a type
 * still reassembles here instead of being silently truncated. */
#define SUBJECT_EXTENT 128U

static O1HeapInstance *s_heap;
static CanardInstance  s_canard;
static CanardTxQueue   s_txq;

static CanardRxSubscription s_sub_heartbeat;
static CanardRxSubscription s_sub_getinfo;
static CanardRxSubscription s_sub_execcmd;
static CanardRxSubscription s_sub_subject[MODEL_MAX_SIGNALS];

static uint32_t s_accepted;
static uint32_t s_unknown;

static uint8_t             s_transfer_id_getinfo;
static uint8_t             s_transfer_id_execcmd;
static cyphal_cmd_result_t s_last_cmd;

/* libcanard wants microseconds; HAL gives milliseconds. Millisecond
 * granularity is all the transfer-ID timeout needs. */
static uint64_t micros64(void)
{
    return (uint64_t)HAL_GetTick() * 1000ULL;
}

static void *mem_alloc(CanardInstance *const ins, const size_t amount)
{
    return o1heapAllocate((O1HeapInstance *)ins->user_reference, amount);
}

static void mem_free(CanardInstance *const ins, void *const pointer)
{
    o1heapFree((O1HeapInstance *)ins->user_reference, pointer);
}

void cyphal_rx_init(uint8_t local_node_id)
{
    s_heap = o1heapInit(s_arena, sizeof s_arena);

    s_canard                = canardInit(&mem_alloc, &mem_free);
    s_canard.user_reference = s_heap;
    s_canard.node_id        = local_node_id;
    s_txq                   = canardTxInit(16, CANARD_MTU_CAN_CLASSIC);

    (void)canardRxSubscribe(&s_canard, CanardTransferKindMessage,
                            uavcan_node_Heartbeat_1_0_FIXED_PORT_ID_,
                            uavcan_node_Heartbeat_1_0_EXTENT_BYTES_,
                            CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC, &s_sub_heartbeat);

    for (size_t i = 0; (i < IGROW_SUBJECT_COUNT) && (i < MODEL_MAX_SIGNALS); i++) {
        (void)canardRxSubscribe(&s_canard, CanardTransferKindMessage,
                                IGROW_SUBJECTS[i].subject_id, SUBJECT_EXTENT,
                                CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC, &s_sub_subject[i]);
    }

    /* Responses to what the panel asks for. */
    (void)canardRxSubscribe(&s_canard, CanardTransferKindResponse,
                            uavcan_node_GetInfo_1_0_FIXED_PORT_ID_,
                            uavcan_node_GetInfo_Response_1_0_EXTENT_BYTES_,
                            CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC, &s_sub_getinfo);
    (void)canardRxSubscribe(&s_canard, CanardTransferKindResponse,
                            uavcan_node_ExecuteCommand_1_0_FIXED_PORT_ID_,
                            uavcan_node_ExecuteCommand_Response_1_0_EXTENT_BYTES_,
                            CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC, &s_sub_execcmd);
}

static void on_heartbeat(const CanardRxTransfer *t)
{
    uavcan_node_Heartbeat_1_0 hb;
    size_t                    sz = t->payload_size;
    if (uavcan_node_Heartbeat_1_0_deserialize_(&hb, (const uint8_t *)t->payload, &sz) < 0) {
        return;
    }
    model_on_heartbeat((uint8_t)t->metadata.remote_node_id, hb.uptime, hb.health.value,
                       hb.mode.value, hb.vendor_specific_status_code, HAL_GetTick());
}

static void on_subject(const CanardRxTransfer *t, const igrow_subject_t *s)
{
    igrow_reading_t r;
    memset(&r, 0, sizeof r);
    r.valid = true;
    if (!s->decode((const uint8_t *)t->payload, t->payload_size, &r)) {
        return;
    }
    model_on_reading((uint8_t)t->metadata.remote_node_id, s->subject_id, &r, HAL_GetTick());
}

static void on_getinfo_response(const CanardRxTransfer *t)
{
    uavcan_node_GetInfo_Response_1_0 resp;
    size_t                           sz = t->payload_size;
    if (uavcan_node_GetInfo_Response_1_0_deserialize_(&resp, (const uint8_t *)t->payload, &sz) < 0) {
        return;
    }
    model_on_name((uint8_t)t->metadata.remote_node_id, (const char *)resp.name.elements,
                  (uint8_t)resp.name.count);
}

static void on_execcmd_response(const CanardRxTransfer *t)
{
    uavcan_node_ExecuteCommand_Response_1_0 resp;
    size_t                                  sz = t->payload_size;
    if (uavcan_node_ExecuteCommand_Response_1_0_deserialize_(&resp, (const uint8_t *)t->payload,
                                                             &sz) < 0) {
        return;
    }
    s_last_cmd.pending  = false;
    s_last_cmd.answered = true;
    s_last_cmd.status   = resp.status;
}

static void dispatch(const CanardRxTransfer *t)
{
    if (t->metadata.transfer_kind == CanardTransferKindMessage) {
        if (t->metadata.port_id == uavcan_node_Heartbeat_1_0_FIXED_PORT_ID_) {
            on_heartbeat(t);
            s_accepted++;
            return;
        }
        const igrow_subject_t *s = igrow_subject_by_id(t->metadata.port_id);
        if (s != NULL) {
            on_subject(t, s);
            s_accepted++;
            return;
        }
        s_unknown++;
        return;
    }
    if (t->metadata.transfer_kind == CanardTransferKindResponse) {
        if (t->metadata.port_id == uavcan_node_GetInfo_1_0_FIXED_PORT_ID_) {
            on_getinfo_response(t);
            s_accepted++;
            return;
        }
        if (t->metadata.port_id == uavcan_node_ExecuteCommand_1_0_FIXED_PORT_ID_) {
            on_execcmd_response(t);
            s_accepted++;
            return;
        }
    }
    s_unknown++;
}

/* Push whatever the transmit queue holds into the CAN peripheral. Expired
 * frames are dropped rather than retried -- a request nobody answered is not
 * worth putting on the bus a second later. */
static void pump_tx(void)
{
    for (const CanardTxQueueItem *ti = NULL; (ti = canardTxPeek(&s_txq)) != NULL;) {
        if ((ti->tx_deadline_usec != 0U) && (ti->tx_deadline_usec < micros64())) {
            s_canard.memory_free(&s_canard, canardTxPop(&s_txq, ti));
            continue;
        }
        if (!can_tx(ti->frame.extended_can_id, (const uint8_t *)ti->frame.payload,
                    (uint8_t)ti->frame.payload_size)) {
            break; /* peripheral queue full; try again next spin */
        }
        s_canard.memory_free(&s_canard, canardTxPop(&s_txq, ti));
    }
}

void cyphal_rx_spin(void)
{
    uint32_t id;
    uint8_t  data[8];
    uint8_t  len;

    /* Bounded so a saturated bus cannot starve the UI. 64 is the FIFO depth. */
    for (unsigned i = 0; i < 64U; i++) {
        if (!can_rx(&id, data, &len)) {
            break;
        }
        const CanardFrame frame = {
            .extended_can_id = id,
            .payload_size    = len,
            .payload         = data,
        };
        CanardRxTransfer transfer;
        const int8_t     r = canardRxAccept(&s_canard, micros64(), &frame, 0, &transfer, NULL);
        if (r == 1) {
            dispatch(&transfer);
            s_canard.memory_free(&s_canard, transfer.payload);
        }
    }

    /* A command with no answer must not stay "pending" for ever. */
    if (s_last_cmd.pending && ((HAL_GetTick() - s_last_cmd.sent_ms) > 2000U)) {
        s_last_cmd.pending = false;
    }

    pump_tx();
}

uint32_t cyphal_rx_accepted(void)
{
    return s_accepted;
}

uint32_t cyphal_rx_unknown(void)
{
    return s_unknown;
}

bool cyphal_request_getinfo(uint8_t target_node_id)
{
    if (can_is_listen_only()) {
        return false;
    }
    uint8_t      buf[uavcan_node_GetInfo_Request_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t       sz = sizeof buf;
    uavcan_node_GetInfo_Request_1_0 req;
    uavcan_node_GetInfo_Request_1_0_initialize_(&req);
    if (uavcan_node_GetInfo_Request_1_0_serialize_(&req, buf, &sz) < 0) {
        return false;
    }
    const CanardTransferMetadata meta = {
        .priority       = CanardPriorityNominal,
        .transfer_kind  = CanardTransferKindRequest,
        .port_id        = uavcan_node_GetInfo_1_0_FIXED_PORT_ID_,
        .remote_node_id = target_node_id,
        .transfer_id    = s_transfer_id_getinfo++,
    };
    return canardTxPush(&s_txq, &s_canard, micros64() + 1000000U, &meta, sz, buf) > 0;
}

bool cyphal_send_command(uint8_t target_node_id, uint16_t command)
{
    if (can_is_listen_only()) {
        return false;
    }
    uavcan_node_ExecuteCommand_Request_1_0 req;
    uavcan_node_ExecuteCommand_Request_1_0_initialize_(&req);
    req.command        = command;
    req.parameter.count = 0;

    uint8_t buf[uavcan_node_ExecuteCommand_Request_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t  sz = sizeof buf;
    if (uavcan_node_ExecuteCommand_Request_1_0_serialize_(&req, buf, &sz) < 0) {
        return false;
    }
    const CanardTransferMetadata meta = {
        .priority       = CanardPriorityNominal,
        .transfer_kind  = CanardTransferKindRequest,
        .port_id        = uavcan_node_ExecuteCommand_1_0_FIXED_PORT_ID_,
        .remote_node_id = target_node_id,
        .transfer_id    = s_transfer_id_execcmd++,
    };
    if (canardTxPush(&s_txq, &s_canard, micros64() + 1000000U, &meta, sz, buf) <= 0) {
        return false;
    }
    s_last_cmd.pending  = true;
    s_last_cmd.answered = false;
    s_last_cmd.target   = target_node_id;
    s_last_cmd.command  = command;
    s_last_cmd.status   = 0;
    s_last_cmd.sent_ms  = HAL_GetTick();
    return true;
}

bool cyphal_publish(uint16_t subject_id, const uint8_t *payload, size_t size,
                    uint8_t *transfer_id)
{
    const CanardTransferMetadata meta = {
        .priority       = CanardPriorityNominal,
        .transfer_kind  = CanardTransferKindMessage,
        .port_id        = subject_id,
        .remote_node_id = CANARD_NODE_ID_UNSET,
        .transfer_id    = (*transfer_id)++,
    };
    return canardTxPush(&s_txq, &s_canard, micros64() + 1000000U, &meta, size, payload) > 0;
}

bool cyphal_send_restart(uint8_t target_node_id)
{
    return cyphal_send_command(target_node_id,
                               uavcan_node_ExecuteCommand_Request_1_0_COMMAND_RESTART);
}

const cyphal_cmd_result_t *cyphal_last_command(void)
{
    return &s_last_cmd;
}
