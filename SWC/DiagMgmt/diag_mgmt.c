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
*  Revision:            $Revision: 3.0 $
*  Author:              $Author: Auto Generated $
*  Module acronym:      DIAG_MGMT
*  Specification:       DIAG_4 DoIP Activation Line Interface
*  Date:                $Date: 2026/04/14 $
*
*  Description:     Diagnostic Management module implementation
*                   Implements DIAG_4: M-Core (Server, ProviderID=52) sends
*                   DoIP activation line status to A-Core (Client, ConsumerID=60)
*                   via IPCF Event notification (Server->Client).
*
*                   Roles (per updated design document):
*                     M-Core = Server (ProviderID=52)
*                     A-Core = Client (ConsumerID=60)
*
*                   Send mechanism: PICC_SendEvent (Server->Client Event)
*                     EventID=5, payload=Test_input value, 30ms period
*                     PICC_EVENT_WITHOUT_ACK (Fire-and-Forget)
*                     M-Core cannot synchronously wait (real-time constraints)
*
*********************************************************************************
*
*  Changes:
*    v2.0: Corrected M-Core role from SERVER to CLIENT per Schaeffler doc.
*          Changed send from PICC_SendEvent to PICC_MethodRequest.
*    v3.0: Reversed roles per updated requirement:
*          M-Core is now SERVER (ProviderID=52), A-Core is CLIENT (ConsumerID=60).
*          M-Core sends Event (ID=5) to A-Core Client (Server->Client notification).
*          Replaced PICC_MethodRequest with PICC_SendEvent.
*          Server passively waits for Client connection (no link request).
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
 *  Configuration (per updated design document):
 *    - localId  = DIAG_MGMT_PROVIDER_ID (52, 0x34) — M-Core is Server
 *    - remoteId = DIAG_MGMT_CONSUMER_ID (60, 0x3C) — A-Core is Client
 *    - role     = PICC_ROLE_SERVER
 *    - channel  = 1 (HP)
 *    - Client_linkReq_PeriodMs = 0 (Ignored for SERVER role)
 *    - No callbacks (polling mode)
 *
 *  As SERVER, M-Core passively waits for A-Core Client to connect.
 *  A-Core Client actively sends link-connect requests.
 *
 *  Link request from A-Core Client:
 *    34 00 3C 00 00 01 00 05 01 xx xx xx xx
 *  M-Core Server link-connect reply:
 *    34 00 3C 00 00 00 00 05 01 xx xx xx xx
 *
 ***********************************************************************************************************************/
void DiagMgmt_Init(void)
{
    static const PICC_AppConfig_t diagMgmt_cfg = {
        .localId                = DIAG_MGMT_PROVIDER_ID,   /* 52 (0x34) - M-Core Server */
        .remoteId               = DIAG_MGMT_CONSUMER_ID,   /* 60 (0x3C) - A-Core Client */
        .role                   = PICC_ROLE_SERVER,
        .channelId              = DIAG_MGMT_CHANNEL_ID,    /* 1 (HP) */
        .Client_linkReq_PeriodMs = 0U, /* Ignored for SERVER role */
        .methodHandler          = NULL,  /* No Method handler needed */
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
 *       - If Test_input == 0: no Event is sent (kNoObd).
 *       - If Test_input == 1: send Event with payload = 1 (kObdCan, high priority).
 *       - If Test_input == 2: send Event with payload = 2 (kObdEth).
 *    3. Uses PICC_SendEvent with PICC_EVENT_WITHOUT_ACK.
 *       Per IPCF protocol: M-Core is Server, uses Fire-and-Forget because
 *       M-Core cannot synchronously wait for ACK (real-time constraints).
 *       Even if configured as WITH_ACK, M-Core ignores the EVENT_ACK.
 *
 *  Protocol frame constructed by PICC driver:
 *    ProviderID = 52 (0x34) — M-Core Server (localId)
 *    MethodID   = 5         — DoIP activation line Event ID
 *    ConsumerID = 60 (0x3C) — A-Core Client (remoteId)
 *    SessionID  = 0x00      — Fire-and-Forget, no session tracking needed
 *    MessageType= 0x09      — NOTIFICATION_WITHOUT_ACK
 *    ReturnCode = 0x00
 *    Length     = 0x0001    — 1 byte payload
 *    Payload    = currentInput (1=kObdCan, 2=kObdEth)
 *
 *  Example frame when Test_input=1:
 *    34 05 3C 00 09 00 00 01 01
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
             * Test_input is 1 (kObdCan) or 2 (kObdEth) — send Event.
             *
             * M-Core (Server) -> A-Core (Client) Event notification:
             *   ProviderID = 52 (0x34) — M-Core Server
             *   EventID    = 5         — DoIP activation line
             *   ConsumerID = 60 (0x3C) — A-Core Client
             *   MessageType= 0x09      — NOTIFICATION_WITHOUT_ACK
             *   Payload    = currentInput (1=kObdCan, 2=kObdEth)
             *
             * Fire-and-Forget: no ACK expected, session ID not used.
             */
            uint8 payload[1];
            payload[0] = currentInput;

            (void)PICC_SendEvent(PICC_APP_RSV0,
                                 DIAG_MGMT_EVENT_DOIP_ACT_LINE,
                                 payload,
                                 1U,
                                 PICC_EVENT_WITHOUT_ACK);
        }
        /* else: Test_input == 0 (kNoObd), no Event sent */
    }
}
