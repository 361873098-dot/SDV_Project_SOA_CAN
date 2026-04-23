/**
 * @file FlexCAN_Ip_main.h
 * @brief FlexCAN AINFC Interface Module Header
 *
 * Provides AINFC_Can_TxMsg / AINFC_Can_RxMsgL interface functions
 * for CAN communication. Supports 2 TX and 2 RX message buffers.
 */

#ifndef FLEXCAN_IP_MAIN_H
#define FLEXCAN_IP_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/*==================================================================================================
 *                                         INCLUDE FILES
 *==================================================================================================*/

#include "FlexCAN_Ip.h"
#include "Mcal.h"
#include "CANdbc_file.h"

/*==================================================================================================
 *                                         MACRO DEFINITIONS
 *==================================================================================================*/


/**
 * @brief Diagnostic update period in milliseconds
 *
 * Controls how often EcuM_Diag_Update() is called from the 10ms periodic task.
 * Change this value to adjust the update frequency:
 *	 1000U = 1 second (default)
 *	 2000U = 2 seconds
 *	 5000U = 5 seconds
 *
 * @note Must be a multiple of 10 (10ms task base period)
 */
#define AINFC_UPDATE_PERIOD_MS (1000U)


/** AINFC CAN return codes */
#define AINFC_CAN_OK 0U     /**< Operation successful */
#define AINFC_CAN_BUSY 1U   /**< MB is busy (previous TX/RX not complete) */
#define AINFC_CAN_ERROR 2U  /**< Operation error */
#define AINFC_CAN_NO_MSG 3U /**< No new message received */

/** TX Message Buffer indices */
#define AINFC_TX_MB0 0U /**< TX MB index 0 (CAN ID 0x100) */
#define AINFC_TX_MB1 1U /**< TX MB index 1 (CAN ID 0x101) */

/** RX Message Buffer indices */
#define AINFC_RX_MB0 2U /**< RX MB index 0 (CAN ID 0x200) */
#define AINFC_RX_MB1 3U /**< RX MB index 1 (CAN ID 0x201) */

/** TX CAN IDs */
#define AINFC_TX_MSG_ID_0 0x100U
#define AINFC_TX_MSG_ID_1 0x101U

/** RX CAN IDs */
#define AINFC_RX_MSG_ID_0 0x200U
#define AINFC_RX_MSG_ID_1 0x201U

/*==================================================================================================
 *                                         GLOBAL VARIABLES
 *==================================================================================================*/

/** TX counters (for debugging) */
extern volatile uint32 g_canTxCount;
extern volatile uint32 g_canTxCount2;

/** RX counters (for debugging) */
extern volatile uint32 g_canRxCount;
extern volatile uint32 g_canRxCount2;


/*==================================================================================================
 *                                         FUNCTION PROTOTYPES
 *==================================================================================================*/

/**
 * @brief Initialize FlexCAN Message Buffers for AINFC interface
 *
 * Configures 2 TX MBs (MB0, MB1) and 2 RX MBs (MB2, MB3).
 * Should be called after FlexCAN_Ip_Init() and FlexCAN_Ip_SetStartMode().
 */
void FlexCAN_Process_Init(void);

/**
 * @brief Send a CAN message via specified MB
 *
 * @param Bus_ID    FlexCAN instance number (0 for CAN0)
 * @param Mbx       TX Message Buffer index (AINFC_TX_MB0 or AINFC_TX_MB1)
 * @param msg_frame Pointer to 8-byte TX data buffer
 * @return AINFC_CAN_OK      - TX started successfully
 * @return AINFC_CAN_BUSY    - Previous TX on this MB not complete yet
 * @return AINFC_CAN_ERROR   - TX error
 */
unsigned char AINFC_Can_TxMsg(unsigned char Bus_ID, unsigned char Mbx,
                              unsigned char *msg_frame);

/**
 * @brief Receive a CAN message from specified MB
 *
 * @param Bus_ID    FlexCAN instance number (0 for CAN0)
 * @param Mbx       RX Message Buffer index (AINFC_RX_MB0 or AINFC_RX_MB1)
 * @param msg_frame Pointer to 8-byte buffer to store received data
 * @return AINFC_CAN_OK      - New message received and copied to msg_frame
 * @return AINFC_CAN_NO_MSG  - No new message available
 * @return AINFC_CAN_ERROR   - RX error
 */
unsigned char AINFC_Can_RxMsgL(unsigned char Bus_ID, unsigned char Mbx,
                               unsigned char *msg_frame);




/**
 * @brief Unpacks and receives a Standard_200 CAN message from specified MB.
 *
 * This function polls the specified message buffer (MB) for a new Standard_200
 * CAN message. If a message is successfully received, it unpacks the raw data
 * into the provided structure pointed to by \c msgRx using DBC-generated functions.
 *
 * @param[in]  Bus_ID The FlexCAN instance number (e.g., 0 for CAN0).
 * @param[in]  Mbx    The RX Message Buffer index (e.g., AINFC_RX_MB0).
 * @param[in]  DCL    The Data Length Code for the expected message.
 * @param[out] msgRx  Pointer to a \c Standard_200_Rx_t structure to store
 *                    the unpacked signal values.
 */

void FlexCAN_Message_Rx_200_unpack(uint8 Bus_ID,uint8 Mbx,uint8 DCL,Standard_200_Rx_t *msgRx);

/**
 * @brief Packs and sends a Standard_100 CAN message via specified MB.
 *
 * This function takes the signal values contained in the \c TxData structure,
 * packs them into an 8-byte CAN frame using DBC-generated functions, and
 * initiates transmission via the specified message buffer (MB).
 *
 * @param[in] Bus_ID  The FlexCAN instance number (e.g., 0 for CAN0).
 * @param[in] Mbx     The TX Message Buffer index (e.g., AINFC_TX_MB0).
 * @param[in] DCL     The Data Length Code for the message to be sent.
 * @param[in] TxData  Pointer to a \c Standard_100_Tx_t structure containing
 *                    the signal values to be packed and transmitted.
 */


void FlexCAN_Message_Tx_100_pack(uint8 Bus_ID,uint8 Mbx,uint8 DCL,Standard_100_Tx_t *TxData);

/**
 * @brief 10ms periodic CAN processing (Runnable)
 *
 * Handles 2 TX and 2 RX message cyclic processing.
 * Called from TASK_M0_10MS() in Ostask_main.c.
 */

void AINFC_Can_Cyclic_10ms(void);

#ifdef __cplusplus
}
#endif

#endif /* FLEXCAN_IP_MAIN_H */
