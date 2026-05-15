/**
 * @file soa_adapter_main.h
 * @brief SOA Adapter Module — Public Interface
 *
 * Provides the public API for the SOA Adapter module, which bridges
 * M-Core CAN signals to A-Core SOA services via IPCF communication.
 *
 * Integration points:
 *   - Init:     Call SoaAdapter_Init() from App_Init_All() in EcuM_main_init.c
 *   - Periodic: Call SoaAdapter_Main() from TASK_M0_10MS() in Ostask_main.c
 */

#ifndef SOA_ADAPTER_MAIN_H
#define SOA_ADAPTER_MAIN_H

#if defined(__cplusplus)
extern "C"{
#endif

/*==================================================================================================
 *                                         INCLUDE FILES
 *==================================================================================================*/

#include "Platform.h"

/*==================================================================================================
 *                                         FUNCTION PROTOTYPES
 *==================================================================================================*/

/**
 * @brief Initialize the SOA Adapter module
 *
 * Registers the SOA application with the PICC driver (PICC_ID_SOA_LOCAL)
 * and initializes internal state (Notifier cache, link state tracking).
 *
 * Prerequisites:
 *   - PICC_PreOS_Init() must have been called.
 *   - FlexCAN_Process_Init() must have been called.
 *
 * Called from: App_Init_All() in EcuM_main_init.c
 */
void SoaAdapter_Init(void);

/**
 * @brief SOA Adapter 10ms periodic main function
 *
 * Execution sequence:
 *   1. Check PICC application-level link state
 *   2. If newly connected: send all Notifier initial values
 *   3. Process CAN RX (update DBC structs)
 *   4. Check Notifier signals for changes, send if changed
 *   5. Poll for Getter/Setter requests from A-Core
 *   6. Process CAN TX
 *
 * Called from: TASK_M0_10MS() in Ostask_main.c
 */
void SoaAdapter_Main(void);

/**
 * @brief CAN RX processing
 *
 * Polls CAN RX mailboxes and updates DBC structs.
 * Called internally by SoaAdapter_Main().
 */
void SoaAdapter_CanRxProcess(void);

/**
 * @brief CAN TX processing
 *
 * Packs DBC TX structs and transmits via FlexCAN.
 * Called internally by SoaAdapter_Main().
 */
void SoaAdapter_CanTxProcess(void);

#if defined(__cplusplus)
}
#endif

#endif /* SOA_ADAPTER_MAIN_H */
