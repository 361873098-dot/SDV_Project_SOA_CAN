/**
 * @file CANdbc_file.h
 * @brief Auto-generated CAN message pack/unpack interface
 *
 * Generated from DBC by dbc_to_c_generator.py (cantools-based)
 * Date: 2026-04-20 10:35:40
 *
 * DO NOT EDIT — re-run the generator to update.
 */

#ifndef CANDBC_FILE_H
#define CANDBC_FILE_H

#include <stdint.h>
#include <string.h>

/* ====================================================================== */
/* Message: Standard_100_Tx  (0x100, 1 bytes, TX, sender=SDV_M_CORE0) */
/* ====================================================================== */

#define STANDARD_100_TX_ID          (0x100U)
#define STANDARD_100_TX_DLC         (1U)
#define STANDARD_100_TX_CYCLE_MS    (100U)

#define STANDARD_100_TX_POWER_MSGSTATE_PWSM_MSG_STATE_IDLE  (0U)
#define STANDARD_100_TX_POWER_MSGSTATE_PWSM_STATE_MSG_TX_ID1  (17U)
#define STANDARD_100_TX_POWER_MSGSTATE_PWSM_STATE_MSG_RX_ID2  (18U)
#define STANDARD_100_TX_POWER_MSGSTATE_PWSM_STATE_MSG_RX_ID8  (19U)
#define STANDARD_100_TX_POWER_MSGSTATE_PWSM_STATE_MSG_TX_ID4  (20U)
#define STANDARD_100_TX_POWER_MSGSTATE_PWSM_STATE_MSG_RX_ID11  (21U)
#define STANDARD_100_TX_POWER_MSGSTATE_PWSM_MSG_STATE_NO_RESPONSE  (22U)
#define STANDARD_100_TX_POWER_MSGSTATE_PWSM_STATE_MSG_SHUTDOWN_DONE  (23U)

typedef struct {
    uint8_t    Power_Msgstate;  /* [0, 1] */
} Standard_100_Tx_t;

int Standard_100_Tx_pack(uint8_t *data, const Standard_100_Tx_t *msg, uint8_t dlc);

/* ====================================================================== */
/* Message: Standard_200_Rx  (0x200, 6 bytes, RX, sender=SDV_Tester) */
/* ====================================================================== */

#define STANDARD_200_RX_ID          (0x200U)
#define STANDARD_200_RX_DLC         (6U)
#define STANDARD_200_RX_CYCLE_MS    (100U)

#define STANDARD_200_RX_GEAR_P  (0U)
#define STANDARD_200_RX_GEAR_R  (1U)
#define STANDARD_200_RX_GEAR_N  (2U)
#define STANDARD_200_RX_GEAR_D  (3U)
#define STANDARD_200_RX_GEAR_INVALID  (4U)

typedef struct {
    uint8_t    Gear;  /* [0, 4] */
    uint8_t    Battery;  /* unit="%", [0, 100] */
    int16_t    Vehicle_Speed;  /* factor=0.01, offset=0, unit="km/h", [-327.68, 327.67] */
} Standard_200_Rx_t;

int Standard_200_Rx_unpack(Standard_200_Rx_t *msg, const uint8_t *data, uint8_t dlc);

/* ====================================================================== */
/*  Global message struct instances                                     */
/* ====================================================================== */

extern Standard_100_Tx_t g_tx_Standard_100_Tx;  /**< TX 0x100 - DLC=1 */
extern Standard_200_Rx_t g_rx_Standard_200_Rx;  /**< RX 0x200 - DLC=6 */

#endif /* CANDBC_FILE_H */
