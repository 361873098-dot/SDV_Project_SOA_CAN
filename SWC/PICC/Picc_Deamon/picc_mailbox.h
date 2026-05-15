/**
 * @file picc_mailbox.h
 * @brief PICC Mailbox Module — Internal Driver Interface
 *
 * Manages per-application receive mailboxes and app registration context.
 * This is an INTERNAL driver header — application layers should NOT include this file.
 * Application layers only include picc_api.h.
 *
 * Copyright 2024 NXP
 * All Rights Reserved.
 */

#ifndef PICC_MAILBOX_H
#define PICC_MAILBOX_H

#if defined(__cplusplus)
extern "C" {
#endif

#include "picc_api.h"        /* PICC_REGISTRY_SIZE, PICC_AppConfig_t, error codes */
#include "picc_protocol.h"   /* PICC_MsgHeader_t */

/** Maximum callback result size in bytes */
#define PICC_CB_RESULT_MAX_LEN  (8U)

/** Role-based default slot allocation
 *
 * SERVER: receives Method requests, sends Event notifications.
 *   - methodSlots   = PICC_SERVER_DEFAULT_METHOD_SLOTS   (needs to store incoming Requests)
 *   - responseSlots = 0                                    (Server never sends Request, no Response to wait for)
 *   - eventSlots    = PICC_SERVER_DEFAULT_EVENT_SLOTS     (may receive Events from A-core)
 *
 * CLIENT: sends Method requests, receives Event notifications.
 *   - methodSlots   = 0                                    (Client never receives Method Requests)
 *   - responseSlots = PICC_CLIENT_DEFAULT_RESPONSE_SLOTS  (needs to wait for Responses)
 *   - eventSlots    = PICC_CLIENT_DEFAULT_EVENT_SLOTS     (receives Events from Server)
 *
 * These defaults are applied automatically in PICC_MailboxRegisterApp()
 * when the application does not explicitly specify slot counts.
 *
 * TUNING GUIDE: Each slot ≈ 52 bytes. Total slot pool = PICC_SLOT_POOL_SIZE × 52.
 *   - 2 slots/type: 120 apps × 4 = 480 slots ≈ 25 KB  (conservative, most apps only have 1-3 IDs)
 *   - 4 slots/type:  60 apps × 8 = 480 slots ≈ 25 KB  (moderate headroom per app)
 *   - 6 slots/type:  40 apps ×12 = 480 slots ≈ 25 KB  (generous, fewer apps)
 * Adjust these macros AND PICC_SLOT_POOL_SIZE together to match your system scale.
 */
#define PICC_SERVER_DEFAULT_METHOD_SLOTS    (2U)
#define PICC_SERVER_DEFAULT_EVENT_SLOTS     (2U)
#define PICC_CLIENT_DEFAULT_RESPONSE_SLOTS  (2U)
#define PICC_CLIENT_DEFAULT_EVENT_SLOTS     (2U)

/** Legacy macro kept for backward compatibility — no longer used by role-based logic */
#define PICC_DEFAULT_SLOTS                  (2U)

/*==================================================================================================
 *                                  Mailbox Initialization
 *==================================================================================================*/

/**
 * @brief Initialize all global resources (called by PICC_InfraInit)
 *
 * Clears g_appRegistry, g_slotPool, g_remoteToLocal, and resets
 * g_slotPoolNextFree. Called once during PICC_InfraInit().
 */
void PICC_GlobalInit(void);

/**
 * @brief Mark infrastructure ready (called after PICC_GlobalInit)
 */
void PICC_MailboxReady(void);

/**
 * @brief Check if infrastructure is initialized
 * @return TRUE if ready, FALSE otherwise
 */
boolean PICC_MailboxIsReady(void);

/*==================================================================================================
 *                                  App Registration
 *==================================================================================================*/

/**
 * @brief Register an application's config in the mailbox context
 *
 * @param[in] config    Application configuration
 * @return PICC_E_OK on success, PICC_E_PARAM on invalid params,
 *         PICC_E_DUPLICATE on duplicate localId,
 *         PICC_E_NOMEM on slot pool exhaustion,
 *         PICC_E_REMOTE_ID_CONFLICT on remoteId mapping conflict
 */
sint8 PICC_MailboxRegisterApp(const PICC_AppConfig_t *config);

/**
 * @brief Check if an application is registered
 */
boolean PICC_MailboxIsAppRegistered(uint8 localId);

/**
 * @brief Get a registered application's config
 *
 * @param[in]  localId   Application local ID
 * @param[out] config    Output config pointer
 * @return PICC_E_OK on success, PICC_E_PARAM if not registered or out of range
 */
sint8 PICC_MailboxGetAppConfig(uint8 localId, const PICC_AppConfig_t **config);

/**
 * @brief Get the linkSharedIdx for a given localId
 *
 * @param[in] localId Application local ID
 * @return linkSharedIdx (0xFFFF = not allocated yet)
 */
uint16 PICC_MailboxGetLinkSharedIdx(uint8 localId);

/**
 * @brief Set the linkSharedIdx for a given localId
 *
 * Called by PICC_Init() after PICC_LinkRegisterShared() returns the pool index.
 *
 * @param[in] localId        Application local ID
 * @param[in] linkSharedIdx  Link pool index from PICC_LinkRegisterShared()
 */
void PICC_MailboxSetLinkSharedIdx(uint8 localId, uint16 linkSharedIdx);

/*==================================================================================================
 *                                  Mailbox Store (incoming messages)
 *==================================================================================================*/

/**
 * @brief Store received message into the appropriate mailbox
 *
 * Called from PICC_ProcessSingleMessage() in picc_main.c.
 * Routes message to the correct app's mailbox based on header IDs.
 * Includes channelId defense check.
 *
 * @param[in] header      Protocol header
 * @param[in] payload     Message payload
 * @param[in] payloadLen  Payload length
 * @param[in] channelId   IPCF channel ID the message was received on
 */
void PICC_StoreToMailbox(const PICC_MsgHeader_t *header,
                          const uint8 *payload, uint16 payloadLen,
                          uint8 channelId);

/**
 * @brief Store callback result into the mailbox slot that was just written
 *
 * Called from PICC_ProcessSingleMessage() after the callback returns.
 *
 * @param[in] header      Protocol header (used to find the correct slot)
 * @param[in] channelId   IPCF channel ID
 * @param[in] cbResult    Callback result data
 * @param[in] cbResultLen Callback result length (max PICC_CB_RESULT_MAX_LEN)
 */
void PICC_StoreCallbackResult(const PICC_MsgHeader_t *header,
                               uint8 channelId,
                               const uint8 *cbResult, uint16 cbResultLen);

/*==================================================================================================
 *                                  Mailbox Read (polling)
 *==================================================================================================*/

/**
 * @brief Read Method request data from mailbox
 */
sint8 PICC_MailboxGetMethodData(uint8 localId, uint8 methodId,
                                uint8 *data, uint16 maxLen, uint16 *actualLen,
                                uint8 *outSessionId,
                                uint8 *cbResult, uint16 *cbResultLen);

/**
 * @brief Read Method response data from mailbox
 */
sint8 PICC_MailboxGetResponseData(uint8 localId, uint8 methodId,
                                  uint8 sessionId, uint8 *returnCode,
                                  uint8 *data, uint16 maxLen, uint16 *actualLen,
                                  uint8 *cbResult, uint16 *cbResultLen);

/**
 * @brief Read Event notification data from mailbox
 */
sint8 PICC_MailboxGetEventData(uint8 localId, uint8 eventId,
                               uint8 *data, uint16 maxLen, uint16 *actualLen,
                               uint8 *cbResult, uint16 *cbResultLen);

/*==================================================================================================
 *                                  Remote ID Mapping
 *==================================================================================================*/

/**
 * @brief Register remote-to-local ID mapping
 *
 * @param[in] remoteId  Remote ID
 * @param[in] localId   Local ID it maps to
 * @return PICC_E_OK on success, PICC_E_REMOTE_ID_CONFLICT on conflict
 */
sint8 PICC_RegisterRemoteMapping(uint8 remoteId, uint8 localId);

#if defined(__cplusplus)
}
#endif

#endif /* PICC_MAILBOX_H */
