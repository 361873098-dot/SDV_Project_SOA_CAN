/**
 * @file picc_link.c
 * @brief M-Core Inter-Core Communication Link Management Layer - Implementation
 *
 * Implements shared link pool for per-app connection management.
 * Links are deduplicated by (remoteId, channelId, role) — multiple apps
 * with the same (remoteId, channelId, role) share one link context.
 * Both Server and Client apps allocate a link entry:
 *   - Server: initial state = DISCONNECTED, passively waits for Client requests.
 *   - Client: initial state = CONNECTING, actively sends link-connect requests.
 * Server needs link entry for: state tracking, disconnect/reconnect handling,
 * and guarding PICC_SendEvent()/PICC_MethodResponse() with link state check.
 *
 * Note: Heartbeat (Ping/Pong) is in picc_heartbeat.c
 *
 * Copyright 2024 NXP
 * All Rights Reserved.
 */

#if defined(__cplusplus)
extern "C" {
#endif

#include "picc_link.h"
#include "picc_mailbox.h"  /* For PICC_MailboxGetLinkSharedIdx */
#include "FreeRTOS.h"
#include "Picc_main.h" /* For PICC_HANDLE_ERROR */
#include "ipc-shm.h"
#include "ipcf_Ip_Cfg_Defines.h" /* For IPCF_INSTANCE0 */
#include "picc_stack.h"
#include "picc_heartbeat.h" /* For PICC_HeartbeatGetMissCount */
#include "task.h" /* For taskENTER_CRITICAL, taskEXIT_CRITICAL */

/*==================================================================================================
 *                                         Macro Definitions
 *==================================================================================================*/

/** Send flow control: maximum backoff value (100 * 10ms = 1000ms) */
#define PICC_SEND_BACKOFF_MAX       (100U)

/** Send flow control: backoff increment */
#define PICC_SEND_BACKOFF_INCREMENT (10U)

/*==================================================================================================
 *                                         Private Variables
 *==================================================================================================*/

/** Shared link pool — deduplicated by (remoteId, channelId, role) */
static PICC_LinkShared_t g_linkSharedPool[PICC_LINK_SHARED_POOL_SIZE];

/*==================================================================================================
 *                                         Private Function Declarations
 *==================================================================================================*/

static PICC_LinkShared_t* PICC_GetLinkSharedByIds(uint8 localId, uint8 remoteId);
static PICC_LinkShared_t* PICC_GetLinkSharedByHeader(uint8 providerId, uint8 consumerId, uint8 channelId);

/*==================================================================================================
 *                                         Private Functions
 *==================================================================================================*/

/**
 * @brief Get shared link context by local/remote ID pair
 */
static PICC_LinkShared_t* PICC_GetLinkSharedByIds(uint8 localId, uint8 remoteId)
{
    uint16 i;
    for (i = 0U; i < PICC_LINK_SHARED_POOL_SIZE; i++) {
        if ((g_linkSharedPool[i].isInitialized != 0U) &&
            (g_linkSharedPool[i].config.localId == localId) &&
            (g_linkSharedPool[i].config.remoteId == remoteId)) {
            return &g_linkSharedPool[i];
        }
    }
    return NULL;
}

/**
 * @brief Get shared link context by protocol header + channelId
 */
static PICC_LinkShared_t* PICC_GetLinkSharedByHeader(uint8 providerId, uint8 consumerId, uint8 channelId)
{
    uint16 i;
    PICC_LinkShared_t *ctx;

    for (i = 0U; i < PICC_LINK_SHARED_POOL_SIZE; i++) {
        ctx = &g_linkSharedPool[i];
        if (ctx->isInitialized == 0U) {
            continue;
        }
        if (ctx->config.channelId != channelId) {
            continue;
        }

        if (ctx->config.role == PICC_ROLE_SERVER) {
            if ((ctx->config.localId == providerId) &&
                (ctx->config.remoteId == consumerId)) {
                return ctx;
            }
        } else {
            if ((ctx->config.localId == consumerId) &&
                (ctx->config.remoteId == providerId)) {
                return ctx;
            }
        }
    }
    return NULL;
}

/**
 * @brief Send link related message (unified through Stack stacking)
 */
static sint8 PICC_LinkSendMessage(uint8 providerId, uint8 consumerId,
                                  PICC_LinkSubType_e subType, uint8 returnCode,
                                  uint8 instanceId, uint8 channelId)
{
    uint8 txBuf[PICC_HEADER_SIZE + sizeof(PICC_LinkPayload_t)];
    PICC_MsgHeader_t header;
    PICC_LinkPayload_t linkPayload;
    uint32 packedLen;

    (void)instanceId;

    if (ipc_shm_is_remote_ready(IPCF_INSTANCE0) != 0) {
        if ((returnCode != (uint8)PICC_RET_OK) && (subType == PICC_LINK_CONNECT)) {
            return -10;
        }
    }

    header.providerId = providerId;
    header.methodId   = PICC_LINK_METHOD_ID;
    header.consumerId = consumerId;
    header.sessionId  = 0x00U;
    header.msgType    = (uint8)PICC_MSG_LINK_AVAILABLE;
    header.returnCode = returnCode;

    linkPayload.subType   = (uint8)subType;
    linkPayload.clientId  = consumerId;
    linkPayload.mediaType = (uint8)PICC_MEDIA_IPCF;
    linkPayload.serverId  = providerId;

    packedLen = PICC_PackMessage(txBuf, sizeof(txBuf), &header,
                                 (const uint8 *)&linkPayload, sizeof(PICC_LinkPayload_t));
    if (packedLen == 0U) {
        PICC_HANDLE_ERROR(-1);
        return -1;
    }

    return PICC_StackAddMessageToChannel(channelId, txBuf, packedLen);
}

/**
 * @brief Update connection state (critical section protected)
 */
static void PICC_LinkSetState(PICC_LinkShared_t *ctx, PICC_LinkState_e newState)
{
    taskENTER_CRITICAL();
    ctx->state = newState;
    taskEXIT_CRITICAL();
}

/*==================================================================================================
 *                                         Public Functions
 *==================================================================================================*/

/**
 * @brief Process link management — called from PICC periodic task (10ms)
 *
 * Iterates over all shared link contexts. For each CLIENT in CONNECTING state:
 *   1. Checks if the physical channel (heartbeat) is connected
 *   2. Periodically sends link-connect requests to A-Core Server
 *   3. Implements exponential backoff on send failure
 *
 * Server entries exist in the pool (for state tracking) but are skipped
 * during processing — Server passively waits for Client connections.
 */
void PICC_LinkProcess(void)
{
    uint16 i;
    PICC_LinkShared_t *ctx;

    for (i = 0U; i < PICC_LINK_SHARED_POOL_SIZE; i++) {
        ctx = &g_linkSharedPool[i];

        /* Skip uninitialized or unreferenced entries */
        if ((ctx->isInitialized == 0U) || (ctx->refCount == 0U)) {
            continue;
        }

        /* Only CLIENT role in CONNECTING state sends connection requests */
        if ((ctx->config.role == PICC_ROLE_CLIENT) &&
            (ctx->state == PICC_LINK_STATE_CONNECTING)) {

            /* Do not send link requests if physical channel (heartbeat) is down */
            if (PICC_LinkGetState(ctx->config.channelId) != PICC_LINK_STATE_CONNECTED) {
                continue;
            }

            uint16 periodTicks;
            sint8 sendResult;

            ctx->periodCounter++;

            /* Calculate period in 10ms ticks (default: 10ms / 10 = 1 tick) */
            if (ctx->config.Client_linkReq_PeriodMs == 0U) {
                periodTicks = PICC_LINK_REQUEST_PERIOD_MS / 10U;
            } else {
                periodTicks = ctx->config.Client_linkReq_PeriodMs / 10U;
            }
            if (periodTicks == 0U) {
                periodTicks = 1U;
            }

            if (ctx->periodCounter >= periodTicks) {
                ctx->periodCounter = 0U;

                if (ctx->backoffCounter > 0U) {
                    /* Backoff active — skip this cycle */
                    ctx->backoffCounter--;
                } else {
                    /* Send link-connect request: ProviderID=remoteId, ConsumerID=localId
                     * ReturnCode=NOT_OK indicates initial request (per protocol) */
                    sendResult = PICC_LinkSendMessage(
                        ctx->config.remoteId,
                        ctx->config.localId,
                        PICC_LINK_CONNECT,
                        (uint8)PICC_RET_NOT_OK,
                        ctx->config.instanceId,
                        ctx->config.channelId);

                    if (sendResult != 0) {
                        /* Send failed — exponential backoff: 10, 20, 40, ..., max 100 */
                        if (ctx->backoffCounter == 0U) {
                            ctx->backoffCounter = PICC_SEND_BACKOFF_INCREMENT;
                        } else {
                            ctx->backoffCounter = ctx->backoffCounter * 2U;
                            if (ctx->backoffCounter > PICC_SEND_BACKOFF_MAX) {
                                ctx->backoffCounter = PICC_SEND_BACKOFF_MAX;
                            }
                        }
                    } else {
                        /* Send succeeded — reset backoff */
                        ctx->backoffCounter = 0U;
                    }
                }
            }
        }
    }
}

void PICC_LinkLayerInit(void)
{
    uint16 i;

    for (i = 0U; i < PICC_LINK_SHARED_POOL_SIZE; i++) {
        g_linkSharedPool[i].isInitialized = 0U;
        g_linkSharedPool[i].state = PICC_LINK_STATE_DISCONNECTED;
        g_linkSharedPool[i].periodCounter = 0U;
        g_linkSharedPool[i].backoffCounter = 0U;
        g_linkSharedPool[i].refCount = 0U;
    }
}

sint16 PICC_LinkRegisterShared(uint8 localId, uint8 remoteId,
                               uint8 channelId, uint8 role,
                               uint16 linkReqPeriodMs)
{
    uint16 i;
    PICC_LinkState_e initialState;

    /* Determine initial state based on role:
     *   Server: DISCONNECTED — passively waits for Client connect request.
     *   Client: CONNECTING   — actively sends link-connect requests. */
    if (role == (uint8)PICC_ROLE_SERVER) {
        initialState = PICC_LINK_STATE_DISCONNECTED;
    } else {
        initialState = PICC_LINK_STATE_CONNECTING;
    }

    /* Find existing shared link by (remoteId, channelId, role) */
    for (i = 0U; i < PICC_LINK_SHARED_POOL_SIZE; i++) {
        if ((g_linkSharedPool[i].isInitialized != 0U) &&
            (g_linkSharedPool[i].config.remoteId == remoteId) &&
            (g_linkSharedPool[i].config.channelId == channelId) &&
            (g_linkSharedPool[i].config.role == (PICC_Role_e)role)) {
            /* Found existing link — share it */
            g_linkSharedPool[i].refCount++;
            return (sint16)i;
        }
    }

    /* Allocate new link from pool */
    for (i = 0U; i < PICC_LINK_SHARED_POOL_SIZE; i++) {
        if (g_linkSharedPool[i].isInitialized == 0U) {
            g_linkSharedPool[i].config.localId = localId;
            g_linkSharedPool[i].config.remoteId = remoteId;
            g_linkSharedPool[i].config.role = (PICC_Role_e)role;
            g_linkSharedPool[i].config.channelId = channelId;
            g_linkSharedPool[i].config.instanceId = IPCF_INSTANCE0;
            g_linkSharedPool[i].config.Client_linkReq_PeriodMs = linkReqPeriodMs;
            g_linkSharedPool[i].periodCounter = 0U;
            g_linkSharedPool[i].backoffCounter = 0U;
            g_linkSharedPool[i].refCount = 1U;
            g_linkSharedPool[i].state = initialState;
            g_linkSharedPool[i].isInitialized = 1U;
            return (sint16)i;
        }
    }

    PICC_HANDLE_ERROR(-4);
    return -1;  /* No free slot */
}

void PICC_LinkDeinit(void)
{
    uint16 i;

    for (i = 0U; i < PICC_LINK_SHARED_POOL_SIZE; i++) {
        g_linkSharedPool[i].isInitialized = 0U;
        g_linkSharedPool[i].state = PICC_LINK_STATE_DISCONNECTED;
        g_linkSharedPool[i].periodCounter = 0U;
        g_linkSharedPool[i].backoffCounter = 0U;
        g_linkSharedPool[i].refCount = 0U;
    }
}

/**
 * @brief Handle link-connect response (Client role)
 *
 * Called when M-Core (Client) receives a link-connect reply from A-Core Server.
 * If returnCode=OK: transition to CONNECTED state.
 * If returnCode=NOT_OK: transition to DISCONNECTED (Server rejected).
 */
sint8 PICC_LinkHandleResponse(const PICC_MsgHeader_t *header,
                              const uint8 *payload, uint16 len,
                              uint8 instanceId, uint8 channelId)
{
    const PICC_LinkPayload_t *linkPayload;
    PICC_LinkShared_t *ctx;

    (void)len;
    (void)instanceId;

    if ((header == NULL) || (payload == NULL)) {
        PICC_HANDLE_ERROR(-6);
        return -1;
    }

    linkPayload = (const PICC_LinkPayload_t *)payload;

    ctx = PICC_GetLinkSharedByHeader(header->providerId, header->consumerId, channelId);
    if ((ctx != NULL) && (ctx->config.role == PICC_ROLE_CLIENT)) {
        if (linkPayload->subType == (uint8)PICC_LINK_CONNECT) {
            if (header->returnCode == (uint8)PICC_RET_OK) {
                /* Server accepted connection — link established */
                PICC_LinkSetState(ctx, PICC_LINK_STATE_CONNECTED);
            } else {
                /* Server rejected connection */
                PICC_LinkSetState(ctx, PICC_LINK_STATE_DISCONNECTED);
            }
        }
    }

    return 0;
}

/**
 * @brief Handle link-connect request (Server role)
 *
 * Called when A-Core Client sends a link-connect request to M-Core Server.
 * If the physical channel (heartbeat) is healthy, reply with OK and
 * transition to CONNECTED state.
 */
sint8 PICC_LinkHandleRequest(const PICC_MsgHeader_t *header,
                             const uint8 *payload, uint16 len,
                             uint8 instanceId, uint8 channelId)
{
    const PICC_LinkPayload_t *linkPayload;
    PICC_LinkShared_t *ctx;
    sint8 ret;

    (void)len;
    (void)instanceId;

    if ((header == NULL) || (payload == NULL)) {
        PICC_HANDLE_ERROR(-7);
        return -1;
    }

    linkPayload = (const PICC_LinkPayload_t *)payload;

    if (linkPayload->subType == (uint8)PICC_LINK_CONNECT) {

        /* Reject if physical channel (heartbeat) is not connected */
        if (PICC_LinkGetState(channelId) != PICC_LINK_STATE_CONNECTED) {
            return 0;
        }

        ctx = PICC_GetLinkSharedByHeader(header->providerId, header->consumerId, channelId);
        if ((ctx != NULL) && (ctx->config.role == PICC_ROLE_SERVER)) {

            /* Reply with OK — Server accepts connection */
            ret = PICC_LinkSendMessage(ctx->config.localId,
                                       header->consumerId,
                                       PICC_LINK_CONNECT,
                                       (uint8)PICC_RET_OK,
                                       ctx->config.instanceId,
                                       ctx->config.channelId);
            if (ret == 0) {
                PICC_LinkSetState(ctx, PICC_LINK_STATE_CONNECTED);
            } else {
                PICC_HANDLE_ERROR(-8);
            }
        }
    }

    return 0;
}

/**
 * @brief Handle disconnect/reconnect notification
 *
 * Disconnect: A-Core notifies M-Core that it is disconnecting.
 *   - M-Core replies with disconnect ACK
 *   - If M-Core is Client: transition to CONNECTING to restart link requests
 *   - If M-Core is Server: transition to DISCONNECTED
 *
 * Reconnect: A-Core Server restarted after crash.
 *   - M-Core Client should re-initiate connection requests
 *   - Per IPCF protocol: A-Core Server sends reconnect, M-Core Client
 *     responds by sending new link-connect requests
 */
sint8 PICC_LinkHandleDisconnect(const PICC_MsgHeader_t *header,
                                const uint8 *payload, uint16 len,
                                uint8 instanceId, uint8 channelId)
{
    const PICC_LinkPayload_t *linkPayload;
    PICC_LinkShared_t *ctx;

    (void)len;
    (void)instanceId;

    if (payload == NULL) {
        PICC_HANDLE_ERROR(-10);
        return -1;
    }
    linkPayload = (const PICC_LinkPayload_t *)payload;

    ctx = PICC_GetLinkSharedByHeader(header->providerId, header->consumerId, channelId);
    if (ctx == NULL) {
        return 0;
    }

    if (linkPayload->subType == (uint8)PICC_LINK_DISCONNECT) {

        PICC_LinkSetState(ctx, PICC_LINK_STATE_DISCONNECTED);

        /* Reply with disconnect ACK (ReturnCode=OK) */
        (void)PICC_LinkSendMessage(header->providerId,
                                   header->consumerId,
                                   PICC_LINK_DISCONNECT,
                                   (uint8)PICC_RET_OK,
                                   IPCF_INSTANCE0, channelId);

        /* Client role: immediately re-enter CONNECTING to restart link requests */
        if (ctx->config.role == PICC_ROLE_CLIENT) {
            PICC_LinkSetState(ctx, PICC_LINK_STATE_CONNECTING);
            ctx->periodCounter = 0U;
            ctx->backoffCounter = 0U;
        }

    } else if (linkPayload->subType == (uint8)PICC_LINK_RECONNECT) {
        /* A-Core Server crash-restart: M-Core Client must re-connect */
        PICC_LinkSetState(ctx, PICC_LINK_STATE_DISCONNECTED);
        if (ctx->config.role == PICC_ROLE_CLIENT) {
            PICC_LinkSetState(ctx, PICC_LINK_STATE_CONNECTING);
            ctx->periodCounter = 0U;
            ctx->backoffCounter = 0U;
        }
    }

    return 0;
}

PICC_LinkState_e PICC_LinkGetState(uint8 channelId)
{
    uint8 missCount;

    missCount = PICC_HeartbeatGetMissCount(IPCF_INSTANCE0, channelId);
    if (missCount < PICC_HEARTBEAT_TIMEOUT_COUNT) {
        return PICC_LINK_STATE_CONNECTED;
    }
    return PICC_LINK_STATE_DISCONNECTED;
}

PICC_LinkState_e PICC_LinkGetStateByIdx(uint16 linkSharedIdx)
{
    if (linkSharedIdx >= PICC_LINK_SHARED_POOL_SIZE) {
        return PICC_LINK_STATE_DISCONNECTED;
    }
    if (g_linkSharedPool[linkSharedIdx].isInitialized == 0U) {
        return PICC_LINK_STATE_DISCONNECTED;
    }
    return g_linkSharedPool[linkSharedIdx].state;
}

PICC_LinkState_e PICC_LinkGetStateByIds(uint8 localId, uint8 remoteId)
{
    PICC_LinkShared_t *ctx = PICC_GetLinkSharedByIds(localId, remoteId);
    if (ctx != NULL) {
        return ctx->state;
    }
    return PICC_LINK_STATE_DISCONNECTED;
}

void PICC_LinkTriggerReconnect(uint8 instanceId, uint8 channelId)
{
    uint16 i;
    PICC_LinkShared_t *ctx;

    (void)instanceId;

    for (i = 0U; i < PICC_LINK_SHARED_POOL_SIZE; i++) {
        ctx = &g_linkSharedPool[i];

        if (ctx->isInitialized == 0U) {
            continue;
        }
        if (ctx->config.channelId != channelId) {
            continue;
        }

        if (ctx->config.role == PICC_ROLE_CLIENT) {
            PICC_LinkSetState(ctx, PICC_LINK_STATE_CONNECTING);
            ctx->periodCounter = 0U;
            ctx->backoffCounter = 0U;
        } else {
            PICC_LinkSetState(ctx, PICC_LINK_STATE_DISCONNECTED);
        }
    }
}

sint8 PICC_LinkProcessMessage(const PICC_MsgHeader_t *header,
                              const uint8 *payload, uint16 len,
                              uint8 instanceId, uint8 channelId)
{
    const PICC_LinkPayload_t *linkPayload;
    PICC_LinkShared_t *ctx;

    if ((header == NULL) || (payload == NULL)) {
        PICC_HANDLE_ERROR(-11);
        return -1;
    }

    if (len < 1U) {
        PICC_HANDLE_ERROR(-12);
        return -1;
    }

    linkPayload = (const PICC_LinkPayload_t *)payload;

    ctx = PICC_GetLinkSharedByHeader(header->providerId, header->consumerId, channelId);
    if (ctx == NULL) {
        return 0;
    }

    switch (linkPayload->subType) {
        case (uint8)PICC_LINK_CONNECT:
            if (ctx->config.role == PICC_ROLE_SERVER) {
                return PICC_LinkHandleRequest(header, payload, len, instanceId, channelId);
            } else {
                return PICC_LinkHandleResponse(header, payload, len, instanceId, channelId);
            }

        case (uint8)PICC_LINK_DISCONNECT:
        case (uint8)PICC_LINK_RECONNECT:
            return PICC_LinkHandleDisconnect(header, payload, len, instanceId, channelId);

        default:
            break;
    }

    return 0;
}

#if defined(__cplusplus)
}
#endif
