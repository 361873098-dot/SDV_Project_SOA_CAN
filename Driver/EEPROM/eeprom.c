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
*  File name:           $Source: eeprom.c $
*  Revision:            $Revision: 1.0 $
*  Author:              $Author: Li Song (uic59152)  $
*  Module acronym:      EEPROM
*  Specification:
*  Date:                $Date: 2026/05/12  $
*
*  Description:     This Unit processes the EEPROM module
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
#include "CDD_I2c.h"
#include "CDD_I2c_CfgDefines.h"
#include "eeprom.h"

#define EEPROM_CPU_CYCLES_PER_US       (400U)
#define EEPROM_WRITE_CYCLE_DELAY_MS    (10U)
#define EEPROM_DWT_CYCCNT_ADDR         (0xE0001004UL)
#define EEPROM_DWT_CTRL_ADDR           (0xE0001000UL)
#define EEPROM_DEMCR_ADDR              (0xE000EDFCUL)
#define EEPROM_DWT_CTRL_CYCCNTENA_MASK (0x00000001UL)
#define EEPROM_DEMCR_TRCENA_MASK       (0x01000000UL)
#define EEPROM_DWT_STARTUP_GUARD       (1000U)

uint8 TstRdEepromData[EEPROM_8BYTE_LEN] = {0};
uint8 TstWrEepromData[EEPROM_8BYTE_LEN] = {0xA5U, 0x5AU, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U};

/***********************************************************************************************************************
 *  Function name    : Eeprom_DelayMs()
 *
 *  Description      : Busy-wait delay using the ARM Cortex-M7 DWT cycle counter.
 *                     The DWT counter is enabled during ECU diagnostic init.
 *
 *  List of arguments: ms - Delay time in milliseconds.
 *                          Used for EEPROM internal write-cycle wait (t_WR).
 *
 *  Return value     : E_OK     - Delay completed
 *                     E_NOT_OK - DWT cycle counter is not enabled or not running
 *
 ***********************************************************************************************************************/
static Std_ReturnType Eeprom_DelayMs(uint32 ms)
{
    volatile uint32 *const DWT_CYCCNT = (volatile uint32 *)EEPROM_DWT_CYCCNT_ADDR;
    volatile uint32 *const DWT_CTRL = (volatile uint32 *)EEPROM_DWT_CTRL_ADDR;
    volatile uint32 *const DEMCR = (volatile uint32 *)EEPROM_DEMCR_ADDR;
    uint32 start;
    uint32 target;
    uint32 guard;

    if (((*DEMCR & EEPROM_DEMCR_TRCENA_MASK) == 0U) ||
        ((*DWT_CTRL & EEPROM_DWT_CTRL_CYCCNTENA_MASK) == 0U))
    {
        return E_NOT_OK;
    }

    start = *DWT_CYCCNT;

    /* Verify CYCCNT is actually incrementing before entering the delay loop. */
    for (guard = 0U; guard < EEPROM_DWT_STARTUP_GUARD; guard++)
    {
        if (*DWT_CYCCNT != start)
        {
            break;
        }
    }
    if (guard >= EEPROM_DWT_STARTUP_GUARD)
    {
        return E_NOT_OK;
    }

    start = *DWT_CYCCNT;
    target = ms * 1000U * EEPROM_CPU_CYCLES_PER_US;
    /* Unsigned subtraction handles 32-bit counter wrap-around. */
    while ((*DWT_CYCCNT - start) < target)
    {
        /* busy wait */
    }

    return E_OK;
}

/***********************************************************************************************************************
 *  Function name    : Eeprom_ReadBytes()
 *
 *  Description      : Read bytes from EEPROM.
 *
 *  List of arguments: address - EEPROM address to read from
 *                     data - Pointer to buffer to store read data
 *                     length - Number of bytes to read
 *
 *  Return value     : None
 *
 ***********************************************************************************************************************/
Std_ReturnType Eeprom_ReadBytes(uint8 address, uint8 *data, uint16 length)
{
    Std_ReturnType ret;
    uint8 addrBuf[1];
    I2c_RequestType req;

    addrBuf[0] = address;

    if ((data == NULL_PTR) || (length == 0U))
    {
        return E_NOT_OK;
    }

     /*
     * Step 1:
     * Send the EEPROM internal memory address.
     * RepeatedStart = TRUE means no STOP condition is generated
     * at the end of this transfer. The following read operation
     * will generate a repeated START condition.
     */
    req.SlaveAddress = EEPROM_I2C_SLAVE_ADDR_7BIT;
    req.BitsSlaveAddressSize = FALSE;
    req.HighSpeedMode = FALSE;
    req.ExpectNack = FALSE;
    req.RepeatedStart = TRUE;
    req.BufferSize = 1U;
    req.DataDirection = I2C_SEND_DATA;
    req.DataBuffer = addrBuf;

    ret = I2c_SyncTransmit(EEPROM_I2C_CHANNEL, &req);
    if (ret != E_OK)
    {
        return E_NOT_OK;
    }
    /*
     * Step 2:
     * Read data from EEPROM after the repeated START condition.
     * RepeatedStart = FALSE means a STOP condition is generated
     * after the read transfer is completed.
     */
    req.SlaveAddress = EEPROM_I2C_SLAVE_ADDR_7BIT;
    req.BitsSlaveAddressSize = FALSE;
    req.HighSpeedMode = FALSE;
    req.ExpectNack = FALSE;
    req.RepeatedStart = FALSE;
    req.BufferSize = length;
    req.DataDirection = I2C_RECEIVE_DATA;
    req.DataBuffer = data;

    ret = I2c_SyncTransmit(EEPROM_I2C_CHANNEL, &req);
    if (ret != E_OK)
    {
        return E_NOT_OK;
    }

    return E_OK;
}


/***********************************************************************************************************************
 *  Function name    : Eeprom_WriteBytes()
 *
 *  Description      : Write bytes to EEPROM with page-aligned splitting.
 *
 *                     CRITICAL: EEPROM devices wrap the internal address pointer
 *                     at page boundaries during a write operation. If a single write
 *                     crosses a page boundary, bytes that overflow wrap to the START
 *                     of the current page, silently overwriting existing data.
 *
 *                     This function automatically splits writes at page boundaries
 *                     (defined by EEPROM_PAGE_SIZE) to prevent this corruption.
 *                     A 10ms delay is inserted between page writes for the EEPROM
 *                     internal write cycle time (t_WR).
 *
 *  List of arguments: address - EEPROM internal address to write to
 *                     data    - Pointer to buffer containing data to write
 *                     length  - Number of bytes to write
 *
 *  Return value     : E_OK     - Write succeeded
 *                     E_NOT_OK - Write request failed or invalid parameters
 *
 ***********************************************************************************************************************/
Std_ReturnType Eeprom_WriteBytes(uint8 address, uint8 *data, uint16 length)
{
    Std_ReturnType ret;
    uint16 offset;
    uint16 chunkLen;
    uint16 pageRemaining;
    uint16 idx;
    uint8  txBuf[1U + EEPROM_PAGE_SIZE]; /* 1 byte address + max 1 page of data */
    I2c_RequestType req;

    if ((data == NULL_PTR) || (length == 0U))
    {
        return E_NOT_OK;
    }

    offset = 0U;

    while (offset < length)
    {
        /* Calculate how many bytes remain in the current page */
        pageRemaining = (uint16)EEPROM_PAGE_SIZE - (uint16)(((uint16)address + offset) % (uint16)EEPROM_PAGE_SIZE);

        /* Chunk size = min(remaining_data, page_remaining) */
        chunkLen = length - offset;
        if (chunkLen > pageRemaining)
        {
            chunkLen = pageRemaining;
        }

        /* Build I2C transmit buffer: [eeprom_addr][data_byte_0]...[data_byte_n] */
        txBuf[0] = (uint8)((uint16)address + offset);
        for (idx = 0U; idx < chunkLen; idx++)
        {
            txBuf[idx + 1U] = data[offset + idx];
        }

        req.SlaveAddress = EEPROM_I2C_SLAVE_ADDR_7BIT;
        req.BitsSlaveAddressSize = FALSE;
        req.HighSpeedMode = FALSE;
        req.ExpectNack = FALSE;
        req.RepeatedStart = FALSE;
        req.BufferSize = chunkLen + 1U;
        req.DataDirection = I2C_SEND_DATA;
        req.DataBuffer = txBuf;

        ret = I2c_SyncTransmit(EEPROM_I2C_CHANNEL, &req);
        if (ret != E_OK)
        {
            return E_NOT_OK;
        }

        offset += chunkLen;

        /* Wait for EEPROM internal write cycle (t_WR = 5~10ms typical).
         * ALWAYS delay after every page write, including the last one.
         * This ensures the EEPROM has completed its physical write
         * before this function returns, so callers do NOT need to add
         * their own t_WR delay. */
        ret = Eeprom_DelayMs(EEPROM_WRITE_CYCLE_DELAY_MS);
        if (ret != E_OK)
        {
            return E_NOT_OK;
        }
    }

    return E_OK;
}

/***********************************************************************************************************************
 *  Function name    : Test_WrEeprom()
 *
 *  Description      : Test function to write bytes to EEPROM.
 *
 *  List of arguments: None
 *
 *  Return value     : None
 *
 ***********************************************************************************************************************/
void Test_WrEeprom(void)
{
    /* EEPROM address range 0x00U to 0x04U is reserved and must not be used. */
    Eeprom_WriteBytes(0x05U, TstWrEepromData, EEPROM_8BYTE_LEN);;

}

/***********************************************************************************************************************
 *  Function name    : Test_RdEeprom()
 *
 *  Description      : Test function to read bytes from EEPROM.
 *
 *  List of arguments: None
 *
 *  Return value     : None
 *
 ***********************************************************************************************************************/
void Test_RdEeprom(void)
{
    Eeprom_ReadBytes(0x05U, TstRdEepromData, EEPROM_8BYTE_LEN);
}
