/*!
** Copyright 2021 NXP
** @file FlexCAN_Ip_main.c
** @brief
**         FlexCAN AINFC interface module.
**         Provides AINFC_Can_TxMsg / AINFC_Can_RxMsgL and 10ms cyclic
* processing.
**         Supports 2 TX MBs and 2 RX MBs.
*/
/*!
**  @addtogroup main_module main module documentation
**  @{
*/
/* MODULE main */

#ifdef __cplusplus
extern "C" {
#endif
/*==================================================================================================
 *                                         MACRO DEFINITIONS
 *==================================================================================================*/

/*==================================================================================================
 *                                         INCLUDE FILES
 *==================================================================================================*/

#include "FlexCAN_Ip_main.h"
#include "FlexCAN_Ip.h"
#include "Mcal.h"
#include "TJA1145A_Spi_Baremetal.h"
#include "SOA_CANdbc_Generated.h"



#include <string.h>

/*==================================================================================================
 *                                         GLOBAL VARIABLES
 *==================================================================================================*/

/** TX counters (for debugging) */
volatile uint32 g_canTxCount = 0;
volatile uint32 g_canTxCount2 = 0;

/** RX counters (for debugging) */
volatile uint32 g_canRxCount = 0;
volatile uint32 g_canRxCount2 = 0;




/*==================================================================================================
 *                                         PRIVATE VARIABLES
 *==================================================================================================*/

/** TX data buffers for 2 TX channels */
//static uint8 g_txData0[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
static uint8 g_txData1[8] = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18};

//static uint8 g_RxData0[8] = {0};
static uint8 g_RxData1[8] = {0};

/** RX message buffer structures for 2 RX channels */
static Flexcan_Ip_MsgBuffType g_rxBuffer0;
static Flexcan_Ip_MsgBuffType g_rxBuffer1;

/** TX busy flags for each MB */
static volatile boolean g_txBusy[2] = {FALSE, FALSE};

/** RX configured flags for each MB */
static volatile boolean g_rxConfigured[2] = {FALSE, FALSE};

/** TX data info - standard frame, 8 bytes, polling mode */
static const Flexcan_Ip_DataInfoType g_txDataInfo = {.msg_id_type =
                                                         FLEXCAN_MSG_ID_STD,
                                                     .data_length = 8u,
                                                     .is_polling = TRUE,
                                                     .is_remote = FALSE};

/** RX data info - standard frame, 8 bytes, polling mode */
static const Flexcan_Ip_DataInfoType g_rxDataInfo = {.msg_id_type =
                                                         FLEXCAN_MSG_ID_STD,
                                                     .data_length = 8u,
                                                     .is_polling = TRUE,
                                                     .is_remote = FALSE};

/** TX CAN ID lookup table (indexed by MB: 0->MB0, 1->MB1) */
static const uint32 g_txMsgId[2] = {AINFC_TX_MSG_ID_0, AINFC_TX_MSG_ID_1};

/** RX CAN ID lookup table (indexed by RX channel: 0->MB2, 1->MB3) */
static const uint32 g_rxMsgId[2] = {AINFC_RX_MSG_ID_0, AINFC_RX_MSG_ID_1};




static unsigned char FlexCAN_GetTxMsgIdByMbx(uint8 Mbx, uint32 *msgId) {
  if (msgId == (uint32 *)0) {
    return AINFC_CAN_ERROR;
  }

  if (Mbx == AINFC_TX_MB0) {
    *msgId = g_txMsgId[0];
    return AINFC_CAN_OK;
  }

  if (Mbx == AINFC_TX_MB1) {
    *msgId = g_txMsgId[1];
    return AINFC_CAN_OK;
  }

  return AINFC_CAN_ERROR;
}

static unsigned char FlexCAN_GetRxMsgIdByMbx(uint8 Mbx, uint32 *msgId) {
  if (msgId == (uint32 *)0) {
    return AINFC_CAN_ERROR;
  }

  if (Mbx == AINFC_RX_MB0) {
    *msgId = g_rxMsgId[0];
    return AINFC_CAN_OK;
  }

  if (Mbx == AINFC_RX_MB1) {
    *msgId = g_rxMsgId[1];
    return AINFC_CAN_OK;
  }

  return AINFC_CAN_ERROR;
}

/**
 * @brief Reverse lookup: find TX MB index from CAN message ID
 */
static unsigned char FlexCAN_GetMbxByTxMsgId(uint32 msgId, uint8 *Mbx) {
  if (Mbx == (uint8 *)0) {
    return AINFC_CAN_ERROR;
  }

  if (msgId == g_txMsgId[0]) {
    *Mbx = AINFC_TX_MB0;
    return AINFC_CAN_OK;
  }

  if (msgId == g_txMsgId[1]) {
    *Mbx = AINFC_TX_MB1;
    return AINFC_CAN_OK;
  }

  return AINFC_CAN_ERROR;
}

/**
 * @brief Reverse lookup: find RX MB index from CAN message ID
 */
static unsigned char FlexCAN_GetMbxByRxMsgId(uint32 msgId, uint8 *Mbx) {
  if (Mbx == (uint8 *)0) {
    return AINFC_CAN_ERROR;
  }

  if (msgId == g_rxMsgId[0]) {
    *Mbx = AINFC_RX_MB0;
    return AINFC_CAN_OK;
  }

  if (msgId == g_rxMsgId[1]) {
    *Mbx = AINFC_RX_MB1;
    return AINFC_CAN_OK;
  }

  return AINFC_CAN_ERROR;
}

static unsigned char AINFC_Can_TxMsgById(unsigned char Bus_ID, unsigned char Mbx,
                                         uint32 msgId,
                                         const unsigned char *msg_frame) {
  Flexcan_Ip_StatusType status;

  if (msg_frame == (const unsigned char *)0) {
    return AINFC_CAN_ERROR;
  }

  if ((Mbx != AINFC_TX_MB0) && (Mbx != AINFC_TX_MB1)) {
    return AINFC_CAN_ERROR;
  }

  /* Check if previous TX on this MB completed */
  if (g_txBusy[Mbx]) {
    /* Poll for completion */
    FlexCAN_Ip_MainFunctionWrite((uint8)Bus_ID, Mbx);
    status = FlexCAN_Ip_GetTransferStatus((uint8)Bus_ID, Mbx);
    if (status == FLEXCAN_STATUS_SUCCESS) {
      g_txBusy[Mbx] = FALSE;
      if (Mbx == AINFC_TX_MB0) {
        g_canTxCount++;
      } else {
        g_canTxCount2++;
      }
    } else if (status == FLEXCAN_STATUS_ERROR) {
      g_txBusy[Mbx] = FALSE;
    } else {
      /* Still busy */
      return AINFC_CAN_BUSY;
    }
  }

  /* Send new message */
  status = FlexCAN_Ip_Send((uint8)Bus_ID, Mbx, &g_txDataInfo, msgId,
                           (const uint8 *)msg_frame);
  if (status == FLEXCAN_STATUS_SUCCESS) {
    g_txBusy[Mbx] = TRUE;
    return AINFC_CAN_OK;
  } else if (status == FLEXCAN_STATUS_BUSY) {
    return AINFC_CAN_BUSY;
  }

  return AINFC_CAN_ERROR;
}

/*==================================================================================================
 *                                         PUBLIC FUNCTIONS
 *==================================================================================================*/

/**
 * @brief Initialize FlexCAN Message Buffers for AINFC interface
 */
void FlexCAN_Process_Init(void) {
  /* Configure RX Message Buffer 0 (MB2, ID=0x200) */
  FlexCAN_Ip_ConfigRxMb(0U, AINFC_RX_MB0, &g_rxDataInfo, AINFC_RX_MSG_ID_0);
  FlexCAN_Ip_Receive(0U, AINFC_RX_MB0, &g_rxBuffer0, FALSE);
  g_rxConfigured[0] = TRUE;

  /* Configure RX Message Buffer 1 (MB3, ID=0x201) */
  FlexCAN_Ip_ConfigRxMb(0U, AINFC_RX_MB1, &g_rxDataInfo, AINFC_RX_MSG_ID_1);
  FlexCAN_Ip_Receive(0U, AINFC_RX_MB1, &g_rxBuffer1, FALSE);
  g_rxConfigured[1] = TRUE;

  /* Reset counters and flags */
  g_canTxCount = 0;
  g_canTxCount2 = 0;
  g_canRxCount = 0;
  g_canRxCount2 = 0;
  g_txBusy[0] = FALSE;
  g_txBusy[1] = FALSE;
}

/**
 * @brief Send a CAN message via specified MB
 */
unsigned char AINFC_Can_TxMsg(unsigned char Bus_ID, unsigned char Mbx,
                              unsigned char *msg_frame) {
  uint32 msgId;

  if (FlexCAN_GetTxMsgIdByMbx(Mbx, &msgId) != AINFC_CAN_OK) {
    return AINFC_CAN_ERROR;
  }

  return AINFC_Can_TxMsgById(Bus_ID, Mbx, msgId, msg_frame);
}

/**
 * @brief Receive a CAN message from specified MB
 */
unsigned char AINFC_Can_RxMsgL(unsigned char Bus_ID, unsigned char Mbx,
                               unsigned char *msg_frame) {
  Flexcan_Ip_StatusType status;
  Flexcan_Ip_MsgBuffType *pRxBuf;
  uint8 rxIdx; /* 0 or 1 for internal arrays */

  /* Map MB index to internal RX channel */
  if (Mbx == AINFC_RX_MB0) {
    rxIdx = 0U;
    pRxBuf = &g_rxBuffer0;
  } else if (Mbx == AINFC_RX_MB1) {
    rxIdx = 1U;
    pRxBuf = &g_rxBuffer1;
  } else {
    return AINFC_CAN_ERROR;
  }

  /* Check if RX is configured */
  if (!g_rxConfigured[rxIdx]) {
    return AINFC_CAN_ERROR;
  }

  /* Poll for received message */
  FlexCAN_Ip_MainFunctionRead((uint8)Bus_ID, Mbx);

  /* Check RX status */
  status = FlexCAN_Ip_GetTransferStatus((uint8)Bus_ID, Mbx);

  if (status == FLEXCAN_STATUS_SUCCESS) {
    /* Message received - copy data to output buffer */
    uint8 i;
    uint8 len = pRxBuf->dataLen;
    if (len > 8U) {
      len = 8U;
    }
    for (i = 0; i < len; i++) {
      msg_frame[i] = pRxBuf->data[i];
    }
    /* Zero-fill remaining bytes */
    for (; i < 8U; i++) {
      msg_frame[i] = 0U;
    }

    /* Update counter */
    if (rxIdx == 0U) {
      g_canRxCount++;
    } else {
      g_canRxCount2++;
    }

    /* Re-configure RX for next message */
    FlexCAN_Ip_Receive((uint8)Bus_ID, Mbx, pRxBuf, FALSE);

    return AINFC_CAN_OK;
  } else if (status == FLEXCAN_STATUS_ERROR) {
    /* RX error - re-configure */
    FlexCAN_Ip_Receive((uint8)Bus_ID, Mbx, pRxBuf, FALSE);
    return AINFC_CAN_ERROR;
  }

  /* FLEXCAN_STATUS_BUSY - no new message */
  return AINFC_CAN_NO_MSG;
}

unsigned char FlexCAN_Message_Rx_unpack(uint32 MsgId, void *msgRx) {
  uint8 rxStatus;
  uint8 msg_frame[8] = {0U};
  uint8 Mbx;

  if (msgRx == (void *)0) {
    return AINFC_CAN_ERROR;
  }

  /* Reverse lookup: find the MB index from the CAN message ID */
  if (FlexCAN_GetMbxByRxMsgId(MsgId, &Mbx) != AINFC_CAN_OK) {
    return AINFC_CAN_ERROR;
  }

  rxStatus = AINFC_Can_RxMsgL(0U, Mbx, msg_frame);
  if (rxStatus != AINFC_CAN_OK) {
    return rxStatus;
  }

  /* Delegate to auto-generated unified unpack */
  if (Standard_Rx_unpack((uint32_t)MsgId, msgRx,
                          (const uint8_t *)msg_frame, (uint8_t)8U) != 0) {
    return AINFC_CAN_ERROR;
  }

  return AINFC_CAN_OK;
}



unsigned char FlexCAN_Message_Tx_pack(uint32 MsgId, const void *TxData) {
  uint8 txMsg[8] = {0U};
  uint8 Mbx;

  if (TxData == (const void *)0) {
    return AINFC_CAN_ERROR;
  }

  /* Reverse lookup: find the MB index from the CAN message ID */
  if (FlexCAN_GetMbxByTxMsgId(MsgId, &Mbx) != AINFC_CAN_OK) {
    return AINFC_CAN_ERROR;
  }

  /* Delegate to auto-generated unified pack */
  if (Standard_Tx_pack((uint32_t)MsgId, (uint8_t *)txMsg,
                        TxData, (uint8_t)8U) != 0) {
    return AINFC_CAN_ERROR;
  }

  return AINFC_Can_TxMsgById(0U, Mbx, MsgId, txMsg);
}




/**
 * @brief 10ms periodic CAN processing
 *
 * Handles 2 TX and 2 RX message cyclic processing.
 */
void AINFC_Can_Cyclic_10ms(void) {

  /* ====================================================================
   * RX Processing - poll both RX MBs
   * ==================================================================== */

  (void)FlexCAN_Message_Rx_unpack(STANDARD_200_RX_ID, &g_rx_Standard_200_Rx);
  

  /* RX MB1 (ID=0x201) — no DBC mapping */
  (void)AINFC_Can_RxMsgL(0U, AINFC_RX_MB1, g_RxData1);

  /* ====================================================================
   * TX Processing - send on both TX MBs
   * ==================================================================== */

  /* TX MB0 (ID=0x100, DBC: Standard_100_Tx)
   * Application layer sets physical values in g_tx_Standard_100_Tx
   * before this function is called, e.g.:
   *   g_tx_Standard_100_Tx.IgnitionSts = 1u;
   *   g_tx_Standard_100_Tx.VehicleMode = 0u;
   */
  //Standard_100_Tx_pack(g_txData0, &g_tx_Standard_100_Tx, STANDARD_100_TX_DLC);
  //(void)AINFC_Can_TxMsg(0U, AINFC_TX_MB0, g_txData0);

  (void)FlexCAN_Message_Tx_pack(STANDARD_100_TX_ID, &g_tx_Standard_100_Tx);



  

  /* TX MB1 (ID=0x101) — no DBC mapping */
  g_txData1[0] = (uint8)(g_canTxCount2 & 0xFFU);
  g_txData1[1] = (uint8)((g_canTxCount2 >> 8) & 0xFFU);
  (void)AINFC_Can_TxMsg(0U, AINFC_TX_MB1, g_txData1);
}

/*==================================================================================================
 *  CAN BUS DIAGNOSTIC IMPLEMENTATION
 *==================================================================================================*/

/*
 * ESR1 register bit masks (from S32G3 Reference Manual, FlexCAN chapter).
 * Defined locally to avoid dependency on internal RTD header FlexCAN_Ip_HwAccess.h.
 *
 *  ESR1 bit layout (relevant fields):
 *  ┌────────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┐
 *  │ Bit 15 │  14   │  13   │  12   │  11   │  10   │   9   │   8   │
 *  │BIT1ERR │BIT0ERR│ACKERR │CRCERR │FRMERR │STFERR │ TXWRN │ RXWRN │
 *  ├────────┼───────┼───────┼───────┼───────┼───────┼───────┼───────┤
 *  │  Bit 7 │   6   │  5:4  │   3   │   2   │   1   │   0   │
 *  │  IDLE  │  TX   │FLTCONF│  RX   │BOFFINT│ERRINT │ SYNCH │
 *  └────────┴───────┴───────┴───────┴───────┴───────┴───────┘
 *
 *  FLTCONF[5:4] — Fault Confinement State:
 *    00b = Error Active  (normal operation)
 *    01b = Error Passive (degraded — TEC or REC >= 128)
 *    1xb = Bus-Off       (fatal — TEC reached 256)
 */
#define AINFC_ESR1_BIT1ERR_MASK   (0x00008000U)  /* Bit 15 */
#define AINFC_ESR1_BIT0ERR_MASK   (0x00004000U)  /* Bit 14 */
#define AINFC_ESR1_ACKERR_MASK    (0x00002000U)  /* Bit 13 */
#define AINFC_ESR1_CRCERR_MASK    (0x00001000U)  /* Bit 12 */
#define AINFC_ESR1_FRMERR_MASK    (0x00000800U)  /* Bit 11 */
#define AINFC_ESR1_STFERR_MASK    (0x00000400U)  /* Bit 10 */
#define AINFC_ESR1_TXWRN_MASK     (0x00000200U)  /* Bit 9  */
#define AINFC_ESR1_RXWRN_MASK     (0x00000100U)  /* Bit 8  */
#define AINFC_ESR1_FLTCONF_MASK   (0x00000030U)  /* Bits [5:4] */
#define AINFC_ESR1_FLTCONF_SHIFT  (4U)
#define AINFC_ESR1_BOFFINT_MASK   (0x00000004U)  /* Bit 2  */

/** FLTCONF decoded values */
#define AINFC_FLTCONF_ERROR_ACTIVE   (0U)  /* 00b */
#define AINFC_FLTCONF_ERROR_PASSIVE  (1U)  /* 01b */
/* 10b or 11b = Bus-Off (bit 5 set) */

unsigned char FlexCAN_GetBusDiagStatus(AINFC_CanDiagInfo_t *diagInfo) {
  uint32 esr1;
  uint32 fltconf;

  if (diagInfo == (AINFC_CanDiagInfo_t *)0) {
    return AINFC_CAN_ERROR;
  }

  /* ---- Read ESR1 register (snapshot) ---- */
  esr1 = FlexCAN_Ip_GetErrorStatus(0U);
  diagInfo->rawEsr1 = esr1;

  /* ---- Read TX / RX error counters from ECR register ---- */
  diagInfo->txErrorCounter = FlexCAN_Ip_GetControllerTxErrorCounter(0U);
  diagInfo->rxErrorCounter = FlexCAN_Ip_GetControllerRxErrorCounter(0U);

  /* ---- Decode FLTCONF[5:4] — Fault Confinement State ---- */
  fltconf = (esr1 & AINFC_ESR1_FLTCONF_MASK) >> AINFC_ESR1_FLTCONF_SHIFT;
  if (fltconf == AINFC_FLTCONF_ERROR_ACTIVE) {
    diagInfo->busState = AINFC_CAN_STATE_ERROR_ACTIVE;
  } else if (fltconf == AINFC_FLTCONF_ERROR_PASSIVE) {
    diagInfo->busState = AINFC_CAN_STATE_ERROR_PASSIVE;
  } else {
    /* fltconf == 2 or 3 (bit 5 set) => Bus-Off */
    diagInfo->busState = AINFC_CAN_STATE_BUS_OFF;
  }

  /* ---- Decode individual error flags ---- */
  diagInfo->bitError   = ((esr1 & (AINFC_ESR1_BIT1ERR_MASK | AINFC_ESR1_BIT0ERR_MASK)) != 0U)
                             ? TRUE : FALSE;
  diagInfo->ackError   = ((esr1 & AINFC_ESR1_ACKERR_MASK)  != 0U) ? TRUE : FALSE;
  diagInfo->crcError   = ((esr1 & AINFC_ESR1_CRCERR_MASK)  != 0U) ? TRUE : FALSE;
  diagInfo->formError  = ((esr1 & AINFC_ESR1_FRMERR_MASK)  != 0U) ? TRUE : FALSE;
  diagInfo->stuffError = ((esr1 & AINFC_ESR1_STFERR_MASK)  != 0U) ? TRUE : FALSE;
  diagInfo->txWarning  = ((esr1 & AINFC_ESR1_TXWRN_MASK)   != 0U) ? TRUE : FALSE;
  diagInfo->rxWarning  = ((esr1 & AINFC_ESR1_RXWRN_MASK)   != 0U) ? TRUE : FALSE;
  diagInfo->busOffInt  = ((esr1 & AINFC_ESR1_BOFFINT_MASK)  != 0U) ? TRUE : FALSE;

  return AINFC_CAN_OK;
}

#ifdef __cplusplus
}
#endif

/* END FlexCAN_Ip_main */
/*!
** @}
*/
