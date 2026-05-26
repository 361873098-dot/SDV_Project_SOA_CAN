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
*  File name:           $Source: stm_nvm.c $
*  Revision:            $Revision: 1.0 $
*  Author:              $Author: Li Song (uic59152)  $
*  Module acronym:      STM
*  Specification:
*  Date:                $Date: 2026/05/25  $
*
*  Description:     Storage Middleware NVM management implementation
*                   EEPROM persistence + RAM mirror with segmented I2C writes
*
*  Architecture:
*    - Each data item has a RAM mirror (Stm_NvmBlock_t) for zero-latency reads.
*    - EEPROM stores persistent data at computed offsets within the data area.
*    - EEPROM layout per block: [valid(1B)][len(1B)][data(maxDataLen)]
*    - Writes to EEPROM are segmented (max EEPROM_WRITE_MAX_LEN bytes per I2C transfer)
*      to comply with I2C transaction size limits of the EEPROM driver.
*    - On init, EEPROM magic byte (0xA5) is checked. If invalid, the entire
*      EEPROM data area is formatted (all zeros) and RAM mirror is cleared.
*
*********************************************************************************/

/***********************************************************************************************************************
*  include files
***********************************************************************************************************************/
#include "Platform.h"
#include "stm_nvm.h"
#include "eeprom.h"
#include <string.h>

/***********************************************************************************************************************
*  local variable definitions
***********************************************************************************************************************/

/** RAM mirror: one block per configured data item.
 *  Each block holds the current data, its length, validity flag,
 *  dirty flag (needs sync to A-core), and computed EEPROM offset. */
static Stm_NvmBlock_t g_nvmBlocks[STM_MAX_DATA_ITEMS];

/** NVM readiness flag. Set to TRUE only after successful StmNvm_Init(). */
static boolean g_nvmReady = FALSE;

/***********************************************************************************************************************
*  static helper functions
***********************************************************************************************************************/

/**
 * @brief Find block index by dataId
 *
 * Linear search through the config table to map a dataId to its
 * internal array index. Used by all public NVM functions.
 *
 * @param dataId  Data item identifier to look up
 * @return Index 0..STM_MAX_DATA_ITEMS-1 on success, 0xFFFF if not found
 */
static uint16 StmNvm_FindIndex(uint16 dataId)
{
    uint16 i;
    for (i = 0U; i < STM_MAX_DATA_ITEMS; i++)
    {
        if (g_StmDataItemCfg[i].dataId == dataId)
        {
            return i;
        }
    }
    return 0xFFFFU;
}

/**
 * @brief Compute EEPROM offsets for all data blocks at init time
 *
 * Walks through the config table and assigns a contiguous EEPROM offset
 * to each block. The offset is relative to STM_EEPROM_DATA_START_ADDR.
 *
 * EEPROM layout within data area (0x11~0x50):
 *   Block 0: [valid(1B)][len(1B)][data(maxDataLen_0)]
 *   Block 1: [valid(1B)][len(1B)][data(maxDataLen_1)]
 *   ...
 *   Block N: ...
 *
 * If the total size exceeds STM_EEPROM_DATA_SIZE (64 bytes), the NVM
 * layer is marked as not ready (g_nvmReady = FALSE) as a safety measure.
 */
static void StmNvm_ComputeOffsets(void)
{
    uint16 offset = 0U;
    uint16 i;

    for (i = 0U; i < STM_MAX_DATA_ITEMS; i++)
    {
        g_nvmBlocks[i].eepromOffset = offset;
        /* Each block in EEPROM: 1B valid + 1B len + maxDataLen bytes of data */
        offset += (2U + g_StmDataItemCfg[i].maxDataLen);
    }

    /* Safety check: total must fit in EEPROM data area */
    if (offset > STM_EEPROM_DATA_SIZE)
    {
        /* Configuration exceeds EEPROM capacity - should never happen
         * if config table is validated. Disable NVM. */
        g_nvmReady = FALSE;
    }
}

/**
 * @brief Read one data block from EEPROM to RAM mirror
 *
 * Reads the 2-byte header (valid + len) first, then reads the actual
 * data payload if the block is marked valid and has non-zero length.
 *
 * EEPROM format at computed offset:
 *   [0]     valid flag (TRUE/FALSE)
 *   [1]     data length (0..maxDataLen)
 *   [2..]   data bytes
 *
 * @param index  Block index in g_nvmBlocks array
 * @return E_OK on success, E_NOT_OK on EEPROM read failure or invalid data
 */
static Std_ReturnType StmNvm_ReadBlockFromEeprom(uint16 index)
{
    Std_ReturnType ret;
    uint8 headerBuf[2U];
    uint8 eepromAddr;
    uint16 dataOffset;

    dataOffset = g_nvmBlocks[index].eepromOffset;
    eepromAddr = (uint8)(STM_EEPROM_DATA_START_ADDR + dataOffset);

    /* Step 1: Read valid + len header (2 bytes) */
    ret = Eeprom_ReadBytes(eepromAddr, headerBuf, 2U);
    if (ret != E_OK)
    {
        return E_NOT_OK;
    }

    g_nvmBlocks[index].valid = headerBuf[0];
    g_nvmBlocks[index].dataLen = (uint16)headerBuf[1];

    /* Validate data length against configured maximum */
    if (g_nvmBlocks[index].dataLen > g_StmDataItemCfg[index].maxDataLen)
    {
        /* Corrupted length field - mark block as invalid */
        g_nvmBlocks[index].valid = FALSE;
        g_nvmBlocks[index].dataLen = 0U;
        return E_NOT_OK;
    }

    /* Step 2: Read actual data if block is valid and has data */
    if ((g_nvmBlocks[index].valid == TRUE) && (g_nvmBlocks[index].dataLen > 0U))
    {
        eepromAddr = (uint8)(STM_EEPROM_DATA_START_ADDR + dataOffset + 2U);
        ret = Eeprom_ReadBytes(eepromAddr, g_nvmBlocks[index].data, g_nvmBlocks[index].dataLen);
        if (ret != E_OK)
        {
            /* EEPROM read failure - mark block as invalid to prevent stale data usage */
            g_nvmBlocks[index].valid = FALSE;
            return E_NOT_OK;
        }
    }

    /* Clear dirty flag since this data was just read from EEPROM (no pending changes) */
    g_nvmBlocks[index].dirty = FALSE;
    return E_OK;
}

/**
 * @brief Write one data block from RAM mirror to EEPROM (segmented)
 *
 * Writes the block in two phases:
 *   1. Header: valid(1B) + len(1B)
 *   2. Data: written in segments of EEPROM_WRITE_MAX_LEN (16) bytes each
 *
 * Segmented writes are required because the I2C EEPROM driver has a
 * maximum transaction size per I2C write operation.
 *
 * Note: s_writeBuf is declared static to avoid placing large buffers
 * on the stack (important for FreeRTOS tasks with limited stack space).
 *
 * @param index  Block index in g_nvmBlocks array
 * @return E_OK on success, E_NOT_OK on EEPROM write failure
 */
static Std_ReturnType StmNvm_WriteBlockToEeprom(uint16 index)
{
    Std_ReturnType ret;
    uint8 eepromAddr;
    uint16 dataOffset;
    uint16 bytesRemaining;
    uint16 writeOffset;
    static uint8 s_writeBuf[EEPROM_WRITE_MAX_LEN]; /* static to avoid stack overflow */

    dataOffset = g_nvmBlocks[index].eepromOffset;

    /* Phase 1: Write valid + len header */
    eepromAddr = (uint8)(STM_EEPROM_DATA_START_ADDR + dataOffset);
    s_writeBuf[0] = g_nvmBlocks[index].valid;
    s_writeBuf[1] = (uint8)g_nvmBlocks[index].dataLen;

    ret = Eeprom_WriteBytes(eepromAddr, s_writeBuf, 2U);
    if (ret != E_OK)
    {
        return E_NOT_OK;
    }

    /* Phase 2: Write data payload in segments of EEPROM_WRITE_MAX_LEN */
    bytesRemaining = g_nvmBlocks[index].dataLen;
    writeOffset = 0U;

    while (bytesRemaining > 0U)
    {
        uint16 chunkLen;
        uint16 i;

        /* Determine chunk size: min(remaining, max I2C write size) */
        chunkLen = (bytesRemaining > EEPROM_WRITE_MAX_LEN) ? EEPROM_WRITE_MAX_LEN : bytesRemaining;

        /* Copy chunk from RAM mirror to write buffer */
        for (i = 0U; i < chunkLen; i++)
        {
            s_writeBuf[i] = g_nvmBlocks[index].data[writeOffset + i];
        }

        /* Write chunk to EEPROM at the correct offset */
        eepromAddr = (uint8)(STM_EEPROM_DATA_START_ADDR + dataOffset + 2U + writeOffset);
        ret = Eeprom_WriteBytes(eepromAddr, s_writeBuf, (uint16)chunkLen);
        if (ret != E_OK)
        {
            return E_NOT_OK;
        }

        writeOffset += chunkLen;
        bytesRemaining -= chunkLen;
    }

    return E_OK;
}

/***********************************************************************************************************************
*  public function definitions
***********************************************************************************************************************/

/**
 * Initialize NVM layer:
 * 1. Clear all RAM mirror blocks
 * 2. Compute EEPROM offsets from config table
 * 3. Check EEPROM magic byte (0xA5 at address 0x10)
 *    - If valid: read all data blocks from EEPROM to RAM mirror
 *    - If invalid: format EEPROM (write magic byte + clear data area)
 * 4. Mark NVM as ready
 */
Std_ReturnType StmNvm_Init(void)
{
    Std_ReturnType ret;
    uint8 magicVal;
    uint16 i;

    /* Step 1: Clear all RAM mirror blocks to zero */
    (void)memset(g_nvmBlocks, 0, sizeof(g_nvmBlocks));

    /* Step 2: Compute EEPROM offsets from config table */
    StmNvm_ComputeOffsets();

    /* Step 3: Read magic byte from EEPROM to determine if data area is formatted */
    ret = Eeprom_ReadBytes(STM_EEPROM_MAGIC_ADDR, &magicVal, 1U);
    if (ret != E_OK)
    {
        g_nvmReady = FALSE;
        return E_NOT_OK;
    }

    if (magicVal != STM_EEPROM_MAGIC_VALUE)
    {
        /* EEPROM not formatted (first boot or corrupted) - format it now */
        ret = StmNvm_FormatEeprom();
        if (ret != E_OK)
        {
            g_nvmReady = FALSE;
            return E_NOT_OK;
        }
    }
    else
    {
        /* EEPROM is formatted - read all data blocks from EEPROM to RAM mirror */
        for (i = 0U; i < STM_MAX_DATA_ITEMS; i++)
        {
            (void)StmNvm_ReadBlockFromEeprom(i);
        }
    }

    g_nvmReady = TRUE;
    return E_OK;
}

boolean StmNvm_IsReady(void)
{
    return g_nvmReady;
}

/**
 * Read data from RAM mirror by dataId.
 * Returns the smaller of dataLen and maxLen bytes to prevent buffer overflow.
 */
Std_ReturnType StmNvm_Read(uint16 dataId, uint8 *data, uint16 maxLen, uint16 *actualLen)
{
    uint16 idx;

    /* Guard: NVM must be initialized */
    if (g_nvmReady == FALSE)
    {
        return E_NOT_OK;
    }

    /* Guard: output buffer must not be NULL */
    if (data == NULL)
    {
        return E_NOT_OK;
    }

    /* Look up block index by dataId */
    idx = StmNvm_FindIndex(dataId);
    if (idx == 0xFFFFU)
    {
        return E_NOT_OK;
    }

    /* Block must be valid (previously written) */
    if (g_nvmBlocks[idx].valid == FALSE)
    {
        return E_NOT_OK;
    }

    /* Return actual data length if caller requested it */
    if (actualLen != NULL)
    {
        *actualLen = g_nvmBlocks[idx].dataLen;
    }

    /* Copy min(dataLen, maxLen) bytes to prevent buffer overflow in caller */
    if (g_nvmBlocks[idx].dataLen > 0U)
    {
        uint16 copyLen = (g_nvmBlocks[idx].dataLen > maxLen) ? maxLen : g_nvmBlocks[idx].dataLen;
        (void)memcpy(data, g_nvmBlocks[idx].data, copyLen);
    }

    return E_OK;
}

/**
 * Write data to RAM mirror and immediately sync to EEPROM.
 *
 * Steps:
 * 1. Validate dataId and input parameters
 * 2. Copy data to RAM mirror and mark as valid + dirty
 * 3. Write to EEPROM (segmented)
 * 4. Clear dirty flag after successful EEPROM write
 */
Std_ReturnType StmNvm_Write(uint16 dataId, const uint8 *data, uint16 len)
{
    uint16 idx;
    Std_ReturnType ret;

    /* Guard: NVM must be initialized */
    if (g_nvmReady == FALSE)
    {
        return E_NOT_OK;
    }

    /* Guard: source data must not be NULL */
    if (data == NULL)
    {
        return E_NOT_OK;
    }

    /* Look up block index by dataId */
    idx = StmNvm_FindIndex(dataId);
    if (idx == 0xFFFFU)
    {
        return E_NOT_OK;
    }

    /* Guard: length must not exceed configured maximum */
    if (len > g_StmDataItemCfg[idx].maxDataLen)
    {
        return E_NOT_OK;
    }

    /* Step 1: Update RAM mirror */
    if (len > 0U)
    {
        (void)memcpy(g_nvmBlocks[idx].data, data, len);
    }
    g_nvmBlocks[idx].dataLen = len;
    g_nvmBlocks[idx].valid = TRUE;
    g_nvmBlocks[idx].dirty = TRUE;  /* Mark as dirty for potential A-core sync */

    /* Step 2: Sync to EEPROM immediately (non-volatile persistence) */
    ret = StmNvm_WriteBlockToEeprom(idx);
    if (ret != E_OK)
    {
        return E_NOT_OK;
    }

    /* EEPROM write succeeded. Dirty flag stays TRUE because data
     * still needs to be synced to A-core via Method 0x04.
     * It will be cleared by StmNvm_ClearDirty() after A-core confirms. */
    return E_OK;
}

boolean StmNvm_IsDirty(uint16 dataId)
{
    uint16 idx;

    idx = StmNvm_FindIndex(dataId);
    if (idx == 0xFFFFU)
    {
        return FALSE;
    }

    return (g_nvmBlocks[idx].dirty == TRUE) ? TRUE : FALSE;
}

void StmNvm_ClearDirty(uint16 dataId)
{
    uint16 idx;

    idx = StmNvm_FindIndex(dataId);
    if (idx != 0xFFFFU)
    {
        g_nvmBlocks[idx].dirty = FALSE;
    }
}

/**
 * Find the next data block that is dirty (modified) and needs to be
 * synced to A-core. Starts searching from startIndex for round-robin fairness.
 *
 * Returns the dataId, a pointer to the RAM data buffer, and the data length.
 * Note: The returned data pointer points directly into the RAM mirror,
 * so the caller must process the data before the next write to this block.
 */
Std_ReturnType StmNvm_GetSyncableItem(uint16 startIndex, uint16 *outDataId,
                                        const uint8 **outData, uint16 *outLen)
{
    uint16 i;

    if ((outDataId == NULL) || (outData == NULL) || (outLen == NULL))
    {
        return E_NOT_OK;
    }

    /* Search from startIndex to end of table (round-robin) */
    for (i = startIndex; i < STM_MAX_DATA_ITEMS; i++)
    {
        /* Only sync blocks that are both valid and dirty */
        if ((g_nvmBlocks[i].valid == TRUE) && (g_nvmBlocks[i].dirty == TRUE))
        {
            *outDataId = g_StmDataItemCfg[i].dataId;
            *outData = g_nvmBlocks[i].data;   /* Direct pointer to RAM mirror */
            *outLen = g_nvmBlocks[i].dataLen;
            return E_OK;
        }
    }

    /* No dirty blocks found from startIndex onwards */
    return E_NOT_OK;
}

Std_ReturnType StmNvm_WriteFromA(uint16 dataId, const uint8 *data, uint16 len)
{
    Std_ReturnType ret;

    /* Delegate to StmNvm_Write for RAM mirror + EEPROM persistence */
    ret = StmNvm_Write(dataId, data, len);
    if (ret == E_OK)
    {
        /* Data received FROM A-core does NOT need to be synced BACK to A-core.
         * Clear dirty immediately to prevent unnecessary Method 0x04 round-trip. */
        StmNvm_ClearDirty(dataId);
    }
    return ret;
}

/**
 * Format EEPROM data area:
 * 1. Write magic byte (0xA5) at address 0x10
 * 2. Clear all data blocks in EEPROM (write zeros using segmented writes)
 * 3. Clear all RAM mirror blocks
 *
 * This is called on first boot (no valid magic byte) or when
 * EEPROM data corruption is detected.
 */
Std_ReturnType StmNvm_FormatEeprom(void)
{
    Std_ReturnType ret;
    uint16 i;
    uint8 zeroBuf[16U];
    uint8 eepromAddr;
    uint16 dataOffset;
    uint16 totalBytes;
    uint16 bytesRemaining;
    uint16 writeOffset;

    /* Step 1: Write magic byte to mark EEPROM as formatted */
    uint8 magicVal = STM_EEPROM_MAGIC_VALUE;
    ret = Eeprom_WriteBytes(STM_EEPROM_MAGIC_ADDR, &magicVal, 1U);
    if (ret != E_OK)
    {
        return E_NOT_OK;
    }

    /* Step 2: Clear all data blocks in EEPROM (write zeros) */
    (void)memset(zeroBuf, 0, sizeof(zeroBuf));

    for (i = 0U; i < STM_MAX_DATA_ITEMS; i++)
    {
        dataOffset = g_nvmBlocks[i].eepromOffset;
        /* Total EEPROM bytes for this block: 2 (header) + maxDataLen */
        totalBytes = 2U + g_StmDataItemCfg[i].maxDataLen;
        bytesRemaining = totalBytes;
        writeOffset = 0U;

        /* Write in segments of EEPROM_WRITE_MAX_LEN (16 bytes) */
        while (bytesRemaining > 0U)
        {
            uint16 chunkLen = (bytesRemaining > EEPROM_WRITE_MAX_LEN) ? EEPROM_WRITE_MAX_LEN : bytesRemaining;
            eepromAddr = (uint8)(STM_EEPROM_DATA_START_ADDR + dataOffset + writeOffset);

            ret = Eeprom_WriteBytes(eepromAddr, zeroBuf, (uint16)chunkLen);
            if (ret != E_OK)
            {
                return E_NOT_OK;
            }

            writeOffset += chunkLen;
            bytesRemaining -= chunkLen;
        }

        /* Step 3: Clear corresponding RAM mirror block */
        (void)memset(g_nvmBlocks[i].data, 0, sizeof(g_nvmBlocks[i].data));
        g_nvmBlocks[i].dataLen = 0U;
        g_nvmBlocks[i].valid = FALSE;
        g_nvmBlocks[i].dirty = FALSE;
    }

    return E_OK;
}

/**
 * Reset NVM state on link disconnect.
 *
 * Per protocol requirement:
 * - Clear dirty flags (pending syncs are abandoned since A-core may have restarted)
 * - Keep data in RAM mirror and EEPROM intact (data is still valid locally)
 * - CRC/send counter is NOT reset (handled by PICC layer, persists across disconnects)
 */
void StmNvm_ResetOnDisconnect(void)
{
    uint16 i;

    for (i = 0U; i < STM_MAX_DATA_ITEMS; i++)
    {
        g_nvmBlocks[i].dirty = FALSE;
    }
}
