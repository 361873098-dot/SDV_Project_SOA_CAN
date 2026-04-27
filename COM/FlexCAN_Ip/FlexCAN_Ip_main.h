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
#include "SOA_CANdbc_Generated.h"

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
 * @brief Generic DBC RX dispatcher.
 *
 * Polls the RX mailbox (auto-resolved from MsgId) and unpacks the payload
 * using the auto-generated Standard_Rx_unpack() dispatch function from
 * SOA_CANdbc_Generated.c. Bus_ID and Mbx are resolved internally.
 *
 * @param[in]  MsgId  CAN message ID used to select the DBC unpack function
 *                     and resolve the corresponding RX mailbox.
 * @param[out] msgRx  Pointer to the target DBC structure.
 * @return AINFC_CAN_OK      - RX completed and DBC unpack succeeded
 * @return AINFC_CAN_NO_MSG  - No new message available
 * @return AINFC_CAN_ERROR   - Unsupported ID, parameter error, or unpack error
 */
unsigned char FlexCAN_Message_Rx_unpack(uint32 MsgId, void *msgRx);

/**
 * @brief Generic DBC TX dispatcher.
 *
 * Packs the DBC structure using the auto-generated Standard_Tx_pack()
 * dispatch function from SOA_CANdbc_Generated.c, then sends through the
 * TX mailbox (auto-resolved from MsgId). Bus_ID and Mbx are resolved internally.
 *
 * @param[in] MsgId  CAN message ID used to select the DBC pack function
 *                    and resolve the corresponding TX mailbox.
 * @param[in] TxData Pointer to the source DBC structure.
 * @return AINFC_CAN_OK    - TX started successfully
 * @return AINFC_CAN_BUSY  - Previous TX on this mailbox is still pending
 * @return AINFC_CAN_ERROR - Unsupported ID, parameter error, pack error, or TX error
 */
unsigned char FlexCAN_Message_Tx_pack(uint32 MsgId, const void *TxData);



/**

/**
 * @brief 10ms periodic CAN processing (Runnable)
 *
 * Handles 2 TX and 2 RX message cyclic processing.
 * Called from TASK_M0_10MS() in Ostask_main.c.
 */

void AINFC_Can_Cyclic_10ms(void);

/*==================================================================================================
 *                           CAN BUS DIAGNOSTIC TYPES & API
 *==================================================================================================*/

/**
 * @brief CAN bus fault confinement state
 *
 * Derived from FlexCAN ESR1 register FLTCONF[5:4] field:
 *   - 00b = Error Active  : Normal operation, TEC/REC < 128
 *   - 01b = Error Passive : TEC or REC >= 128, node sends passive error frames
 *   - 1xb = Bus-Off       : TEC >= 256, node is off the bus
 *
 * @note See S32G3 Reference Manual — FlexCAN ESR1 register description.
 */
typedef enum {
  AINFC_CAN_STATE_ERROR_ACTIVE  = 0U,  /**< Normal: TEC < 128 and REC < 128 */
  AINFC_CAN_STATE_ERROR_PASSIVE = 1U,  /**< Degraded: TEC or REC >= 128 */
  AINFC_CAN_STATE_BUS_OFF       = 2U,  /**< Fatal: TEC >= 256, controller is off-bus */
  AINFC_CAN_STATE_UNKNOWN       = 3U   /**< Unable to determine (should not occur) */
} AINFC_CanBusState_t;

/**
 * @brief CAN bus diagnostic information structure
 *
 * Aggregates all runtime diagnostic data from the FlexCAN controller:
 *   - Fault confinement state (Error Active / Passive / Bus-Off)
 *   - TX and RX error counters (ECR register)
 *   - Raw ESR1 register snapshot
 *   - Individual error flags decoded from ESR1
 */
typedef struct {
  /** Current fault confinement state */
  AINFC_CanBusState_t busState;

  /** TX error counter from ECR register (TXERRCNT, 0–255).
   *  Incremented on each TX error, decremented on successful TX.
   *  Bus-Off occurs when this counter reaches 256. */
  uint8               txErrorCounter;

  /** RX error counter from ECR register (RXERRCNT, 0–255).
   *  Incremented on each RX error, decremented on successful RX. */
  uint8               rxErrorCounter;

  /** Raw ESR1 register value — see RM for full bit field mapping.
   *  Useful for low-level debugging via Trace32. */
  uint32              rawEsr1;

  /* ---------- Individual error flags from ESR1 ---------- */
  boolean             bitError;       /**< Bit0 or Bit1 error detected (ESR1[15:14]) */
  boolean             ackError;       /**< ACK error — no other node acknowledged (ESR1[13]) */
  boolean             crcError;       /**< CRC error in received frame (ESR1[12]) */
  boolean             formError;      /**< Form error — fixed-form bit violation (ESR1[11]) */
  boolean             stuffError;     /**< Stuff error — bit-stuffing rule violation (ESR1[10]) */
  boolean             txWarning;      /**< TX warning — TEC >= 96 (ESR1[9]) */
  boolean             rxWarning;      /**< RX warning — REC >= 96 (ESR1[8]) */
  boolean             busOffInt;      /**< Bus-Off interrupt occurred (ESR1[2], w1c) */
} AINFC_CanDiagInfo_t;

/**
 * @brief Get CAN bus diagnostic status
 *
 * Reads the FlexCAN ESR1 register and ECR error counters, then populates
 * an AINFC_CanDiagInfo_t structure with the current bus health status.
 *
 * Typical usage in a 10ms/100ms periodic task or upon TX/RX failure:
 * @code
 *   AINFC_CanDiagInfo_t diag;
 *   FlexCAN_GetBusDiagStatus(&diag);
 *   if (diag.busState == AINFC_CAN_STATE_BUS_OFF) {
 *       // Handle bus-off recovery
 *   }
 * @endcode
 *
 * @param[out] diagInfo  Pointer to diagnostic info structure (caller-allocated)
 * @return AINFC_CAN_OK    — Diagnostic data retrieved successfully
 * @return AINFC_CAN_ERROR — NULL pointer passed
 *
 * @note This function reads registers of FlexCAN instance 0 (CAN0).
 *       The raw ESR1 value is a snapshot — error interrupt flags (w1c)
 *       are NOT cleared by this function; call FlexCAN_Ip_ClearErrorStatus()
 *       explicitly if needed.
 */
unsigned char FlexCAN_GetBusDiagStatus(AINFC_CanDiagInfo_t *diagInfo);

#ifdef __cplusplus
}
#endif

#endif /* FLEXCAN_IP_MAIN_H */
