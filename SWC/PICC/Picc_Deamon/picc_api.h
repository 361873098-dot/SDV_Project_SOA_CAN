/**
 * @file picc_api.h
 * @brief PICC Driver Public API — Application Layer Interface
 *
 * This is the ONLY header file that application layers (Pwsm, OTA, etc.) should include.
 * It exposes exactly 8 public functions:
 *   - PICC_Init()          : Register an application
 *   - PICC_SendEvent()     : Send Event notification (M->A)  [declared in picc_service.h]
 *   - PICC_MethodRequest() : Send Method request, Client (M->A)
 *   - PICC_MethodResponse(): Send Method response, Server (M->A)
 *   - PICC_GetMethodData() : Get received Method request data (A->M)
 *   - PICC_GetResponseData(): Get received Method response data (A->M)
 *   - PICC_GetEventData()  : Get received Event data (A->M)
 *   - PICC_GetLinkState()  : Query link connection state
 *
 * Copyright 2024 NXP
 * All Rights Reserved.
 */

#ifndef PICC_API_H
#define PICC_API_H

#if defined(__cplusplus)
extern "C" {
#endif

#include "picc_service.h"   /* PICC_SendEvent, PICC_EventCallback_t, PICC_MethodCallback_t */
#include "picc_link.h"      /* PICC_LinkState_e */
#include "picc_id_map.h"   /* PICC_ID_xxx_LOCAL / PICC_ID_xxx_REMOTE */

/*==================================================================================================
 *                                         Error Codes
 *==================================================================================================*/

#define PICC_E_OK               (0)
#define PICC_E_NOT_INIT         (-1)
#define PICC_E_PARAM            (-2)
#define PICC_E_NOMEM            (-3)
#define PICC_E_SEND             (-4)
#define PICC_E_NOT_CONNECTED    (-5)
#define PICC_E_NO_DATA          (-6)   /**< No new data available */
#define PICC_E_DUPLICATE        (-7)   /**< Duplicate localId registration */
#define PICC_E_REMOTE_ID_CONFLICT (-8) /**< Remote ID mapping conflict */

/*==================================================================================================
 *                                         Registry Configuration
 *==================================================================================================*/

/** Maximum registry size — supports localId/remoteId 1~254 as direct array index.
 *  Set to 256 to accommodate 120+ applications per protocol ID range [1, 254].
 *  All internal arrays (g_appRegistry, g_remoteToLocal, g_appConfigs, appLinkState)
 *  are sized by this macro. Increasing it linearly increases .bss RAM usage. */
#define PICC_REGISTRY_SIZE      (256U)

/*==================================================================================================
 *                                         Application Configuration
 *==================================================================================================*/

/**
 * @brief Per-application PICC configuration
 *
 * Passed to PICC_Init() to register one application with the PICC driver.
 * All internal registrations (Link, Method handler, Event handler) are
 * performed automatically inside PICC_Init().
 *
 * Callback Design Principle:
 *   Both callback fields (methodHandler and eventHandler) are OPTIONAL.
 *   Passing NULL means the application uses polling mode for that feature.
 *   Passing a valid function pointer enables immediate callback mode in
 *   addition to polling.
 *
 * Link state handling:
 *   Link state callbacks are intentionally not part of the application
 *   configuration. Applications query link state explicitly through the
 *   public PICC_GetLinkState(channelId) API when needed.
 *
 * methodHandler:
 *   - NULL  : Received Method requests are stored in the internal mailbox.
 *             Application retrieves data via PICC_GetMethodData() polling.
 *             Suitable for non-time-critical services (e.g., power management).
 *   - !NULL : Driver calls this function immediately when an A-core Method
 *             request arrives. The callback can return response data directly.
 *             Suitable for time-critical services (e.g., OTA flash write,
 *             diagnostic data read) where 10ms polling delay is unacceptable.
 *             NOTE: Mailbox storage still occurs even when callback is set.
 *
 * eventHandler:
 *   - NULL  : Received Event notifications are stored in the internal mailbox.
 *             Application retrieves data via PICC_GetEventData() polling.
 *   - !NULL : Driver calls this function immediately when an A-core Event
 *             notification arrives. Suitable for microsecond-precision tasks
 *             (e.g., time synchronization timestamp capture).
 *             NOTE: Mailbox storage still occurs even when callback is set.
 *
 * Slot configuration:
 *   Slot counts (methodSlots / responseSlots / eventSlots) are now
 *   automatically derived from the role field:
 *     - SERVER: methodSlots=6, responseSlots=0, eventSlots=6
 *     - CLIENT: methodSlots=0, responseSlots=6, eventSlots=6
 *   Applications no longer need to specify slot counts manually.
 *   See picc_mailbox.h (PICC_SERVER_DEFAULT_xxx / PICC_CLIENT_DEFAULT_xxx)
 *   for the default values, which can be tuned for your system.
 *
 * This design ensures PICC_Init() is fully compatible with all application
 * scenarios — from simple polling to complex real-time callback processing —
 * without requiring any API changes.
 */
typedef struct {
    uint8                    localId;           /**< Local ID (ProviderID for Server, ConsumerID for Client). Used as direct index into g_appRegistry. */
    uint8                    remoteId;          /**< Remote ID (peer's ProviderID or ConsumerID) */
    PICC_Role_e              role;              /**< PICC_ROLE_SERVER or PICC_ROLE_CLIENT */
    uint8                    channelId;         /**< IPCF channel ID (1 or 2) */
    uint16                   Client_linkReq_PeriodMs; /**< CLIENT connection request period (ms), 0=default 10ms. Ignored for SERVER. */
    PICC_MethodCallback_t    methodHandler;     /**< Method request callback (NULL = use PICC_GetMethodData polling) */
    PICC_EventCallback_t     eventHandler;      /**< Event notification callback (NULL = use PICC_GetEventData polling) */
} PICC_AppConfig_t;

/*==================================================================================================
 *                              Public API — Initialization (1 function)
 *==================================================================================================*/

/**
 * @brief Register one application with the PICC driver
 *
 * Performs all internal registrations (Link, Method handler, Event handler,
 * slot pool allocation) in a single call. The application layer only needs
 * to call this function once during its own Xxx_Init().
 *
 * The localId from config is used as a direct index into the internal
 * registry (g_appRegistry[localId]). Valid range: 1~254.
 *
 * Prerequisites:
 *   - PICC_PreOS_Init() must have been called first.
 *
 * @param[in] config    Pointer to application configuration (IDs, role, channel,
 *                      optional Method/Event callbacks, slot counts).
 *
 * @return PICC_E_OK        on success
 * @return PICC_E_PARAM     if config is NULL or localId/remoteId out of range [1, 254]
 * @return PICC_E_NOT_INIT  if PICC infrastructure (mailbox) not yet initialized
 * @return PICC_E_DUPLICATE if localId is already registered
 * @return PICC_E_NOMEM     if slot pool exhausted
 *
 * @warning ISR CONTEXT EXECUTION WARNING
 *          Both `methodHandler` and `eventHandler` run directly within the IPCF hardware
 *          RX Interrupt Service Routine (ISR) context.
 *          Application implementations MUST adhere to the following strict rules:
 *          1. MAX EXECUTION TIME: Must not exceed 50 microseconds.
 *          2. NO BLOCKING: Absolutely NO blocking OS calls.
 *          3. PURPOSE: Only use for instant hardware capture or rapid non-blocking state updates.
 */
sint8 PICC_Init(const PICC_AppConfig_t *config);

/*==================================================================================================
 *                              Public API — Sending M->A (3 functions)
 *==================================================================================================*/

/**
 * @brief Send an Event notification from M-Core to A-Core
 *
 * @param[in] localId  Application local ID (e.g., PICC_ID_PWR_LOCAL).
 * @param[in] eventId   Event ID to send.
 * @param[in] data      Pointer to the event payload data.
 * @param[in] len       Length of the event payload data (max 32 bytes).
 * @param[in] withAck   Whether ACK is needed (PICC_EVENT_WITH_ACK or PICC_EVENT_WITHOUT_ACK).
 *
 * @return PICC_E_OK on success.
 *         Negative error code on failure (e.g., PICC_E_NOT_INIT, PICC_E_PARAM).
 */
sint8 PICC_SendEvent(uint8 localId, uint8 eventId,
                     const uint8 *data, uint16 len, PICC_EventType_e withAck);

/**
 * @brief Send a Method Request from M-Core to A-Core
 *
 * @param[in] localId  Application local ID.
 * @param[in] methodId  Method ID to be invoked on the A-Core side.
 * @param[in] data      Pointer to the request payload data.
 * @param[in] len       Length of the request payload data (max 32 bytes).
 * @param[in] type      Request type (e.g., requires ACK, or requires full Response).
 *
 * @return A non-zero Session ID on success (used later to match the response).
 *         Returns 0 on failure (e.g., link disconnected, app not registered).
 */
uint8 PICC_MethodRequest(uint8 localId, uint8 methodId,
                         const uint8 *data, uint16 len,
                         PICC_MethodType_e type);

/**
 * @brief Send a Method Response from M-Core to A-Core
 *
 * @param[in] localId    Application local ID.
 * @param[in] methodId   The Method ID that M-Core is responding to.
 * @param[in] sessionId  The unique Session ID extracted from the original A-Core request.
 * @param[in] returnCode Execution result code (e.g., 0x00 for Success, 0x01 for Error).
 * @param[in] data       Pointer to the response payload data.
 * @param[in] len        Length of the response payload data (max 32 bytes).
 *
 * @return PICC_E_OK on success.
 *         Negative error code on failure.
 */
sint8 PICC_MethodResponse(uint8 localId, uint8 methodId,
                          uint8 sessionId, uint8 returnCode,
                          const uint8 *data, uint16 len);

/*==================================================================================================
 *                              Public API — Receiving A->M (3 functions)
 *==================================================================================================*/

/**
 * @brief Retrieve a Method Request sent by A-Core to M-Core (M-Core is Server)
 *
 * @param[in]  localId     M-Core Application local ID.
 * @param[in]  methodId    The specific Method ID M-Core is looking for.
 * @param[out] data        Buffer to store the raw payload data sent by A-Core.
 * @param[in]  maxLen      Maximum size of the 'data' buffer provided by M-Core.
 * @param[out] actualLen   Returns the actual length of the payload data received from A-Core.
 * @param[out] outSessionId Returns the sessionId from A-Core's REQUEST.
 * @param[out] cbResult    Buffer to store the result produced by the M-Core callback handler.
 * @param[out] cbResultLen Returns the length of the callback result.
 *
 * @return PICC_E_OK      = New request from A-Core was successfully retrieved.
 *         PICC_E_NO_DATA = No new request from A-Core has arrived yet.
 *         PICC_E_PARAM   = Invalid parameters provided.
 */
sint8 PICC_GetMethodData(uint8 localId, uint8 methodId,
                         uint8 *data, uint16 maxLen, uint16 *actualLen,
                         uint8 *outSessionId,
                         uint8 *cbResult, uint16 *cbResultLen);

/**
 * @brief Retrieve a Method Response returned by A-Core to M-Core (M-Core is Client)
 *
 * @param[in]  localId    M-Core Application local ID.
 * @param[in]  methodId   The Method ID mapping to the original request sent by M-Core.
 * @param[in]  sessionId  Session ID returned by PICC_MethodRequest().
 * @param[out] returnCode Buffer to store the return code sent back by A-Core.
 * @param[out] data       Buffer to store the response payload data generated by A-Core.
 * @param[in]  maxLen     Maximum size of the 'data' buffer provided by M-Core.
 * @param[out] actualLen  Returns the actual length of the response payload received from A-Core.
 * @param[out] cbResult   Buffer to store the result produced by the M-Core callback handler.
 * @param[out] cbResultLen Returns the length of the callback result.
 *
 * @return PICC_E_OK       = A-Core has responded and data is retrieved.
 *         PICC_E_NO_DATA  = A-Core has not responded yet.
 *         PICC_E_PARAM    = Invalid parameters provided.
 */
sint8 PICC_GetResponseData(uint8 localId, uint8 methodId,
                           uint8 sessionId, uint8 *returnCode,
                           uint8 *data, uint16 maxLen, uint16 *actualLen,
                           uint8 *cbResult, uint16 *cbResultLen);

/**
 * @brief Retrieve an Event Notification sent by A-Core to M-Core (Fire & Forget)
 *
 * @param[in]  localId    M-Core Application local ID.
 * @param[in]  eventId    The specific Event ID M-Core is checking for.
 * @param[out] data       Buffer to store the payload data broadcasted by A-Core.
 * @param[in]  maxLen     Maximum size of the 'data' buffer provided by M-Core.
 * @param[out] actualLen  Returns the actual length of the event payload received from A-Core.
 * @param[out] cbResult   Buffer to store the result produced by the M-Core callback handler.
 * @param[out] cbResultLen Returns the length of the callback result.
 *
 * @return PICC_E_OK       = New event from A-Core was successfully retrieved.
 *         PICC_E_NO_DATA  = No new event from A-Core has arrived.
 *         PICC_E_PARAM    = Invalid parameters provided.
 */
sint8 PICC_GetEventData(uint8 localId, uint8 eventId,
                        uint8 *data, uint16 maxLen, uint16 *actualLen,
                        uint8 *cbResult, uint16 *cbResultLen);

/*==================================================================================================
 *                              Public API — Status Query (2 functions)
 *==================================================================================================*/

/**
 * @brief Get the physical channel health state (heartbeat-based)
 *
 * @param[in] channelId IPCF channel ID.
 * @return Physical channel health state.
 */
PICC_LinkState_e PICC_GetLinkState(uint8 channelId);

/**
 * @brief Get the application-level link connection state (per-app)
 *
 * @param[in] localId Application local ID (e.g., PICC_ID_PWR_LOCAL).
 * @return Application-level link state (DISCONNECTED, CONNECTING, or CONNECTED).
 */
PICC_LinkState_e PICC_GetAppLinkState(uint8 localId);

#if defined(__cplusplus)
}
#endif

#endif /* PICC_API_H */
