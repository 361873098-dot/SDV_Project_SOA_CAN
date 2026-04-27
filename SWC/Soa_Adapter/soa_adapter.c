/**
 * @file soa_adapter.c
 * @brief SOA Adapter Module Implementation
 *
 * Provides unified CAN TX/RX processing functions that combine
 * DBC pack/unpack operations with FlexCAN driver calls.
 *
 * This module merges:
 *   - Standard_200_Rx_unpack + AINFC_Can_RxMsgL  -> SoaAdapter_CanRxProcess()
 *   - Standard_100_Tx_pack   + AINFC_Can_TxMsg   -> SoaAdapter_CanTxProcess()
 *
 * Called from TASK_M0_10MS() in Ostask_main.c.
 */

#if defined(__cplusplus)
extern "C"{
#endif

/*==================================================================================================
 *                                         INCLUDE FILES
 *==================================================================================================*/

#include "soa_adapter.h"
#include "FlexCAN_Ip_main.h"    /* AINFC_Can_TxMsg, AINFC_Can_RxMsgL, MB/ID defines */
#include "SOA_CANdbc_Generated.h" /* Standard_100_Tx_pack, Standard_200_Rx_unpack, DBC structs */
#include "picc_api.h"           /* PICC_Init, PICC_APP_SOA, PICC_AppConfig_t */

/*==================================================================================================
 *                                         MACRO DEFINITIONS
 *==================================================================================================*/

/** SOA Adapter IPCF configuration */
#define SOA_CHANNEL_ID          (2U)

/** SOA ProviderID / ConsumerID (from ID range 71-80 for SOA) */
#define SOA_PROVIDER_ID         (71U)    /* 0x47, M-Core SOA Provider */
#define SOA_CONSUMER_ID         (76U)    /* 0x4C, A-Core SOA Consumer */

/*==================================================================================================
 *                                         PRIVATE VARIABLES
 *==================================================================================================*/

/** Local TX data buffer (8 bytes CAN frame) */
static uint8 s_txData0[8];

/** Local TX data buffer for MB1 (debug counter, no DBC) */
static uint8 s_txData1[8];

/** Local RX data buffer (8 bytes CAN frame) */
static uint8 s_rxData0[8];

/** Local RX data buffer for MB1 (no DBC) */
static uint8 s_rxData1[8];

/*==================================================================================================
 *                                         PUBLIC FUNCTIONS
 *==================================================================================================*/

/**
 * @brief Initialize the SOA Adapter module
 */
void SoaAdapter_Init(void)
{
    static const PICC_AppConfig_t soa_cfg = {
        .localId           = SOA_PROVIDER_ID,   /* 71 (0x47) */
        .remoteId          = SOA_CONSUMER_ID,   /* 76 (0x4C) */
        .role              = PICC_ROLE_SERVER,
        .channelId         = SOA_CHANNEL_ID,    /* 2 */
        .methodHandler     = NULL,  /* Polling mode */
        .eventHandler      = NULL   /* Polling mode */
    };

    (void)PICC_Init(PICC_APP_SOA, &soa_cfg);
}

/**
 * @brief SOA Adapter 10ms periodic main function
 *
 * Executes RX first (to get fresh data), then TX.
 */
void SoaAdapter_Main(void)
{
    /* RX first — update DBC structs with latest CAN data */
    SoaAdapter_CanRxProcess();

    /* TX second — send current DBC struct values onto the bus */
    SoaAdapter_CanTxProcess();
}






/**
 * @brief Unified CAN RX processing
 *
 * Polls all RX mailboxes. On successful reception at MB0 (ID=0x200),
 * unpacks raw bytes into g_rx_Standard_200_Rx DBC struct.
 */
void SoaAdapter_CanRxProcess(void)
{
  
}

/**
 * @brief Unified CAN TX processing
 *
 * Packs global DBC TX struct into raw bytes, then transmits via FlexCAN.
 */
void SoaAdapter_CanTxProcess(void)
{

}

#if defined(__cplusplus)
}
#endif
