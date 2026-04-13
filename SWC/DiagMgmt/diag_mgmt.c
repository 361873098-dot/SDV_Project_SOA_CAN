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
*  File name:           $Source: diag_mgmt.c $
*  Revision:            $Revision: 2.0 $
*  Author:              $Author: Auto Generated $
*  Module acronym:      DIAG_MGMT
*  Specification:       Schaeffler_A-Sample Diagnostic Management (DoIP Activation Line)
*  Date:                $Date: 2026/04/08 $
*
*  Description:     Diagnostic Management module implementation
*                   Implements DIAG_4: M-Core (Client, ConsumerID=60) sends
*                   DoIP activation line status to A-Core (Server, ProviderID=52)
*                   via IPCF Method Request (Fire-and-Forget).
*
*                   Roles (per Schaeffler_A-Sample design document):
*                     A-Core = Server (ProviderID=52, AsfDiagnosticInfo)
*                     M-Core = Client (ConsumerID=60, SWC)
*
*                   Send mechanism: PICC_MethodRequest (Client->Server request)
*                     MethodID=5, payload=Test_input value, 30ms period
*                     PICC_METHOD_NO_RETURN_WITHOUT_ACK (Fire-and-Forget)
*                     M-Core cannot synchronously wait (real-time constraints)
*
*********************************************************************************
*
*  Changes:
*    v2.0: Corrected M-Core role from SERVER to CLIENT per Schaeffler doc.
*          Changed send from PICC_SendEvent to PICC_MethodRequest.
*          Client actively sends link-connect requests.
*          localId=60(Consumer), remoteId=52(Provider).
*
*********************************************************************************/

/***********************************************************************************************************************
*  include files
***********************************************************************************************************************/
#include "Platform.h"
#include "diag_mgmt.h"

/* The ONLY PICC header needed by the application layer */
#include "picc_api.h"

/***********************************************************************************************************************
*  Global Variable Definitions
***********************************************************************************************************************/

/**
 * @brief Test input variable — controls DoIP activation line report
 *
 * 0 = no send, 1 = kObdCan (high priority), 2 = kObdEth
 */
volatile uint8 Test_input = 0U;

/***********************************************************************************************************************
*  Local Variable Definitions
***********************************************************************************************************************/

/** 10ms tick counter for 30ms period dispatch */
static uint16 DiagMgmt_PeriodCounter = 0U;

/***********************************************************************************************************************
*  Function Implementations
***********************************************************************************************************************/

/***********************************************************************************************************************
 *  Function name    : DiagMgmt_Init()
 *
 *  Description      : Initialize DiagMgmt module and register with PICC driver
 *
 *  Configuration (per Schaeffler_A-Sample design document):
 *    - localId  = DIAG_MGMT_CONSUMER_ID (60, 0x3C) — M-Core is Client
 *    - remoteId = DIAG_MGMT_PROVIDER_ID (52, 0x34) — A-Core is Server
 *    - role     = PICC_ROLE_CLIENT
 *    - channel  = 1 (HP)
 *    - Client_linkReq_PeriodMs = 10ms (CLIENT actively sends link requests)
 *    - No callbacks (polling mode)
 *
 *  As CLIENT, M-Core actively sends link-connect requests to A-Core Server.
 *  Link request report (before A-Core Server responds):
 *    34 00 3C 00 00 01 00 05 01 xx xx xx xx
 *  A-Core Server reply:
 *    34 00 3C 00 00 00 00 05 01 xx xx xx xx
 *
 ***********************************************************************************************************************/
void DiagMgmt_Init(void)
{
    static const PICC_AppConfig_t diagMgmt_cfg = {
        .localId                = DIAG_MGMT_CONSUMER_ID,   /* 60 (0x3C) - M-Core Client */
        .remoteId               = DIAG_MGMT_PROVIDER_ID,   /* 52 (0x34) - A-Core Server */
        .role                   = PICC_ROLE_CLIENT,
        .channelId              = DIAG_MGMT_CHANNEL_ID,    /* 1 (HP) */
        .Client_linkReq_PeriodMs = DIAG_MGMT_LINK_REQ_PERIOD_MS, /* 10ms link request period */
        .methodHandler          = NULL,  /* No Method handler needed (Client doesn't receive requests) */
        .eventHandler           = NULL   /* No Event handler needed */
    };

    (void)PICC_Init(PICC_APP_RSV0, &diagMgmt_cfg);

    /* Initialize state */
    DiagMgmt_PeriodCounter = 0U;
}

/***********************************************************************************************************************
 *  Function name    : DiagMgmt_Main()
 *
 *  Description      : Periodic 10ms main function for Diagnostic Management
 *
 *  Logic:
 *    1. Every 10ms call, increment PeriodCounter.
 *    2. Every 30ms (PeriodCounter % 3 == 0), check Test_input:
 *       - If Test_input == 0: no Method Request is sent (kNoObd).
 *       - If Test_input == 1: send Method Request with payload = 1 (kObdCan, high priority).
 *       - If Test_input == 2: send Method Request with payload = 2 (kObdEth).
 *    3. Uses PICC_MethodRequest with PICC_METHOD_NO_RETURN_WITHOUT_ACK.
 *       Per IPCF protocol: M-Core is Client, uses Fire-and-Forget because
 *       M-Core cannot synchronously wait for Response (real-time constraints).
 *       This is a status report — no business response is needed from A-Core.
 *
 *  Protocol frame constructed by PICC driver:
 *    ProviderID = 52 (0x34) — A-Core Server (remoteId)
 *    MethodID   = 5         — DoIP activation line
 *    ConsumerID = 60 (0x3C) — M-Core Client (localId)
 *    SessionID  = 0x00      — Fire-and-Forget, no session tracking needed
 *    MessageType= 0x07      — REQUEST_NO_RETURN_WITHOUT_ACK
 *    ReturnCode = 0x00
 *    Length     = 0x0001    — 1 byte payload
 *    Payload    = currentInput (1=kObdCan, 2=kObdEth)
 *
 *  Example frame when Test_input=1:
 *    34 05 3C 00 07 00 00 01 01
 *
 ***********************************************************************************************************************/
void DiagMgmt_Main(void)
{
    DiagMgmt_PeriodCounter++;

    /* Check every 30ms (3 x 10ms ticks) */
    if ((DiagMgmt_PeriodCounter % DIAG_MGMT_SEND_PERIOD_TICKS) == 0U)
    {
        /* Read current Test_input — take a snapshot */
        uint8 currentInput = Test_input;

        if (currentInput != DIAG_MGMT_NO_OBD)
        {
            /*
             * Test_input is 1 (kObdCan) or 2 (kObdEth) — send Method Request.
             *
             * M-Core (Client) -> A-Core (Server) Method Request:
             *   ProviderID = 52 (0x34) — A-Core Server
             *   MethodID   = 5         — DoIP activation line
             *   ConsumerID = 60 (0x3C) — M-Core Client
             *   MessageType= 0x07      — REQUEST_NO_RETURN_WITHOUT_ACK
             *   Payload    = currentInput (1=kObdCan, 2=kObdEth)
             *
             * Fire-and-Forget: no Response expected, session ID is ignored.
             */
            uint8 payload[1];
            payload[0] = currentInput;

            (void)PICC_MethodRequest(PICC_APP_RSV0,
                                     DIAG_MGMT_METHOD_DOIP_ACT_LINE,
                                     payload,
                                     1U,
                                     PICC_METHOD_NO_RETURN_WITHOUT_ACK);
        }
        /* else: Test_input == 0 (kNoObd), no Method Request sent */
    }
}
