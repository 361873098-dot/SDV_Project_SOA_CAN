/********************************************************************************
* COPYRIGHT (C) Vitesco Technologies 2026
* ALL RIGHTS RESERVED.
*
* The reproduction, transmission or use of this document or its
* contents is not permitted without express written authority.
* Offenders will be liable for damages. All rights, including rights
* created by patent grant or registration of a utility model or design,
* are reserved.
*********************************************************************************
*
*  File name:           $Source: diag_mgmt.h $
*  Revision:            $Revision: 3.0 $
*  Author:              $Author: Auto Generated $
*  Module acronym:      DIAG_MGMT
*  Specification:       DIAG_4 DoIP Activation Line Interface
*  Date:                $Date: 2026/04/14 $
*
*  Description:     Diagnostic Management module header
*                   Implements DIAG_4: DoIP Activation Line status service.
*
*                   Protocol details (from DOIP Activation Line Interface table):
*                     - ProviderID (M-Core Server): 52 (0x34)
*                     - ConsumerID (A-Core Client): 60 (0x3C)
*                     - Event ID: 5 (DoIP activation line status)
*                     - Event Payload: 1 byte (DiagnosisType value)
*                     - Service Type: Triggered, 30ms period
*                     - Channel: HP (High Priority, channel 1)
*                     - Transport: IPCF
*
*                   Roles (per updated design document):
*                     M-Core = Server (ProviderID=52):
*                       sends Event (ID=5) with DoIP activation line status
*                       to A-Core Client. Passively waits for Client to connect.
*                     A-Core = Client (ConsumerID=60):
*                       receives Event (DoIP activation line status),
*                       actively initiates link-connect requests.
*
*                   Data flow: M-Core (Server) --Event--> A-Core (Client)
*                   M-Core uses PICC_SendEvent with PICC_EVENT_WITHOUT_ACK
*                   (fire-and-forget) due to real-time constraints.
*
*********************************************************************************
*
*  Changes:
*    v2.0: Corrected M-Core role from SERVER to CLIENT per Schaeffler design doc.
*          Changed ProviderID/ConsumerID assignment (localId=60, remoteId=52).
*          Changed send mechanism from PICC_SendEvent to PICC_MethodRequest.
*    v3.0: Reversed roles per updated requirement:
*          M-Core is now SERVER (ProviderID=52), A-Core is CLIENT (ConsumerID=60).
*          M-Core sends Event (ID=5) to A-Core Client (Server->Client notification).
*          Replaced PICC_MethodRequest with PICC_SendEvent.
*
*********************************************************************************/

#ifndef DIAG_MGMT_H
#define DIAG_MGMT_H

#if defined(__cplusplus)
extern "C"{
#endif

/***********************************************************************************************************************
*  include files
***********************************************************************************************************************/
#include "Platform.h"

/***********************************************************************************************************************
*  Macro Definitions
***********************************************************************************************************************/

/** IPCF data channel ID for diagnostic management (HP = High Priority = channel 1) */
#define DIAG_MGMT_CHANNEL_ID            (2U)

/**
 * M-Core Server ProviderID for DIAG_4
 * This is the LOCAL ID (M-Core is the Server/Provider).
 */
#define DIAG_MGMT_PROVIDER_ID           (52U)   /* 0x34 */

/**
 * A-Core Client ConsumerID for DIAG_4
 * This is the REMOTE peer's ID (A-Core is the Client/Consumer).
 */
#define DIAG_MGMT_CONSUMER_ID           (60U)   /* 0x3C */

/** Event ID for DoIP activation line status notification */
#define DIAG_MGMT_EVENT_DOIP_ACT_LINE   (5U)

/** Send period in units of 10ms task ticks (30ms = 3 * 10ms) */
#define DIAG_MGMT_SEND_PERIOD_TICKS     (3U)

/***********************************************************************************************************************
*  Type Definitions
***********************************************************************************************************************/

/**
 * @brief DoIP activation line diagnosis type
 *
 * Matches the A-Core enum DiagnosisType:
 *   kNoObd  = 0x00  — No OBD active, do not send
 *   kObdCan = 0x01  — OBD-CAN active (high priority)
 *   kObdEth = 0x02  — OBD-ETH / DoIP activation line active
 *
 * When both kObdCan and kObdEth are triggered simultaneously,
 * the higher-priority value (kObdCan=1, lower numeric) takes precedence.
 */
typedef enum {
    DIAG_MGMT_NO_OBD    = 0x00U,   /**< No OBD — do not send */
    DIAG_MGMT_OBD_CAN   = 0x01U,   /**< OBD-CAN active (high priority) */
    DIAG_MGMT_OBD_ETH   = 0x02U    /**< OBD-ETH / DoIP activation (lower priority) */
} DiagMgmt_DiagType_e;

/***********************************************************************************************************************
*  Global Variable Declarations
***********************************************************************************************************************/

/**
 * @brief Test input variable — controls DoIP activation line report
 *
 * Set by application or test harness:
 *   0 = kNoObd  : do not send
 *   1 = kObdCan : send OBD-CAN status (high priority)
 *   2 = kObdEth : send OBD-ETH / DoIP activation status
 *
 * This value is directly used as the 1-byte Event payload.
 */
extern volatile uint8 Test_input;

/***********************************************************************************************************************
*  Function Declarations
***********************************************************************************************************************/

/**
 * @brief Initialize the Diagnostic Management module
 *
 * Registers with the PICC driver using PICC_APP_RSV0 slot:
 *   - Role: SERVER (M-Core is the service provider)
 *   - localId  = DIAG_MGMT_PROVIDER_ID (52) — M-Core Server
 *   - remoteId = DIAG_MGMT_CONSUMER_ID (60) — A-Core Client
 *   - Channel: 1 (HP)
 *   - No callbacks (polling mode)
 *
 * As SERVER, M-Core passively waits for A-Core Client to connect.
 * A-Core (Client, ConsumerID=60) actively sends link-connect requests.
 *
 * Must be called after PICC_PreOS_Init() and before the scheduler starts.
 */
//void DiagMgmt_Init(void);

/**
 * @brief Diagnostic Management 10ms periodic main function
 *
 * Called from TASK_M0_10MS() every 10ms.
 * Internally uses a counter to achieve 30ms send period.
 *
 * Behavior:
 *   - If Test_input == 0 (kNoObd): no Event is sent.
 *   - If Test_input == 1 (kObdCan) or 2 (kObdEth): sends an Event
 *     (NOTIFICATION_WITHOUT_ACK, 0x09, Fire-and-Forget) with Test_input
 *     as the 1-byte payload every 30ms.
 *
 * M-Core uses PICC_EVENT_WITHOUT_ACK because:
 *   1. M-Core cannot synchronously wait for ACK (real-time constraints).
 *   2. Per IPCF protocol: M-Core ignores EVENT_ACK even if configured.
 */
//void DiagMgmt_Main(void);


//	void Hm_Init(void);


#if defined(__cplusplus)
}
#endif

#endif /* DIAG_MGMT_H */
