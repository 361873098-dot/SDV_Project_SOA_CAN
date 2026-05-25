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
*  File name:           $Source: stm_cnf.c $
*  Revision:            $Revision: 1.0 $
*  Author:              $Author: Li Song (uic59152)  $
*  Module acronym:      STM
*  Specification:
*  Date:                $Date: 2026/05/25  $
*
*  Description:     Storage Middleware configuration instances
*
*********************************************************************************/

/***********************************************************************************************************************
*  include files
***********************************************************************************************************************/
#include "stm_cnf.h"

/***********************************************************************************************************************
*  Data Item Configuration Table
*
*  Each entry defines dataId and its maximum data length.
*  EEPROM storage layout: 1B valid + 1B len + data[maxDataLen]
*  Constraint: SUM(2 + maxDataLen) <= STM_EEPROM_DATA_SIZE (64)
*
*  Current total: (2+8) + (2+16) + (2+4) + (2+12) + (2+8) = 58 <= 64
***********************************************************************************************************************/
const Stm_DataItemCfg_t g_StmDataItemCfg[STM_MAX_DATA_ITEMS] = {
    { 0x0001U,  8U  },  /* Item 1: e.g. calibration data */
    { 0x0002U, 16U  },  /* Item 2: e.g. config block */
    { 0x0003U,  4U  },  /* Item 3: e.g. status flags */
    { 0x0004U, 12U  },  /* Item 4: e.g. sensor offsets */
    { 0x0005U,  8U  },  /* Item 5: e.g. runtime params */
};

/***********************************************************************************************************************
*  Retry Interval Table for 0x04 (M sync to A)
*
*  Stepped intervals: 100ms, 200ms, 400ms, 800ms (in 10ms ticks)
***********************************************************************************************************************/
const uint16 g_StmRetryIntervals[STM_RETRY_INTERVAL_COUNT] = {
    10U,   /* 100ms = 10 ticks */
    20U,   /* 200ms = 20 ticks */
    40U,   /* 400ms = 40 ticks */
    80U    /* 800ms = 80 ticks */
};
