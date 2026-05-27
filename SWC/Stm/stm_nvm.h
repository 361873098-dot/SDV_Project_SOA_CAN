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
*  File name:           $Source: stm_nvm.h $
*  Revision:            $Revision: 1.0 $
*  Author:              $Author: Li Song (uic59152)  $
*  Module acronym:      STM
*  Specification:
*  Date:                $Date: 2026/05/25  $
*
*  Description:     Storage Middleware NVM management interface
*
*********************************************************************************/

#ifndef STM_NVM_H
#define STM_NVM_H

#if defined(__cplusplus)
extern "C"{
#endif

#include "Platform.h"
#include "stm_cnf.h"

/***********************************************************************************************************************
 *  NVM Block Type
 ***********************************************************************************************************************/

/**
 * @brief Single NVM block in RAM mirror
 *
 * Each block corresponds to one dataId from the configuration table.
 * EEPROM offset is computed at init time from cumulative item sizes.
 *
 * EEPROM on-disk format per block:
 *   [valid(1B)][len(1B)][data(maxDataLen)]
 *   - valid:  TRUE(1) if block contains valid data, FALSE(0) if empty/corrupt
 *   - len:    actual data length (0..maxDataLen)
 *   - data:   up to maxDataLen bytes of persistent data
 */
typedef struct {
    uint8  data[STM_NVM_BLOCK_MAX_SIZE]; /**< RAM mirror data buffer (always maxDataLen size) */
    uint16 dataLen;                       /**< Current actual data length (0..maxDataLen) */
    uint8  valid;                         /**< TRUE if data is valid (has been written at least once) */
    uint8  dirty;                         /**< TRUE if data modified and needs sync to A-core */
    uint16 eepromOffset;                  /**< EEPROM offset within data area (computed at init) */
} Stm_NvmBlock_t;

/***********************************************************************************************************************
 *  Function Declarations
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 *  Function name    : StmNvm_Init()
 *
 *  Description      : Initialize NVM layer. Read EEPROM magic byte at 0x10.
 *                     If valid, read all data blocks to RAM mirror.
 *                     If invalid, format EEPROM (write magic byte) and
 *                     clear all RAM mirror blocks.
 *
 *  List of arguments: none
 *
 *  Return value     : E_OK     - Initialization succeeded
 *                     E_NOT_OK - EEPROM read/write failure
 *
 ***********************************************************************************************************************/
extern Std_ReturnType StmNvm_Init(void);

/***********************************************************************************************************************
 *  Function name    : StmNvm_IsReady()
 *
 *  Description      : Check if NVM layer is initialized and ready
 *
 *  List of arguments: none
 *
 *  Return value     : TRUE  - NVM is ready
 *                     FALSE - NVM not initialized
 *
 ***********************************************************************************************************************/
extern boolean StmNvm_IsReady(void);

/***********************************************************************************************************************
 *  Function name    : StmNvm_Read()
 *
 *  Description      : Read data from RAM mirror by dataId (zero latency)
 *
 *  List of arguments: dataId     - Data item identifier
 *                     data       - Destination buffer
 *                     maxLen     - Buffer capacity
 *                     actualLen  - Actual data length (may be NULL)
 *
 *  Return value     : E_OK     - Read succeeded
 *                     E_NOT_OK - Invalid dataId or block not valid
 *
 ***********************************************************************************************************************/
extern Std_ReturnType StmNvm_Read(uint16 dataId, uint8 *data, uint16 maxLen, uint16 *actualLen);

/***********************************************************************************************************************
 *  Function name    : StmNvm_ReadFromEeprom()
 *
 *  Description      : Read data directly from EEPROM hardware (bypasses RAM mirror).
 *                     Does NOT update the RAM mirror. Used for debug verification
 *                     to compare EEPROM actual contents vs RAM mirror contents.
 *
 *  List of arguments: dataId     - Data item identifier
 *                     data       - Destination buffer
 *                     maxLen     - Buffer capacity
 *                     actualLen  - Actual data length read from EEPROM (may be NULL)
 *
 *  Return value     : E_OK     - Read succeeded
 *                     E_NOT_OK - Invalid dataId, block not valid in EEPROM, or EEPROM read failure
 *
 ***********************************************************************************************************************/
extern Std_ReturnType StmNvm_ReadFromEeprom(uint16 dataId, uint8 *data, uint16 maxLen, uint16 *actualLen);

/***********************************************************************************************************************
 *  Function name    : StmNvm_Write()
 *
 *  Description      : Write data to RAM mirror and sync to EEPROM.
 *                     Updates RAM immediately, then writes to EEPROM
 *                     using segmented I2C writes (max 16 bytes per transfer).
 *
 *  List of arguments: dataId - Data item identifier
 *                     data   - Source buffer
 *                     len    - Number of bytes to write
 *
 *  Return value     : E_OK     - Write succeeded
 *                     E_NOT_OK - Invalid dataId, NULL data, or EEPROM failure
 *
 ***********************************************************************************************************************/
extern Std_ReturnType StmNvm_Write(uint16 dataId, const uint8 *data, uint16 len);

/***********************************************************************************************************************
 *  Function name    : StmNvm_IsDirty()
 *
 *  Description      : Check if a data block has been modified
 *
 *  List of arguments: dataId - Data item identifier
 *
 *  Return value     : TRUE  - Block has pending changes
 *                     FALSE - Block is clean or invalid dataId
 *
 ***********************************************************************************************************************/
extern boolean StmNvm_IsDirty(uint16 dataId);

/***********************************************************************************************************************
 *  Function name    : StmNvm_ClearDirty()
 *
 *  Description      : Mark a data block as clean (after successful sync)
 *
 *  List of arguments: dataId - Data item identifier
 *
 *  Return value     : none
 *
 ***********************************************************************************************************************/
extern void StmNvm_ClearDirty(uint16 dataId);

/***********************************************************************************************************************
 *  Function name    : StmNvm_GetSyncableItem()
 *
 *  Description      : Find the next data block that is dirty and needs to
 *                     be synced to A-core. Used by Stm_ProcessSyncToA().
 *
 *  List of arguments: startIndex - Start searching from this index
 *                     outDataId  - Found dataId (output)
 *                     outData    - Pointer to data buffer (output, points to RAM)
 *                     outLen     - Data length (output)
 *
 *  Return value     : E_OK     - Found a dirty block
 *                     E_NOT_OK - No dirty blocks found
 *
 ***********************************************************************************************************************/
extern Std_ReturnType StmNvm_GetSyncableItem(uint16 startIndex, uint16 *outDataId,
                                               const uint8 **outData, uint16 *outLen);

/***********************************************************************************************************************
 *  Function name    : StmNvm_WriteFromA()
 *
 *  Description      : Write data received from A-core to RAM mirror and EEPROM.
 *                     Used for Method 0x02 (A-core write to M-core).
 *
 *  List of arguments: dataId - Data item identifier
 *                     data   - Source data from A-core
 *                     len    - Data length
 *
 *  Return value     : E_OK     - Write succeeded
 *                     E_NOT_OK - Invalid dataId or EEPROM failure
 *
 ***********************************************************************************************************************/
extern Std_ReturnType StmNvm_WriteFromA(uint16 dataId, const uint8 *data, uint16 len);

/***********************************************************************************************************************
 *  Function name    : StmNvm_FormatEeprom()
 *
 *  Description      : Format EEPROM data area: write magic byte and
 *                     clear all data blocks (write 0x00 with valid=FALSE).
 *
 *  List of arguments: none
 *
 *  Return value     : E_OK     - Format succeeded
 *                     E_NOT_OK - EEPROM write failure
 *
 ***********************************************************************************************************************/
extern Std_ReturnType StmNvm_FormatEeprom(void);

/***********************************************************************************************************************
 *  Function name    : StmNvm_ResetOnDisconnect()
 *
 *  Description      : Reset state on disconnect (clear dirty flags,
 *                     keep data in RAM mirror and EEPROM intact).
 *
 *  List of arguments: none
 *
 *  Return value     : none
 *
 ***********************************************************************************************************************/
extern void StmNvm_ResetOnDisconnect(void);

/***********************************************************************************************************************
 *  Function name    : StmNvm_SetAllValidDirty()
 *
 *  Description      : Mark all valid NVM blocks as dirty.
 *                     Called when A-core consistency check (Method 0x01) passes,
 *                     to trigger full data sync from M-core to A-core.
 *                     Only blocks with valid==TRUE are marked dirty; invalid
 *                     blocks are skipped (no data to sync).
 *
 *  List of arguments: none
 *
 *  Return value     : none
 *
 ***********************************************************************************************************************/
extern void StmNvm_SetAllValidDirty(void);

#if defined(__cplusplus)
}
#endif

#endif /* STM_NVM_H */
