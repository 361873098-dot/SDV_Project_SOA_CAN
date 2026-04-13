/**
 * @file CANdbc_file.h
 * @brief Auto-generated CAN message pack/unpack interface
 *
 * Generated from DBC by dbc_to_c_generator.py (cantools-based)
 * Date: 2026-04-13 17:13:04
 *
 * DO NOT EDIT — re-run the generator to update.
 */

#ifndef CANDBC_FILE_H
#define CANDBC_FILE_H

#include <stdint.h>
#include <string.h>

/* ====================================================================== */
/* Message: Standard_100_Tx  (0x100, 8 bytes, TX, sender=SDV_M_CORE0) */
/* ====================================================================== */

#define STANDARD_100_TX_ID          (0x100U)
#define STANDARD_100_TX_DLC         (8U)
#define STANDARD_100_TX_CYCLE_MS    (100U)

#define STANDARD_100_TX_KL15_STATUS_KL15_OFF  (0U)
#define STANDARD_100_TX_KL15_STATUS_KL15_ON  (1U)

typedef struct {
    uint8_t    KL15_Status;  /* [0, 1] */
    float      Vehicle_Speed;  /* unit="km/h", [-3.4e+38, 2000] */
} Standard_100_Tx_t;

int Standard_100_Tx_pack(uint8_t *data, const Standard_100_Tx_t *msg, uint8_t dlc);

/* ====================================================================== */
/* Message: Standard_200_Rx  (0x200, 1 bytes, RX, sender=SDV_Tester) */
/* ====================================================================== */

#define STANDARD_200_RX_ID          (0x200U)
#define STANDARD_200_RX_DLC         (1U)
#define STANDARD_200_RX_CYCLE_MS    (100U)

typedef struct {
    uint8_t    Test_ID_0x200;  /* [0, 1000], test */
} Standard_200_Rx_t;

int Standard_200_Rx_unpack(Standard_200_Rx_t *msg, const uint8_t *data, uint8_t dlc);

/* ====================================================================== */
/*  Global message struct instances                                     */
/* ====================================================================== */

extern Standard_100_Tx_t g_tx_Standard_100_Tx;  /**< TX 0x100 - DLC=8 */
extern Standard_200_Rx_t g_rx_Standard_200_Rx;  /**< RX 0x200 - DLC=1 */

#endif /* CANDBC_FILE_H */
