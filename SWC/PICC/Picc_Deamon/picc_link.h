/**
 * @file picc_link.h
 * @brief M-Core Inter-Core Communication Link Management Layer - Interface Definition
 *
 * Implements application-level connection request/response and disconnect notification.
 * This layer uses CONSUMER_ID/PROVIDER_ID for connection handshake.
 *
 * Note: Heartbeat (Ping/Pong) functionality is now in picc_heartbeat.h/c
 *
 * Copyright 2024 NXP
 * All Rights Reserved.
 */

#ifndef PICC_LINK_H
#define PICC_LINK_H

#if defined(__cplusplus)
extern "C" {
#endif

#include "picc_protocol.h"

/*==================================================================================================
 *                                         Macro Definitions
 *==================================================================================================*/

/** Default connection request period (ms) for CLIENT apps */
#define PICC_LINK_REQUEST_PERIOD_MS     (10U)

/** Maximum supported physical channels */
#define PICC_MAX_CHANNELS               (2U)

/** Maximum shared link pool size — one entry per (remoteId, channelId) pair.
 *  Both Server and Client apps allocate a link entry (Server needs it for
 *  link state tracking, disconnect handling, and SendEvent guard check).
 *  256 entries supports 120+ apps with room for deduplication sharing. */
#define PICC_LINK_SHARED_POOL_SIZE      (256U)

/*==================================================================================================
 *                                         Enum Types
 *==================================================================================================*/

/**
 * @brief Connection state
 */
typedef enum {
    PICC_LINK_STATE_DISCONNECTED = 0U,  /**< Disconnected */
    PICC_LINK_STATE_CONNECTING,         /**< Connecting (waiting for response) */
    PICC_LINK_STATE_CONNECTED           /**< Connected */
} PICC_LinkState_e;

/*==================================================================================================
 *                                         Structure Definitions
 *==================================================================================================*/

/**
 * @brief Link configuration (application-level IDs)
 */
typedef struct {
    uint8        localId;       /**< Local ID (ConsumerID as Client, ProviderID as Server) */
    uint8        remoteId;      /**< Remote ID (ProviderID as Client, ConsumerID as Server) */
    PICC_Role_e  role;          /**< Role */
    uint8        instanceId;    /**< IPCF instance ID */
    uint8        channelId;     /**< IPCF channel ID */
    uint16       Client_linkReq_PeriodMs; /**< CLIENT connection request period (ms), 0=default 10ms */
} PICC_LinkConfig_t;

/**
 * @brief Shared link context — deduplicated by (remoteId, channelId)
 *
 * Both Server and Client apps allocate a link entry. Multiple apps with
 * the same (remoteId, channelId, role) share one link context via refCount.
 * Server needs link entry for: state tracking, disconnect/reconnect handling,
 * and guarding PICC_SendEvent()/PICC_MethodResponse() with link state check.
 */
typedef struct {
    PICC_LinkConfig_t      config;           /**< Link configuration */
    volatile PICC_LinkState_e state;          /**< Current state (volatile) */
    uint8                  isInitialized;     /**< Whether initialized */
    uint16                 periodCounter;     /**< Period counter for CLIENT request timing */
    uint8                  backoffCounter;    /**< Send backoff counter */
    uint8                  refCount;          /**< Number of apps sharing this link */
} PICC_LinkShared_t;

/*==================================================================================================
 *                                         Function Declarations
 *==================================================================================================*/

/**
 * @brief One-time initialization of the link management layer
 *
 * Clears all shared link pool entries. Must be called once during
 * PICC_InfraInit() before any PICC_Init()/PICC_LinkRegisterShared() calls.
 */
void PICC_LinkLayerInit(void);

/**
 * @brief Register a shared link for an application
 *
 * Both Server and Client roles allocate a link pool entry.
 *   - Finds or creates a shared link by (remoteId, channelId, role).
 *   - If a link already exists for this (remoteId, channelId, role),
 *     increments refCount and shares it.
 *   - Otherwise, allocates a new link from the pool.
 *
 * Server: initial state = DISCONNECTED (waits for Client connect request).
 * Client: initial state = CONNECTING (starts sending link-connect requests).
 *
 * @param[in] localId          Application local ID
 * @param[in] remoteId         Remote peer ID
 * @param[in] channelId        IPCF channel ID
 * @param[in] role              Application role
 * @param[in] linkReqPeriodMs  Link request period (ms), only used by Client
 * @return Non-negative linkSharedIdx on success, negative on failure
 */
sint16 PICC_LinkRegisterShared(uint8 localId, uint8 remoteId,
                               uint8 channelId, uint8 role,
                               uint16 linkReqPeriodMs);

/**
 * @brief Deinitialize link management layer
 */
void PICC_LinkDeinit(void);

/**
 * @brief Handle connection response (Client role)
 */
sint8 PICC_LinkHandleResponse(const PICC_MsgHeader_t *header,
                              const uint8 *payload, uint16 len,
                              uint8 instanceId, uint8 channelId);

/**
 * @brief Handle connection request (Server role)
 */
sint8 PICC_LinkHandleRequest(const PICC_MsgHeader_t *header,
                             const uint8 *payload, uint16 len,
                             uint8 instanceId, uint8 channelId);

/**
 * @brief Get application-level link state by pool index (O(1) lookup)
 *
 * Preferred over PICC_LinkGetStateByIds() — avoids the O(n) linear scan
 * and correctly handles shared link entries where multiple apps with
 * different localIds share the same pool slot.
 *
 * @param[in] linkSharedIdx  Link pool index (from PICC_MailboxGetLinkSharedIdx)
 * @return Link state, or DISCONNECTED if index is invalid/uninitialized
 */
PICC_LinkState_e PICC_LinkGetStateByIdx(uint16 linkSharedIdx);

/**
 * @brief Get application-level link state by ID pair (legacy, O(n) scan)
 *
 * @deprecated Use PICC_LinkGetStateByIdx() instead for O(1) lookup.
 * Kept for backward compatibility with any code that does not have
 * linkSharedIdx available.
 */
PICC_LinkState_e PICC_LinkGetStateByIds(uint8 localId, uint8 remoteId);

/**
 * @brief Handle disconnect notification
 */
sint8 PICC_LinkHandleDisconnect(const PICC_MsgHeader_t *header,
                                const uint8 *payload, uint16 len,
                                uint8 instanceId, uint8 channelId);

/**
 * @brief Get current connection state for specified channel
 */
PICC_LinkState_e PICC_LinkGetState(uint8 channelId);

/**
 * @brief Set link state to CONNECTING (called by heartbeat on timeout)
 */
void PICC_LinkTriggerReconnect(uint8 instanceId, uint8 channelId);

/**
 * @brief Link process - called from PICC periodic task (10ms)
 */
void PICC_LinkProcess(void);

/**
 * @brief Process received link management message (dispatch by subType)
 */
sint8 PICC_LinkProcessMessage(const PICC_MsgHeader_t *header,
                              const uint8 *payload, uint16 len,
                              uint8 instanceId, uint8 channelId);

#if defined(__cplusplus)
}
#endif

#endif /* PICC_LINK_H */
