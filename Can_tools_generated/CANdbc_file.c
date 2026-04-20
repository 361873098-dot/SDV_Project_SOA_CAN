/**
 * @file CANdbc_file.c
 * @brief Auto-generated CAN message pack/unpack implementation
 *
 * Generated from DBC by dbc_to_c_generator.py (cantools-based)
 * Date: 2026-04-20 10:35:40
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
    if (dlc < 1U) { return -1; }

    (void)memset(data, 0, (size_t)1U);

    /* Power_Msgstate: start_bit=7, len=8, Motorola */
    {
        uint8_t raw_val = (uint8_t)msg->Power_Msgstate;
        data[0] = (uint8_t)raw_val;
    }

    return 0;
}

int Standard_200_Rx_unpack(Standard_200_Rx_t *msg, const uint8_t *data, uint8_t dlc)
{
    if ((data == (void *)0) || (msg == (void *)0)) { return -1; }
    if (dlc < 6U) { return -1; }

    /* Gear */
    {
        uint8_t raw_val = 0u;
        raw_val |= data[0];
        msg->Gear = (uint8_t)raw_val;
    }
    /* Battery */
    {
        uint8_t raw_val = 0u;
        raw_val |= data[1];
        msg->Battery = (uint8_t)raw_val;
    }
    /* Vehicle_Speed */
    {
        uint16_t raw_val = 0u;
        raw_val |= (uint16_t)data[2] << 8u;
        raw_val |= (uint16_t)data[3];
        msg->Vehicle_Speed = (int16_t)((int16_t)(int16_t)raw_val * 0.01 + 0);
    }

    return 0;
}
