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

#define A_CORE_PARTITION_ID			(4U)
#define M_CORE_PARTITION_ID			(1U)

#define HM_HEARTBEAT_STARTUP_TIMEOUT	2000
#define HM_HEARTBEAT_POOLING_TIMEOUT	2000

typedef enum
{
	HM_HEARTBEAT_STATE_STARUP = ((uint8)0),
	HM_HEARTBEAT_STATE_RX_POOLING = ((uint8)0x11),
	HM_HEARTBEAT_STATE_TIMEOUT = ((uint8)0xFF),

} Hm_HeartBeat_MsgStateType;


