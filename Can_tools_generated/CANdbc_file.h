/**
 * @file CANdbc_file.h
 * @brief Auto-generated CAN message pack/unpack interface
 *
 * Generated from DBC by dbc_to_c_generator.py
 * Date: 2026-04-13 16:05:12
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
    float      Vehicle_Speed;  /* unit="km/h", [-3.4e+38, 2000.0] */
    uint8_t    KL15_Status;  /* [0.0, 1.0] */
} Standard_100_Tx_t;

int Standard_100_Tx_pack(uint8_t *data, const Standard_100_Tx_t *msg, uint8_t dlc);
int Standard_100_Tx_unpack(Standard_100_Tx_t *msg, const uint8_t *data, uint8_t dlc);

/* ====================================================================== */
/* Message: Standard_100_Rx  (0x200, 1 bytes, RX, sender=SDV_Tester) */
/* ====================================================================== */

#define STANDARD_100_RX_ID          (0x200U)
#define STANDARD_100_RX_DLC         (1U)
#define STANDARD_100_RX_CYCLE_MS    (100U)

typedef struct {
    uint8_t    Test_ID_0x200;  /* [0.0, 1000.0], test */
} Standard_100_Rx_t;

int Standard_100_Rx_pack(uint8_t *data, const Standard_100_Rx_t *msg, uint8_t dlc);
int Standard_100_Rx_unpack(Standard_100_Rx_t *msg, const uint8_t *data, uint8_t dlc);

#endif /* CANDBC_FILE_H */
