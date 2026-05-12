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
*  File name:           $Source: hm_cnf.c $
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
#include "hm.h"
#include "hm_cnf.h"

/* */

HealthHazard_st Hm_FaultEventIndex[HM_REPORT_FAULT_MAX_NUMBER] =
{
    {
        .ID = "KL30_UV",
        .Value = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
        .Category = HW_FAULT_CATEGORY_POWER,
        .PartitionID = M_CORE_PARTITION_ID,
        .DetectedBy = HM_DETECTED_BY_POWER_MONITOR,
        .Frequency = 0,
        .Severity = HM_SEVERITY_WARNING,
        .Reserved = {0xFF, 0xFF, 0xFF}
    },
    {
        .ID = "CAN_BUS_OFF",
        .Value = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
        .Category = HW_FAULT_CATEGORY_COMMUNICATION,
        .PartitionID = M_CORE_PARTITION_ID,
        .DetectedBy = HM_DETECTED_BY_CAN_MONITOR,
        .Frequency = 0,
        .Severity = HM_SEVERITY_ERROR,
        .Reserved = {0xFF, 0xFF, 0xFF}
    }

    };

uint32 Hm_FaultEventMaskIndex[HM_REPORT_FAULT_MAX_NUMBER] = 
{
    HM_FAULT_MASK_KL30_UV,
    HM_FAULT_MASK_CAN_BUS_OFF
};
