/**
 * @file hm.h
 * @brief Health Management Module — Public Interface
 *
 * Provides health monitoring data structures and API for the HM module.
 *
 * The HM module registers two PICC applications:
 *   1. Heartbeat Provider (ProviderID=21): sends periodic Ping/Pong
 *   2. Health Reporter (ProviderID=81): reports real-time info and fault events
 *
 * Copyright 2025 Vitesco Technologies
 * All Rights Reserved.
 */

#ifndef HM_H
#define HM_H

#if defined(__cplusplus)
extern "C" {
#endif

/***********************************************************************************************************************
*  include files
***********************************************************************************************************************/
#include "Platform.h"

/*==================================================================================================
*                                         Data Structures — Real-Time Info
*==================================================================================================*/

/**
 * @brief Per-partition real-time health data
 *
 * Describes the state and data count of a single partition
 * (e.g., M-Core partition or A-Core partition).
 */
typedef struct
{
    uint8 PartitionID;       /**< Partition identifier (1=M-Core, 2=A-Core) */
    uint8 NrOfPartitions;   /**< Total number of partitions in the system */
    uint8 PartitionState;   /**< Current partition state (running, shutdown, etc.) */
    uint8 Reserved[4];      /**< Reserved for future use */
    uint8 NrOfRealTimeData; /**< Number of real-time data entries following */
} HealthRealTimeData_st;

/**
 * @brief Real-time information report payload
 *
 * Wraps partition ID and its real-time health data.
 * Sent via PICC Method response when A-Core queries real-time info.
 */
typedef struct
{
    uint8 PartitionId;               /**< Partition identifier */
    uint8 Reserved[3];               /**< Reserved for alignment */
    HealthRealTimeData_st RealTimeData; /**< Nested real-time data */
} RealTimeInfo_st;

/*==================================================================================================
*                                         Data Structures — Fault / Hazard
*==================================================================================================*/

/**
 * @brief Single fault/hazard entry
 *
 * Describes one fault event with its ID, associated values, category,
 * detection source, frequency, and severity. Designed to be packed
 * for transmission over IPCF without padding.
 */
typedef struct __attribute__((packed))
{
    uint8  ID[16];      /**< Unique fault ID in ASCII format, designed by each module */
    uint32 Value[4];    /**< Up to 4 x 32-bit fault values, module-specific */
    uint8  Category;    /**< Fault category, system-defined */
    uint8  PartitionID; /**< Partition where the fault occurred */
    uint8  DetectedBy;  /**< Module/channel that detected the fault (e.g., DTC, bus) */
    uint8  Frequency;   /**< Number of fault occurrences */
    uint8  Severity;    /**< Fault severity level, system-defined */
    uint8  Reserved[3]; /**< Reserved for alignment */
} HealthHazard_st;

/*==================================================================================================
*                                         Data Structures — Health Indicator
*==================================================================================================*/

/**
 * @brief Health indicator summary
 *
 * Aggregated health metrics for a partition: performance, reliability,
 * partition count, state, and health index.
 */
typedef struct __attribute__((packed))
{
    uint8 Performance;    /**< Performance metric (currently unused) */
    uint8 Reliability;   /**< Reliability metric (currently unused) */
    uint8 NrOfPartitions;/**< Total number of partitions (A-Core=1, M-Core=1, total=2) */
    uint8 PartitionState;/**< Current partition state */
    uint8 HealthIndex;   /**< Health index (currently unused) */
    uint8 Reserved[3];   /**< Reserved for alignment */
} HealthIndicator_st;

/*==================================================================================================
*                                         Data Structures — Full Health Info
*==================================================================================================*/

/**
 * @brief Complete health information snapshot
 *
 * Contains a timestamp, health indicator summary, per-category
 * hazard counts, and up to 10 fault entries.
 * Designed to be packed for IPCF transmission.
 */
typedef struct __attribute__((packed))
{
    uint64 Timestamp;                     /**< Timestamp of the health snapshot */
    HealthIndicator_st Indicator;          /**< Aggregated health indicators */
    uint8 PartitionID;                    /**< M-Core Partition ID (currently 1) */
    uint16 CategoryMaxNrOfHazard[3];      /**< [0]=SWC count monitored by CP WDGM, [1],[2] unused */
    uint8 Reserved[4];                     /**< Reserved for alignment */
    uint8 NrOfHazards;                    /**< Number of valid fault entries in Hazards[] */
    HealthHazard_st Hazards[10];          /**< Fault information array (max 10 entries) */
} HealthInfo_st;

/**
 * @brief PICC health information message payload
 *
 * Top-level structure sent as the payload for the health report Event.
 * Contains partition ID and the full health info snapshot.
 * Designed to be packed for IPCF transmission.
 */
typedef struct __attribute__((packed))
{
    uint8   PartitionId;           /**< M-Core Partition ID (currently 1) */
    uint8   Reserved[3];           /**< Reserved for alignment */
    HealthInfo_st HealthInfo;      /**< Complete health data */
} PICCHealthInfo_st;

/*==================================================================================================
*                                         Function Declarations
*==================================================================================================*/

/**
 * @brief Initialize the Health Management module
 *
 * Registers two PICC applications with the PICC driver:
 *   1. Heartbeat Provider (PICC_ID_HEALTH_HB_LOCAL=21, channel 1):
 *      Sends periodic Ping/Pong for channel health detection.
 *   2. Health Reporter (PICC_ID_HEALTH_RPT_LOCAL=81, channel 2):
 *      Reports real-time info (Method) and fault events (Event).
 *
 * Must be called after PICC_PreOS_Init() and before the scheduler starts.
 */
void Hm_Init(void);

/**
 * @brief Health Management 10ms periodic main function
 *
 * Called from TASK_M0_10MS() every 10ms.
 *
 * Internally runs three sub-tasks:
 *   1. Hm_HeartBeat_Task() — Ping/Pong transmission and timeout detection
 *   2. Hm_ReportRealTimeInfo_Task() — Respond to A-Core real-time info queries
 *   3. Hm_ReportFault_Task() — Edge-triggered fault event reporting
 *
 * Also manages PMIC (VR55XX) fault flag register read/clear cycle.
 */
void Hm_Main(void);

#if defined(__cplusplus)
}
#endif

#endif /* HM_H */
