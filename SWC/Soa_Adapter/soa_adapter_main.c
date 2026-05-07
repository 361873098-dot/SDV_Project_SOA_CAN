/**
 * @file soa_adapter_main.c
 * @brief SOA Adapter Module — Core Implementation
 *
 * Bridges M-Core CAN signals to A-Core SOA services via IPCF.
 *
 * Architecture:
 *   CAN Bus <-> DBC structs <-> SOA Adapter <-> PICC API <-> IPCF <-> A-Core
 *
 * SOA Payload format (inside IPCF Payload):
 *   [0-1] SOA_ServiceID  [2-3] SOA_MethodID  [4-5] SOA_InstanceID
 *   [6-7] SOA_SessionID  [8-9] SOA_ReturnCode [10-11] SOA_Length
 *   [12+] SOA actual data
 */

#if defined(__cplusplus)
extern "C"{
#endif

/*==================================================================================================
 *                                         INCLUDE FILES
 *==================================================================================================*/

#include "soa_adapter_main.h"
#include "soa_adapter_cnf.h"
#include "FlexCAN_Ip_main.h"
#include "SOA_CANdbc_Generated.h"
#include "picc_api.h"

#include <string.h>

/*==================================================================================================
 *                                         INTERNAL TYPES
 *==================================================================================================*/

/** Notifier change-detection cache entry */
typedef struct {
    uint8  prevData[SOA_MAX_DATA_SIZE]; /**< Previous signal value */
    uint16 prevLen;                     /**< Previous data length */
    boolean isValid;                    /**< TRUE after first read */
} SOA_NotifierCache_t;

/** Module internal state */
typedef struct {
    boolean isInitialized;                              /**< Module init flag */
    PICC_LinkState_e prevLinkState;                     /**< Previous link state for edge detect */
    SOA_NotifierCache_t notifCache[SOA_NOTIFIER_COUNT]; /**< Notifier change caches */
} SOA_AdapterState_t;

/*==================================================================================================
 *                                         PRIVATE VARIABLES
 *==================================================================================================*/

static SOA_AdapterState_t s_soaState;

/** Scratch buffer for SOA message construction (header + data) */
static uint8 s_soaTxBuf[SOA_MAX_MSG_SIZE];

/** Scratch buffer for received method request data */
static uint8 s_methodRxBuf[SOA_MAX_MSG_SIZE];

/** Scratch buffer for method response payload */
static uint8 s_methodRspBuf[SOA_MAX_MSG_SIZE];

/*==================================================================================================
 *                              SOA HEADER SERIALIZATION (Big-Endian)
 *==================================================================================================*/

/**
 * @brief Serialize SOA header into big-endian buffer
 * @param[out] buf    Output buffer (must be >= SOA_HEADER_SIZE)
 * @param[in]  hdr    SOA header to serialize
 */
static void SOA_SerializeHeader(uint8 *buf, const SOA_Header_t *hdr)
{
    buf[0]  = (uint8)(hdr->SOA_ServiceID >> 8U);
    buf[1]  = (uint8)(hdr->SOA_ServiceID & 0xFFU);
    buf[2]  = (uint8)(hdr->SOA_MethodID >> 8U);
    buf[3]  = (uint8)(hdr->SOA_MethodID & 0xFFU);
    buf[4]  = (uint8)(hdr->SOA_InstanceID >> 8U);
    buf[5]  = (uint8)(hdr->SOA_InstanceID & 0xFFU);
    buf[6]  = (uint8)(hdr->SOA_SessionID >> 8U);
    buf[7]  = (uint8)(hdr->SOA_SessionID & 0xFFU);
    buf[8]  = (uint8)(hdr->SOA_ReturnCode >> 8U);
    buf[9]  = (uint8)(hdr->SOA_ReturnCode & 0xFFU);
    buf[10] = (uint8)(hdr->SOA_Length >> 8U);
    buf[11] = (uint8)(hdr->SOA_Length & 0xFFU);
}

/**
 * @brief Deserialize SOA header from big-endian buffer
 * @param[in]  buf    Input buffer (must be >= SOA_HEADER_SIZE)
 * @param[out] hdr    Parsed SOA header
 */
static void SOA_DeserializeHeader(const uint8 *buf, SOA_Header_t *hdr)
{
    hdr->SOA_ServiceID  = ((uint16)buf[0] << 8U) | (uint16)buf[1];
    hdr->SOA_MethodID   = ((uint16)buf[2] << 8U) | (uint16)buf[3];
    hdr->SOA_InstanceID = ((uint16)buf[4] << 8U) | (uint16)buf[5];
    hdr->SOA_SessionID  = ((uint16)buf[6] << 8U) | (uint16)buf[7];
    hdr->SOA_ReturnCode = ((uint16)buf[8] << 8U) | (uint16)buf[9];
    hdr->SOA_Length     = ((uint16)buf[10] << 8U) | (uint16)buf[11];
}

/*==================================================================================================
 *                              SERVICE TABLE LOOKUP
 *==================================================================================================*/

/**
 * @brief Find service config by SOA_ServiceID + SOA_MethodID + SOA_InstanceID
 * @return Table index (0..SOA_SERVICE_TABLE_COUNT-1), or 0xFF if not found
 */
static uint8 SOA_FindService(uint16 serviceId, uint16 methodId, uint16 instanceId)
{
    uint8 i;
    for (i = 0U; i < SOA_SERVICE_TABLE_COUNT; i++)
    {
        if ((g_soaServiceTable[i].SOA_ServiceID  == serviceId) &&
            (g_soaServiceTable[i].SOA_MethodID   == methodId) &&
            (g_soaServiceTable[i].SOA_InstanceID == instanceId))
        {
            return i;
        }
    }
    return 0xFFU;
}

/*==================================================================================================
 *                              NOTIFIER: BUILD AND SEND
 *==================================================================================================*/

/**
 * @brief Build a single Notifier SOA message and send via PICC Event
 *
 * Constructs: SOA Header (12B) + signal data, then calls PICC_SendEvent.
 *
 * @param[in] svcIdx  Index into g_soaServiceTable
 * @return PICC_E_OK on success
 */
static sint8 SOA_SendNotifier(uint8 svcIdx)
{
    const SOA_ServiceConfig_t *svc = &g_soaServiceTable[svcIdx];
    SOA_Header_t hdr;
    uint16 dataLen;
    uint16 totalLen;

    if ((svc->readFunc == NULL_PTR) || (svc->serviceType != SOA_SERVICE_NOTIFIER))
    {
        return PICC_E_PARAM;
    }

    /* Read current signal value into buffer after header area */
    dataLen = svc->readFunc(&s_soaTxBuf[SOA_HEADER_SIZE], SOA_MAX_DATA_SIZE);
    if (dataLen == 0U)
    {
        return PICC_E_PARAM;
    }

    /* Build SOA header */
    hdr.SOA_ServiceID  = svc->SOA_ServiceID;
    hdr.SOA_MethodID   = svc->SOA_MethodID;
    hdr.SOA_InstanceID = svc->SOA_InstanceID;
    hdr.SOA_SessionID  = 0U;  /* Notifier: SessionID = 0 */
    hdr.SOA_ReturnCode = 0U;  /* Notifier: ReturnCode = 0 */
    hdr.SOA_Length     = dataLen;

    SOA_SerializeHeader(s_soaTxBuf, &hdr);
    totalLen = SOA_HEADER_SIZE + dataLen;

    /* Send via PICC Event (IPC EventID=3, WITHOUT_ACK per M-core constraint) */
    return PICC_SendEvent(PICC_APP_SOA, SOA_IPC_EVENT_ID_FOR_NOTIF,
                          s_soaTxBuf, totalLen, PICC_EVENT_WITHOUT_ACK);
}

/**
 * @brief Send all Notifier initial values (called once after link established)
 */
static void SOA_SendAllNotifierInitValues(void)
{
    uint8 i;
    for (i = 0U; i < SOA_NOTIFIER_COUNT; i++)
    {
        uint8 svcIdx = g_soaNotifierIndices[i];
        (void)SOA_SendNotifier(svcIdx);

        /* Initialize change-detection cache */
        if (g_soaServiceTable[svcIdx].readFunc != NULL_PTR)
        {
            s_soaState.notifCache[i].prevLen =
                g_soaServiceTable[svcIdx].readFunc(
                    s_soaState.notifCache[i].prevData, SOA_MAX_DATA_SIZE);
            s_soaState.notifCache[i].isValid = TRUE;
        }
    }
}

/**
 * @brief Check all Notifiers for changes, send if changed
 */
static void SOA_CheckAndSendNotifiers(void)
{
    uint8 i;
    uint8 tmpBuf[SOA_MAX_DATA_SIZE];

    for (i = 0U; i < SOA_NOTIFIER_COUNT; i++)
    {
        uint8 svcIdx = g_soaNotifierIndices[i];
        const SOA_ServiceConfig_t *svc = &g_soaServiceTable[svcIdx];
        uint16 curLen;

        if (svc->readFunc == NULL_PTR)
        {
            continue;
        }

        curLen = svc->readFunc(tmpBuf, SOA_MAX_DATA_SIZE);
        if (curLen == 0U)
        {
            continue;
        }

        /* Compare with cached value */
        if (s_soaState.notifCache[i].isValid &&
            (curLen == s_soaState.notifCache[i].prevLen) &&
            (memcmp(tmpBuf, s_soaState.notifCache[i].prevData, curLen) == 0))
        {
            /* No change — skip */
            continue;
        }

        /* Value changed — send Notifier */
        (void)SOA_SendNotifier(svcIdx);

        /* Update cache */
        (void)memcpy(s_soaState.notifCache[i].prevData, tmpBuf, curLen);
        s_soaState.notifCache[i].prevLen = curLen;
        s_soaState.notifCache[i].isValid = TRUE;
    }
}

/*==================================================================================================
 *                              GETTER/SETTER REQUEST HANDLING
 *==================================================================================================*/

/**
 * @brief Process a Getter/Setter request from A-Core
 *
 * Parses the SOA header from the received method data, looks up the service,
 * executes read or write, and sends the response via PICC_MethodResponse.
 *
 * @param[in] reqData     Raw method request payload (SOA header + data)
 * @param[in] reqLen      Request payload length
 * @param[in] ipcSessionId  IPC-layer session ID (must be echoed in response)
 */
static void SOA_HandleMethodRequest(const uint8 *reqData, uint16 reqLen, uint8 ipcSessionId)
{
    SOA_Header_t reqHdr;
    SOA_Header_t rspHdr;
    uint8 svcIdx;
    uint16 rspDataLen = 0U;
    uint8 ipcReturnCode = 0U;  /* PICC ReturnCode: 0=OK */
    uint16 rspTotalLen;

    /* Need at least SOA header */
    if (reqLen < SOA_HEADER_SIZE)
    {
        return;
    }

    /* Parse SOA header */
    SOA_DeserializeHeader(reqData, &reqHdr);

    /* Find matching service */
    svcIdx = SOA_FindService(reqHdr.SOA_ServiceID, reqHdr.SOA_MethodID, reqHdr.SOA_InstanceID);
    if (svcIdx == 0xFFU)
    {
        /* Service not found — send error response */
        rspHdr.SOA_ServiceID  = reqHdr.SOA_ServiceID;
        rspHdr.SOA_MethodID   = reqHdr.SOA_MethodID;
        rspHdr.SOA_InstanceID = reqHdr.SOA_InstanceID;
        rspHdr.SOA_SessionID  = reqHdr.SOA_SessionID;
        rspHdr.SOA_ReturnCode = 1U;  /* E_NOT_OK */
        rspHdr.SOA_Length     = 0U;

        SOA_SerializeHeader(s_methodRspBuf, &rspHdr);
        ipcReturnCode = 0x01U;

        (void)PICC_MethodResponse(PICC_APP_SOA, SOA_IPC_METHOD_ID_FOR_RR,
                                  ipcSessionId, ipcReturnCode,
                                  s_methodRspBuf, SOA_HEADER_SIZE);
        return;
    }

    const SOA_ServiceConfig_t *svc = &g_soaServiceTable[svcIdx];

    /* Prepare response header (echo back IDs and SessionID) */
    rspHdr.SOA_ServiceID  = reqHdr.SOA_ServiceID;
    rspHdr.SOA_MethodID   = reqHdr.SOA_MethodID;
    rspHdr.SOA_InstanceID = reqHdr.SOA_InstanceID;
    rspHdr.SOA_SessionID  = reqHdr.SOA_SessionID;  /* Echo SOA SessionID */
    rspHdr.SOA_ReturnCode = 0U;

    switch (svc->serviceType)
    {
        case SOA_SERVICE_GETTER:
        {
            /* Read current value */
            if (svc->readFunc != NULL_PTR)
            {
                rspDataLen = svc->readFunc(&s_methodRspBuf[SOA_HEADER_SIZE], SOA_MAX_DATA_SIZE);
            }
            if (rspDataLen == 0U)
            {
                rspHdr.SOA_ReturnCode = 1U;  /* Read failed */
                ipcReturnCode = 0x01U;
            }
            break;
        }

        case SOA_SERVICE_SETTER:
        {
            /* Write the value from request payload */
            uint8 writeResult = 1U;

            if ((svc->writeFunc != NULL_PTR) && (reqHdr.SOA_Length > 0U) &&
                ((uint16)(reqLen - SOA_HEADER_SIZE) >= reqHdr.SOA_Length))
            {
                writeResult = svc->writeFunc(&reqData[SOA_HEADER_SIZE], reqHdr.SOA_Length);
            }

            if (writeResult != 0U)
            {
                rspHdr.SOA_ReturnCode = 1U;  /* Write failed */
                ipcReturnCode = 0x01U;
                rspDataLen = 0U;
            }
            else if (svc->hasLinkedNotifier && (svc->readFunc != NULL_PTR))
            {
                /* Setter+Notifier: read current Notifier value for response */
                rspDataLen = svc->readFunc(&s_methodRspBuf[SOA_HEADER_SIZE], SOA_MAX_DATA_SIZE);
            }
            else
            {
                /* Setter without Notifier: return empty data (Length=0) */
                rspDataLen = 0U;
            }
            break;
        }

        default:
            /* Notifier type should not arrive as method request */
            rspHdr.SOA_ReturnCode = 1U;
            ipcReturnCode = 0x01U;
            rspDataLen = 0U;
            break;
    }

    /* Fill response SOA header */
    rspHdr.SOA_Length = rspDataLen;
    SOA_SerializeHeader(s_methodRspBuf, &rspHdr);
    rspTotalLen = SOA_HEADER_SIZE + rspDataLen;

    /* Send response via PICC (IPC MethodID=1, echo IPC sessionId) */
    (void)PICC_MethodResponse(PICC_APP_SOA, SOA_IPC_METHOD_ID_FOR_RR,
                              ipcSessionId, ipcReturnCode,
                              s_methodRspBuf, rspTotalLen);
}

/**
 * @brief Poll for pending Getter/Setter requests from A-Core
 */
static void SOA_PollMethodRequests(void)
{
    uint16 actualLen = 0U;
    uint8  ipcSessionId = 0U;

    /* Poll for Method data on IPC MethodID=1 */
    if (PICC_GetMethodData(PICC_APP_SOA, SOA_IPC_METHOD_ID_FOR_RR,
                           s_methodRxBuf, SOA_MAX_MSG_SIZE, &actualLen,
                           &ipcSessionId, NULL, NULL) == PICC_E_OK)
    {
        if (actualLen >= SOA_HEADER_SIZE)
        {
            SOA_HandleMethodRequest(s_methodRxBuf, actualLen, ipcSessionId);
        }
    }
}

/*==================================================================================================
 *                                         PUBLIC FUNCTIONS
 *==================================================================================================*/

/**
 * @brief Initialize the SOA Adapter module
 */
void SoaAdapter_Init(void)
{
    static const PICC_AppConfig_t soa_cfg = {
        .localId           = SOA_PROVIDER_ID,    /* 71 (0x47) */
        .remoteId          = SOA_CONSUMER_ID,    /* 76 (0x4C) */
        .role              = PICC_ROLE_SERVER,
        .channelId         = SOA_CHANNEL_ID,     /* 2 */
        .Client_linkReq_PeriodMs = 0U,
        .methodHandler     = NULL_PTR,           /* Polling mode */
        .eventHandler      = NULL_PTR            /* Polling mode */
    };

    (void)memset(&s_soaState, 0, sizeof(s_soaState));
    s_soaState.prevLinkState = PICC_LINK_STATE_DISCONNECTED;

    (void)PICC_Init(PICC_APP_SOA, &soa_cfg);

    s_soaState.isInitialized = TRUE;
}

/**
 * @brief SOA Adapter 10ms periodic main function
 */
void SoaAdapter_Main(void)
{
    PICC_LinkState_e curLinkState;

    if (!s_soaState.isInitialized)
    {
        return;
    }

    /* 1. Check application-level link state */
    curLinkState = PICC_GetAppLinkState(PICC_APP_SOA);

    /* 2. Detect DISCONNECTED → CONNECTED transition: send initial values */
    if ((curLinkState == PICC_LINK_STATE_CONNECTED) &&
        (s_soaState.prevLinkState != PICC_LINK_STATE_CONNECTED))
    {
        /* Link just established — send all Notifier initial values */
        SoaAdapter_CanRxProcess();  /* Get fresh CAN data first */
        SOA_SendAllNotifierInitValues();
    }

    /* 3. If link not connected, skip business logic */
    if (curLinkState != PICC_LINK_STATE_CONNECTED)
    {
        s_soaState.prevLinkState = curLinkState;

        /* If disconnected, invalidate Notifier caches for re-sync on reconnect */
        if (curLinkState == PICC_LINK_STATE_DISCONNECTED)
        {
            uint8 i;
            for (i = 0U; i < SOA_NOTIFIER_COUNT; i++)
            {
                s_soaState.notifCache[i].isValid = FALSE;
            }
        }
        return;
    }

    s_soaState.prevLinkState = curLinkState;

    /* 4. Process CAN RX (update DBC structs with latest CAN data) */
    SoaAdapter_CanRxProcess();

    /* 5. Check Notifiers for value changes and send if changed */
    SOA_CheckAndSendNotifiers();

    /* 6. Poll and handle Getter/Setter requests from A-Core */
    SOA_PollMethodRequests();

    /* 7. Process CAN TX (send updated DBC struct values onto the bus) */
    SoaAdapter_CanTxProcess();
}

/**
 * @brief CAN RX processing — polls RX mailbox and unpacks DBC struct
 */
void SoaAdapter_CanRxProcess(void)
{
    (void)FlexCAN_Message_Rx_unpack(STANDARD_200_RX_ID, &g_rx_Standard_200_Rx);
}

/**
 * @brief CAN TX processing — packs DBC struct and transmits
 */
void SoaAdapter_CanTxProcess(void)
{
    (void)FlexCAN_Message_Tx_pack(STANDARD_100_TX_ID, &g_tx_Standard_100_Tx);
}

#if defined(__cplusplus)
}
#endif
