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
*  File name:           $Source: eeprom.h $
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


#define EEPROM_I2C_CHANNEL          (I2cChannel_1)  /* I2cChannel_1 -> I2C0 */
#define EEPROM_I2C_SLAVE_ADDR_7BIT  (0x50U)         /* CMM 0xA0 >> 1 */
#define EEPROM_8BYTE_LEN            (8U)
#define EEPROM_WRITE_MAX_LEN       (16U)          /* Maximum number of bytes to write in one I2C transfer, depends on the EEPROM device capability */

/**
 * EEPROM page size in bytes.
 * CRITICAL: All EEPROM writes MUST NOT cross a page boundary!
 * If a write spans two pages, the EEPROM wraps the address pointer
 * within the current page, silently overwriting data at the page start.
 *
 * Common values:
 *   24C02  = 8 bytes/page
 *   24C04  = 16 bytes/page
 *   24C08  = 16 bytes/page
 *   24C16  = 16 bytes/page
 *   24C32  = 32 bytes/page
 *   24C64  = 32 bytes/page
 *   24C128 = 64 bytes/page
 *   24C256 = 64 bytes/page
 *
 * Set this to match the actual EEPROM device on your hardware.
 * Using a value smaller than the actual page size is always safe.
 */
#define EEPROM_PAGE_SIZE            (8U)

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
extern Std_ReturnType Eeprom_ReadBytes(uint8 address, uint8 *data, uint16 length);

/***********************************************************************************************************************
 *  Function name    : Eeprom_WriteBytes()
 *
 *  Description      : Write bytes to EEPROM.
 *
 *  List of arguments: address - EEPROM internal address to write to
 *                     data    - Pointer to buffer containing data to write
 *                     length  - Number of bytes to write
 *
 *  Return value     : E_OK     - Write request accepted by I2C driver
 *                     E_NOT_OK - Write request failed
 *
 ***********************************************************************************************************************/
extern Std_ReturnType Eeprom_WriteBytes(uint8 address, uint8 *data, uint16 length);

extern void Test_WrEeprom(void);
extern void Test_RdEeprom(void);