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
*  Revision:            $Revision: 2.0 $
*  Author:              $Author: Auto Generated $
*  Module acronym:      DIAG_MGMT
*  Specification:       Schaeffler_A-Sample Diagnostic Management (DoIP Activation Line)
*  Date:                $Date: 2026/04/08 $
*
*  Description:     Diagnostic Management module header
*                   Implements DIAG_4: M-Core (Client) sends DoIP activation
*                   line status to A-Core (Server) via IPCF Method Request.
*
*                   Protocol details (from cross-core design document):
*                     - ProviderID (A-Core Server): 52 (0x34)
*                     - ConsumerID (M-Core Client): 60 (0x3C)
*                     - MethodID: 5 (DoIP activation line status)
*                     - MessageType: REQUEST_NO_RETURN_WITHOUT_ACK (0x07)
*                     - Method-Request Payload: 1 byte (DiagnosisType enum value)
*                     - Send Period: 30ms (triggered, only when Test_input != 0)
*                     - Channel: HP (High Priority, channel 1)
*                     - Transport: IPCF
*
*                   Roles (per Schaeffler_A-Sample design document):
*                     A-Core = Server (ProviderID=52, AsfDiagnosticInfo):
*                       receives DoIP activation line status from M-Core.
*                     M-Core = Client (ConsumerID=60, SWC):
*                       periodically sends DoIP activation line status,
*                       actively initiates link-connect requests.
*
*********************************************************************************
*
*  Changes:
*    v2.0: Corrected M-Core role from SERVER to CLIENT per Schaeffler design doc.
*          Changed ProviderID/ConsumerID assignment (localId=60, remoteId=52).
*          Changed send mechanism from PICC_SendEvent to PICC_MethodRequest
*          (Client->Server Method Request, Fire-and-Forget).
*          Client actively sends link-connect requests (10ms default).
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
#define DIAG_MGMT_CHANNEL_ID            (1U)

/**
 * A-Core Server ProviderID for DIAG_4
 * This is the REMOTE peer's ID (A-Core is the Server).
 */
#define DIAG_MGMT_PROVIDER_ID           (52U)   /* 0x34 */

/**
 * M-Core Client ConsumerID for DIAG_4
 * This is the LOCAL ID (M-Core is the Client).
 */
#define DIAG_MGMT_CONSUMER_ID           (60U)   /* 0x3C */

/** Method ID for DoIP activation line status request */
#define DIAG_MGMT_METHOD_DOIP_ACT_LINE  (5U)

/** Send period in units of 10ms task ticks (30ms = 3 * 10ms) */
#define DIAG_MGMT_SEND_PERIOD_TICKS     (3U)

/** Client link-connect request period (ms). 0 = use default 10ms. */
#define DIAG_MGMT_LINK_REQ_PERIOD_MS    (10U)

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
 * This value is directly used as the 1-byte Method Request payload.
 */
extern volatile uint8 Test_input;

/***********************************************************************************************************************
*  Function Declarations
***********************************************************************************************************************/

/**
 * @brief Initialize the Diagnostic Management module
 *
 * Registers with the PICC driver using PICC_APP_RSV0 slot:
 *   - Role: CLIENT (M-Core is the service consumer)
 *   - localId  = DIAG_MGMT_CONSUMER_ID (60) — M-Core Client
 *   - remoteId = DIAG_MGMT_PROVIDER_ID (52) — A-Core Server
 *   - Channel: 1 (HP)
 *   - Client_linkReq_PeriodMs: 10ms (active link request)
 *   - No callbacks (polling mode)
 *
 * As CLIENT, M-Core actively sends link-connect requests to A-Core Server.
 * A-Core (Server, ProviderID=52) passively listens.
 *
 * Must be called after PICC_PreOS_Init() and before the scheduler starts.
 */
void DiagMgmt_Init(void);

/**
 * @brief Diagnostic Management 10ms periodic main function
 *
 * Called from TASK_M0_10MS() every 10ms.
 * Internally uses a counter to achieve 30ms send period.
 *
 * Behavior:
 *   - If Test_input == 0 (kNoObd): no Method Request is sent.
 *   - If Test_input == 1 (kObdCan) or 2 (kObdEth): sends a Method Request
 *     (REQUEST_NO_RETURN_WITHOUT_ACK, 0x07, Fire-and-Forget) with Test_input
 *     as the 1-byte payload every 30ms.
 *
 * M-Core uses PICC_METHOD_NO_RETURN_WITHOUT_ACK because:
 *   1. M-Core cannot synchronously wait for Response (real-time constraints).
 *   2. This is a status report — no business response needed from A-Core.
 */
void DiagMgmt_Main(void);

#if defined(__cplusplus)
}
#endif

#endif /* DIAG_MGMT_H */
