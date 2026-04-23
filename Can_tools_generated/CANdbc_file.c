/**
 * @file CANdbc_file.c
 * @brief Auto-generated CAN message pack/unpack implementation
 *
 * Generated from DBC by dbc_to_c_generator.py (cantools-based)
 * Date: 2026-04-23 15:59:27
 *
 * DO NOT EDIT — re-run the generator to update.
 */

#include "CANdbc_file.h"

/* ====================================================================== */
/*  Global message struct instances                                     */
/* ====================================================================== */
Standard_100_Tx_t g_tx_Standard_100_Tx = {0};
Standard_200_Rx_t g_rx_Standard_200_Rx = {0};

int Standard_100_Tx_pack(uint8_t *data, const Standard_100_Tx_t *msg, uint8_t dlc)
{
    if ((data == (void *)0) || (msg == (void *)0)) { return -1; }
    if (dlc < 2U) { return -1; }

    (void)memset(data, 0, (size_t)2U);

    /* IgnitionSts: start_bit=7, len=8, Motorola */
    {
        uint8_t raw_val = (uint8_t)msg->IgnitionSts;
        data[0] = (uint8_t)raw_val;
    }
    /* VehicleMode: start_bit=15, len=8, Motorola */
    {
        uint8_t raw_val = (uint8_t)msg->VehicleMode;
        data[1] = (uint8_t)raw_val;
    }

    return 0;
}

int Standard_200_Rx_unpack(Standard_200_Rx_t *msg, const uint8_t *data, uint8_t dlc)
{
    if ((data == (void *)0) || (msg == (void *)0)) { return -1; }
    if (dlc < 8U) { return -1; }

    /* ParkingSts */
    {
        uint8_t raw_val = 0u;
        raw_val |= data[0];
        msg->ParkingSts = (uint8_t)raw_val;
    }
    /* WorkVehicleModeSt */
    {
        uint8_t raw_val = 0u;
        raw_val |= data[1];
        msg->WorkVehicleModeSt = (uint8_t)raw_val;
    }
    /* HighVoltageBatterySts */
    {
        uint16_t raw_val = 0u;
        raw_val |= (uint16_t)data[2] << 8u;
        raw_val |= (uint16_t)data[3];
        msg->HighVoltageBatterySts = (uint16_t)(raw_val * 0.01 + 0);
    }
    /* VehicleSpeed */
    {
        uint16_t raw_val = 0u;
        raw_val |= (uint16_t)data[4] << 8u;
        raw_val |= (uint16_t)data[5];
        msg->VehicleSpeed = (uint16_t)raw_val;
    }

    return 0;
}
