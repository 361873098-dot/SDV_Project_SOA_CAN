/**
 * @file CANdbc_file.c
 * @brief Auto-generated CAN message pack/unpack implementation
 *
 * Generated from DBC by dbc_to_c_generator.py
 * Date: 2026-04-13 16:05:12
 *
 * DO NOT EDIT — re-run the generator to update.
 */

#include "CANdbc_file.h"

int Standard_100_Tx_pack(uint8_t *data, const Standard_100_Tx_t *msg, uint8_t dlc)
{
    if ((data == (void *)0) || (msg == (void *)0)) { return -1; }
    if (dlc < 8U) { return -1; }

    (void)memset(data, 0, (size_t)dlc);

    /* Vehicle_Speed: start_bit=23, len=32, Motorola, float */
    {
        uint32_t raw_val;
        (void)memcpy(&raw_val, &msg->Vehicle_Speed, sizeof(uint32_t));
        data[2] = (uint8_t)(raw_val >> 24u);
        data[3] = (uint8_t)(raw_val >> 16u);
        data[4] = (uint8_t)(raw_val >> 8u);
        data[5] = (uint8_t)(raw_val >> 0u);
    }
    /* KL15_Status: start_bit=0, len=1, Motorola */
    {
        uint8_t raw_val = (uint8_t)msg->KL15_Status;
        data[0] |= (uint8_t)(((uint8_t)raw_val) & 0x01u) << 0u;
    }

    return 0;
}

int Standard_100_Tx_unpack(Standard_100_Tx_t *msg, const uint8_t *data, uint8_t dlc)
{
    if ((data == (void *)0) || (msg == (void *)0)) { return -1; }
    if (dlc < 8U) { return -1; }

    /* Vehicle_Speed */
    {
        uint32_t raw_val = 0u;
        raw_val |= ((uint32_t)data[2]) << 24u;
        raw_val |= ((uint32_t)data[3]) << 16u;
        raw_val |= ((uint32_t)data[4]) << 8u;
        raw_val |= ((uint32_t)data[5]) << 0u;
        (void)memcpy(&msg->Vehicle_Speed, &raw_val, sizeof(float));
    }
    /* KL15_Status */
    {
        uint8_t raw_val = 0u;
        raw_val |= (data[0] >> 0u) & 0x01u;
        msg->KL15_Status = (uint8_t)raw_val;
    }

    return 0;
}

int Standard_100_Rx_pack(uint8_t *data, const Standard_100_Rx_t *msg, uint8_t dlc)
{
    if ((data == (void *)0) || (msg == (void *)0)) { return -1; }
    if (dlc < 1U) { return -1; }

    (void)memset(data, 0, (size_t)dlc);

    /* Test_ID_0x200: start_bit=7, len=8, Motorola */
    {
        uint8_t raw_val = (uint8_t)msg->Test_ID_0x200;
        data[0] = (uint8_t)raw_val;
    }

    return 0;
}

int Standard_100_Rx_unpack(Standard_100_Rx_t *msg, const uint8_t *data, uint8_t dlc)
{
    if ((data == (void *)0) || (msg == (void *)0)) { return -1; }
    if (dlc < 1U) { return -1; }

    /* Test_ID_0x200 */
    {
        uint8_t raw_val = 0u;
        raw_val |= data[0];
        msg->Test_ID_0x200 = (uint8_t)raw_val;
    }

    return 0;
}
