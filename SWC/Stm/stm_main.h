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
*  File name:           $Source: stm.h $
*  Revision:            $Revision: 1.0 $
*  Author:              $Author: weizhichun (uix08649)  $
*  Module acronym:      STM
*  Specification:
*  Date:                $Date: 2026/05/25  $
*
*  Description:     Storage Middleware public API
*
*********************************************************************************/

#ifndef STM_H
#define STM_H

#if defined(__cplusplus)
extern "C"{
#endif

#include "Platform.h"

/***********************************************************************************************************************
 *  Function name    : Stm_Init()
 *
 *  Description      : Initialize STM module: register dual-role PICC endpoints,
 *                     initialize NVM layer (EEPROM read-back to RAM mirror).
 *                     Must be called in App_Init_All() after Hm_Init().
 *
 *  List of arguments: none
 *
 *  Return value     : none
 *
 ***********************************************************************************************************************/
extern void Stm_Init(void);

/***********************************************************************************************************************
 *  Function name    : Stm_Main()
 *
 *  Description      : 10ms periodic entry point. Drives state machine and
 *                     all sub-task processing. Must be called in TASK_M0_10MS
 *                     after Hm_Main().
 *
 *  List of arguments: none
 *
 *  Return value     : none
 *
 ***********************************************************************************************************************/
extern void Stm_Main(void);

/***********************************************************************************************************************
 *  Function name    : Stm_WriteLocal()
 *
 *  Description      : Write data to a local NVM block by dataId.
 *                     Updates RAM mirror and marks block as dirty for
 *                     subsequent EEPROM sync. Does NOT block.
 *
 *  List of arguments: dataId   - Data item identifier (from config table)
 *                     data     - Pointer to source data buffer
 *                     len      - Number of bytes to write
 *
 *  Return value     : E_OK     - Write succeeded (RAM updated)
 *                     E_NOT_OK - Invalid dataId, NULL data, or len mismatch
 *
 ***********************************************************************************************************************/
extern Std_ReturnType Stm_WriteLocal(uint16 dataId, const uint8 *data, uint16 len);

/***********************************************************************************************************************
 *  Function name    : Stm_ReadLocal()
 *
 *  Description      : Read data from a local NVM block by dataId.
 *                     Reads from RAM mirror (zero latency).
 *
 *  List of arguments: dataId     - Data item identifier
 *                     data       - Pointer to destination buffer
 *                     maxLen     - Maximum buffer size
 *                     actualLen  - Actual data length returned (may be NULL)
 *
 *  Return value     : E_OK     - Read succeeded
 *                     E_NOT_OK - Invalid dataId, NULL data, or block not valid
 *
 ***********************************************************************************************************************/
extern Std_ReturnType Stm_ReadLocal(uint16 dataId, uint8 *data, uint16 maxLen, uint16 *actualLen);

/***********************************************************************************************************************
 *  Function name    : Stm_RequestReadFromA()
 *
 *  Description      : Asynchronously request A-core to send data for
 *                     the specified dataId. Supports both Method 0x03 (M read
 *                     from A) and Method 0x05 (M async read from A).
 *                     Response is handled in Stm_Main() state machine.
 *
 *  List of arguments: methodId - Method identifier (STM_METHOD_M_READ_FROM_A or STM_METHOD_M_ASYNC_READ)
 *                     dataId   - Data item identifier to request from A-core
 *
 *  Return value     : E_OK     - Request sent successfully
 *                     E_NOT_OK - Link not ready, invalid methodId, or send failed
 *
 ***********************************************************************************************************************/
extern Std_ReturnType Stm_RequestReadFromA(uint8 methodId, uint16 dataId);

#if defined(__cplusplus)
}
#endif

#endif /* STM_H */
