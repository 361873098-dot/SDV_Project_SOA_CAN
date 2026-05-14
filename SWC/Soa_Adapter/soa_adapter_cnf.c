/**
 * @file soa_adapter_cnf.c
 * @brief SOA Adapter Configuration — Signal Read/Write Functions and Service Table
 *
 * Implements CAN signal read/write functions that bridge SOA service
 * parameters to/from the DBC global variables. Also instantiates the
 * SOA service configuration table derived from the service matrix.
 *
 * Signal Mapping:
 *   Notifier  DriSpeedSt      → g_rx_Standard_200_Rx.VehicleSpeed       (RX 0x200)
 *   Notifier  VehicleModeSt   -> g_tx_Standard_100_Tx.VehicleMode       (TX 0x100)
 *   Getter    ParkingSts      → g_rx_Standard_200_Rx.ParkingSts          (RX 0x200)
 *   Getter    HVBatterySts    → g_rx_Standard_200_Rx.HighVoltageBatterySts (RX 0x200)
 *   Getter    IgnitionSts     → g_tx_Standard_100_Tx.IgnitionSts         (TX 0x100)
 *   Setter    VehicleMode     → g_tx_Standard_100_Tx.VehicleMode         (TX 0x100)
 */

#if defined(__cplusplus)
extern "C"{
#endif

/*==================================================================================================
 *                                         INCLUDE FILES
 *==================================================================================================*/

#include "soa_adapter_cnf.h"
#include "CANdbc_Generated.h"   /* DBC global structs */
#include "pwsm_cnf.h"

/*==================================================================================================
 *                              SIGNAL READ FUNCTIONS (Notifier + Getter)
 *
 * Each function reads the current value from the DBC global variable,
 * serializes it into big-endian byte order, and returns the byte count.
 *==================================================================================================*/

/**
 * @brief Read VehicleSpeed signal (Notifier: Atom_VCU_DriSpeedSt)
 *
 * Source: g_rx_Standard_200_Rx.VehicleSpeed (uint16, 0-400 km/h)
 * Serialized as 2 bytes, big-endian.
 */
uint16 SOA_ReadVehicleSpeed(uint8 *outBuf, uint16 maxLen)
{
    if ((outBuf == NULL_PTR) || (maxLen < 2U))
    {
        return 0U;
    }
    uint16_t speed = Can_Get_Rx_signal_VehicleSpeed();
    outBuf[0] = (uint8)(speed >> 8U);
    outBuf[1] = (uint8)(speed & 0xFFU);
    return 2U;
}

/**
 * @brief Read VehicleMode signal (Notifier: Atom_BCM_VehicleMode)
 *
 * Reads the value set by Entry [4] Setter (Atom_BCM_VehicleMode).
 * Source: g_tx_Standard_100_Tx.VehicleMode (uint8, 0-2)
 * Serialized as 1 byte.
 */
uint16 SOA_ReadWorkVehicleMode(uint8 *outBuf, uint16 maxLen)
{
    if ((outBuf == NULL_PTR) || (maxLen < 1U))
    {
        return 0U;
    }
    outBuf[0] = (uint8)Pwsm_TstVehicleMode(NULL);
	
    return 1U;
}

/**
 * @brief Read ParkingSts signal (Getter: Atom_VCU_ParkingSt)
 *
 * Source: g_rx_Standard_200_Rx.ParkingSts (uint8, 0-4)
 * Serialized as 1 byte.
 */
uint16 SOA_ReadParkingSts(uint8 *outBuf, uint16 maxLen)
{
    if ((outBuf == NULL_PTR) || (maxLen < 1U))
    {
        return 0U;
    }
    outBuf[0] = (uint8)Can_Get_Rx_signal_ParkingSts();
    return 1U;
}

/**
 * @brief Read HighVoltageBatterySts signal (Getter: Atom_VCU_HighVoltageBatterySt)
 *
 * Source: g_rx_Standard_200_Rx.HighVoltageBatterySts (uint16, factor=0.01, 0-100%)
 * Serialized as 2 bytes, big-endian (raw uint16 value).
 */
uint16 SOA_ReadHighVoltageBatterySts(uint8 *outBuf, uint16 maxLen)
{
    if ((outBuf == NULL_PTR) || (maxLen < 2U))
    {
        return 0U;
    }
    uint16_t hvBatt = Can_Get_Rx_signal_HighVoltageBatterySts();
    outBuf[0] = (uint8)(hvBatt >> 8U);
    outBuf[1] = (uint8)(hvBatt & 0xFFU);
    return 2U;
}

/**
 * @brief Read IgnitionSts signal (Getter: Atom_VCU_IgnitionSt)
 *
 * Source: g_tx_Standard_100_Tx.IgnitionSts (uint8, 0-1)
 * Serialized as 1 byte.
 */
uint16 SOA_ReadIgnitionSts(uint8 *outBuf, uint16 maxLen)
{
    if ((outBuf == NULL_PTR) || (maxLen < 1U))
    {
        return 0U;
    }
    outBuf[0] = (uint8)Can_Get_Rx_signal_IgnitionSts();
    return 1U;
}

/*==================================================================================================
 *                              SIGNAL WRITE FUNCTION (Setter)
 *==================================================================================================*/

/**
 * @brief Write VehicleMode signal (Setter: Atom_BCM_VehicleMode)
 *
 * Target: g_tx_Standard_100_Tx.VehicleMode (uint8, 0-2)
 * Expects 1 byte input.
 */
uint8 SOA_WriteVehicleMode(const uint8 *inBuf, uint16 len)
{
    if ((inBuf == NULL_PTR) || (len < 1U))
    {
        return 1U;  /* Error */
    }
    uint8 mode = inBuf[0];
    Pwsm_TstVehicleMode(&mode);

   
    return 0U;  /* Success */
}


/*==================================================================================================
 *                              SOA SERVICE CONFIGURATION TABLE
 *==================================================================================================*/

/**
 * @brief SOA service configuration table
 *
 * Index order matches SOA_SVC_IDX_xxx defines in soa_adapter_cnf.h.
 */
const SOA_ServiceConfig_t g_soaServiceTable[SOA_SERVICE_TABLE_COUNT] =
{
    /* [0] Atom_VCU_DriSpeedSt — Notifier: VehicleSpeed (M→A) */
    {
        .SOA_ServiceID      = SOA_SID_VCU_DRI_SPEED,       /* 0x0001 */
        .SOA_MethodID       = SOA_MID_DRI_SPEED_NOTIF,     /* 0x8001 */
        .SOA_InstanceID     = SOA_INSTANCE_ID_DEFAULT,      /* 0x0001 */
        .serviceType        = SOA_SERVICE_NOTIFIER,
        .readFunc           = SOA_ReadVehicleSpeed,
        .writeFunc          = NULL_PTR,
        .SOA_EventGroupID   = SOA_EG_DRI_SPEED_SWTS,       /* 0x0001 */
        .dataSize           = 2U,
        .hasLinkedNotifier  = FALSE,
        .linkedNotifierIdx  = 0U
    },

    /* [1] Atom_VCU_ParkingSt — Getter: ParkingSts (A→M→A) */
    {
        .SOA_ServiceID      = SOA_SID_VCU_PARKING,          /* 0x0002 */
        .SOA_MethodID       = SOA_MID_PARKING_GETTER,       /* 0x5001 */
        .SOA_InstanceID     = SOA_INSTANCE_ID_DEFAULT,      /* 0x0001 */
        .serviceType        = SOA_SERVICE_GETTER,
        .readFunc           = SOA_ReadParkingSts,
        .writeFunc          = NULL_PTR,
        .SOA_EventGroupID   = 0U,
        .dataSize           = 1U,
        .hasLinkedNotifier  = FALSE,
        .linkedNotifierIdx  = 0U
    },

    /* [2] Atom_VCU_HighVoltageBatterySt — Getter: HighVoltageBatterySts (A→M→A) */
    {
        .SOA_ServiceID      = SOA_SID_VCU_HV_BATTERY,      /* 0x0003 */
        .SOA_MethodID       = SOA_MID_HV_BATT_GETTER,      /* 0x5001 */
        .SOA_InstanceID     = SOA_INSTANCE_ID_DEFAULT,      /* 0x0001 */
        .serviceType        = SOA_SERVICE_GETTER,
        .readFunc           = SOA_ReadHighVoltageBatterySts,
        .writeFunc          = NULL_PTR,
        .SOA_EventGroupID   = 0U,
        .dataSize           = 2U,
        .hasLinkedNotifier  = FALSE,
        .linkedNotifierIdx  = 0U
    },

    /* [3] Atom_VCU_IgnitionSt — Getter: IgnitionSts (A→M→A) */
    {
        .SOA_ServiceID      = SOA_SID_VCU_IGNITION,         /* 0x0004 */
        .SOA_MethodID       = SOA_MID_IGNITION_GETTER,      /* 0x5001 */
        .SOA_InstanceID     = SOA_INSTANCE_ID_DEFAULT,       /* 0x0001 */
        .serviceType        = SOA_SERVICE_GETTER,
        .readFunc           = SOA_ReadIgnitionSts,
        .writeFunc          = NULL_PTR,
        .SOA_EventGroupID   = 0U,
        .dataSize           = 1U,
        .hasLinkedNotifier  = FALSE,
        .linkedNotifierIdx  = 0U
    },

    /* [4] Atom_BCM_VehicleMode — Setter: VehicleMode (A→M) */
    {
        .SOA_ServiceID      = SOA_SID_BCM_VEHICLE_MODE,     /* 0x0005 */
        .SOA_MethodID       = SOA_MID_VEHICLE_MODE_SETTER,  /* 0x5001 */
        .SOA_InstanceID     = SOA_INSTANCE_ID_DEFAULT,       /* 0x0001 */
        .serviceType        = SOA_SERVICE_SETTER,
        .readFunc           = NULL_PTR,                      /* Setter: no readFunc, uses linked Notifier [5] */
        .writeFunc          = SOA_WriteVehicleMode,
        .SOA_EventGroupID   = 0U,
        .dataSize           = 1U,
        .hasLinkedNotifier  = TRUE,
        .linkedNotifierIdx  = SOA_SVC_IDX_VEHICLE_MODE_NOTIF  /* Index 5 */
    },

    /* [5] Atom_BCM_VehicleModeSt -- Notifier: VehicleMode from TX (M->A) */
    {
        .SOA_ServiceID      = SOA_SID_BCM_VEHICLE_MODE,     /* 0x0005 */
        .SOA_MethodID       = SOA_MID_VEHICLE_MODE_NOTIF,   /* 0x8001 */
        .SOA_InstanceID     = SOA_INSTANCE_ID_DEFAULT,       /* 0x0001 */
        .serviceType        = SOA_SERVICE_NOTIFIER,
        .readFunc           = SOA_ReadWorkVehicleMode,
        .writeFunc          = NULL_PTR,
        .SOA_EventGroupID   = SOA_EG_VEHICLE_MODE_STS,      /* 0x0001 */
        .dataSize           = 1U,
        .hasLinkedNotifier  = FALSE,
        .linkedNotifierIdx  = 0U
    }
};

/**
 * @brief Notifier index mapping
 *
 * Lists indices into g_soaServiceTable for all Notifier-type services.
 * Used for initial value sync and change detection.
 */
const uint8 g_soaNotifierIndices[SOA_NOTIFIER_COUNT] =
{
    SOA_SVC_IDX_DRI_SPEED_NOTIF,    /* [0] → g_soaServiceTable[0] */
    SOA_SVC_IDX_VEHICLE_MODE_NOTIF  /* [1] → g_soaServiceTable[5] */
};

#if defined(__cplusplus)
}
#endif
