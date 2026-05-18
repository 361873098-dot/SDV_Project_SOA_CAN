/**
 * @file picc_api.c
 * @brief PICC Driver Public API — Implementation
 *
 * This file contains ONLY the 9 public API functions.
 * All internal logic is delegated to:
 *   - picc_mailbox.c  : App registration, mailbox store/read
 *   - picc_service.c  : Event sending, method/response protocol
 *   - picc_link.c     : Link state management
 *
 * Copyright 2024 NXP
 * All Rights Reserved.
 */

#if defined(__cplusplus)
extern "C" {
#endif

#include "picc_api.h"
#include "picc_mailbox.h"
#include "picc_service.h"
#include "picc_link.h"
#include "ipcf_Ip_Cfg_Defines.h"  /* For IPCF_INSTANCE0 */

/*==================================================================================================
 *                              Public API — Initialization (1 function)
 *==================================================================================================*/

sint8 PICC_Init(const PICC_AppConfig_t *config)
{
    sint8 ret;
    sint16 linkIdx;

    if (config == NULL) {
        return PICC_E_PARAM;
    }
    /* ID range check */
    if (config->localId == 0U || config->localId >= PICC_REGISTRY_SIZE) {
        return PICC_E_PARAM;
    }
    if (config->remoteId == 0U || config->remoteId >= PICC_REGISTRY_SIZE) {
        return PICC_E_PARAM;
    }
    if (PICC_MailboxIsReady() == FALSE) {
        return PICC_E_NOT_INIT;
    }

    /* 1. Register app in mailbox context (includes slot pool allocation) */
    ret = PICC_MailboxRegisterApp(config);
    if (ret != PICC_E_OK) {
        return ret;
    }

    /* 2. Register Link (shared pool — both Server and Client allocate an entry)
     *   Server: initial state = DISCONNECTED (passively waits for Client connect)
     *   Client: initial state = CONNECTING   (actively sends connect requests) */
    linkIdx = PICC_LinkRegisterShared(config->localId, config->remoteId,
                                       config->channelId, (uint8)config->role,
                                       config->Client_linkReq_PeriodMs);
    if (linkIdx < 0) {
        /* Link registration failed — but we can't easily undo mailbox registration.
         * Log error but continue. Only negative values indicate actual failure. */
    }
    /* Store linkSharedIdx for both Server and Client roles */
    if (linkIdx >= 0) {
        PICC_MailboxSetLinkSharedIdx(config->localId, (uint16)linkIdx);
    }

    /* 3. Register Method handler (optional) */
    if (config->methodHandler != NULL) {
        (void)PICC_RegisterMethodHandler(config->localId, config->methodHandler);
    }

    /* 4. Register Event handler (optional) */
    if (config->eventHandler != NULL) {
        (void)PICC_RegisterEventHandler(config->remoteId, config->eventHandler);
    }

    return PICC_E_OK;
}

/*==================================================================================================
 *                              Public API — Sending M->A (3 functions)
 *==================================================================================================*/

sint8 PICC_SendEvent(uint8 localId, uint8 eventId,
                     const uint8 *data, uint16 len, PICC_EventType_e withAck)
{
    const PICC_AppConfig_t *cfg;
    sint8 ret;

    if (PICC_MailboxIsReady() == FALSE) {
        return PICC_E_NOT_INIT;
    }

    ret = PICC_MailboxGetAppConfig(localId, &cfg);
    if (ret != PICC_E_OK) {
        return PICC_E_PARAM;
    }

    if (PICC_LinkGetStateByIdx(PICC_MailboxGetLinkSharedIdx(cfg->localId)) != PICC_LINK_STATE_CONNECTED) {
        return PICC_E_NOT_CONNECTED;
    }

    return PICC_ServiceEventSend(cfg->localId, eventId, cfg->remoteId,
                                 data, len, withAck, cfg->channelId);
}

uint8 PICC_MethodRequest(uint8 localId, uint8 methodId,
                         const uint8 *data, uint16 len,
                         PICC_MethodType_e type)
{
    const PICC_AppConfig_t *cfg;
    sint8 ret;

    if (PICC_MailboxIsReady() == FALSE) {
        return 0U;
    }

    ret = PICC_MailboxGetAppConfig(localId, &cfg);
    if (ret != PICC_E_OK) {
        return 0U;
    }

    if (PICC_LinkGetStateByIdx(PICC_MailboxGetLinkSharedIdx(cfg->localId)) != PICC_LINK_STATE_CONNECTED) {
        return 0U;
    }

    return PICC_ServiceMethodSend(cfg->remoteId, methodId, data, len, type,
                                  IPCF_INSTANCE0, cfg->channelId);
}

sint8 PICC_MethodResponse(uint8 localId, uint8 methodId,
                          uint8 sessionId, uint8 returnCode,
                          const uint8 *data, uint16 len)
{
    const PICC_AppConfig_t *cfg;
    sint8 ret;

    if (PICC_MailboxIsReady() == FALSE) {
        return PICC_E_NOT_INIT;
    }

    ret = PICC_MailboxGetAppConfig(localId, &cfg);
    if (ret != PICC_E_OK) {
        return PICC_E_PARAM;
    }

    return PICC_ServiceResponseSend(cfg->localId, cfg->remoteId, methodId, sessionId,
                                    returnCode, data, len, IPCF_INSTANCE0, cfg->channelId);
}

/*==================================================================================================
 *                              Public API — Receiving A->M (3 functions)
 *==================================================================================================*/

sint8 PICC_GetMethodData(uint8 localId, uint8 methodId,
                         uint8 *data, uint16 maxLen, uint16 *actualLen,
                         uint8 *outSessionId,
                         uint8 *cbResult, uint16 *cbResultLen)
{
    return PICC_MailboxGetMethodData(localId, methodId, data, maxLen, actualLen,
                                     outSessionId, cbResult, cbResultLen);
}

sint8 PICC_GetResponseData(uint8 localId, uint8 methodId,
                           uint8 sessionId, uint8 *returnCode,
                           uint8 *data, uint16 maxLen, uint16 *actualLen,
                           uint8 *cbResult, uint16 *cbResultLen)
{
    return PICC_MailboxGetResponseData(localId, methodId, sessionId, returnCode,
                                       data, maxLen, actualLen,
                                       cbResult, cbResultLen);
}

sint8 PICC_GetEventData(uint8 localId, uint8 eventId,
                        uint8 *data, uint16 maxLen, uint16 *actualLen,
                        uint8 *cbResult, uint16 *cbResultLen)
{
    return PICC_MailboxGetEventData(localId, eventId, data, maxLen, actualLen,
                                    cbResult, cbResultLen);
}

/*==================================================================================================
 *                              Public API — Status (2 functions)
 *==================================================================================================*/

PICC_LinkState_e PICC_GetLinkState(uint8 channelId)
{
    return PICC_LinkGetState(channelId);
}

PICC_LinkState_e PICC_GetAppLinkState(uint8 localId)
{
    const PICC_AppConfig_t *cfg;
    sint8 ret;

    ret = PICC_MailboxGetAppConfig(localId, &cfg);
    if (ret != PICC_E_OK) {
        return PICC_LINK_STATE_DISCONNECTED;
    }

    return PICC_LinkGetStateByIdx(PICC_MailboxGetLinkSharedIdx(cfg->localId));
}

#if defined(__cplusplus)
}
#endif
