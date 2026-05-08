/**
 * @file soa_adapter_cnf.h
 * @brief SOA Adapter Configuration — Service Matrix Definitions
 *
 * Defines SOA protocol header, service types, and the service configuration
 * table derived from the SOA service matrix. All SOA-layer fields use
 * the SOA_ prefix to distinguish from IPCF-layer protocol fields.
 *
 * Data flow:
 *   IPCF 8B Header | SOA 12B Header | SOA Payload (actual parameters)
 *   ^^^^^^^^^^^^^^^   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *   Handled by PICC   Handled by SOA Adapter (this module)
 */

#ifndef SOA_ADAPTER_CNF_H
#define SOA_ADAPTER_CNF_H

#if defined(__cplusplus)
extern "C"{
#endif

/*==================================================================================================
 *                                         INCLUDE FILES
 *==================================================================================================*/

#include "Platform.h"

/*==================================================================================================
 *                                  IPC-LAYER FIXED ID MAPPING
 *
 * Per soa.md specification:
 *   - All Notifier/Event use fixed IPC EventID = 3
 *   - All Getter/Setter/R-R Method use fixed IPC MethodID = 1
 *==================================================================================================*/

/** IPC-layer EventID for all SOA Notifier/Event messages (M->A) */
#define SOA_IPC_EVENT_ID_FOR_NOTIF      (3U)

/** IPC-layer MethodID for all SOA Getter/Setter/Method messages (A->M->A) */
#define SOA_IPC_METHOD_ID_FOR_RR        (1U)

/*==================================================================================================
 *                                  SOA PICC CONFIGURATION
 *==================================================================================================*/

/** M-Core SOA Provider ID (from ID range 71-80) */
#define SOA_PROVIDER_ID                 (71U)   /* 0x47 */

/** A-Core SOA Consumer ID (from ID range 71-80) */
#define SOA_CONSUMER_ID                 (76U)   /* 0x4C */

/** IPCF channel for SOA communication */
#define SOA_CHANNEL_ID                  (2U)

/*==================================================================================================
 *                                  SOA PROTOCOL HEADER (12 Bytes)
 *
 * Sits inside the IPCF Payload. All fields are Big-Endian (network byte order).
 * Note: The SOA header is 12 bytes (6 x 2-byte fields), NOT including
 *       the variable-length SOA Payload that follows.
 *==================================================================================================*/

/** SOA header size in bytes (ServiceID + MethodID + InstanceID + SessionID + ReturnCode + Length) */
#define SOA_HEADER_SIZE                 (12U)

/** Maximum SOA Payload data size (single message) */
#define SOA_MAX_DATA_SIZE               (256U)

/** Maximum total SOA message size (header + data) */
#define SOA_MAX_MSG_SIZE                (SOA_HEADER_SIZE + SOA_MAX_DATA_SIZE)

/**
 * @brief SOA Protocol Header (12 bytes, Big-Endian)
 *
 * All 2-byte fields are stored in Big-Endian byte order when transmitted.
 * Use SOA_BuildHeader() / SOA_ParseHeader() for serialization.
 *
 * Layout inside IPCF Payload:
 *   [0-1]  SOA_ServiceID   — AP Service ID
 *   [2-3]  SOA_MethodID    — AP Method/Event ID
 *   [4-5]  SOA_InstanceID  — AP Instance ID
 *   [6-7]  SOA_SessionID   — Session ID (0 for Event/Notifier)
 *   [8-9]  SOA_ReturnCode  — Return code (0=OK)
 *   [10-11] SOA_Length      — Actual parameter data length
 */
typedef struct {
    uint16  SOA_ServiceID;      /**< AP Service ID (from service matrix) */
    uint16  SOA_MethodID;       /**< AP Method/Event ID (from service matrix) */
    uint16  SOA_InstanceID;     /**< AP Instance ID (from service matrix) */
    uint16  SOA_SessionID;      /**< Session ID (0 for Notifier/Event, echoed for Getter/Setter) */
    uint16  SOA_ReturnCode;     /**< Return code: 0=success, non-0=failure */
    uint16  SOA_Length;         /**< Length of actual parameter data following this header */
} SOA_Header_t;

/*==================================================================================================
 *                                  SOA SERVICE TYPE ENUMERATION
 *==================================================================================================*/

/**
 * @brief SOA service type
 */
typedef enum {
    SOA_SERVICE_NOTIFIER = 0U,  /**< Field Notifier (M->A, Event, update-on-changed) */
    SOA_SERVICE_GETTER   = 1U,  /**< Field Getter (A requests, M responds with current value) */
    SOA_SERVICE_SETTER   = 2U   /**< Field Setter (A requests write, M writes and responds) */
} SOA_ServiceType_e;

/*==================================================================================================
 *                                  SIGNAL READ/WRITE FUNCTION TYPES
 *==================================================================================================*/

/**
 * @brief Read a CAN signal value into a buffer
 *
 * @param[out] outBuf   Buffer to write the signal value into (serialized, big-endian)
 * @param[in]  maxLen   Maximum buffer size
 * @return Actual number of bytes written, 0 on error
 */
typedef uint16 (*SOA_SignalReadFunc_t)(uint8 *outBuf, uint16 maxLen);

/**
 * @brief Write a CAN signal value from a buffer
 *
 * @param[in]  inBuf    Buffer containing the value to write (big-endian)
 * @param[in]  len      Number of bytes in the buffer
 * @return 0 on success, non-zero on error
 */
typedef uint8 (*SOA_SignalWriteFunc_t)(const uint8 *inBuf, uint16 len);

/*==================================================================================================
 *                                  SOA SERVICE CONFIGURATION TABLE
 *==================================================================================================*/

/**
 * @brief SOA service configuration entry
 *
 * One entry per service defined in the SOA service matrix.
 * The table is instantiated in soa_adapter_cnf.c.
 */
typedef struct {
    uint16              SOA_ServiceID;      /**< AP Service ID */
    uint16              SOA_MethodID;       /**< AP Method/Event ID */
    uint16              SOA_InstanceID;     /**< AP Instance ID */
    SOA_ServiceType_e   serviceType;        /**< Notifier / Getter / Setter */
    SOA_SignalReadFunc_t  readFunc;         /**< Read signal value (for Notifier and Getter) */
    SOA_SignalWriteFunc_t writeFunc;        /**< Write signal value (for Setter only, NULL otherwise) */
    uint16              SOA_EventGroupID;   /**< Eventgroup ID (Notifier only, 0 for others) */
    uint8               dataSize;           /**< Expected data size in bytes for this signal */
    boolean             hasLinkedNotifier;  /**< TRUE if this Setter has a linked Notifier */
    uint8               linkedNotifierIdx;  /**< Index into service table of the linked Notifier */
} SOA_ServiceConfig_t;

/*==================================================================================================
 *                                  SERVICE MATRIX DEFINITIONS
 *
 * From the SOA service matrix:
 *   | Service Interface         | ServiceID | InstanceID | Type     | SOA MethodID |
 *   |---------------------------|-----------|------------|----------|-------------|
 *   | Atom_VCU_DriSpeedSt       | 0x0001    | 0x0001     | Notifier | 0x8001      |
 *   | Atom_VCU_ParkingSt        | 0x0002    | 0x0001     | Getter   | 0x5001      |
 *   | Atom_VCU_HighVoltageBatt  | 0x0003    | 0x0001     | Getter   | 0x5001      |
 *   | Atom_VCU_IgnitionSt       | 0x0004    | 0x0001     | Getter   | 0x5001      |
 *   | Atom_BCM_VehicleMode      | 0x0005    | 0x0001     | Setter   | (TBD)       |
 *   | Atom_BCM_VehicleModeSt    | 0x0005    | 0x0001     | Notifier | 0x8001      |
 *==================================================================================================*/

/* --- Service table indices --- */
#define SOA_SVC_IDX_DRI_SPEED_NOTIF     (0U)
#define SOA_SVC_IDX_PARKING_GETTER      (1U)
#define SOA_SVC_IDX_HV_BATT_GETTER      (2U)
#define SOA_SVC_IDX_IGNITION_GETTER     (3U)
#define SOA_SVC_IDX_VEHICLE_MODE_SETTER (4U)
#define SOA_SVC_IDX_VEHICLE_MODE_NOTIF  (5U)

/** Total number of SOA services */
#define SOA_SERVICE_TABLE_COUNT         (6U)

/** Number of Notifier services (for initial value sync and change detection) */
#define SOA_NOTIFIER_COUNT              (2U)

/* --- ServiceID definitions --- */
#define SOA_SID_VCU_DRI_SPEED           (0x0001U)
#define SOA_SID_VCU_PARKING             (0x0002U)
#define SOA_SID_VCU_HV_BATTERY          (0x0003U)
#define SOA_SID_VCU_IGNITION            (0x0004U)
#define SOA_SID_BCM_VEHICLE_MODE        (0x0005U)

/* --- SOA MethodID / EventID definitions --- */
#define SOA_MID_DRI_SPEED_NOTIF         (0x8001U)  /**< Notifier: VehicleSpeed */
#define SOA_MID_PARKING_GETTER          (0x5001U)  /**< Getter: ParkingSts */
#define SOA_MID_HV_BATT_GETTER          (0x5001U)  /**< Getter: HighVoltageBatterySts */
#define SOA_MID_IGNITION_GETTER         (0x5001U)  /**< Getter: IgnitionSts */
#define SOA_MID_VEHICLE_MODE_SETTER     (0x5001U)  /**< Setter: VehicleMode (TBD, using 0x5001) */
#define SOA_MID_VEHICLE_MODE_NOTIF      (0x8001U)  /**< Notifier: VehicleMode (from TX) */

/* --- InstanceID (all services use instance 1) --- */
#define SOA_INSTANCE_ID_DEFAULT         (0x0001U)

/* --- EventGroup IDs --- */
#define SOA_EG_DRI_SPEED_SWTS           (0x0001U)
#define SOA_EG_VEHICLE_MODE_STS         (0x0001U)

/*==================================================================================================
 *                                  GLOBAL DECLARATIONS
 *==================================================================================================*/

/** SOA service configuration table (defined in soa_adapter_cnf.c) */
extern const SOA_ServiceConfig_t g_soaServiceTable[SOA_SERVICE_TABLE_COUNT];

/** Notifier index mapping (indices into g_soaServiceTable for Notifier-type services) */
extern const uint8 g_soaNotifierIndices[SOA_NOTIFIER_COUNT];

/*==================================================================================================
 *                                  SIGNAL READ/WRITE FUNCTION DECLARATIONS
 *==================================================================================================*/

/* Notifier read functions */
uint16 SOA_ReadVehicleSpeed(uint8 *outBuf, uint16 maxLen);
uint16 SOA_ReadWorkVehicleModeSt(uint8 *outBuf, uint16 maxLen);

/* Getter read functions */
uint16 SOA_ReadParkingSts(uint8 *outBuf, uint16 maxLen);
uint16 SOA_ReadHighVoltageBatterySts(uint8 *outBuf, uint16 maxLen);
uint16 SOA_ReadIgnitionSts(uint8 *outBuf, uint16 maxLen);

/* Setter write function */
uint8  SOA_WriteVehicleMode(const uint8 *inBuf, uint16 len);

#if defined(__cplusplus)
}
#endif

#endif /* SOA_ADAPTER_CNF_H */
