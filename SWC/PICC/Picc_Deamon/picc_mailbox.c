/**
 * @file picc_mailbox.c
 * @brief PICC Mailbox Module — Implementation
 *
 * Manages per-application receive mailboxes:
 *   - App registry (g_appRegistry[PICC_REGISTRY_SIZE], indexed by localId)
 *   - Global slot pool (g_slotPool[PICC_SLOT_POOL_SIZE], allocated on-demand)
 *   - Remote-to-local ID mapping (g_remoteToLocal[PICC_REGISTRY_SIZE])
 *   - Incoming message routing with channelId defense check
 *   - Polling read interface for Method/Response/Event data
 *
 * This is an INTERNAL driver module. Application layers do NOT call these
 * functions directly — they use PICC_GetMethodData() etc. from picc_api.h.
 *
 * Copyright 2024 NXP
 * All Rights Reserved.
 */

#if defined(__cplusplus)
extern "C" {
#endif

#include "picc_mailbox.h"
#include "Picc_main.h" /* For PICC_HANDLE_ERROR */

/*==================================================================================================
 *                                         Configuration
 *==================================================================================================*/

/** Max payload bytes stored per slot */
#define PICC_RX_MAX_DATA_LEN    (32U)

/** Total slot pool size — supports 120+ apps at 4 slots/app (2+0+2 or 0+2+2).
 *  Each PICC_RxSlot_t ≈ 52 bytes, so 512 slots ≈ 26.6 KB.
 *  Increase if you raise PICC_SERVER/CLIENT_DEFAULT_xxx_SLOTS above 2. */
#define PICC_SLOT_POOL_SIZE     (512U)

/*==================================================================================================
 *                                         Internal Types
 *==================================================================================================*/

/** Single receive slot */
typedef struct {
    boolean  ready;                         /**< New data available */
    uint8    msgId;                         /**< methodId or eventId */
    uint8    sessionId;                     /**< Session ID (for async Response matching) */
    uint8    returnCode;                    /**< Only for Response type */
    uint8    data[PICC_RX_MAX_DATA_LEN];    /**< Payload data copy */
    uint16   dataLen;                       /**< Actual payload length */
    uint8    cbResult[PICC_CB_RESULT_MAX_LEN]; /**< Callback-produced result */
    uint16   cbResultLen;                   /**< Callback result length (0 if no callback) */
} PICC_RxSlot_t;

/**
 * @brief Per-application registry entry (20 bytes, aligned)
 *
 * Field layout: uint8 fields (offset 0~9), uint16 fields (offset 10~19)
 * Ensures all uint16 fields are naturally aligned on 2-byte boundary.
 */
typedef struct {
    /* ---- uint8 fields (offset 0~9) ---- */
    uint8    isRegistered;       /**< 1B, offset  0 */
    uint8    remoteId;           /**< 1B, offset  1 */
    uint8    role;               /**< 1B, offset  2 */
    uint8    channelId;          /**< 1B, offset  3 */
    uint8    methodSlotCount;    /**< 1B, offset  4 */
    uint8    responseSlotCount;  /**< 1B, offset  5 */
    uint8    eventSlotCount;     /**< 1B, offset  6 */
    uint8    methodVictim;       /**< 1B, offset  7 */
    uint8    responseVictim;     /**< 1B, offset  8 */
    uint8    eventVictim;        /**< 1B, offset  9 */
    /* ---- uint16 fields (offset 10~19) ---- */
    uint16   linkSharedIdx;      /**< 2B, offset 10 — 0xFFFF=not allocated yet */
    uint16   linkReqPeriodMs;    /**< 2B, offset 12 */
    uint16   methodSlotStart;    /**< 2B, offset 14 */
    uint16   responseSlotStart;  /**< 2B, offset 16 */
    uint16   eventSlotStart;     /**< 2B, offset 18 */
} PICC_AppEntry_t;

/*==================================================================================================
 *                                         Private Variables
 *==================================================================================================*/

/** Whether infrastructure is initialized */
static boolean g_piccInfraInit = FALSE;

/** Per-application registry (indexed by localId) */
static PICC_AppEntry_t g_appRegistry[PICC_REGISTRY_SIZE];

/** Global slot pool — allocated on-demand from PICC_Init */
static PICC_RxSlot_t g_slotPool[PICC_SLOT_POOL_SIZE];

/** Next free index in g_slotPool */
static uint16 g_slotPoolNextFree;

/** Remote-to-local ID mapping table (0xFF = not mapped) */
static uint8 g_remoteToLocal[PICC_REGISTRY_SIZE];

/** Stored application configs (indexed by localId) */
static PICC_AppConfig_t g_appConfigs[PICC_REGISTRY_SIZE];

/*==================================================================================================
 *                                         Static Helper Functions
 *==================================================================================================*/

/**
 * @brief Store data into a slot array (find by msgId, reuse or allocate)
 *
 * This function searches for an existing slot with the same message ID. If found,
 * it overwrites the existing data. If not, it finds the first free slot. If all
 * slots are occupied, it uses a round-robin strategy to evict an old slot.
 *
 * @param[in,out] slots       Pointer to the slot array to store data into
 * @param[in]     slotCount   Total number of slots available in the array
 * @param[in]     msgId       Message ID (Method ID or Event ID)
 * @param[in]     sessionId   Session ID associated with the message
 * @param[in]     returnCode  Return code (only applicable for Response type)
 * @param[in]     payload     Pointer to the incoming payload data
 * @param[in]     payloadLen  Length of the payload data
 * @param[in,out] victimIdx   Pointer to the index used for round-robin eviction
 */
static void PICC_StoreToSlot(PICC_RxSlot_t *slots, uint8 slotCount,
                              uint8 msgId, uint8 sessionId,
                              uint8 returnCode,
                              const uint8 *payload, uint16 payloadLen,
                              uint8 *victimIdx)
{
    uint8 s;
    uint8 freeSlot = 0xFFU;
    uint16 copyLen;

    /* Lookup strategy: prefer overwriting an existing slot with the same msgId
     * (keep only the latest value), otherwise use the first free slot
     * (msgId == 0xFF indicates free) */
    for (s = 0U; s < slotCount; s++) {
        if (slots[s].msgId == msgId) {
            freeSlot = s;   /* Found a slot with the same msgId — overwrite old data */
            break;
        }
        if ((freeSlot == 0xFFU) && (slots[s].msgId == 0xFFU)) {
            freeSlot = s;   /* Record the first free slot */
        }
    }

    if (freeSlot == 0xFFU) {
        /* All slots occupied with different msgIds — use round-robin eviction.
         * victimIdx cycles through [0, slotCount) to ensure fair eviction. */
        freeSlot = *victimIdx;
        *victimIdx = (uint8)((*victimIdx + 1U) % slotCount);
    }

    copyLen = (payloadLen > PICC_RX_MAX_DATA_LEN) ? PICC_RX_MAX_DATA_LEN : payloadLen;
    slots[freeSlot].msgId = msgId;
    slots[freeSlot].sessionId = sessionId;
    slots[freeSlot].returnCode = returnCode;
    for (s = 0U; s < copyLen; s++) {
        slots[freeSlot].data[s] = payload[s];
    }
    slots[freeSlot].dataLen = payloadLen;
    slots[freeSlot].cbResultLen = 0U;
    slots[freeSlot].ready = TRUE;
}

/**
 * @brief Store Response data using (msgId + sessionId) as composite key
 *
 * Similar to PICC_StoreToSlot, but matches both message ID and session ID
 * to find an existing slot. If not found, allocates a free slot or evicts
 * one using round-robin.
 *
 * @param[in,out] slots       Pointer to the slot array to store data into
 * @param[in]     slotCount   Total number of slots available in the array
 * @param[in]     msgId       Message ID (Method ID)
 * @param[in]     sessionId   Session ID associated with the response
 * @param[in]     returnCode  Return code sent by the remote server
 * @param[in]     payload     Pointer to the incoming payload data
 * @param[in]     payloadLen  Length of the payload data
 * @param[in,out] victimIdx   Pointer to the index used for round-robin eviction
 */
static void PICC_StoreResponseToSlot(PICC_RxSlot_t *slots, uint8 slotCount,
                                      uint8 msgId, uint8 sessionId, uint8 returnCode,
                                      const uint8 *payload, uint16 payloadLen,
                                      uint8 *victimIdx)
{
    uint8 s;
    uint8 freeSlot = 0xFFU;
    uint16 copyLen;

    /* Find existing slot with same (msgId + sessionId), or first free slot */
    for (s = 0U; s < slotCount; s++) {
        if ((slots[s].msgId == msgId) && (slots[s].sessionId == sessionId)) {
            freeSlot = s;
            break;
        }
        if ((freeSlot == 0xFFU) && (slots[s].msgId == 0xFFU)) {
            freeSlot = s;
        }
    }

    if (freeSlot == 0xFFU) {
        freeSlot = *victimIdx;
        *victimIdx = (uint8)((*victimIdx + 1U) % slotCount);
    }

    copyLen = (payloadLen > PICC_RX_MAX_DATA_LEN) ? PICC_RX_MAX_DATA_LEN : payloadLen;
    slots[freeSlot].msgId = msgId;
    slots[freeSlot].sessionId = sessionId;
    slots[freeSlot].returnCode = returnCode;
    for (s = 0U; s < copyLen; s++) {
        slots[freeSlot].data[s] = payload[s];
    }
    slots[freeSlot].dataLen = payloadLen;
    slots[freeSlot].cbResultLen = 0U;
    slots[freeSlot].ready = TRUE;
}

/**
 * @brief Read data from a slot (by msgId, optionally by sessionId)
 *
 * Searches the slot array for a slot that has new data ready and matches the
 * given message ID (and optionally session ID). If found, copies the data out
 * and clears the slot.
 *
 * @param[in,out] slots           Pointer to the slot array to read from
 * @param[in]     slotCount       Total number of slots in the array
 * @param[in]     msgId           Message ID to search for
 * @param[in]     filterSessionId Session ID to filter by (0 means no filter)
 * @param[out]    returnCode      Pointer to store the retrieved return code (can be NULL)
 * @param[out]    outSessionId    Pointer to store the retrieved session ID (can be NULL)
 * @param[out]    data            Buffer to copy the payload data into (can be NULL)
 * @param[in]     maxLen          Maximum capacity of the 'data' buffer
 * @param[out]    actualLen       Pointer to store the actual length of copied data (can be NULL)
 * @param[out]    cbResult        Buffer to copy the callback result into (can be NULL)
 * @param[out]    cbResultLen     Pointer to store the callback result length (can be NULL)
 *
 * @return PICC_E_OK if data was successfully read, PICC_E_NO_DATA if no matching data is ready
 */
static sint8 PICC_ReadFromSlot(PICC_RxSlot_t *slots, uint8 slotCount,
                                uint8 msgId,
                                uint8 filterSessionId, uint8 *returnCode,
                                uint8 *outSessionId,
                                uint8 *data, uint16 maxLen, uint16 *actualLen,
                                uint8 *cbResult, uint16 *cbResultLen)
{
    uint8 s;
    uint16 copyLen;

    for (s = 0U; s < slotCount; s++) {
        if ((slots[s].msgId == msgId) && (slots[s].ready == TRUE)) {
            if ((filterSessionId != 0U) && (slots[s].sessionId != filterSessionId)) {
                continue;
            }
            if (returnCode != NULL) {
                *returnCode = slots[s].returnCode;
            }
            if (outSessionId != NULL) {
                *outSessionId = slots[s].sessionId;
            }
            copyLen = (slots[s].dataLen > maxLen) ? maxLen : slots[s].dataLen;
            copyLen = (copyLen > PICC_RX_MAX_DATA_LEN) ? PICC_RX_MAX_DATA_LEN : copyLen;
            if (data != NULL) {
                uint16 i;
                for (i = 0U; i < copyLen; i++) {
                    data[i] = slots[s].data[i];
                }
            }
            if (actualLen != NULL) {
                *actualLen = slots[s].dataLen;
            }
            if (cbResult != NULL && cbResultLen != NULL) {
                uint16 i;
                uint16 cbCopy = slots[s].cbResultLen;
                if (cbCopy > PICC_CB_RESULT_MAX_LEN) {
                    cbCopy = PICC_CB_RESULT_MAX_LEN;
                }
                for (i = 0U; i < cbCopy; i++) {
                    cbResult[i] = slots[s].cbResult[i];
                }
                *cbResultLen = cbCopy;
            } else if (cbResultLen != NULL) {
                *cbResultLen = 0U;
            }
            slots[s].ready = FALSE;
            slots[s].msgId = 0xFFU;
            slots[s].sessionId = 0U;
            slots[s].returnCode = 0U;
            slots[s].dataLen = 0U;
            slots[s].cbResultLen = 0U;
            return PICC_E_OK;
        }
    }
    return PICC_E_NO_DATA;
}

/*==================================================================================================
 *                                  Public Interface — Initialization
 *==================================================================================================*/

/**
 * @brief Initialize global mailbox structures (registry, pool, mapping)
 *
 * This function clears the application registry, resets all slots in the
 * global slot pool, and clears the remote-to-local ID mapping table.
 * It must be called once during early initialization.
 */
void PICC_GlobalInit(void)
{
    uint16 i;

    /* Clear app registry */
    for (i = 0U; i < PICC_REGISTRY_SIZE; i++) {
        g_appRegistry[i].isRegistered = 0U;
        g_appRegistry[i].remoteId = 0U;
        g_appRegistry[i].role = 0U;
        g_appRegistry[i].channelId = 0U;
        g_appRegistry[i].methodSlotCount = 0U;
        g_appRegistry[i].responseSlotCount = 0U;
        g_appRegistry[i].eventSlotCount = 0U;
        g_appRegistry[i].methodVictim = 0U;
        g_appRegistry[i].responseVictim = 0U;
        g_appRegistry[i].eventVictim = 0U;
        g_appRegistry[i].linkSharedIdx = 0xFFFFU;
        g_appRegistry[i].linkReqPeriodMs = 0U;
        g_appRegistry[i].methodSlotStart = 0U;
        g_appRegistry[i].responseSlotStart = 0U;
        g_appRegistry[i].eventSlotStart = 0U;
    }

    /* Clear slot pool */
    for (i = 0U; i < PICC_SLOT_POOL_SIZE; i++) {
        g_slotPool[i].ready = FALSE;
        g_slotPool[i].msgId = 0xFFU;
        g_slotPool[i].sessionId = 0U;
        g_slotPool[i].returnCode = 0U;
        g_slotPool[i].dataLen = 0U;
        g_slotPool[i].cbResultLen = 0U;
    }
    g_slotPoolNextFree = 0U;

    /* Clear remote-to-local mapping (0xFF = not mapped) */
    for (i = 0U; i < PICC_REGISTRY_SIZE; i++) {
        g_remoteToLocal[i] = 0xFFU;
    }
}

/**
 * @brief Mark the PICC mailbox infrastructure as fully initialized and ready
 */
void PICC_MailboxReady(void)
{
    g_piccInfraInit = TRUE;
}

/**
 * @brief Check if the PICC mailbox infrastructure is initialized
 *
 * @return TRUE if initialized, FALSE otherwise
 */
boolean PICC_MailboxIsReady(void)
{
    return g_piccInfraInit;
}

/*==================================================================================================
 *                                  Public Interface — Remote Mapping
 *==================================================================================================*/

/**
 * @brief Register a mapping from a remote ID to a local ID
 *
 * @param[in] remoteId The remote application ID
 * @param[in] localId  The local application ID to map to
 *
 * @return PICC_E_OK on success, PICC_E_PARAM if invalid, PICC_E_REMOTE_ID_CONFLICT on conflict
 */
sint8 PICC_RegisterRemoteMapping(uint8 remoteId, uint8 localId)
{
    if (remoteId >= PICC_REGISTRY_SIZE) {
        return PICC_E_PARAM;
    }
    if (g_remoteToLocal[remoteId] != 0xFFU &&
        g_remoteToLocal[remoteId] != localId) {
        PICC_HANDLE_ERROR(-30);
        return PICC_E_REMOTE_ID_CONFLICT;
    }
    g_remoteToLocal[remoteId] = localId;
    return PICC_E_OK;
}

/*==================================================================================================
 *                                  Public Interface — App Registration
 *==================================================================================================*/

/**
 * @brief Register an application in the mailbox and allocate slot pools
 *
 * Parses the application configuration and allocates the required number of
 * method, response, and event slots from the global pool based on the app's role.
 *
 * @param[in] config Pointer to the application configuration structure
 *
 * @return PICC_E_OK on success, negative error code on failure
 */
sint8 PICC_MailboxRegisterApp(const PICC_AppConfig_t *config)
{
    uint8 localId;
    uint8 methodSlots, responseSlots, eventSlots;
    uint16 totalNeeded;
    PICC_AppEntry_t *entry;

    if (config == NULL) {
        return PICC_E_PARAM;
    }

    localId = config->localId;

    /* ID range check */
    if (localId == 0U || localId >= PICC_REGISTRY_SIZE) {
        return PICC_E_PARAM;
    }
    if (config->remoteId == 0U || config->remoteId >= PICC_REGISTRY_SIZE) {
        return PICC_E_PARAM;
    }

    /* Duplicate registration check */
    if (g_appRegistry[localId].isRegistered != 0U) {
        PICC_HANDLE_ERROR(-31);
        return PICC_E_DUPLICATE;
    }

    /* Determine slot counts based on role (role-based auto allocation)
     *
     * SERVER: receives Method requests → needs methodSlots
     *         never sends Request → responseSlots = 0
     *         may receive Events → needs eventSlots
     *
     * CLIENT: never receives Method Requests → methodSlots = 0
     *         sends Requests and waits for Responses → needs responseSlots
     *         receives Events from Server → needs eventSlots
     */
    if ((uint8)config->role == (uint8)PICC_ROLE_SERVER) {
        methodSlots   = PICC_SERVER_DEFAULT_METHOD_SLOTS;   /* 2 */
        responseSlots = 0U;                                  /* Server never waits for Response */
        eventSlots    = PICC_SERVER_DEFAULT_EVENT_SLOTS;     /* 2 */
    } else {
        methodSlots   = 0U;                                  /* Client never receives Method Requests */
        responseSlots = PICC_CLIENT_DEFAULT_RESPONSE_SLOTS;  /* 2 */
        eventSlots    = PICC_CLIENT_DEFAULT_EVENT_SLOTS;      /* 2 */
    }

    totalNeeded = (uint16)methodSlots + (uint16)responseSlots + (uint16)eventSlots;

    /* Check slot pool capacity */
    if ((g_slotPoolNextFree + totalNeeded) > PICC_SLOT_POOL_SIZE) {
        PICC_HANDLE_ERROR(-32);
        return PICC_E_NOMEM;
    }

    /* Register remote-to-local mapping */
    if (PICC_RegisterRemoteMapping(config->remoteId, localId) != PICC_E_OK) {
        return PICC_E_REMOTE_ID_CONFLICT;
    }

    /* Store config */
    g_appConfigs[localId] = *config;

    /* Fill registry entry */
    entry = &g_appRegistry[localId];
    entry->isRegistered      = 1U;
    entry->remoteId          = config->remoteId;
    entry->role              = (uint8)config->role;
    entry->channelId         = config->channelId;
    entry->methodSlotCount   = methodSlots;
    entry->responseSlotCount = responseSlots;
    entry->eventSlotCount    = eventSlots;
    entry->methodVictim      = 0U;
    entry->responseVictim    = 0U;
    entry->eventVictim       = 0U;
    entry->linkSharedIdx     = 0xFFFFU; /* Will be set by PICC_Init() after link registration */
    entry->linkReqPeriodMs   = config->Client_linkReq_PeriodMs;

    /* Allocate slot ranges from pool */
    entry->methodSlotStart   = g_slotPoolNextFree;
    g_slotPoolNextFree += methodSlots;

    entry->responseSlotStart = g_slotPoolNextFree;
    g_slotPoolNextFree += responseSlots;

    entry->eventSlotStart    = g_slotPoolNextFree;
    g_slotPoolNextFree += eventSlots;

    return PICC_E_OK;
}

/**
 * @brief Check if an application is registered in the mailbox
 *
 * @param[in] localId The local ID of the application
 *
 * @return TRUE if registered, FALSE otherwise
 */
boolean PICC_MailboxIsAppRegistered(uint8 localId)
{
    if (localId == 0U || localId >= PICC_REGISTRY_SIZE) {
        return FALSE;
    }
    return (g_appRegistry[localId].isRegistered != 0U) ? TRUE : FALSE;
}

/**
 * @brief Retrieve the configuration for a registered application
 *
 * @param[in]  localId The local ID of the application
 * @param[out] config  Pointer to store the configuration pointer
 *
 * @return PICC_E_OK on success, PICC_E_PARAM if invalid or not registered
 */
sint8 PICC_MailboxGetAppConfig(uint8 localId, const PICC_AppConfig_t **config)
{
    if (localId == 0U || localId >= PICC_REGISTRY_SIZE) {
        return PICC_E_PARAM;
    }
    if (g_appRegistry[localId].isRegistered == 0U) {
        return PICC_E_PARAM;
    }
    if (config == NULL) {
        return PICC_E_PARAM;
    }
    *config = &g_appConfigs[localId];
    return PICC_E_OK;
}

/**
 * @brief Get the shared link index for a registered application
 *
 * @param[in] localId The local ID of the application
 *
 * @return The shared link index, or 0xFFFF if invalid/not mapped
 */
uint16 PICC_MailboxGetLinkSharedIdx(uint8 localId)
{
    if (localId == 0U || localId >= PICC_REGISTRY_SIZE) {
        return 0xFFFFU;
    }
    return g_appRegistry[localId].linkSharedIdx;
}

/**
 * @brief Set the shared link index for a registered application
 *
 * @param[in] localId       The local ID of the application
 * @param[in] linkSharedIdx The shared link index to set
 */
void PICC_MailboxSetLinkSharedIdx(uint8 localId, uint16 linkSharedIdx)
{
    if (localId == 0U || localId >= PICC_REGISTRY_SIZE) {
        return;
    }
    g_appRegistry[localId].linkSharedIdx = linkSharedIdx;
}

/*==================================================================================================
 *                                  Public Interface — Store (incoming)
 *==================================================================================================*/

/**
 * @brief Route and store incoming messages into the appropriate mailbox slots
 *
 * This function processes an incoming message header and payload, determines
 * the target application based on the message type (Request, Response, Event),
 * performs boundary and channel checks, and routes the data to the correct slot.
 *
 * @param[in] header     Pointer to the parsed message header
 * @param[in] payload    Pointer to the incoming payload data
 * @param[in] payloadLen Length of the payload data
 * @param[in] channelId  The IPCF channel ID the message was received on
 */
void PICC_StoreToMailbox(const PICC_MsgHeader_t *header,
                          const uint8 *payload, uint16 payloadLen,
                          uint8 channelId)
{
    uint8 localId = 0xFFU;
    PICC_AppEntry_t *entry;

    switch (header->msgType) {
        /* Method Request (Server receives from A-core) */
        case (uint8)PICC_MSG_REQUEST:
        case (uint8)PICC_MSG_REQUEST_NO_RETURN_WITH_ACK:
        case (uint8)PICC_MSG_REQUEST_NO_RETURN_WITHOUT_ACK:
            /* For Method Request: localId = providerId (M-Core is Server) */
            localId = header->providerId;
            break;

        /* Method Response (Client receives reply from A-core) */
        case (uint8)PICC_MSG_RESPONSE:
            /* For Response: localId = consumerId (M-Core is Client) */
            localId = header->consumerId;
            break;

        /* Event Notification (receiving Event from A-core) */
        case (uint8)PICC_MSG_NOTIFICATION_WITH_ACK:
        case (uint8)PICC_MSG_NOTIFICATION_WITHOUT_ACK:
            /* For Event: use reverse mapping from providerId to find localId */
            if (header->providerId < PICC_REGISTRY_SIZE) {
                localId = g_remoteToLocal[header->providerId];
            }
            if (localId == 0xFFU) {
                return;  /* No mapping, discard */
            }
            break;

        default:
            return;
    }

    /* Boundary + registration check */
    if (localId == 0U || localId >= PICC_REGISTRY_SIZE) {
        return;
    }
    entry = &g_appRegistry[localId];
    if (entry->isRegistered == 0U) {
        return;
    }

    /* ChannelId defense check */
    if (entry->channelId != channelId) {
        return;
    }

    /* Route to corresponding slot segment */
    switch (header->msgType) {
        case (uint8)PICC_MSG_REQUEST:
        case (uint8)PICC_MSG_REQUEST_NO_RETURN_WITH_ACK:
        case (uint8)PICC_MSG_REQUEST_NO_RETURN_WITHOUT_ACK:
            PICC_StoreToSlot(&g_slotPool[entry->methodSlotStart],
                             entry->methodSlotCount,
                             header->methodId, header->sessionId,
                             0U, payload, payloadLen,
                             &entry->methodVictim);
            break;

        case (uint8)PICC_MSG_RESPONSE:
            PICC_StoreResponseToSlot(&g_slotPool[entry->responseSlotStart],
                                      entry->responseSlotCount,
                                      header->methodId, header->sessionId,
                                      header->returnCode,
                                      payload, payloadLen,
                                      &entry->responseVictim);
            break;

        case (uint8)PICC_MSG_NOTIFICATION_WITH_ACK:
        case (uint8)PICC_MSG_NOTIFICATION_WITHOUT_ACK:
            PICC_StoreToSlot(&g_slotPool[entry->eventSlotStart],
                             entry->eventSlotCount,
                             header->methodId, 0U,
                             0U, payload, payloadLen,
                             &entry->eventVictim);
            break;

        default:
            break;
    }
}

/*==================================================================================================
 *                                  Public Interface — Store Callback Result
 *==================================================================================================*/

/**
 * @brief Store the result produced by a callback into the corresponding mailbox slot
 *
 * This is used when a registered callback function processes an incoming message
 * and generates a result that needs to be stored along with the message data.
 *
 * @param[in] header      Pointer to the incoming message header
 * @param[in] channelId   The IPCF channel ID the message was received on
 * @param[in] cbResult    Pointer to the callback result data
 * @param[in] cbResultLen Length of the callback result data
 */
void PICC_StoreCallbackResult(const PICC_MsgHeader_t *header,
                               uint8 channelId,
                               const uint8 *cbResult, uint16 cbResultLen)
{
    uint8 localId = 0xFFU;
    PICC_AppEntry_t *entry;
    PICC_RxSlot_t *slots = NULL;
    uint8 msgId;
    uint8 s;
    uint16 copyLen;

    if (header == NULL || cbResult == NULL || cbResultLen == 0U) {
        return;
    }

    switch (header->msgType) {
        case (uint8)PICC_MSG_REQUEST:
        case (uint8)PICC_MSG_REQUEST_NO_RETURN_WITH_ACK:
        case (uint8)PICC_MSG_REQUEST_NO_RETURN_WITHOUT_ACK:
            localId = header->providerId;
            break;

        case (uint8)PICC_MSG_NOTIFICATION_WITH_ACK:
        case (uint8)PICC_MSG_NOTIFICATION_WITHOUT_ACK:
            if (header->providerId < PICC_REGISTRY_SIZE) {
                localId = g_remoteToLocal[header->providerId];
            }
            break;

        default:
            return;
    }

    if (localId == 0U || localId >= PICC_REGISTRY_SIZE) {
        return;
    }
    entry = &g_appRegistry[localId];
    if (entry->isRegistered == 0U) {
        return;
    }
    if (entry->channelId != channelId) {
        return;
    }

    switch (header->msgType) {
        case (uint8)PICC_MSG_REQUEST:
        case (uint8)PICC_MSG_REQUEST_NO_RETURN_WITH_ACK:
        case (uint8)PICC_MSG_REQUEST_NO_RETURN_WITHOUT_ACK:
            slots = &g_slotPool[entry->methodSlotStart];
            break;

        case (uint8)PICC_MSG_NOTIFICATION_WITH_ACK:
        case (uint8)PICC_MSG_NOTIFICATION_WITHOUT_ACK:
            slots = &g_slotPool[entry->eventSlotStart];
            break;

        default:
            return;
    }

    msgId = header->methodId;
    copyLen = (cbResultLen > PICC_CB_RESULT_MAX_LEN) ? PICC_CB_RESULT_MAX_LEN : cbResultLen;

    /* Find the slot that was just written (same msgId, ready=TRUE) */
    for (s = 0U; s < entry->methodSlotCount; s++) {
        if ((slots[s].msgId == msgId) && (slots[s].ready == TRUE)) {
            uint16 i;
            for (i = 0U; i < copyLen; i++) {
                slots[s].cbResult[i] = cbResult[i];
            }
            slots[s].cbResultLen = copyLen;
            break;
        }
    }
}

/*==================================================================================================
 *                                  Public Interface — Read (polling)
 *==================================================================================================*/

/**
 * @brief Poll for incoming Method Request data for a specific application
 *
 * @param[in]  localId      The local ID of the application (Server)
 * @param[in]  methodId     The specific Method ID to look for
 * @param[out] data         Buffer to store the payload data
 * @param[in]  maxLen       Maximum capacity of the 'data' buffer
 * @param[out] actualLen    Pointer to store the actual length of copied data
 * @param[out] outSessionId Pointer to store the retrieved session ID
 * @param[out] cbResult     Buffer to store the callback result
 * @param[out] cbResultLen  Pointer to store the callback result length
 *
 * @return PICC_E_OK if data was read, negative error code otherwise
 */
sint8 PICC_MailboxGetMethodData(uint8 localId, uint8 methodId,
                                uint8 *data, uint16 maxLen, uint16 *actualLen,
                                uint8 *outSessionId,
                                uint8 *cbResult, uint16 *cbResultLen)
{
    PICC_AppEntry_t *entry;

    if (localId == 0U || localId >= PICC_REGISTRY_SIZE) {
        return PICC_E_PARAM;
    }
    entry = &g_appRegistry[localId];
    if (entry->isRegistered == 0U) {
        return PICC_E_PARAM;
    }

    return PICC_ReadFromSlot(&g_slotPool[entry->methodSlotStart],
                             entry->methodSlotCount,
                             methodId, 0U, NULL, outSessionId,
                             data, maxLen, actualLen,
                             cbResult, cbResultLen);
}

/**
 * @brief Poll for incoming Method Response data for a specific application
 *
 * @param[in]  localId      The local ID of the application (Client)
 * @param[in]  methodId     The specific Method ID to look for
 * @param[in]  sessionId    The Session ID to match the specific request
 * @param[out] returnCode   Pointer to store the retrieved return code
 * @param[out] data         Buffer to store the payload data
 * @param[in]  maxLen       Maximum capacity of the 'data' buffer
 * @param[out] actualLen    Pointer to store the actual length of copied data
 * @param[out] cbResult     Buffer to store the callback result
 * @param[out] cbResultLen  Pointer to store the callback result length
 *
 * @return PICC_E_OK if data was read, negative error code otherwise
 */
sint8 PICC_MailboxGetResponseData(uint8 localId, uint8 methodId,
                                  uint8 sessionId, uint8 *returnCode,
                                  uint8 *data, uint16 maxLen, uint16 *actualLen,
                                  uint8 *cbResult, uint16 *cbResultLen)
{
    PICC_AppEntry_t *entry;

    if (localId == 0U || localId >= PICC_REGISTRY_SIZE) {
        return PICC_E_PARAM;
    }
    entry = &g_appRegistry[localId];
    if (entry->isRegistered == 0U) {
        return PICC_E_PARAM;
    }

    return PICC_ReadFromSlot(&g_slotPool[entry->responseSlotStart],
                             entry->responseSlotCount,
                             methodId, sessionId, returnCode, NULL,
                             data, maxLen, actualLen,
                             cbResult, cbResultLen);
}

/**
 * @brief Poll for incoming Event Notification data for a specific application
 *
 * @param[in]  localId      The local ID of the application
 * @param[in]  eventId      The specific Event ID to look for
 * @param[out] data         Buffer to store the payload data
 * @param[in]  maxLen       Maximum capacity of the 'data' buffer
 * @param[out] actualLen    Pointer to store the actual length of copied data
 * @param[out] cbResult     Buffer to store the callback result
 * @param[out] cbResultLen  Pointer to store the callback result length
 *
 * @return PICC_E_OK if data was read, negative error code otherwise
 */
sint8 PICC_MailboxGetEventData(uint8 localId, uint8 eventId,
                               uint8 *data, uint16 maxLen, uint16 *actualLen,
                               uint8 *cbResult, uint16 *cbResultLen)
{
    PICC_AppEntry_t *entry;

    if (localId == 0U || localId >= PICC_REGISTRY_SIZE) {
        return PICC_E_PARAM;
    }
    entry = &g_appRegistry[localId];
    if (entry->isRegistered == 0U) {
        return PICC_E_PARAM;
    }

    return PICC_ReadFromSlot(&g_slotPool[entry->eventSlotStart],
                             entry->eventSlotCount,
                             eventId, 0U, NULL, NULL,
                             data, maxLen, actualLen,
                             cbResult, cbResultLen);
}

#if defined(__cplusplus)
}
#endif
