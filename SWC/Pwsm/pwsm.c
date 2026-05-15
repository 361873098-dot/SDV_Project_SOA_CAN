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
*  File name:           $Source: pwsm.c $
*  Revision:            $Revision: 1.0 $
*  Author:              $Author: Li Song (uic59152)  $
*  Module acronym:      PWSM
*  Specification:
*  Date:                $Date: 2025/12/18  $
*
*  Description:     This Unit processes the power supply module
*
*********************************************************************************
*
*  Changes:
*
*
*********************************************************************************/

/***********************************************************************************************************************
*  include files
***********************************************************************************************************************/
#include "Platform.h"
#include "pwsm_cnf.h"
#include "pwsm.h"
#include "Dio.h"
#include "Mcu.h"

/* The ONLY PICC header needed by the application layer */
#include "picc_api.h"


/***********************************************************************************************************************
*  local variable definitions (module local variables)
***********************************************************************************************************************/

static volatile Pwsm_StateType Pwsm_State = PWSM_STATE_NO_INIT; /**< Holds the current PWSM state. */
static volatile Pwsm_MsgStateType Pwsm_MsgState = PWSM_MSG_STATE_IDLE;	/**< Holds the current PWSM MSG state. */
static uint8 Pwsm_RstReq = FALSE;
static uint16 Pwsm_ShutdownTimer = 0U;
static uint16 Pwsm_RxMsgTimeOutId2 = 0U;
static uint16 Pwsm_RxMsgTimeOutId8 = 0U;
static uint16 Pwsm_RxMsgTimeOutId11 = 0U;

/***********************************************************************************************************************
*  global function definitions
***********************************************************************************************************************/
uint8 Igk_Status = 1;
uint8 McuRst = 0;

uint8 buf[8];
uint16 len;
/***********************************************************************************************************************
 *  Function name    : Pwsm_Init()
 *
 *  Description      : Initialize PWSM module and register with PICC driver
 *
 ***********************************************************************************************************************/
void Pwsm_Init(void)
{
    /* Register PWSM application (M-Core=Server, ProviderID=0x01)
     * A-Core Client (ConsumerID=0x06) actively sends link-connect requests.
     * M-Core Server passively waits for connection.
     * Uses polling mode — no ISR callback handlers needed. */
    static const PICC_AppConfig_t cfg = {
        .localId           = PWR_PROVIDER_ID,     /* 0x01 — M-Core Server */
        .remoteId          = PWR_CONSUMER_ID,     /* 0x06 — A-Core Client */
        .role              = PICC_ROLE_SERVER,
        .channelId         = PWR_CHANNEL_ID,      /* 2 (LP) */
        .methodHandler     = NULL,  /* Pure polling mode, no callback needed */
        .eventHandler      = NULL   /* Pure polling mode, no callback needed */
    };

    /* Register DM application (M-Core=Server, ProviderID=0x02)
     * A-Core Client (ConsumerID=0x07) actively sends link-connect requests.
     * Shares the same IPCF channel as PWSM. */
    static const PICC_AppConfig_t dm_cfg = {
        .localId           = DM_PROVIDER_ID,     /* 0x02 — M-Core Server */
        .remoteId          = DM_CONSUMER_ID,     /* 0x07 — A-Core Client */
        .role              = PICC_ROLE_SERVER,
        .channelId         = PWR_CHANNEL_ID,      /* 2 (LP) */
        .methodHandler     = NULL,  /* Pure polling mode, no callback needed */
        .eventHandler      = NULL   /* Pure polling mode, no callback needed */
    };

    (void)PICC_Init(&cfg);
    (void)PICC_Init(&dm_cfg);
}

/***********************************************************************************************************************
 *  Function name    : Pwsm_CommEvent()
 *
 *  Description      : Handles the power management communication state machine
 *
 ***********************************************************************************************************************/
/**
 * @brief Power management communication state machine
 *
 * Implements the shutdown handshake sequence between M-Core and A-Core:
 *
 *   IDLE -> TX_ID1 -> RX_ID2 -> RX_ID8 -> TX_ID4 -> RX_ID11 -> SHUTDOWN_DONE
 *     |       |         |        |         |          |
 *     |       |         |        |         |          +-- Wait for A-Core ctrl ACK
 *     |       |         |        |         +-- Send ctrl cmd (Event ID=4)
 *     |       |         |        +-- Wait for A-Core event done
 *     |       |         +-- Wait for A-Core state ACK
 *     |       +-- Send standby state notify (Event ID=1)
 *     +-- IGK off or A-Core reset request triggers sequence
 *
 * Trigger conditions:
 *   1. IGK (Ignition) status = OFF: normal shutdown sequence
 *   2. A-Core sends DM reset request (MethodID=PWR_METHOD_RST): reset sequence
 *
 * Protocol flow (per IPCF specification):
 *   M-Core(Server,0x01) --Event(0x09)--> A-Core(Client,0x06)
 *   M-Core(Server,0x01) <--Method(0x05)-- A-Core(Client,0x06)
 */
void Pwsm_CommEvent(void)
{
    switch (Pwsm_MsgState)
    {
    case PWSM_MSG_STATE_IDLE:
    {
        /* Check trigger conditions for shutdown sequence */
    	Igk_Status = Pwsm_GetIgkStatus();
        if(!Igk_Status)
        {
            /* Trigger 1: IGK OFF — enter shutdown notification sequence */
        	Pwsm_MsgState = PWSM_STATE_MSG_TX_ID1;
        }
        else if (PICC_GetMethodData(PICC_ID_DM_LOCAL, PWR_METHOD_RST, buf, sizeof(buf), &len, NULL, NULL, NULL) == PICC_E_OK)
    	{
    		if (len == 2U && buf[0] == PWR_CORE_A &&  buf[1] == PWR_STATE_RESET)
    		{
                /* Trigger 2: A-Core requests M-Core reset via DM channel */
    			Pwsm_MsgState = PWSM_STATE_MSG_TX_ID1;
    			Pwsm_RstReq = TRUE;
    		}
    	}
        break;
    }

    case PWSM_STATE_MSG_TX_ID1:
    {
        /* Step 1: Send Event ID=1 (standby state notification) to A-Core
         * Protocol: ProviderID=0x01, EventID=1, ConsumerID=0x06, MessageType=0x09
         * Payload: 1 byte = PWR_STATE_STANDBY */
        uint8 payload[1] = { (uint8)PWR_STATE_STANDBY };
        (void)PICC_SendEvent(PICC_ID_PWR_LOCAL, PWR_EVENT_STATE_NOTIFY, payload, 1U, PICC_EVENT_WITH_ACK);
        Pwsm_RxMsgTimeOutId2 = 0U;
        Pwsm_RxMsgTimeOutId8 = 0U;
        Pwsm_MsgState = PWSM_STATE_MSG_RX_ID2;
        break;
    }

    case PWSM_STATE_MSG_RX_ID2:
        Pwsm_RxMsgTimeOutId2++;

        /* Step 2: Wait for A-Core state ACK (MethodID=2) */
        if (PICC_GetMethodData(PICC_ID_PWR_LOCAL, PWR_METHOD_STATE_ACK, buf, sizeof(buf), &len, NULL, NULL, NULL) == PICC_E_OK)
        {
            if (len >= 2U && buf[0] == PWR_CORE_A) {
                /* A-Core acknowledged — proceed to wait for event done */
                Pwsm_MsgState = PWSM_STATE_MSG_RX_ID8;
            }
        }
        else if(Pwsm_RxMsgTimeOutId2 >= PWSM_MSG_TIMEOUT_RX_ID2)
        {
            /* Timeout — A-Core did not respond, skip to shutdown done */
            Pwsm_MsgState = PWSM_STATE_MSG_SHUTDOWN_DONE;
        }
        break;

    case PWSM_STATE_MSG_RX_ID8:
        Pwsm_RxMsgTimeOutId8++;

        /* Step 3: Wait for A-Core event done notification (MethodID=8) */
        if (PICC_GetMethodData(PICC_ID_PWR_LOCAL, PWR_METHOD_EVENT_DONE, buf, sizeof(buf), &len, NULL, NULL, NULL) == PICC_E_OK)
        {
            if (len >= 1U && buf[0] == (uint8)PWR_DONE_FIRST_STEP) {
                /* A-Core completed first step — send hardware shutdown command */
                Pwsm_MsgState = PWSM_STATE_MSG_TX_ID4;
            }
        }
        else if(Pwsm_RxMsgTimeOutId8 >= PWSM_MSG_TIMEOUT_RX_ID8)
        {
            /* Timeout — A-Core did not complete, skip to shutdown done */
            Pwsm_MsgState = PWSM_STATE_MSG_SHUTDOWN_DONE;
        }
        break;

    case PWSM_STATE_MSG_TX_ID4:
    {
        /* Step 4: Send hardware shutdown command (Event ID=4) to A-Core
         * Protocol: ProviderID=0x01, EventID=4, ConsumerID=0x06, MessageType=0x09
         * Payload: 2 bytes = [PWR_CORE_A, PWR_CMD_HARDWARE_SHUTDOWN] */
        uint8 payload[2] = { PWR_CORE_A, (uint8)PWR_CMD_HARDWARE_SHUTDOWN };
        (void)PICC_SendEvent(PICC_ID_PWR_LOCAL, PWR_EVENT_CTRL_CMD,
                             payload, 2U, PICC_EVENT_WITH_ACK);
        Pwsm_RxMsgTimeOutId11 = 0U;
        Pwsm_MsgState = PWSM_STATE_MSG_RX_ID11;
        break;
    }

    case PWSM_STATE_MSG_RX_ID11:
        Pwsm_RxMsgTimeOutId11++;

        /* Step 5: Wait for A-Core ctrl ACK (MethodID=11) */
        if (PICC_GetMethodData(PICC_ID_PWR_LOCAL, PWR_METHOD_CTRL_ACK, buf, sizeof(buf), &len, NULL, NULL, NULL) == PICC_E_OK)
        {
            if (len >= 2U && buf[0] == PWR_CORE_A) {
                /* A-Core acknowledged shutdown — sequence complete */
                Pwsm_MsgState = PWSM_STATE_MSG_SHUTDOWN_DONE;
            }
        }
        else if(Pwsm_RxMsgTimeOutId11 >= PWSM_MSG_TIMEOUT_RX_ID11)
        {
            /* Timeout — force shutdown done */
            Pwsm_MsgState = PWSM_STATE_MSG_SHUTDOWN_DONE;
        }
        break;

    default:
        break;
    }
}

/***********************************************************************************************************************
 *  Function name    : Pwsm_Main()
 *
 *  Description      : Implements all activities of the PWSM State Manager.
 *
 ***********************************************************************************************************************/
void Pwsm_Main(void)
{
    switch (Pwsm_State)
    {
        case PWSM_STATE_NO_INIT:
            /* Initial state — transition to startup initialization */
            Pwsm_State = PWSM_STATE_STARTUP_INIT;
        break;

        case PWSM_STATE_STARTUP_INIT:
            /* Keep wake up signal asserted during startup */
            Pwsm_WriteWakeup(STD_HIGH);
            /* Release Ethernet switch from reset */
            Pwsm_EthSwitchRst(STD_HIGH);
            /* Transition to normal running state */
            Pwsm_State = PWSM_STATE_RUN;
        break;

        case PWSM_STATE_RUN:
            /* Run the shutdown communication state machine
             * Checks IGK status and processes handshake with A-Core */
            Pwsm_CommEvent();
            if((Pwsm_MsgState == PWSM_STATE_MSG_SHUTDOWN_DONE) || (Pwsm_MsgState == PWSM_MSG_STATE_NO_RESPONSE))
            {
                Pwsm_State = PWSM_STATE_POST_RUN;
            }
#if 0
            if(!Igk_Status)
            {
                /*Send Event/Method 0x01 to A core to prepare shutdown*/
                Pwsm_CommEvent();
                if((Pwsm_MsgState == PWSM_STATE_MSG_SHUTDOWN_DONE) || (Pwsm_MsgState == PWSM_MSG_STATE_NO_RESPONSE))
                {
                    Pwsm_State = PWSM_STATE_POST_RUN;
                }
            }
#endif
        break;

        case PWSM_STATE_POST_RUN:
            /* Shutdown handshake complete — reset timer and proceed */
            Pwsm_ShutdownTimer = 0U;
            Pwsm_State = PWSM_STATE_SHUTDOWN;
        break;

        case PWSM_STATE_SHUTDOWN:
            /* Wait for shutdown delay to expire */
            if (Pwsm_ShutdownTimer < PWSM_SHUTDOWN_DELAY_TICKS)
            {
            	Pwsm_ShutdownTimer++;
            }
            else if(Pwsm_RstReq)
            {
            	/* Reset requested by A-Core — perform MCU reset */
            	Mcu_PerformReset();
            }
            else
            {
            	/* Normal shutdown — release wake up signal to power off */
            	Pwsm_WriteWakeup(STD_LOW);
            }
        break;

        default:
            break;
    }
}

/***********************************************************************************************************************
 *  Function name    : Pwsm_GetState()
 *
 *  Description      : This API returns the current state of the PWSM.
 *
 ***********************************************************************************************************************/
Std_ReturnType Pwsm_GetState(Pwsm_StateType *state)
{
    Std_ReturnType ReturnValue = E_OK;
    if (state != NULL_PTR)
    {
        *state = Pwsm_State;
    }
    else
    {
        ReturnValue = E_NOT_OK;
    }
    return ReturnValue;
}

/***********************************************************************************************************************
 *  Function name    : Pwsm_ResetMsgState()
 *
 *  Description      : Reset message state machine to initial state. Call this when restarting shutdown sequence.
 *
 ***********************************************************************************************************************/
void Pwsm_ResetMsgState(void)
{
    Pwsm_MsgState = PWSM_STATE_MSG_TX_ID1;
    Pwsm_RxMsgTimeOutId2 = 0U;
    Pwsm_RxMsgTimeOutId8 = 0U;
    Pwsm_RxMsgTimeOutId11 = 0U;
}
