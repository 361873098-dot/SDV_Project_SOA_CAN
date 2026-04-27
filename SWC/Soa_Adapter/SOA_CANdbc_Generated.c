/**
 * @file SOA_CANdbc_Generated.c
 * @brief Auto-generated CAN message pack/unpack implementation
 *
 * Generated from DBC by dbc_to_c_generator.py (cantools-based)
 * Date: 2026-04-24 17:53:02
 *
 * DO NOT EDIT — re-run the generator to update.
 */

#include "SOA_CANdbc_Generated.h"

/* ====================================================================== */
/*  Global message struct instances                                     */
/* ====================================================================== */
Standard_100_Tx_t g_tx_Standard_100_Tx = {0};
Standard_200_Rx_t g_rx_Standard_200_Rx = {0};

/* ====================================================================== */
/*  Per-message pack/unpack (static helpers)                            */
/* ====================================================================== */

static int Standard_100_Tx_pack(uint8_t *data, const Standard_100_Tx_t *msg, uint8_t dlc)
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

static int Standard_200_Rx_unpack(Standard_200_Rx_t *msg, const uint8_t *data, uint8_t dlc)
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

/* ====================================================================== */
/*  Adapter wrappers (void* interface for dispatch table)               */
/* ====================================================================== */

static int Dbc_Standard_100_Tx_PackAdapter(uint8_t *data, const void *msg, uint8_t dlc)
{
    return Standard_100_Tx_pack(data, (const Standard_100_Tx_t *)msg, dlc);
}

static int Dbc_Standard_200_Rx_UnpackAdapter(void *msg, const uint8_t *data, uint8_t dlc)
{
    return Standard_200_Rx_unpack((Standard_200_Rx_t *)msg, data, dlc);
}

/* ====================================================================== */
/*  Auto-generated dispatch table                                      */
/* ====================================================================== */

const Dbc_MessageHandlerType g_dbcMessageTable[DBC_MESSAGE_TABLE_COUNT] = {
    { STANDARD_100_TX_ID, STANDARD_100_TX_DLC, Dbc_Standard_100_Tx_PackAdapter, (Dbc_UnpackFuncType)0 },
    { STANDARD_200_RX_ID, STANDARD_200_RX_DLC, (Dbc_PackFuncType)0, Dbc_Standard_200_Rx_UnpackAdapter }
};

/* ====================================================================== */
/*  Unified dispatch functions                                          */
/* ====================================================================== */

int Standard_Tx_pack(uint32_t CAN_ID, uint8_t *data, const void *msg, uint8_t dlc)
{
    uint32_t idx;

    if ((data == (void *)0) || (msg == (void *)0)) { return -1; }

    for (idx = 0U; idx < DBC_MESSAGE_TABLE_COUNT; idx++)
    {
        if (g_dbcMessageTable[idx].msgId == CAN_ID)
        {
            if (g_dbcMessageTable[idx].pack != (Dbc_PackFuncType)0)
            {
                return g_dbcMessageTable[idx].pack(data, msg, dlc);
            }
            return -1;  /* ID found but no pack function */
        }
    }

    return -1;  /* CAN_ID not found */
}

int Standard_Rx_unpack(uint32_t CAN_ID, void *msg, const uint8_t *data, uint8_t dlc)
{
    uint32_t idx;

    if ((data == (void *)0) || (msg == (void *)0)) { return -1; }

    for (idx = 0U; idx < DBC_MESSAGE_TABLE_COUNT; idx++)
    {
        if (g_dbcMessageTable[idx].msgId == CAN_ID)
        {
            if (g_dbcMessageTable[idx].unpack != (Dbc_UnpackFuncType)0)
            {
                return g_dbcMessageTable[idx].unpack(msg, data, dlc);
            }
            return -1;  /* ID found but no unpack function */
        }
    }

    return -1;  /* CAN_ID not found */
}
