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
*  File name:           $Source: hm_cnf.h $
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
#include "Platform.h"

/** IPCF data channel ID for diagnostic management (HP = High Priority = channel 1) */
#define HM_CHANNEL_ID            (2U)

/**
 * M-Core Server ProviderID
 * This is the LOCAL ID (M-Core is the Server/Provider).
 */
#define HEAL_MAN_HB_PROVIDER_ID           (21U)   /*heart beat */
#define HEAL_MAN_REPORTER_PROVIDER_ID     (81U)   /* Real time data */

/**
 * A-Core Client ConsumerID
 * This is the REMOTE peer's ID (A-Core is the Client/Consumer).
 */
#define HEAL_MAN_HB_CONSUMER_ID           (26U)   /*  */
#define HEAL_MAN_REPORTER_CONSUMER_ID  	  (91U)


#define HM_HB_METHOD_ID				(1U)	/*health management heart beat method ID*/
#define HM_REPORTER_METHOD_ID				(3U)
#define HM_FAULT_EVENT_ID                                (2U)

#define A_CORE_PARTITION_ID			(4U)
#define M_CORE_PARTITION_ID			(1U)

/*	Category	*/
#define HW_FAULT_CATEGORY_POWER                                (1U)
#define HW_FAULT_CATEGORY_COMMUNICATION                        (2U)	

/*	Severity	*/
#define HM_SEVERITY_WARNING                              (1U)
#define HM_SEVERITY_ERROR                                (2U)	

/*	Detected By	*/
#define HM_DETECTED_BY_POWER_MONITOR                           (1U)
#define HM_DETECTED_BY_CAN_MONITOR                             (2U)


#define HM_FAULT_MASK_KL30_OV                  (0x01U)
#define HM_FAULT_MASK_KL30_UV                 (0x02U)
#define HM_FAULT_MASK_CAN_BUS_OFF                        (0x04U)


#define HM_HEARTBEAT_STARTUP_TIMEOUT	2000
#define HM_HEARTBEAT_POOLING_TIMEOUT	2000

#define HM_REPORT_FAULT_MAX_NUMBER	2
typedef enum
{
	HM_HEARTBEAT_STATE_STARUP = ((uint8)0),
	HM_HEARTBEAT_STATE_RX_POOLING = ((uint8)0x11),
	HM_HEARTBEAT_STATE_TIMEOUT = ((uint8)0xFF),

} Hm_HeartBeat_MsgStateType;



extern HealthHazard_st Hm_FaultEventIndex[HM_REPORT_FAULT_MAX_NUMBER];
extern uint32 Hm_FaultEventMaskIndex[HM_REPORT_FAULT_MAX_NUMBER];
