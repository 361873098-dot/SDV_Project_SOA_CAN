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
#include "FlexCAN_Ip_main.h"
#include "TJA1145A_Spi_Baremetal.h"

#include <string.h>
#include "Pmic_driver_main.h"
#include "CDD_Pmic.h"
#include "CDD_I2c.h"
#include "FlexCAN_Ip_HwAccess.h"
/***********************************************************************************************************************
*  local variable definitions (module local variables)
***********************************************************************************************************************/

static volatile Hm_HeartBeat_MsgStateType Hm_HeartBeat_MsgState = HM_HEARTBEAT_STATE_STARUP;	/**< Holds the current PWSM MSG state. */
static uint16 Hm_HeartBeat_RxMsgTimeOut = 0U;
static uint16 Hm_ReportFault_CycleCnt = 0U;

#define HM_MAIN_PERIOD_MS            (10U)
#define HM_REPORT_FAULT_PERIOD_MS    (1000U)
#define HM_REPORT_FAULT_CYCLE_TICKS  (HM_REPORT_FAULT_PERIOD_MS / HM_MAIN_PERIOD_MS)

uint8 hm_heartbeat_rx_buf[8];
uint8 hm_reporter_rx_buf[8];
uint16 hm_heartbeat_rx_len;
uint16 hm_reporter_rx_len;
RealTimeInfo_st Mcore_RealTimeInfo_Data;
static uint32 Hm_LastFaultBitmap = 0U;
boolean IsKl30Ov = 0;
VR5510_MFlg1RegType M_Flag1RegData;
VR5510_MFlg2RegType M_Flag2RegData;
VR5510_MFlg3RegType M_Flag3RegData;


/***********************************************************************************************************************
 *  Function name    : Hm_IsKl30UnderVoltage()
 *
 *  Description      : Check if KL30 voltage is under the threshold
 *
 *  List of arguments: none
 *                     
 *
 *  Return value     : noe
 *
 ***********************************************************************************************************************/
static boolean Hm_IsKl30UnderVoltage(void)
{
	return M_Flag2RegData.B.VSUPUV7;
}

/***********************************************************************************************************************
 *  Function name    : Hm_IsCan0BusOff()
 *
 *  Description      : Check if CAN0 bus is off
 *
 *  List of arguments: none
 *                     
 *
 *  Return value     : noe
 *
 ***********************************************************************************************************************/
static boolean Hm_IsCan0BusOff(void)
{
	 uint32 errorStatus = FlexCAN_Ip_GetErrorStatus(0U);
	 return ((errorStatus & FLEXCAN_IP_ESR1_FLTCONF_BUS_OFF) != 0U) ? TRUE : FALSE;
}

/***********************************************************************************************************************
 *  Function name    : Hm_BuildFaultEvent()
 *
 *  Description      : Build fault event based on the fault bitmap
 *
 *  List of arguments: healthInfo - Pointer to the health info structure
 *                     faultBitmap - Bitmap representing the faults
 *
 *  Return value     : noe
 *
 ***********************************************************************************************************************/
static void Hm_BuildFaultEvent(PICCHealthInfo_st *healthInfo, uint32 faultBitmap)
{
	uint8 hazardIndex = 0U;
	uint8 mapIndex;

	(void)memset(healthInfo, 0, sizeof(*healthInfo));

	healthInfo->PartitionId = M_CORE_PARTITION_ID;
	healthInfo->Reserved[0] = 0xFFU;
	healthInfo->Reserved[1] = 0xFFU;
	healthInfo->Reserved[2] = 0xFFU;
	healthInfo->HealthInfo.Timestamp = 0U; /* Timestamp can be filled with actual time if available */
	healthInfo->HealthInfo.Indicator.Performance = 0U; /* Performance can be calculated based on actual metrics if available */
	healthInfo->HealthInfo.Indicator.Reliability = 0U; /* Reliability can be calculated based on actual metrics if available */	
	healthInfo->HealthInfo.Indicator.NrOfPartitions = 2U;
	healthInfo->HealthInfo.Indicator.PartitionState = 0x01U;
	healthInfo->HealthInfo.Indicator.HealthIndex = 0U; /* HealthIndex can be calculated based on actual metrics if available */
	healthInfo->HealthInfo.Indicator.Reserved[0] = 0xFFU;
	healthInfo->HealthInfo.Indicator.Reserved[1] = 0xFFU;
	healthInfo->HealthInfo.Indicator.Reserved[2] = 0xFFU;
	healthInfo->HealthInfo.PartitionID = M_CORE_PARTITION_ID;
	healthInfo->HealthInfo.CategoryMaxNrOfHazard[0] = 10U;
	healthInfo->HealthInfo.CategoryMaxNrOfHazard[1] = 0U;
	healthInfo->HealthInfo.CategoryMaxNrOfHazard[2] = 0U;
	healthInfo->HealthInfo.Reserved[0] = 0xFFU;
	healthInfo->HealthInfo.Reserved[1] = 0xFFU;
	healthInfo->HealthInfo.Reserved[2] = 0xFFU;
	healthInfo->HealthInfo.Reserved[3] = 0xFFU;
	
	for (mapIndex = 0U; mapIndex < HM_REPORT_FAULT_MAX_NUMBER; mapIndex++)
	{
		if ((faultBitmap & Hm_FaultEventMaskIndex[mapIndex]) != 0U)
		{
			healthInfo->HealthInfo.Hazards[hazardIndex] = Hm_FaultEventIndex[mapIndex];
			hazardIndex++;
		}
		else
		{
			/* Nothing to do */
		}
	}

	healthInfo->HealthInfo.NrOfHazards = hazardIndex;
}
/*****************	******************************************************************************************************
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
			else
			{
				/* Nothing to do */
			}

    	}
    	else if(Hm_HeartBeat_RxMsgTimeOut++ > HM_HEARTBEAT_STARTUP_TIMEOUT)
    	{
    		Hm_HeartBeat_MsgState = HM_HEARTBEAT_STATE_TIMEOUT;
    	}
		else
		{
			/* Nothing to do */
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
			else
			{
				/* Nothing to do */	
			}
    	}
    	else if(Hm_HeartBeat_RxMsgTimeOut++ > HM_HEARTBEAT_POOLING_TIMEOUT)
    	{
    		Hm_HeartBeat_MsgState = HM_HEARTBEAT_STATE_TIMEOUT;
    	}
		else
		{
				/* Nothing to do */	
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
			(void)PICC_MethodResponse(PICC_APP_RSV1, HM_REPORTER_METHOD_ID, sessionId, 0x00, &Mcore_RealTimeInfo_Data, sizeof(Mcore_RealTimeInfo_Data));
		}
		else
		{
			/* Nothing to do */
		}
	}
	else
	{
		/* Nothing to do */
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
	uint32 currentFaultBitmap = 0U;
	PICCHealthInfo_st healthInfo;

	currentFaultBitmap |= (Hm_IsKl30UnderVoltage() != FALSE) ? HM_FAULT_MASK_KL30_UV : 0U;
	currentFaultBitmap |= (Hm_IsCan0BusOff() != FALSE) ? HM_FAULT_MASK_CAN_BUS_OFF : 0U;

	if (currentFaultBitmap != Hm_LastFaultBitmap)
	{
		Hm_BuildFaultEvent(&healthInfo, currentFaultBitmap);
		(void)PICC_SendEvent(PICC_APP_RSV1, HM_FAULT_EVENT_ID, (const uint8 *)&healthInfo, (uint16)sizeof(healthInfo), PICC_EVENT_WITHOUT_ACK);
	
	}
	else{
		/* Nothing to do */
	}
	Hm_LastFaultBitmap = currentFaultBitmap;

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

	if (Hm_ReportFault_CycleCnt++ >= HM_REPORT_FAULT_CYCLE_TICKS)
	{
		Pmic_ReadRegister(PmicConf_PmicDevice_PmicDevice_0, PMIC_MAIN_UNIT, PMIC_VR55XX_M_FLAG1_ADDR8, &M_Flag1RegData);
		Pmic_ReadRegister(PmicConf_PmicDevice_PmicDevice_0, PMIC_MAIN_UNIT, PMIC_VR55XX_M_FLAG2_ADDR8, &M_Flag2RegData);
		Pmic_ReadRegister(PmicConf_PmicDevice_PmicDevice_0, PMIC_MAIN_UNIT, PMIC_VR55XX_M_FLAG3_ADDR8, &M_Flag3RegData);
		
		Hm_ReportFault_Task();

		Pmic_WriteRegister(PmicConf_PmicDevice_PmicDevice_0, PMIC_MAIN_UNIT, PMIC_VR55XX_M_FLAG1_ADDR8, 0xFFFFU);
		Pmic_WriteRegister(PmicConf_PmicDevice_PmicDevice_0, PMIC_MAIN_UNIT, PMIC_VR55XX_M_FLAG2_ADDR8, 0xFFFFU);
		Hm_ReportFault_CycleCnt = 0U;
	}
	else
	{
		/* Nothing to do */
	}
}
