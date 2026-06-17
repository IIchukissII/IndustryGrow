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

#include "registers.h"
#include "board.h" /* CMSIS: NVIC_SystemReset */
#include "clock.h"
#include "can.h"

#include <string.h>

/* --- memory: a fixed o1heap arena feeds libcanard's allocator --- */
#define CYPHAL_HEAP_SIZE 4096u
#define CYPHAL_TX_QUEUE_CAP 24u

static uint8_t s_arena[CYPHAL_HEAP_SIZE] __attribute__((aligned(O1HEAP_ALIGNMENT)));
static O1HeapInstance *s_heap;

static CanardInstance s_canard;
static CanardTxQueue s_txq;
static CanardRxSubscription s_getinfo_sub;
static CanardRxSubscription s_access_sub;
static CanardRxSubscription s_list_sub;
static CanardRxSubscription s_execcmd_sub;
static bool s_pending_reset; /* set by ExecuteCommand RESTART, acted on after TX flush */

static uint8_t s_hb_tid;       /* heartbeat transfer-id (5-bit, wraps) */
static uint64_t s_start_us;    /* for uptime */
static uint64_t s_next_hb_us;  /* next heartbeat deadline */

static void *mem_alloc(CanardInstance *ins, size_t amount)
{
    return o1heapAllocate((O1HeapInstance *)ins->user_reference, amount);
}

static void mem_free(CanardInstance *ins, void *pointer)
{
    o1heapFree((O1HeapInstance *)ins->user_reference, pointer);
}

/* STM32F405 96-bit unique device ID -> 16-byte Cyphal unique_id (zero-padded). */
static void read_unique_id(uint8_t out[16])
{
    const volatile uint32_t *uid = (const volatile uint32_t *)0x1FFF7A10u;
    memset(out, 0, 16);
    for (int i = 0; i < 3; i++) {
        uint32_t w = uid[i];
        out[i * 4 + 0] = (uint8_t)(w);
        out[i * 4 + 1] = (uint8_t)(w >> 8);
        out[i * 4 + 2] = (uint8_t)(w >> 16);
        out[i * 4 + 3] = (uint8_t)(w >> 24);
    }
}

void cyphal_init(uint8_t node_id)
{
    s_heap = o1heapInit(s_arena, sizeof(s_arena));

    s_canard = canardInit(&mem_alloc, &mem_free);
    s_canard.user_reference = s_heap;
    s_canard.node_id = node_id;

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

    registers_init(node_id);

    s_start_us = micros64();
    s_next_hb_us = s_start_us + 1000000u;
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

static void publish_heartbeat(void)
{
    uavcan_node_Heartbeat_1_0 hb;
    hb.uptime = (uint32_t)((micros64() - s_start_us) / 1000000u);
    hb.health.value = uavcan_node_Health_1_0_NOMINAL;
    hb.mode.value = uavcan_node_Mode_1_0_OPERATIONAL;
    hb.vendor_specific_status_code = 0u;

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

    static const char name[] = "org.industrygrow.node.m05";
    resp.name.count = sizeof(name) - 1u;
    memcpy(resp.name.elements, name, resp.name.count);

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
            }
            s_canard.memory_free(&s_canard, transfer.payload);
        }
    }
}

void cyphal_spin(void)
{
    if (micros64() >= s_next_hb_us) {
        s_next_hb_us += 1000000u;
        publish_heartbeat();
    }
    flush_tx();
    pump_rx();

    /* Honour an ExecuteCommand RESTART once its response has been flushed. */
    if (s_pending_reset && (canardTxPeek(&s_txq) == NULL)) {
        NVIC_SystemReset();
    }
}
