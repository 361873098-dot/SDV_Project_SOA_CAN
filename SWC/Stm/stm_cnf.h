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
*  File name:           $Source: stm_cnf.h $
*  Revision:            $Revision: 1.0 $
*  Author:              $Author: Li Song (uic59152)  $
*  Module acronym:      STM
*  Specification:
*  Date:                $Date: 2026/05/25  $
*
*  Description:     Storage Middleware configuration header
*
*********************************************************************************/

#ifndef STM_CNF_H
#define STM_CNF_H

#if defined(__cplusplus)
extern "C"{
#endif

#include "Platform.h"

/***********************************************************************************************************************
 *  PICC ID Configuration
 ***********************************************************************************************************************/

/** IPCF channel for STM (Channel 1 = HP) */
#define STM_CHANNEL_ID                 (1U)

/** M-Core Server ProviderID (Storage module range: 0x41~0x46) */
#define STM_PROVIDER_ID               (41U)   /* 0x29 */

/** M-Core Client ConsumerID */
#define STM_CONSUMER_ID               (42U)   /* 0x2A */

/** A-Core Server ProviderID (remote peer for Client role) */
#define STM_PROVIDER_REMOTE_ID         (47U)   /* 0x2F */

/** A-Core Client ConsumerID (remote peer for Server role) */
#define STM_CONSUMER_REMOTE_ID         (46U)   /* 0x2E */

/***********************************************************************************************************************
 *  Method ID Configuration
 ***********************************************************************************************************************/

/** Method 0x01: Consistency check (A->M, M responds with local data) */
#define STM_METHOD_CONSISTENCY_CHECK   (1U)

/** Method 0x02: A-core write to M-core (A->M, M writes data to local NVM) */
#define STM_METHOD_A_WRITE             (2U)

/** Method 0x03: M-core read from A-core (M->A, no retry on failure) */
#define STM_METHOD_M_READ_FROM_A       (3U)

/** Method 0x04: M-core sync to A-core (M->A, with retry) */
#define STM_METHOD_M_SYNC_TO_A         (4U)

/** Method 0x05: M-core async read from A-core (M->A, no retry) */
#define STM_METHOD_M_ASYNC_READ        (5U)

/***********************************************************************************************************************
 *  NVM Configuration
 ***********************************************************************************************************************/

/** EEPROM magic byte address and value */
#define STM_EEPROM_MAGIC_ADDR          (0x10U)
#define STM_EEPROM_MAGIC_VALUE         (0xA5U)

/** EEPROM data area start address (after magic byte) */
#define STM_EEPROM_DATA_START_ADDR     (0x11U)

/** EEPROM data area end address (inclusive) */
#define STM_EEPROM_DATA_END_ADDR       (0x50U)

/** Available EEPROM data area size in bytes */
#define STM_EEPROM_DATA_SIZE           ((STM_EEPROM_DATA_END_ADDR - STM_EEPROM_DATA_START_ADDR) + 1U)  /* 64 bytes */

/** Maximum NVM block data size */
#define STM_NVM_BLOCK_MAX_SIZE         (64U)

/** Maximum number of data items (constrained by EEPROM space) */
#define STM_MAX_DATA_ITEMS            (5U)

/***********************************************************************************************************************
 *  Data Item Configuration Type
 ***********************************************************************************************************************/

/**
 * @brief Data item configuration entry
 *
 * Each entry defines a data_id with its expected max length.
 * The EEPROM offset is computed at init time by accumulating
 * (1 + 1 + dataLen) for each item in order.
 */
typedef struct {
    uint16 dataId;       /**< Data item identifier */
    uint16 maxDataLen;   /**< Maximum data length for this item */
} Stm_DataItemCfg_t;

/***********************************************************************************************************************
 *  Anti-Storm Configuration
 ***********************************************************************************************************************/

/** Maximum 0x04 sync messages sent per 10ms cycle */
#define STM_SYNC_MAX_PER_CYCLE        (2U)

/***********************************************************************************************************************
 *  Retry Configuration
 ***********************************************************************************************************************/

/** Maximum retry count for 0x04 method */
#define STM_RETRY_MAX_COUNT           (4U)

/** Retry interval table length */
#define STM_RETRY_INTERVAL_COUNT      (4U)

/***********************************************************************************************************************
 *  State Machine Enum
 ***********************************************************************************************************************/

/**
 * @brief STM state machine states
 *
 * Transition diagram:
 *   UNINIT -> WAIT_LINK -> WAIT_CONSISTENCY -> SYNC_TO_A -> RUNNING
 *     |                                                      |
 *     +------------------------------------------------------+
 *                  (link lost -> return to WAIT_LINK)
 *
 * - UNINIT:            Before Stm_Init() is called
 * - WAIT_LINK:         Waiting for both Provider and Consumer PICC links to connect
 * - WAIT_CONSISTENCY:  Waiting for A-core consistency check (future implementation)
 * - SYNC_TO_A:         Syncing dirty NVM data to A-core before normal operation
 * - RUNNING:           Normal operation - all sub-tasks are active
 */
typedef enum {
    STM_STATE_UNINIT = 0U,     /**< Module not initialized */
    STM_STATE_WAIT_LINK,        /**< Waiting for PICC link establishment */
    STM_STATE_WAIT_CONSISTENCY, /**< Waiting for A-core consistency check */
    STM_STATE_SYNC_TO_A,        /**< Syncing dirty data to A-core */
    STM_STATE_RUNNING           /**< Normal operation */
} Stm_StateType_e;

/***********************************************************************************************************************
 *  External Configuration Data
 ***********************************************************************************************************************/

/** Data item configuration table (defined in stm_cnf.c) */
extern const Stm_DataItemCfg_t g_StmDataItemCfg[STM_MAX_DATA_ITEMS];

/** Retry interval table in 10ms ticks (defined in stm_cnf.c) */
extern const uint16 g_StmRetryIntervals[STM_RETRY_INTERVAL_COUNT];

#if defined(__cplusplus)
}
#endif

#endif /* STM_CNF_H */
