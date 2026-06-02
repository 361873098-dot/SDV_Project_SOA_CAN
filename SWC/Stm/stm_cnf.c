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
*  Author:              $Author: weizhichun (uix08649)  $
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
*  Current total: (2+16) + (2+4) + (2+4) + (2+12) + (2+8) = 54 <= 64
***********************************************************************************************************************/
const Stm_DataItemCfg_t g_StmDataItemCfg[STM_MAX_DATA_ITEMS] = {
    { 0x1100U, 16U  },  /* Item 1: e.g. calibration data */
    { 0x0002U,  4U  },  /* Item 2: e.g. config block */
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

/***********************************************************************************************************************
*  TRACE32 Debug Test Implementation
***********************************************************************************************************************/
#include "stm_main.h"
#include "stm_nvm.h"
#include "eeprom.h"
#include <string.h>

/* Global variables for TRACE32 debugging */
volatile uint8 NVM_test_flag = 0U;
uint8 NVM_test_write_val = 0xAAU;      /* Initial test val to write */
uint8 NVM_test_read_buf[16] = {0};     /* Read buffer for read test */
uint16 NVM_test_read_len = 0U;        /* Actual data length read */
Std_ReturnType NVM_test_result = E_OK; /* Store last operation result */

/**
 * @brief Process TRACE32 debug test trigger
 *
 * Checks NVM_test_flag and enters different branches for interactive debugging:
 *   NVM_test_flag = 1: Write local RAM & EEPROM (dataId = 0x0001, len = 8)
 *   NVM_test_flag = 2: Read local RAM (dataId = 0x0001)
 *   NVM_test_flag = 3: Trigger A-core async read request (Method 0x05, dataId = 0x0002)
 *   NVM_test_flag = 4: Format EEPROM
 *   NVM_test_flag = 5: Write local RAM & EEPROM (dataId = 0x0002, len = 16)
 *   NVM_test_flag = 6: Read local RAM (dataId = 0x0002)
 *   NVM_test_flag = 7: Manually mark all valid blocks as dirty to trigger Method 0x04 sync-to-A
 *   NVM_test_flag = 8: Read raw magic byte from EEPROM addr 0x10 (page boundary diagnostic)
 *   NVM_test_flag = 9: Read raw 16 bytes from EEPROM addr 0x10 (dump magic + Block[0] area)
 *
 * Automatically resets NVM_test_flag to 0U after processing.
 */
void Stm_ProcessTest(void)
{
    uint8 tempWriteBuf[16];
    uint16 i;

    if (NVM_test_flag == 0U)
    {
        return; /* No test triggered */
    }

    switch (NVM_test_flag)
    {
        case 1U:
            /* Case 1: Write calibration data to Item 1 (dataId=0x0001, maxLen=8) */
            for (i = 0U; i < g_StmDataItemCfg[0].maxDataLen; i++)
            {
                tempWriteBuf[i] = (uint8)(NVM_test_write_val + i);
            }
            NVM_test_result = Stm_WriteLocal(g_StmDataItemCfg[0].dataId, tempWriteBuf, g_StmDataItemCfg[0].maxDataLen);
            break;

        case 2U:
            /* Case 2: Read data from Item 1 (dataId=0x0001) directly from EEPROM */
            (void)memset(NVM_test_read_buf, 0, sizeof(NVM_test_read_buf));
            NVM_test_result = StmNvm_ReadFromEeprom(g_StmDataItemCfg[0].dataId, NVM_test_read_buf, sizeof(NVM_test_read_buf), &NVM_test_read_len);
            break;

        case 3U:
            /* Case 3: Request A-core to read Item 2 (dataId=0x1100) via Method 0x05 */
            NVM_test_result = Stm_RequestReadFromA(STM_METHOD_M_ASYNC_READ, g_StmDataItemCfg[0].dataId);
            break;

        case 4U:
            /* Case 4: Format EEPROM (magic byte + clear data area) */
            NVM_test_result = StmNvm_FormatEeprom();
            break;

        case 5U:
            /* Case 5: Write configuration data to Item 2 (dataId=0x0002, maxLen=16) */
            for (i = 0U; i < 16U; i++)
            {
                tempWriteBuf[i] = (uint8)(NVM_test_write_val + i);
            }
            NVM_test_result = Stm_WriteLocal(0x0002U, tempWriteBuf, 16U);
            break;

        case 6U:
            /* Case 6: Read data from Item 2 (dataId=0x0002) directly from EEPROM */
            (void)memset(NVM_test_read_buf, 0, sizeof(NVM_test_read_buf));
            NVM_test_result = StmNvm_ReadFromEeprom(0x0002U, NVM_test_read_buf, sizeof(NVM_test_read_buf), &NVM_test_read_len);
            break;

        case 7U:
            /* Case 7: Mark all valid blocks dirty to force a full Method 0x04 sync to A-core */
            StmNvm_SetAllValidDirty();
            NVM_test_result = E_OK;
            break;

        case 8U:
            /* Case 8: Read raw magic byte from EEPROM at addr 0x10
             * Used to diagnose page boundary corruption.
             * After format or write, magic should be 0xA5.
             * If it's NOT 0xA5, a page-crossing write corrupted it.
             * Result: NVM_test_read_buf[0] = raw magic byte value
             *         NVM_test_read_len = 0xA500 | magic_value */
            (void)memset(NVM_test_read_buf, 0, sizeof(NVM_test_read_buf));
            NVM_test_result = Eeprom_ReadBytes(STM_EEPROM_MAGIC_ADDR, NVM_test_read_buf, 1U);
            if (NVM_test_result == E_OK)
            {
                NVM_test_read_len = (uint16)(0xA500U | (uint16)NVM_test_read_buf[0]);
            }
            break;

        case 9U:
            /* Case 9: Read raw 16 bytes from EEPROM starting at addr 0x10
             * Dumps magic byte + Block[0] header + Block[0] data area.
             * Expected layout:
             *   [0x10] = 0xA5 (magic)
             *   [0x11] = valid flag (0x01 if written)
             *   [0x12] = data length
             *   [0x13..] = data bytes
             * Result: NVM_test_read_buf[0..15] = raw EEPROM bytes
             *         NVM_test_read_len = 16 */
            (void)memset(NVM_test_read_buf, 0, sizeof(NVM_test_read_buf));
            NVM_test_result = Eeprom_ReadBytes(STM_EEPROM_MAGIC_ADDR, NVM_test_read_buf, 16U);
            if (NVM_test_result == E_OK)
            {
                NVM_test_read_len = 16U;
            }
            break;

        default:
            NVM_test_result = E_NOT_OK;
            break;
    }

    /* Reset test flag back to 0 to prevent repeating write/read every 10ms */
    NVM_test_flag = 0U;
}
