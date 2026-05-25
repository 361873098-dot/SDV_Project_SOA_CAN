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

uint8 TstRdEepromData[EEPROM_8BYTE_LEN] = {0};
uint8 TstWrEepromData[EEPROM_8BYTE_LEN] = {0xA5U, 0x5AU, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U};
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
 *  Description      : Write bytes to EEPROM.
 *
 *  List of arguments: address - EEPROM internal address to write to
 *                     data    - Pointer to buffer containing data to write
 *
 *  Return value     : E_OK     - Write request accepted by I2C driver
 *                     E_NOT_OK - Write request failed
 *
 ***********************************************************************************************************************/
Std_ReturnType Eeprom_WriteBytes(uint8 address, uint8 *data, uint16 length)
{
  
    Std_ReturnType ret;
    uint16 index;
    uint8 txBuf[1U + EEPROM_WRITE_MAX_LEN];
    I2c_RequestType req;

    if ((data == NULL_PTR) || (length == 0U) || (length > EEPROM_WRITE_MAX_LEN))
    {
        return E_NOT_OK;
    }

    txBuf[0] = address;
    for (index = 0U; index < length; index++)
    {
        txBuf[index + 1U] = data[index];
    }

    req.SlaveAddress = EEPROM_I2C_SLAVE_ADDR_7BIT;
    req.BitsSlaveAddressSize = FALSE;
    req.HighSpeedMode = FALSE;
    req.ExpectNack = FALSE;
    req.RepeatedStart = FALSE;
    req.BufferSize = length + 1U;
    req.DataDirection = I2C_SEND_DATA;
    req.DataBuffer = txBuf;

    ret = I2c_SyncTransmit(EEPROM_I2C_CHANNEL, &req);
    if (ret != E_OK)
    {
        return E_NOT_OK;
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