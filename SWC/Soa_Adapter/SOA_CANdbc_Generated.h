/**
 * @file SOA_CANdbc_Generated.h
 * @brief Auto-generated CAN message pack/unpack interface
 *
 * Generated from DBC by dbc_to_c_generator.py (cantools-based)
 * Date: 2026-04-24 17:53:02
 *
 * DO NOT EDIT — re-run the generator to update.
 */

#ifndef SOA_CANDBC_GENERATED_H
#define SOA_CANDBC_GENERATED_H

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

/* ====================================================================== */
/*  DBC dispatch table & unified pack/unpack API                        */
/* ====================================================================== */

/** Function-pointer types used by the dispatch table */
typedef int (*Dbc_PackFuncType)(uint8_t *data, const void *msg, uint8_t dlc);
typedef int (*Dbc_UnpackFuncType)(void *msg, const uint8_t *data, uint8_t dlc);

/** One entry in the auto-generated DBC dispatch table */
typedef struct {
    uint32_t  msgId;    /**< CAN message ID */
    uint8_t   dlc;      /**< Message DLC */
    Dbc_PackFuncType   pack;    /**< NULL if RX-only */
    Dbc_UnpackFuncType unpack;  /**< NULL if TX-only */
} Dbc_MessageHandlerType;

/** Number of entries in the dispatch table */
#define DBC_MESSAGE_TABLE_COUNT  (2U)

/** Auto-generated dispatch table (defined in SOA_CANdbc_Generated.c) */
extern const Dbc_MessageHandlerType g_dbcMessageTable[DBC_MESSAGE_TABLE_COUNT];

/**
 * @brief Unified TX pack function — dispatches by CAN_ID
 *
 * @param[in]  CAN_ID  CAN message ID (e.g. 0x100U)
 * @param[out] data    Raw CAN data bytes (caller-allocated)
 * @param[in]  msg     Pointer to the typed DBC struct
 * @param[in]  dlc     Data length code
 * @return  0 on success, -1 on error
 */
int Standard_Tx_pack(uint32_t CAN_ID, uint8_t *data, const void *msg, uint8_t dlc);

/**
 * @brief Unified RX unpack function — dispatches by CAN_ID
 *
 * @param[in]  CAN_ID  CAN message ID (e.g. 0x200U)
 * @param[out] msg     Pointer to the typed DBC struct
 * @param[in]  data    Raw CAN data bytes
 * @param[in]  dlc     Data length code
 * @return  0 on success, -1 on error
 */
int Standard_Rx_unpack(uint32_t CAN_ID, void *msg, const uint8_t *data, uint8_t dlc);

/* ====================================================================== */
/*  Global message struct instances                                     */
/* ====================================================================== */

extern Standard_100_Tx_t g_tx_Standard_100_Tx;  /**< TX 0x100 - DLC=2 */
extern Standard_200_Rx_t g_rx_Standard_200_Rx;  /**< RX 0x200 - DLC=8 */

#endif /* SOA_CANDBC_GENERATED_H */
