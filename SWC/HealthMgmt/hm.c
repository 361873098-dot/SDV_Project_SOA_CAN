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
*  File name:           $Source: hm.c $
*  Revision:            $Revision: 1.0 $
*  Author:              $Author: Li Song (uic59152)  $
*  Module acronym:      PWSM
*  Specification:
*  Date:                $Date: 2026/4/18  $
*
*  Description:     This Unit processes health management module
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
#include "picc_api.h"
#include "hm.h"
#include "hm_cnf.h"


/***********************************************************************************************************************
*  local variable definitions (module local variables)
***********************************************************************************************************************/

static volatile Hm_HeartBeat_MsgStateType Hm_HeartBeat_MsgState = HM_HEARTBEAT_STATE_STARUP;	/**< Holds the current PWSM MSG state. */
static uint16 Hm_HeartBeat_RxMsgTimeOut = 0U;

uint8 hm_heartbeat_rx_buf[8];
uint8 hm_reporter_rx_buf[8];
uint16 hm_heartbeat_rx_len;
uint16 hm_reporter_rx_len;
RealTimeInfo_st Mcore_RealTimeInfo_Data;
/***********************************************************************************************************************
 *  Function name    : Hm_Init()
 *
 *  Description      : Initialize HM module and register with PICC driver
 *
 *  List of arguments: none
 *
 *  Return value     : noe
 *
 ***********************************************************************************************************************/
void Hm_Init(void)
{
    static const PICC_AppConfig_t heal_man_hb_cfg = {
        .localId                = HEAL_MAN_HB_PROVIDER_ID,   /* M-Core Server */
        .remoteId               = HEAL_MAN_HB_CONSUMER_ID,   /* A-Core Client */
        .role                   = PICC_ROLE_SERVER,
        .channelId              = HM_CHANNEL_ID,    /* 1 (HP) */
        .Client_linkReq_PeriodMs = 0U, /* Ignored for SERVER role */
        .methodHandler          = NULL,  /* No Method handler needed */
        .eventHandler           = NULL   /* No Event handler needed */
    };

    static const PICC_AppConfig_t heal_man_reporter_cfg = {
        .localId                = HEAL_MAN_REPORTER_PROVIDER_ID,   /* M-Core Server */
        .remoteId               = HEAL_MAN_REPORTER_CONSUMER_ID,   /* A-Core Client */
        .role                   = PICC_ROLE_SERVER,
        .channelId              = HM_CHANNEL_ID,    /* 1 (HP) */
        .Client_linkReq_PeriodMs = 0U, /* Ignored for SERVER role */
        .methodHandler          = NULL,  /* No Method handler needed */
        .eventHandler           = NULL   /* No Event handler needed */
    };

    (void)PICC_Init(PICC_APP_HEALTH, &heal_man_hb_cfg);
    (void)PICC_Init(PICC_APP_RSV1, &heal_man_reporter_cfg);

}



/***********************************************************************************************************************
 *  Function name    : Hm_HeartBeat_Task()
 *
 *  Description      : Implements heart beat activities .
 *
 *  List of arguments: none
 *
 *  Return value     : noe
 *
 ***********************************************************************************************************************/
void Hm_HeartBeat_Task(void)
{

    switch (Hm_HeartBeat_MsgState)
    {
    case HM_HEARTBEAT_STATE_STARUP:
    	if (PICC_GetMethodData(PICC_APP_HEALTH, HM_HB_METHOD_ID, hm_heartbeat_rx_buf, sizeof(hm_heartbeat_rx_buf), &hm_heartbeat_rx_len, NULL, NULL, NULL) == PICC_E_OK)
    	{
    		if (hm_heartbeat_rx_len == 4U && hm_heartbeat_rx_buf[0] == A_CORE_PARTITION_ID)
    		{
    			Hm_HeartBeat_MsgState = HM_HEARTBEAT_STATE_RX_POOLING;
    			Hm_HeartBeat_RxMsgTimeOut = 0;
    		}
    	}
    	else if(Hm_HeartBeat_RxMsgTimeOut++ > HM_HEARTBEAT_STARTUP_TIMEOUT)
    	{
    		Hm_HeartBeat_MsgState = HM_HEARTBEAT_STATE_TIMEOUT;
    	}
    	break;

    case HM_HEARTBEAT_STATE_RX_POOLING:
    	if (PICC_GetMethodData(PICC_APP_HEALTH, HM_HB_METHOD_ID, hm_heartbeat_rx_buf, sizeof(hm_heartbeat_rx_buf), &hm_heartbeat_rx_len, NULL, NULL, NULL) == PICC_E_OK)
    	{
    		if (hm_heartbeat_rx_len == 4U && hm_heartbeat_rx_buf[0] == A_CORE_PARTITION_ID)
    		{
    			Hm_HeartBeat_MsgState = HM_HEARTBEAT_STATE_RX_POOLING;
    			Hm_HeartBeat_RxMsgTimeOut = 0;
    		}
    	}
    	else if(Hm_HeartBeat_RxMsgTimeOut++ > HM_HEARTBEAT_POOLING_TIMEOUT)
    	{
    		Hm_HeartBeat_MsgState = HM_HEARTBEAT_STATE_TIMEOUT;
    	}

    	break;

    case HM_HEARTBEAT_STATE_TIMEOUT:

    	break;

    default:
    	break;

    }

}

/***********************************************************************************************************************
 *  Function name    : Hm_ReportRealTimeInfo_Task()
 *
 *  Description      : Report M core state.
 *
 *  List of arguments: none
 *
 *  Return value     : none
 *
 ***********************************************************************************************************************/
void Hm_ReportRealTimeInfo_Task(void)
{
	uint8 sessionId = 0U;

	if (PICC_GetMethodData(PICC_APP_RSV1, HM_REPORTER_METHOD_ID, hm_reporter_rx_buf, sizeof(hm_reporter_rx_buf), &hm_reporter_rx_len, &sessionId, NULL, NULL) == PICC_E_OK)
	{
		if (hm_reporter_rx_len == 4U && hm_reporter_rx_buf[0] == M_CORE_PARTITION_ID)
		{
			Mcore_RealTimeInfo_Data.PartitionId = M_CORE_PARTITION_ID;
			Mcore_RealTimeInfo_Data.Reserved[0] = 0U;
			Mcore_RealTimeInfo_Data.Reserved[1] = 0U;
			Mcore_RealTimeInfo_Data.Reserved[2] = 0U;
			Mcore_RealTimeInfo_Data.RealTimeData.PartitionID = M_CORE_PARTITION_ID;
			Mcore_RealTimeInfo_Data.RealTimeData.NrOfPartitions = 2;
			Mcore_RealTimeInfo_Data.RealTimeData.PartitionState = 0x01;
			Mcore_RealTimeInfo_Data.RealTimeData.Reserved[0] = 0;
			Mcore_RealTimeInfo_Data.RealTimeData.Reserved[1] = 0;
			Mcore_RealTimeInfo_Data.RealTimeData.Reserved[2] = 0;
			Mcore_RealTimeInfo_Data.RealTimeData.Reserved[3] = 0;
			Mcore_RealTimeInfo_Data.RealTimeData.NrOfRealTimeData = 0;

//	        (void)PICC_SendEvent(PICC_APP_RSV1, HM_REPORTER_METHOD_ID, &Mcore_RealTimeInfo_Data, sizeof(Mcore_RealTimeInfo_Data), PICC_METHOD_WITH_RESPONSE);
			(void)PICC_MethodResponse(PICC_APP_RSV1, HM_REPORTER_METHOD_ID, sessionId, 0x00, &Mcore_RealTimeInfo_Data, sizeof(Mcore_RealTimeInfo_Data));
		}
	}
}

/***********************************************************************************************************************
 *  Function name    : Hm_ReportFault_Task()
 *
 *  Description      : Report M core fault event to A core by IPCF
 *
 *  List of arguments: none
 *
 *  Return value     : none
 *
 ***********************************************************************************************************************/
void Hm_ReportFault_Task(void)
{


}

/***********************************************************************************************************************
 *  Function name    : Hm_Main()
 *
 *  Description      : Implements all activities of the health State Manager.
 *
 *  List of arguments: none
 *
 *  Return value     : noe
 *
 ***********************************************************************************************************************/
void Hm_Main(void)
{
	Hm_HeartBeat_Task();
	Hm_ReportRealTimeInfo_Task();
	Hm_ReportFault_Task();
}
