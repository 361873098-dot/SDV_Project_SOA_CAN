/********************************************************************************
* COPYRIGHT (C) Vitesco Technologies 2025
* ALL RIGHTS RESERVED.
*
* The reproduction, transmission or use of this document or its
* contents is not permitted without express written authority.
* Offenders will be liable for damages. All rights, including rights
* created by patent grant or registration of a utility model or design,
* are reserved.
*********************************************************************************
*
*  File name:           $Source: stm.c $
*  Revision:            $Revision: 1.0 $
*  Author:              $Author: Li Song (uic59152)  $
*  Module acronym:      STM
*  Specification:
*  Date:                $Date: 2026/05/25  $
*
*  Description:     Storage Middleware core implementation
*                   State machine + 5 sub-task handlers + PICC interaction
*
*  Architecture Overview:
*    STM operates as a dual-role PICC endpoint:
*      - Provider (Server): M-core receives Method requests from A-core
*        (Method 0x01 consistency check, Method 0x02 A-core write)
*      - Consumer (Client): M-core sends Method requests to A-core
*        (Method 0x03 M read from A, Method 0x04 M sync to A with retry,
*         Method 0x05 M async read from A)
*
*  State Machine:
*    UNINIT -> WAIT_LINK -> WAIT_CONSISTENCY -> SYNC_TO_A -> RUNNING
*      |                                                        |
*      +--------------------------------------------------------+
*                   (link lost -> return to WAIT_LINK)
*
*    - WAIT_LINK:       Both Provider and Consumer links must be connected
*    - WAIT_CONSISTENCY: Wait for A-core Method 0x01 consistency check, then mark all valid NVM dirty
*    - SYNC_TO_A:       Sync all dirty NVM data to A-core (Method 0x04)
*    - RUNNING:          Normal operation, process all sub-tasks
*
*********************************************************************************/

/***********************************************************************************************************************
*  include files
***********************************************************************************************************************/
#include "Platform.h"
#include "stm_main.h"
#include "stm_cnf.h"
#include "stm_nvm.h"
#include "picc_api.h"
#include <string.h>

/***********************************************************************************************************************
*  local variable definitions
***********************************************************************************************************************/

/** Main state machine state. Volatile because it may be read from
 *  debug/trace contexts outside the 10ms task. */
static volatile Stm_StateType_e Stm_State = STM_STATE_UNINIT;

/** Retry state for Method 0x04 (M sync to A).
 *  Tracks the current retry attempt, tick counter for interval,
 *  and the dataId being synced. Only one sync can be in-flight at a time. */
typedef struct {
    uint16 dataId;        /**< DataId currently being synced to A-core */
    uint8  retryCount;   /**< Current retry attempt (0..STM_RETRY_MAX_COUNT) */
    uint16 tickCounter;   /**< Tick counter for stepped retry interval */
    uint8  active;       /**< TRUE if a sync request is in-flight and awaiting response */
    uint8  sessionId;    /**< Session ID of the in-flight request, for precise response matching */
} Stm_RetryState_t;

static Stm_RetryState_t Stm_RetryState;

/** Consumer Session ID counter (1..0xFF, wraps to 0x01).
 *  Used for async Method requests from M to A (Method 0x03/0x05).
 *  Reset to 0 on disconnect per protocol requirement. */
static uint8 Stm_SessionId = 0U;

/** Sync cycle counter for anti-storm limiting.
 *  Counts how many 0x04 sync messages have been sent in the current
 *  10ms cycle. Reset at the start of each Stm_Main() call. */
static uint16 Stm_SyncCycleCount = 0U;

/** Pending app read request (Method 0x03/0x05) tracking.
 *  Only one read request can be pending at a time.
 *  Stores the method ID and session ID for response matching. */
typedef struct {
    uint16 dataId;       /**< Requested dataId */
    uint8  methodId;     /**< Method ID used (0x03 or 0x05) */
    uint8  sessionId;    /**< Session ID returned by PICC_MethodRequest */
    uint8  active;       /**< TRUE if a read request is pending */
} Stm_AppReadReq_t;

static Stm_AppReadReq_t Stm_PendingReadReq;

/** Current sync scan index for GetSyncableItem round-robin.
 *  Ensures fairness when multiple dirty blocks need syncing. */
static uint16 Stm_SyncScanIndex = 0U;

/***********************************************************************************************************************
*  static helper functions
***********************************************************************************************************************/

/**
 * @brief Get next Session ID (1..0xFF, wraps to 0x01)
 *
 * Per protocol: SessionID is used to match async Method requests
 * with their responses. Range 0x01~0xFF, wraps to 0x01 (not 0x00).
 * 0x00 is reserved for non-session contexts.
 */
static uint8 Stm_GetNextSessionId(void)
{
    Stm_SessionId++;
    if (Stm_SessionId == 0U)
    {
        /* Wrap from 0xFF+1 back to 0x01, never use 0x00 */
        Stm_SessionId = 1U;
    }
    return Stm_SessionId;
}

/**
 * @brief Check if both Provider and Consumer links are connected
 *
 * STM requires BOTH links to be established before it can operate:
 * - Provider link: M-core server can receive Method requests from A-core
 * - Consumer link: M-core client can send Method requests to A-core
 */
static boolean Stm_IsLinkReady(void)
{
    PICC_LinkState_e provState = PICC_GetAppLinkState(PICC_APP_STORAGE);
    PICC_LinkState_e cliState  = PICC_GetAppLinkState(PICC_APP_STM_CLI);

    return ((provState == PICC_LINK_STATE_CONNECTED) &&
            (cliState == PICC_LINK_STATE_CONNECTED)) ? TRUE : FALSE;
}

/**
 * @brief Reset Session ID on disconnect
 *
 * Per protocol requirement: SessionID counter must be reset on
 * disconnect so that reconnection starts with a fresh sequence.
 */
static void Stm_ResetSessionId(void)
{
    Stm_SessionId = 0U;
}

/**
 * @brief Build payload for Method 0x04 sync message: 2B dataId (big-endian) + data
 *
 * Output format:
 *   [0]     dataId high byte
 *   [1]     dataId low byte
 *   [2..]   data bytes (up to STM_NVM_BLOCK_MAX_SIZE - 2)
 *
 * @param dataId    Data item identifier
 * @param data      Pointer to data buffer (may be NULL for zero-length data)
 * @param dataLen   Length of data
 * @param outBuf    Output buffer (must be at least STM_NVM_BLOCK_MAX_SIZE + 2 bytes)
 * @param outLen    Output payload length
 */
static void Stm_BuildSyncPayload(uint16 dataId, const uint8 *data, uint16 dataLen,
                                   uint8 *outBuf, uint16 *outLen)
{
    /* dataId in big-endian per protocol byte order requirement */
    outBuf[0] = (uint8)(dataId >> 8U);
    outBuf[1] = (uint8)(dataId & 0xFFU);

    if ((data != NULL) && (dataLen > 0U))
    {
        /* Truncate if data exceeds max payload size (2 bytes used for dataId) */
        uint16 copyLen = (dataLen > (STM_NVM_BLOCK_MAX_SIZE - 2U)) ?
                          (STM_NVM_BLOCK_MAX_SIZE - 2U) : dataLen;
        (void)memcpy(&outBuf[2], data, copyLen);
        *outLen = 2U + copyLen;
    }
    else
    {
        /* No data - payload contains only dataId */
        *outLen = 2U;
    }
}

/***********************************************************************************************************************
*  Sub-task: Check link state
*
*  Monitors both PICC link states. If either link is lost, resets the
*  state machine back to WAIT_LINK and clears all pending operations.
*  Per protocol:
*    - SessionID is reset on disconnect
*    - NVM dirty flags are cleared (pending syncs abandoned)
*    - NVM data in RAM/EEPROM is preserved
*    - CRC/send counter is NOT reset (handled by PICC layer)
***********************************************************************************************************************/
static void Stm_CheckLinkState(void)
{
    if (Stm_IsLinkReady() == FALSE)
    {
        /* Link lost - reset state machine if not already in WAIT_LINK */
        if (Stm_State != STM_STATE_WAIT_LINK)
        {
            Stm_State = STM_STATE_WAIT_LINK;
            StmNvm_ResetOnDisconnect();     /* Clear dirty flags, preserve data */
            Stm_ResetSessionId();           /* Reset session ID per protocol */
            Stm_RetryState.active = 0U;     /* Cancel pending sync retry */
            Stm_PendingReadReq.active = 0U; /* Cancel pending read request */
            Stm_SyncScanIndex = 0U;         /* Reset round-robin scan index */
        }
    }
}

/***********************************************************************************************************************
*  Sub-task: Process RX from A-core (Server role: Method 0x02 only)
*
*  Polls PICC for incoming Method requests on the Provider (Server) endpoint.
*
*  Method 0x01 (Consistency Check) is NOT handled here.
*  It is processed exclusively in the WAIT_CONSISTENCY state to avoid
*  misinterpreting dataId=0x0000 (global consistency probe) as a business
*  data block lookup, which would always return NOT_OK.
*
*  Method 0x02 (A-core Write):
*    A-core -> M-core: 2B dataId (big-endian) + data
*    M-core -> A-core: 2B dataId + 2B status(0x0000=OK, 0x0001=NOT_OK)
*    Purpose: A-core pushes data to M-core's local NVM.
***********************************************************************************************************************/
static void Stm_ProcessRxFromA(void)
{
    static uint8 s_methodBuf[STM_NVM_BLOCK_MAX_SIZE + 4U]; /* static to avoid stack overflow */
    static uint8 s_respBuf[STM_NVM_BLOCK_MAX_SIZE + 4U];
    uint16 methodLen;
    uint8 sessionId;

    /* --- Method 0x01: Reject consistency check outside WAIT_CONSISTENCY ---
     * Method 0x01 is only valid during the WAIT_CONSISTENCY state (handled there).
     * If received in RUNNING/SYNC_TO_A, reply with error so A-core knows
     * the request was rejected (consistency check already completed). */
    if (PICC_GetMethodData(PICC_APP_STORAGE, STM_METHOD_CONSISTENCY_CHECK,
                           s_methodBuf, sizeof(s_methodBuf), &methodLen,
                           &sessionId, NULL, NULL) == PICC_E_OK)
    {
        (void)PICC_MethodResponse(PICC_APP_STORAGE, STM_METHOD_CONSISTENCY_CHECK,
                                   sessionId, 0x01U, NULL, 0U);
    }

    /* --- Method 0x02: A-core write to M-core ---
     * Note: Method 0x01 (Consistency Check) is handled exclusively in the
     * WAIT_CONSISTENCY state. It is NOT processed here to avoid misinterpreting
     * dataId=0x0000 (global consistency probe) as a lookup for a business
     * data block with ID 0, which would always return NOT_OK. */
    if (PICC_GetMethodData(PICC_APP_STORAGE, STM_METHOD_A_WRITE,
                           s_methodBuf, sizeof(s_methodBuf), &methodLen,
                           &sessionId, NULL, NULL) == PICC_E_OK)
    {
        /* A-core sends: 2B dataId (big-endian) + data */
        if (methodLen >= 2U)
        {
            uint16 reqDataId = ((uint16)s_methodBuf[0] << 8U) | (uint16)s_methodBuf[1];
            uint16 writeDataLen = methodLen - 2U;

            if (StmNvm_WriteFromA(reqDataId, &s_methodBuf[2], writeDataLen) == E_OK)
            {
                /* Write succeeded - response: 2B dataId + 2B status(OK) */
                s_respBuf[0] = s_methodBuf[0];
                s_respBuf[1] = s_methodBuf[1];
                s_respBuf[2] = 0x00U;  /* status high byte */
                s_respBuf[3] = 0x00U;  /* status low byte: OK */
                (void)PICC_MethodResponse(PICC_APP_STORAGE, STM_METHOD_A_WRITE,
                                           sessionId, 0x00U, s_respBuf, 4U);
            }
            else
            {
                /* Write failed (invalid dataId, length mismatch, or EEPROM error) */
                s_respBuf[0] = s_methodBuf[0];
                s_respBuf[1] = s_methodBuf[1];
                s_respBuf[2] = 0x00U;
                s_respBuf[3] = 0x01U;  /* status: NOT OK */
                (void)PICC_MethodResponse(PICC_APP_STORAGE, STM_METHOD_A_WRITE,
                                           sessionId, 0x01U, s_respBuf, 4U);
            }
        }
        else
        {
            /* Invalid payload length (< 2 bytes, missing dataId) */
            (void)PICC_MethodResponse(PICC_APP_STORAGE, STM_METHOD_A_WRITE,
                                       sessionId, 0x01U, NULL, 0U);
        }
    }
}

/***********************************************************************************************************************
*  Sub-task: Process sync to A (Method 0x04, with retry)
*
*  Syncs dirty NVM blocks to A-core using Method 0x04 (REQUEST with RESPONSE).
*  Only one sync can be in-flight at a time. Retry logic with stepped intervals
*  handles temporary communication failures.
*
*  Response handling strategy (data integrity first):
*  ┌──────────────────────────────────────────────────────────────────┐
*  │  A-core response          │  Action                            │
*  ├───────────────────────────┼────────────────────────────────────┤
*  │  ReturnCode=0x00 (OK)     │  clear dirty, release slot        │
*  │  ReturnCode≠0x00 (NOT_OK) │  keep dirty, release slot,        │
*  │                           │  do NOT count as retry attempt     │
*  │                           │  (A-core rejected, retry is futile │
*  │                           │  until A-core side recovers)       │
*  │  No response (timeout)    │  keep dirty, retryCount++,         │
*  │                           │  stepped retry (100->200->400->800)│
*  │  Max retries exhausted    │  keep dirty, release slot,         │
*  │                           │  next round-robin scan will retry  │
*  └──────────────────────────────────────────────────────────────────┘
*
*  Anti-storm: Max STM_SYNC_MAX_PER_CYCLE (2) sync messages per 10ms cycle.
***********************************************************************************************************************/
static void Stm_ProcessSyncToA(void)
{
    static uint8 s_syncBuf[STM_NVM_BLOCK_MAX_SIZE + 4U]; /* static to avoid stack overflow */
    uint16 syncDataId;
    const uint8 *syncData;
    uint16 syncLen;
    uint16 payloadLen;
    uint8 sessionId;
    uint8 syncReturnCode;

    /* --- Handle ongoing retry if a sync is already in-flight --- */
    if (Stm_RetryState.active != 0U)
    {
        if (Stm_RetryState.retryCount >= STM_RETRY_MAX_COUNT)
        {
            /* Max retries exhausted - release slot so other items can proceed.
             * IMPORTANT: Do NOT clear dirty flag! The data has NOT been
             * successfully synced to A-core. Clearing dirty would cause
             * silent data loss. The item will be picked up again by the next
             * round-robin scan via StmNvm_GetSyncableItem(). This provides
             * "degraded background retry" - the system moves forward but
             * continues attempting to sync this item. */
            Stm_RetryState.active = 0U;
            return;
        }

        /* Check if A-core has responded to our sync request (precise sessionId matching) */
        if (PICC_GetResponseData(PICC_APP_STM_CLI, STM_METHOD_M_SYNC_TO_A,
                                 Stm_RetryState.sessionId, &syncReturnCode,
                                 s_syncBuf, sizeof(s_syncBuf), &syncLen,
                                 NULL, NULL) == PICC_E_OK)
        {
            if (syncReturnCode == (uint8)PICC_RET_OK)
            {
                /* A-core protocol layer accepted the request - sync successful.
                 * Dirty flag is cleared because A-core has confirmed receipt. */
                StmNvm_ClearDirty(Stm_RetryState.dataId);
                Stm_RetryState.active = 0U;
            }
            else
            {
                /* A-core protocol layer rejected (ReturnCode != 0x00).
                 * This means A-core received the message but refused it
                 * (e.g. internal error, not ready, resource exhausted).
                 *
                 * Key design decision: Do NOT count this as a retry attempt.
                 * A-core rejection is typically a persistent condition (config
                 * mismatch, resource exhaustion). Retrying immediately with
                 * the same data is unlikely to succeed until A-core recovers.
                 * The slot is released so other dirty items can proceed,
                 * and this item will be retried in the next round-robin scan.
                 *
                 * Reset retryCount so that when this item is picked up again,
                 * it starts with a fresh retry budget rather than being close
                 * to max retries from previous failed attempts. */
                Stm_RetryState.active = 0U;
            }
            return;
        }

        /* No response yet - check if retry interval has elapsed */
        Stm_RetryState.tickCounter++;
        if (Stm_RetryState.tickCounter < g_StmRetryIntervals[Stm_RetryState.retryCount])
        {
            return; /* Wait for next interval tick */
        }

        /* Interval elapsed - retry with next stepped delay */
        Stm_RetryState.tickCounter = 0U;
        Stm_RetryState.retryCount++;

        /* Re-read the data from NVM and resend */
        if (StmNvm_Read(Stm_RetryState.dataId, s_syncBuf, STM_NVM_BLOCK_MAX_SIZE, &syncLen) == E_OK)
        {
            static uint8 s_txPayload[STM_NVM_BLOCK_MAX_SIZE + 2U];
            Stm_BuildSyncPayload(Stm_RetryState.dataId, s_syncBuf, syncLen,
                                   s_txPayload, &payloadLen);
            Stm_RetryState.sessionId = PICC_MethodRequest(PICC_APP_STM_CLI, STM_METHOD_M_SYNC_TO_A,
                                                           s_txPayload, payloadLen,
                                                           PICC_METHOD_WITH_RESPONSE);
        }
        return;
    }

    /* --- Anti-storm check: limit sync messages per 10ms cycle --- */
    if (Stm_SyncCycleCount >= STM_SYNC_MAX_PER_CYCLE)
    {
        return;
    }

    /* --- Find next dirty block to sync (round-robin) --- */
    if (StmNvm_GetSyncableItem(Stm_SyncScanIndex, &syncDataId, &syncData, &syncLen) == E_OK)
    {
        /* Found a dirty block - build payload and send via Method 0x04 */
        static uint8 s_txPayload[STM_NVM_BLOCK_MAX_SIZE + 2U];
        Stm_BuildSyncPayload(syncDataId, syncData, syncLen, s_txPayload, &payloadLen);

        sessionId = PICC_MethodRequest(PICC_APP_STM_CLI, STM_METHOD_M_SYNC_TO_A,
                                        s_txPayload, payloadLen,
                                        PICC_METHOD_WITH_RESPONSE);
        if (sessionId != 0U)
        {
            /* Request sent successfully - set up retry state to track this sync */
            Stm_RetryState.dataId = syncDataId;
            Stm_RetryState.sessionId = sessionId;
            Stm_RetryState.retryCount = 0U;
            Stm_RetryState.tickCounter = 0U;
            Stm_RetryState.active = 1U;

            Stm_SyncCycleCount++;

            /* Advance scan index for next round-robin iteration */
            Stm_SyncScanIndex++;
            if (Stm_SyncScanIndex >= STM_MAX_DATA_ITEMS)
            {
                Stm_SyncScanIndex = 0U;
            }
        }
        else
        {
            /* Send failed (PICC busy or link issue) - will retry next cycle */
        }
    }
    else
    {
        /* No dirty blocks found - reset scan index for next scan */
        Stm_SyncScanIndex = 0U;
    }
}

/***********************************************************************************************************************
*  Sub-task: Process app read request (Method 0x03/0x05, no retry)
*
*  Handles responses for previously sent Method 0x03 (M read from A) and
*  Method 0x05 (M async read from A) requests. These requests do NOT
*  implement retry logic - if the response is not received, the request
*  simply times out and the application must re-request.
*
*  Only one read request can be pending at a time (Stm_PendingReadReq).
*  Response matching uses the session ID returned by PICC_MethodRequest.
 *  Both the PICC response returnCode and the STM payload status must be OK
 *  before returned data is accepted into local NVM.
***********************************************************************************************************************/
static void Stm_ProcessAppReadReq(void)
{
    static uint8 s_readRspBuf[STM_NVM_BLOCK_MAX_SIZE + 4U]; /* static to avoid stack overflow */
    uint16 rspLen;
    uint8 readReturnCode;

    if (Stm_PendingReadReq.active == 0U)
    {
        return; /* No pending read request */
    }

    /* Poll for response from A-core matching the pending request's session ID */
    if (PICC_GetResponseData(PICC_APP_STM_CLI, Stm_PendingReadReq.methodId,
                             Stm_PendingReadReq.sessionId, &readReturnCode,
                             s_readRspBuf, sizeof(s_readRspBuf), &rspLen,
                             NULL, NULL) == PICC_E_OK)
    {
        if (readReturnCode == (uint8)PICC_RET_OK)
        {
            /* Response received from A-core - parse and write to local NVM */
            if (Stm_PendingReadReq.methodId == STM_METHOD_M_ASYNC_READ)
            {
                /* Method 0x05 response format: 2B dataId + 2B status + data
                 * Minimum valid payload: 4 bytes (2B dataId + 2B status) */
                if ((rspLen >= 4U) && (s_readRspBuf[2] == 0x00U) && (s_readRspBuf[3] == 0x00U))
                {
                    /* Status = 0x0000 (OK): write returned data to local NVM */
                    uint16 rspDataId = ((uint16)s_readRspBuf[0] << 8U) | (uint16)s_readRspBuf[1];
                    uint16 rspDataLen = rspLen - 4U;

                    if (rspDataLen > 0U)
                    {
                        (void)StmNvm_WriteFromA(rspDataId, &s_readRspBuf[4], rspDataLen);
                    }
                }
                /* else: payload status != 0x0000 (NOT_OK) or payload too short - discard */
            }
            else
            {
                /* Method 0x03 response format: 2B dataId + 2B status + data
                 * Minimum valid payload: 4 bytes (2B dataId + 2B status) */
                if ((rspLen >= 4U) && (s_readRspBuf[2] == 0x00U) && (s_readRspBuf[3] == 0x00U))
                {
                    /* Status = 0x0000 (OK): write returned data to local NVM */
                    uint16 rspDataId = ((uint16)s_readRspBuf[0] << 8U) | (uint16)s_readRspBuf[1];
                    uint16 rspDataLen = rspLen - 4U;

                    if (rspDataLen > 0U)
                    {
                        (void)StmNvm_WriteFromA(rspDataId, &s_readRspBuf[4], rspDataLen);
                    }
                }
                /* else: payload status != 0x0000 (NOT_OK) or payload too short - discard */
            }
        }
        /* else: PICC returnCode != OK - discard response payload */

        /* Mark request as completed - application can issue new requests */
        Stm_PendingReadReq.active = 0U;
    }
    /* Note: No timeout handling here. If A-core never responds, the request
     * will remain pending until disconnect resets it. A future enhancement
     * could add a timeout counter. */
}

/***********************************************************************************************************************
*  Public function definitions
***********************************************************************************************************************/

/**
 * Initialize STM module:
 * 1. Register dual-role PICC endpoints on Channel 1
 *    - Provider (Server): receives Method requests from A-core
 *    - Consumer (Client): sends Method requests to A-core
 * 2. Initialize NVM layer (EEPROM read-back to RAM mirror)
 * 3. Set initial state to WAIT_LINK
 */
void Stm_Init(void)
{
    /* --- Provider (Server) configuration ---
     * M-Core acts as Server for Methods 0x01 and 0x02.
     * A-Core sends requests TO this Provider.
     * localId  = STM_PROVIDER_ID (41/0x29): identifies this Server
     * remoteId = STM_CONSUMER_REMOTE_ID (46/0x2E): the A-Core Client that talks to us */
    static const PICC_AppConfig_t stmSrvCfg = {
        .localId                = STM_PROVIDER_ID,       /* 41 (0x29) */
        .remoteId               = STM_CONSUMER_REMOTE_ID, /* 46 (0x2E) */
        .role                   = PICC_ROLE_SERVER,
        .channelId              = STM_CHANNEL_ID,        /* 1 (HP channel) */
        .Client_linkReq_PeriodMs = 0U,  /* Server does not send link requests */
        .methodHandler          = NULL,  /* Polling mode (no callback) */
        .eventHandler           = NULL   /* No event needed for Server role */
    };

    /* --- Consumer (Client) configuration ---
     * M-Core acts as Client for Methods 0x03, 0x04, 0x05.
     * M-Core sends requests TO A-Core's Provider.
     * localId  = STM_CONSUMER_ID (42/0x2A): identifies this Client
     * remoteId = STM_PROVIDER_REMOTE_ID (47/0x2F): the A-Core Server we talk to */
    static const PICC_AppConfig_t stmCliCfg = {
        .localId                = STM_CONSUMER_ID,        /* 42 (0x2A) */
        .remoteId               = STM_PROVIDER_REMOTE_ID,  /* 47 (0x2F) */
        .role                   = PICC_ROLE_CLIENT,
        .channelId              = STM_CHANNEL_ID,         /* 1 (HP channel) */
        .Client_linkReq_PeriodMs = 0U,  /* Default 10ms link request period */
        .methodHandler          = NULL,  /* Polling mode (no callback) */
        .eventHandler           = NULL   /* No event handler needed */
    };

    /* Register both roles with PICC middleware */
    (void)PICC_Init(PICC_APP_STORAGE, &stmSrvCfg);
    (void)PICC_Init(PICC_APP_STM_CLI, &stmCliCfg);

    /* Initialize NVM layer (EEPROM -> RAM mirror) */
    (void)StmNvm_Init();

    /* Set initial state machine state */
    Stm_State = STM_STATE_WAIT_LINK;
    Stm_RetryState.active = 0U;
    Stm_PendingReadReq.active = 0U;
    Stm_SyncScanIndex = 0U;
    Stm_SyncCycleCount = 0U;
}

/**
 * 10ms periodic entry point. Drives the state machine and all sub-task processing.
 *
 * State machine transitions:
 *   WAIT_LINK       -> WAIT_CONSISTENCY  (both links connected)
 *   WAIT_CONSISTENCY -> SYNC_TO_A        (consistency check done/passed)
 *   SYNC_TO_A       -> RUNNING           (all dirty data synced)
 *   RUNNING         -> WAIT_LINK        (link lost)
 *
 * In SYNC_TO_A and RUNNING states, the following sub-tasks run:
 *   - Stm_ProcessRxFromA():    Handle Method 0x02 from A-core (0x01 handled in WAIT_CONSISTENCY)
 *   - Stm_ProcessSyncToA():    Sync dirty data to A-core (Method 0x04)
 *   - Stm_ProcessAppReadReq(): Handle responses for Method 0x03/0x05
 *   - Stm_CheckLinkState():    Monitor link health (RUNNING only)
 */
void Stm_Main(void)
{
    /* Reset anti-storm counter at the start of each 10ms cycle */
    Stm_SyncCycleCount = 0U;

    switch (Stm_State)
    {
        case STM_STATE_UNINIT:
            /* Should never happen - Stm_Init() transitions to WAIT_LINK */
            break;

        case STM_STATE_WAIT_LINK:
            /* Wait for both Provider and Consumer links to be connected.
             * PICC handles link request/acknowledgment automatically.
             * No business data is sent until both links are established. */
            if (Stm_IsLinkReady() == TRUE)
            {
                Stm_State = STM_STATE_WAIT_CONSISTENCY;
            }
            break;

        case STM_STATE_WAIT_CONSISTENCY:
            /* Check link state FIRST - if link is lost while waiting for
             * consistency check, immediately fall back to WAIT_LINK. */
            Stm_CheckLinkState();
            if (Stm_State != STM_STATE_WAIT_CONSISTENCY)
            {
                break;  /* Link lost, already transitioned to WAIT_LINK */
            }

            /* Poll for Method 0x01 (Consistency Check) from A-core.
             * Protocol: A-core sends 2B dataId (must be 0x0000 for global
             * consistency check). M-core replies with 4-byte success
             * payload [0x00,0x00]+[0x00,0x00] (dataId + status=OK). */
            {
                static uint8 s_consReqBuf[4U]; /* static to avoid stack overflow */
                uint16 consReqLen;
                uint8 consSessionId;

                if (PICC_GetMethodData(PICC_APP_STORAGE, STM_METHOD_CONSISTENCY_CHECK,
                                       s_consReqBuf, sizeof(s_consReqBuf), &consReqLen,
                                       &consSessionId, NULL, NULL) == PICC_E_OK)
                {
                    /* Validate: payload must be >= 2 bytes and dataId must be 0x0000 */
                    if ((consReqLen >= 2U) &&
                        (s_consReqBuf[0] == 0x00U) && (s_consReqBuf[1] == 0x00U))
                    {
                        /* Consistency check passed - reply with 4-byte success */
                        static uint8 s_consRespBuf[4U];
                        s_consRespBuf[0] = 0x00U;  /* dataId high byte */
                        s_consRespBuf[1] = 0x00U;  /* dataId low byte */
                        s_consRespBuf[2] = 0x00U;  /* status high byte */
                        s_consRespBuf[3] = 0x00U;  /* status low byte: OK */
                        (void)PICC_MethodResponse(PICC_APP_STORAGE, STM_METHOD_CONSISTENCY_CHECK,
                                                   consSessionId, 0x00U, s_consRespBuf, 4U);

                        /* Mark all valid NVM blocks as dirty to trigger full sync */
                        StmNvm_SetAllValidDirty();

                        /* Transition to SYNC_TO_A to begin data synchronization */
                        Stm_State = STM_STATE_SYNC_TO_A;
                    }
                    else
                    {
                        /* Invalid consistency check request (dataId != 0x0000 or len < 2).
                         * Reply with error so A-core knows M-core rejected it. */
                        (void)PICC_MethodResponse(PICC_APP_STORAGE, STM_METHOD_CONSISTENCY_CHECK,
                                                   consSessionId, 0x01U, NULL, 0U);
                    }
                }
            }
            break;

        case STM_STATE_SYNC_TO_A:
            /* Check link state FIRST - if link is lost during sync,
             * immediately fall back to WAIT_LINK to avoid blind retries.
             * Without this check, SYNC_TO_A would waste up to 4 stepped
             * retries per dirty item before eventually reaching RUNNING
             * and detecting the disconnect there. */
            Stm_CheckLinkState();
            if (Stm_State != STM_STATE_SYNC_TO_A)
            {
                break;  /* Link lost, already transitioned to WAIT_LINK */
            }

            /* Sync all dirty NVM data to A-core before entering normal operation.
             * This ensures A-core has the latest M-core data after reconnection. */
            Stm_ProcessSyncToA();

            /* Check if all dirty data has been synced (no more syncable items
             * and no retry in progress) */
            {
                uint16 dummyId;
                const uint8 *dummyData;
                uint16 dummyLen;
                if (StmNvm_GetSyncableItem(0U, &dummyId, &dummyData, &dummyLen) != E_OK)
                {
                    /* No more dirty items - transition to RUNNING if retry is done */
                    if (Stm_RetryState.active == 0U)
                    {
                        Stm_State = STM_STATE_RUNNING;
                    }
                }
            }
            /* Also process RX from A-core while syncing */
            Stm_ProcessRxFromA();
            break;

        case STM_STATE_RUNNING:
            /* Normal operation: all sub-tasks are active */
            Stm_ProcessRxFromA();      /* Handle A-core Method requests */
            Stm_ProcessSyncToA();       /* Sync any new dirty data */
            Stm_ProcessAppReadReq();   /* TODO: Handle pending read responses */

            /* Check link state - transition to WAIT_LINK if either link is lost */
            Stm_CheckLinkState();
            break;

        default:
            /* Invalid state - should never happen */
            break;
    }

    /* Process TRACE32 interactive debug test command (if NVM_test_flag != 0) */
    Stm_ProcessTest();
}

/**
 * Write data to a local NVM block by dataId.
 * Updates RAM mirror and immediately persists to EEPROM.
 * The block is marked dirty for potential A-core sync.
 */
Std_ReturnType Stm_WriteLocal(uint16 dataId, const uint8 *data, uint16 len)
{
    return StmNvm_Write(dataId, data, len);
}

/**
 * Read data from a local NVM block by dataId.
 * Reads from RAM mirror (zero latency, EEPROM is already synced).
 */
Std_ReturnType Stm_ReadLocal(uint16 dataId, uint8 *data, uint16 maxLen, uint16 *actualLen)
{
    return StmNvm_Read(dataId, data, maxLen, actualLen);
}

/**
 * Asynchronously request A-core to send data for the specified dataId.
 *
 * Uses Method 0x03 /0x5 (M async read from A) with PICC_METHOD_WITH_RESPONSE.
 * The response is handled asynchronously in Stm_ProcessAppReadReq().
 *
 * Constraints:
/**
 * Asynchronously request A-core to send data for the specified dataId.
 * Supports both Method 0x03 (M read from A) and Method 0x05 (M async read from A).
 * Response is handled in Stm_Main() state machine.
 *
 * - Must be in RUNNING state (link established and data synced)
 * - Only one read request can be pending at a time
 *
 * @param methodId  Method identifier (STM_METHOD_M_READ_FROM_A or STM_METHOD_M_ASYNC_READ)
 * @param dataId    Data item identifier to request from A-core
 * @return E_OK if request sent, E_NOT_OK if not in RUNNING state or request already pending
 */
Std_ReturnType Stm_RequestReadFromA(uint8 methodId, uint16 dataId)
{
    static uint8 s_reqPayload[4U]; /* 2B dataId (big-endian) + 2B reserved */
    uint8 sessionId;

    /* Guard: must be in RUNNING state to send requests */
    if (Stm_State != STM_STATE_RUNNING)
    {
        return E_NOT_OK;
    }

    /* Guard: only allow Method 0x03 and Method 0x05 */
    if ((methodId != STM_METHOD_M_READ_FROM_A) && (methodId != STM_METHOD_M_ASYNC_READ))
    {
        return E_NOT_OK;
    }

    /* Guard: only one pending read request at a time */
    if (Stm_PendingReadReq.active != 0U)
    {
        return E_NOT_OK; /* Previous request still pending */
    }

    /* Build request payload: 2B dataId in big-endian per protocol */
    s_reqPayload[0] = (uint8)(dataId >> 8U);
    s_reqPayload[1] = (uint8)(dataId & 0xFFU);
    s_reqPayload[2] = 0U;  /* Reserved byte */
    s_reqPayload[3] = 0U;  /* Reserved byte */

    /* Send Method request with RESPONSE expected (async on M-core) over IPCF/PICC */
    sessionId = PICC_MethodRequest(PICC_APP_STM_CLI, methodId,
                                    s_reqPayload, 4U,
                                    PICC_METHOD_WITH_RESPONSE);
    if (sessionId == 0U)
    {
        /* PICC_MethodRequest failed (link busy or internal error) */
        return E_NOT_OK;
    }

    /* Track the pending request for response matching in Stm_ProcessAppReadReq() */
    Stm_PendingReadReq.dataId = dataId;
    Stm_PendingReadReq.methodId = methodId;
    Stm_PendingReadReq.sessionId = sessionId;
    Stm_PendingReadReq.active = 1U;

    return E_OK;
}
