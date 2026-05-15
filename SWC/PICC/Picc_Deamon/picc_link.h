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

/** Maximum shared link pool size */
#define PICC_LINK_SHARED_POOL_SIZE      (10U)

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
 * Multiple Client apps connecting to the same (remoteId, channelId) share
 * one link context. refCount tracks how many apps share this link.
 * Server apps do NOT allocate a link pool entry (linkSharedIdx = 0xFF).
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
 * Server role: Sets linkSharedIdx=0xFF in the app registry and returns.
 * Client role: Finds or creates a shared link by (remoteId, channelId).
 *   - If a link already exists for this (remoteId, channelId), increments
 *     refCount and shares it.
 *   - Otherwise, allocates a new link from the pool.
 *
 * @param[in] localId          Application local ID
 * @param[in] remoteId         Remote peer ID
 * @param[in] channelId        IPCF channel ID
 * @param[in] role              Application role
 * @param[in] linkReqPeriodMs  Link request period (ms)
 * @return PICC_E_OK on success, negative on failure
 */
sint8 PICC_LinkRegisterShared(uint8 localId, uint8 remoteId,
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
 * @brief Get application-level link state by ID pair
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
