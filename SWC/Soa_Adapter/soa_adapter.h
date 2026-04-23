/**
 * @file soa_adapter.h
 * @brief SOA Adapter Module Header
 *
 * Provides unified CAN TX/RX processing functions that combine
 * DBC pack/unpack operations with FlexCAN driver calls.
 * Designed to be called from TASK_M0_10MS() periodic task.
 *
 * TX flow: DBC struct -> Standard_100_Tx_pack -> AINFC_Can_TxMsg
 * RX flow: AINFC_Can_RxMsgL -> Standard_200_Rx_unpack -> DBC struct
 */

#ifndef SOA_ADAPTER_H
#define SOA_ADAPTER_H

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
 * Registers the SOA application with the PICC driver and initializes
 * internal state. Must be called after PICC_PreOS_Init() and before
 * the FreeRTOS scheduler starts.
 */
void SoaAdapter_Init(void);

/**
 * @brief SOA Adapter 10ms periodic main function
 *
 * Executes both TX and RX CAN processing in sequence.
 * Called from TASK_M0_10MS() in Ostask_main.c.
 */
void SoaAdapter_Main(void);

/**
 * @brief Unified CAN TX processing
 *
 * Packs the global DBC TX struct (g_tx_Standard_100_Tx) into raw bytes
 * via Standard_100_Tx_pack(), then transmits through AINFC_Can_TxMsg().
 *
 * TX MB0 (ID=0x100, DBC: Standard_100_Tx):
 *   Application sets physical values in g_tx_Standard_100_Tx before
 *   this function is called, e.g.:
 *     g_tx_Standard_100_Tx.IgnitionSts = 1u;
 *     g_tx_Standard_100_Tx.VehicleMode = 0u;
 *
 * TX MB1 (ID=0x101): Raw debug counter, no DBC mapping.
 */
void SoaAdapter_CanTxProcess(void);

/**
 * @brief Unified CAN RX processing
 *
 * Polls all RX mailboxes via AINFC_Can_RxMsgL(). When a new message
 * is received on MB0 (ID=0x200), unpacks it into the global DBC RX
 * struct (g_rx_Standard_200_Rx) via Standard_200_Rx_unpack().
 *
 * After this function returns, application can read:
 *   g_rx_Standard_200_Rx.HighVoltageBatterySts
 *   g_rx_Standard_200_Rx.VehicleSpeedSts
 *   g_rx_Standard_200_Rx.ParkingSts
 *   g_rx_Standard_200_Rx.WorkVehicleModeSt
 */
void SoaAdapter_CanRxProcess(void);

#if defined(__cplusplus)
}
#endif

#endif /* SOA_ADAPTER_H */
