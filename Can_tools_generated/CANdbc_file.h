/**
 * @file CANdbc_file.h
 * @brief Auto-generated CAN message pack/unpack interface
 *
 * Generated from DBC by dbc_to_c_generator.py (cantools-based)
 * Date: 2026-04-23 15:59:27
 *
 * DO NOT EDIT — re-run the generator to update.
 */

#ifndef CANDBC_FILE_H
#define CANDBC_FILE_H

#include <stdint.h>
#include <string.h>

/* ====================================================================== */
/* Message: Standard_100_Tx  (0x100, 2 bytes, TX, sender=SDV_M_CORE0) */
/* ====================================================================== */

#define STANDARD_100_TX_ID          (0x100U)
#define STANDARD_100_TX_DLC         (2U)
#define STANDARD_100_TX_CYCLE_MS    (100U)

#define STANDARD_100_TX_IGNITION_STS_OFF  (0U)
#define STANDARD_100_TX_IGNITION_STS_ON  (1U)

#define STANDARD_100_TX_VEHICLE_MODE_NORMAL  (0U)
#define STANDARD_100_TX_VEHICLE_MODE_OBD  (1U)
#define STANDARD_100_TX_VEHICLE_MODE_OTA  (2U)

typedef struct {
    uint8_t    IgnitionSts;  /* [0, 1] */
    uint8_t    VehicleMode;  /* [0, 2] */
} Standard_100_Tx_t;

int Standard_100_Tx_pack(uint8_t *data, const Standard_100_Tx_t *msg, uint8_t dlc);

/* ====================================================================== */
/* Message: Standard_200_Rx  (0x200, 8 bytes, RX, sender=SDV_Tester) */
/* ====================================================================== */

#define STANDARD_200_RX_ID          (0x200U)
#define STANDARD_200_RX_DLC         (8U)
#define STANDARD_200_RX_CYCLE_MS    (100U)

#define STANDARD_200_RX_PARKING_STS_P  (0U)
#define STANDARD_200_RX_PARKING_STS_N  (1U)
#define STANDARD_200_RX_PARKING_STS_S  (2U)
#define STANDARD_200_RX_PARKING_STS_R  (3U)
#define STANDARD_200_RX_PARKING_STS_INVALID  (4U)

#define STANDARD_200_RX_WORK_VEHICLE_MODE_ST_NORMAL  (0U)
#define STANDARD_200_RX_WORK_VEHICLE_MODE_ST_OBD  (1U)
#define STANDARD_200_RX_WORK_VEHICLE_MODE_ST_OTA  (2U)

typedef struct {
    uint8_t    ParkingSts;  /* [0, 4] */
    uint8_t    WorkVehicleModeSt;  /* [0, 2] */
    uint16_t   HighVoltageBatterySts;  /* factor=0.01, offset=0, unit="%", [0, 100] */
    uint16_t   VehicleSpeed;  /* unit="km/h", [0, 400] */
} Standard_200_Rx_t;

int Standard_200_Rx_unpack(Standard_200_Rx_t *msg, const uint8_t *data, uint8_t dlc);

/* ====================================================================== */
/*  Global message struct instances                                     */
/* ====================================================================== */

extern Standard_100_Tx_t g_tx_Standard_100_Tx;  /**< TX 0x100 - DLC=2 */
extern Standard_200_Rx_t g_rx_Standard_200_Rx;  /**< RX 0x200 - DLC=8 */

#endif /* CANDBC_FILE_H */
