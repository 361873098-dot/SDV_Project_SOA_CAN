/**
 * @file main.c
 * @brief M-Core Inter-Core Communication Application - Main Program
 *
 * IPC Shared Memory Driver application with PICC middleware.
 * Integrates PICC inter-core communication middleware module for A-Core communication.
 *
 * Copyright 2024 NXP
 * All Rights Reserved.
 */

#if defined(__cplusplus)
extern "C"{
#endif

/*==================================================================================================
 *                                         Header Includes
 *==================================================================================================*/

#include "Mcal.h"
#include "Platform.h"


/* Application headers */
#include "Picc_main.h"

/* IPCF driver */
#include "ipc-shm.h"
#include "ipcf_Ip_Cfg_Defines.h"

/* FreeRTOS */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* PICC module */
#include "picc_api.h"
#include "picc_mailbox.h"   /* For PICC_StoreToMailbox, PICC_GlobalInit */
#include "picc_stack.h"     /* For PICC_STACK_MAX_SIZE, PICC_StackProcessRx, PICC_StackRegisterMsgCallback */
#include "picc_protocol.h"  /* For PICC_MsgHeader_t, PICC_MSG_LINK_AVAILABLE */
#include "picc_heartbeat.h" /* For PICC_HeartbeatInit, PICC_HeartbeatProcess */
#include "picc_trace.h"     /* For PICC_TraceInit */

#include "Port.h"

/* Forward declaration — defined below PICC_PreOS_Init */
static void PICC_ProcessSingleMessage(const PICC_MsgHeader_t *header,
                                      const uint8 *payload, uint16 payloadLen,
                                      uint8 instanceId, uint8 channelId);
/*==================================================================================================
 *                                         Macro Definitions
 *==================================================================================================*/

/** Control channel configuration */
#define CTRL_CHAN_ID            (0U)
#define CTRL_CHAN_SIZE          (64U)

/** Maximum message length */

#define MAX_MSG_LEN             (PICC_STACK_MAX_SIZE)

/*==================================================================================================
 *                                         Private Type Definitions
 *==================================================================================================*/

/**
 * @brief Application private data
 */
typedef struct {
    sint8          *ctrl_shm;      /**< Control channel shared memory */
    volatile uint16 rx_count;      /**< Receive message count */
    volatile uint16 tx_count;      /**< Send/Process message count */
    volatile uint16 error_count;   /**< Error count */
    volatile sint8  last_error;    /**< Last error code */
    volatile uint8  link_state;    /**< Current connection state */
    const char     *error_file;    /**< Error occurred file name */
    int             error_line;    /**< Error occurred line number */
    uint8           last_rx_ch;    /**< Last receive channel */
} App_Data_t;

/**
 * @brief Receive message structure (for queue)
 */
typedef struct {
    uint8   instance;   /**< IPCF instance */
    uint8   chanId;     /**< Channel ID */
    void   *buf;        /**< Buffer pointer */
    uint32  size;       /**< Data size */
    boolean isManaged;  /**< TRUE=Managed(needs release), FALSE=Unmanaged */
} App_RxMsg_t;

/*==================================================================================================
 *                                         Private Variables
 *==================================================================================================*/

/** Application data */
static App_Data_t g_appData;
static App_Data_t g_unmngDat;


/** link with generated variables */
const void *rx_mng_cb_arg = &g_appData;

const void *rx_unmg_cb_arg  = &g_unmngDat;


/** Receive queue handle */
static QueueHandle_t g_rxQueue = NULL;

/** Exit code (for main loop) */
volatile uint8 exit_code;

#if (PICC_DIAG_RECORD_ENABLE == 1U)
/*==================================================================================================
 *                                   Diagnostic Recording Variables
 *==================================================================================================*/

/** Diagnostic record buffer: 20 rows × 30 bytes per row */
#define PICC_DIAG_RECORD_ROWS       (20U)
#define PICC_DIAG_RECORD_COLS       (30U)

/** Diagnostic record structure */
typedef struct {
    uint8   buffer[PICC_DIAG_RECORD_ROWS][PICC_DIAG_RECORD_COLS];  /**< Data buffer */
    uint16  currentRow;                                              /**< Current row index (0-19) */
    uint16  currentCol;                                              /**< Current column index in row */
    uint16  totalRecords;                                            /**< Total records count */
} PICC_DiagRecord_t;

/** Combined diagnostic record (view in TRACE32: Var.View g_diagRecord_Debug) */
typedef struct {
    PICC_DiagRecord_t tx;  /**< TX diagnostic buffer */
    PICC_DiagRecord_t rx;  /**< RX diagnostic buffer */
    uint8 appLinkState[PICC_REGISTRY_SIZE];            /**< Per-app link state (index=localId, value=PICC_LinkState_e) */
    PICC_LinkState_e  channelLinkState[PICC_MAX_CHANNELS]; /**< Per-channel link state (index=0..1 => channelId 1..2) */
} PICC_ChannelDiag_t;

PICC_ChannelDiag_t g_diagRecord_Debug;


/**
 * @brief Initialize diagnostic record structure
 */
static void PICC_DiagRecordInit(PICC_DiagRecord_t *record)
{
    uint16 i, j;

    if (record == NULL) {
        return;
    }

    for (i = 0U; i < PICC_DIAG_RECORD_ROWS; i++) {
        for (j = 0U; j < PICC_DIAG_RECORD_COLS; j++) {
            record->buffer[i][j] = 0U;
        }
    }

    record->currentRow = 0U;
    record->currentCol = 0U;
    record->totalRecords = 0U;
}

/**
 * @brief Add data to diagnostic record buffer (parse stacked packet, one message per row)
 */
static void PICC_DiagRecordAdd(PICC_DiagRecord_t *record, const uint8 *data, uint32 len)
{
    uint32 offset;
    uint16 payloadLen;
    uint32 msgLen;
    uint32 copyLen;
    uint32 col;
    uint16 row;
    const uint8 *msgPtr;

    if (record == NULL || data == NULL || len == 0U) {
        return;
    }

    if (len < 13U) {
        return;
    }

    offset = 1U;

    while (offset + PICC_HEADER_SIZE <= (len - 4U)) {
        msgPtr = &data[offset];

        payloadLen = ((uint16)msgPtr[6] << 8U) | (uint16)msgPtr[7];
        msgLen = PICC_HEADER_SIZE + payloadLen;

        if (offset + msgLen > (len - 4U)) {
            break;
        }

        if ((msgPtr[0] == 0xFFU) || (msgPtr[1] == 0xFFU)) {
            offset += msgLen;
            continue;
        }

        if ((msgPtr[0] != 81U) || (msgPtr[2] != 91U)) {
            offset += msgLen;
            continue;
        }

        row = record->currentRow;

        copyLen = msgLen;
        if (copyLen > PICC_DIAG_RECORD_COLS) {
            copyLen = PICC_DIAG_RECORD_COLS;
        }

        for (col = 0U; col < copyLen; col++) {
            record->buffer[row][col] = msgPtr[col];
        }

        for (col = copyLen; col < PICC_DIAG_RECORD_COLS; col++) {
            record->buffer[row][col] = 0U;
        }

        record->currentRow++;
        if (record->currentRow >= PICC_DIAG_RECORD_ROWS) {
            record->currentRow = 0U;
        }

        record->totalRecords++;

        offset += msgLen;
    }
}

#endif /* PICC_DIAG_RECORD_ENABLE */

#if (PICC_DIAG_RECORD_ENABLE == 1U)
void PICC_DiagRecordAddTx(const uint8 *data, uint32 len)
{
    PICC_DiagRecordAdd(&g_diagRecord_Debug.tx, data, len);
}

/**
 * @brief Update link state diagnostic fields in g_diagRecord_Debug
 *
 * Iterates over all 128 registry entries to find registered apps,
 * then queries their link state. Only registered apps have valid states.
 */
void PICC_DiagUpdateLinkState(void)
{
    uint16 i;

    /* Update per-app link states — iterate full registry */
    for (i = 0U; i < PICC_REGISTRY_SIZE; i++) {
        if (PICC_MailboxIsAppRegistered((uint8)i)) {
            g_diagRecord_Debug.appLinkState[i] = (uint8)PICC_GetAppLinkState((uint8)i);
        } else {
            g_diagRecord_Debug.appLinkState[i] = (uint8)PICC_LINK_STATE_DISCONNECTED;
        }
    }

    /* Update per-channel link states (channelId 1 and 2 mapped to index 0 and 1) */
    g_diagRecord_Debug.channelLinkState[0] = PICC_GetLinkState(1U);
    g_diagRecord_Debug.channelLinkState[1] = PICC_GetLinkState(2U);
}
#endif /* PICC_DIAG_RECORD_ENABLE */

/*==================================================================================================
 *                                         FreeRTOS Static Memory
 *==================================================================================================*/

#if (configSUPPORT_STATIC_ALLOCATION == 1)

static StaticTask_t xIdleTaskTCB;
static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
        StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

#if (configUSE_TIMERS == 1)
static StaticTask_t xTimerTaskTCB;
static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
        StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize)
{
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxIdleTaskStackBuffer = uxTimerTaskStack;
    *pulIdleTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
#endif

#endif /* configSUPPORT_STATIC_ALLOCATION */

/*==================================================================================================
 *                                         IPCF Callback Functions
 *==================================================================================================*/

/**
 * @brief IPCF managed channel RX callback (ISR context)
 *
 * Called by IPCF driver when a new message arrives on a managed channel.
 * Copies message metadata into App_RxMsg_t and enqueues to g_rxQueue.
 * The actual processing happens in PICC_Rx_Msg_10ms_Task() which dequeues
 * and processes messages in task context (not ISR).
 *
 * @param[in] arg      Callback argument (pointer to g_appData)
 * @param[in] instance IPCF instance ID
 * @param[in] chan_id  Channel ID the message was received on
 * @param[in] buf      Pointer to the received data buffer (managed by IPCF)
 * @param[in] size     Size of the received data
 */
void PICC_data_mng_rx_cb(void *arg, const uint8 instance, uint8 chan_id, void *buf,
        uint32 size)
{
    App_Data_t *appPtr = (App_Data_t *)(*((uintptr *)arg));
    App_RxMsg_t msg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    (void)instance;

    if (appPtr != &g_appData || size > MAX_MSG_LEN) {
        PICC_HANDLE_ERROR(-IPC_SHM_E_INVAL);
        return;
    }

    appPtr->last_rx_ch = chan_id;

#if (PICC_DIAG_RECORD_ENABLE == 1U)
    PICC_DiagRecordAdd(&g_diagRecord_Debug.rx, (const uint8 *)buf, size);
#endif

    msg.instance  = instance;
    msg.chanId    = chan_id;
    msg.buf       = buf;
    msg.size      = size;
    msg.isManaged = TRUE;

    if (g_rxQueue != NULL) {
        if (xQueueSendFromISR(g_rxQueue, &msg, &xHigherPriorityTaskWoken) != pdPASS) {
            (void)ipc_shm_release_buf(instance, chan_id, buf);
            appPtr->error_count++;

        }
    } else {
        (void)ipc_shm_release_buf(instance, chan_id, buf);
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void PICC_data_unmng_rx_cb(void *arg, const uint8 instance, uint8 chan_id, void *mem)
{
    App_Data_t *appPtr = (App_Data_t *)(*((uintptr *)arg));
    
    (void)instance;
    (void)chan_id;
    (void)mem;

    if (appPtr != &g_unmngDat) {
        PICC_HANDLE_ERROR(-IPC_SHM_E_INVAL);
        return;
    }
}

/*==================================================================================================
 *                                   Infrastructure Initialization
 *==================================================================================================*/

static void PICC_HeartbeatTimeoutHandler(uint8 instanceId, uint8 channelId)
{
    PICC_LinkTriggerReconnect(instanceId, channelId);
}

/**
 * @brief Initialize PICC infrastructure (trace, service layer, mailbox, global init)
 *
 * Called by PICC_PreOS_Init() before channel initialization.
 */
void PICC_InfraInit(void)
{
    /* 1. Initialize debug trace module */
    PICC_TraceInit();

    /* 2. Initialize service layer registry */
    PICC_ServiceLayerInit();

    /* 3. Stack message callback is registered separately in PICC_PreOS_Init */

    /* 4. Global init — clear all registry/slot pool/mapping tables */
    PICC_GlobalInit();

    /* 5. Mark mailbox ready for registration */
    PICC_MailboxReady();

    /* 6. One-time initialization of link management layer (shared pool) */
    PICC_LinkLayerInit();
}

/**
 * @brief Initialize specified IPCF channel (Stack + Heartbeat)
 */
sint8 PICC_InitChannel(uint8 instanceId, uint8 channelId)
{
    PICC_StackConfig_t stackCfg;
    sint8 ret;
    static boolean heartbeatInitialized = FALSE;

    if (heartbeatInitialized == FALSE) {
        ret = PICC_HeartbeatInit();
        if (ret != 0) {
            return PICC_E_NOT_INIT;
        }
        (void)PICC_HeartbeatRegisterTimeoutCallback(PICC_HeartbeatTimeoutHandler);
        heartbeatInitialized = TRUE;
    }

    stackCfg.channelId  = channelId;
    stackCfg.maxSize    = PICC_STACK_MAX_SIZE;
    stackCfg.periodMs   = PICC_STACK_SEND_PERIOD_MS;
    stackCfg.crcEnabled = PICC_STACK_CRC_ENABLED;
    ret = PICC_StackInitChannel(&stackCfg);
    if (ret != 0) {
        return ret;
    }

    ret = PICC_HeartbeatAddChannel(instanceId, channelId);
    if (ret != 0) {
        return ret;
    }

    return PICC_E_OK;
}

/*==================================================================================================
 *                                         PreOS Initialization
 *==================================================================================================*/

void PICC_PreOS_Init(void)
{
    sint8 err = -IPC_SHM_E_INVAL;

    /* 1. Initialize receive queue */
    g_rxQueue = xQueueCreate(10, sizeof(App_RxMsg_t));
    if (g_rxQueue == NULL) {
        PICC_HANDLE_ERROR(-1);
    }

    /* 2. Initialize application data */
    g_appData.rx_count    = 0U;
    g_appData.tx_count    = 0U;
    g_appData.error_count = 0U;
    g_appData.last_error  = 0;
    g_appData.error_file  = NULL;
    g_appData.error_line  = 0;
    g_appData.link_state  = (uint8)PICC_LINK_STATE_DISCONNECTED;

#if (PICC_DIAG_RECORD_ENABLE == 1U)
    PICC_DiagRecordInit(&g_diagRecord_Debug.rx);
    PICC_DiagRecordInit(&g_diagRecord_Debug.tx);

    for (uint16 k = 0U; k < PICC_REGISTRY_SIZE; k++) {
        g_diagRecord_Debug.appLinkState[k] = (uint8)PICC_LINK_STATE_DISCONNECTED;
    }
    g_diagRecord_Debug.channelLinkState[0] = (uint8)PICC_LINK_STATE_DISCONNECTED;
    g_diagRecord_Debug.channelLinkState[1] = (uint8)PICC_LINK_STATE_DISCONNECTED;
#endif

    /* 3. Initialize IPCF driver */
    do {
        err = ipc_shm_init(&ipcf_shm_instances_cfg);
    } while (err == -IPC_SHM_E_REMOTE_INIT_IN_PROGRESS);
    
    if (err != 0) {
        PICC_HANDLE_ERROR(err);
    }

    if (ipc_shm_is_remote_ready(IPCF_INSTANCE0) != 0) {
        /* Remote not ready, but don't block */
    }

    g_appData.ctrl_shm = ipc_shm_unmanaged_acquire(IPCF_INSTANCE0, CTRL_CHAN_ID);
    if (g_appData.ctrl_shm == NULL) {
        PICC_HANDLE_ERROR(-IPC_SHM_E_NOMEM);
    }

    /* 4. Initialize PICC infrastructure (includes PICC_GlobalInit) */
    PICC_InfraInit();

    /* 4b. Register stack message callback */
    (void)PICC_StackRegisterMsgCallback(PICC_ProcessSingleMessage);

    /* 5. Initialize IPCF channels */
    err = PICC_InitChannel(IPCF_INSTANCE0, 1U);
    if (err != 0) {
        PICC_HANDLE_ERROR(err);
    }

    err = PICC_InitChannel(IPCF_INSTANCE0, 2U);
    if (err != 0) {
        PICC_HANDLE_ERROR(err);
    }
}

/*==================================================================================================
 *                                    Message Dispatching
 *==================================================================================================*/

/**
 * @brief Process a single received message — dispatch by MessageType
 *
 * Message processing flow:
 *   1. LINK_AVAILABLE messages → dispatch to picc_link.c for connection handling
 *   2. All other messages (Event, Method, Response):
 *      a. Store to mailbox (PICC_StoreToMailbox) with channelId defense check
 *      b. Dispatch to service layer (PICC_ServiceProcessMessage) for callback processing
 *      c. Store callback result into the same mailbox slot (PICC_StoreCallbackResult)
 *
 * The mailbox store ALWAYS happens first, regardless of whether a callback
 * exists. This ensures polling-mode applications can always retrieve data.
 *
 * @param[in] header      Protocol header (8 bytes)
 * @param[in] payload     Payload data after header
 * @param[in] payloadLen  Payload length
 * @param[in] instanceId  IPCF instance ID
 * @param[in] channelId   IPCF channel ID (1 or 2)
 */
static void PICC_ProcessSingleMessage(const PICC_MsgHeader_t *header,
                                      const uint8 *payload, uint16 payloadLen,
                                      uint8 instanceId, uint8 channelId)
{
    if (header == NULL) {
        return;
    }

    if (header->msgType == (uint8)PICC_MSG_LINK_AVAILABLE) {
        /* Link management message (connect/disconnect/reconnect) */
        (void)PICC_LinkProcessMessage(header, payload, payloadLen, instanceId, channelId);
    } else {
        uint8  cbResult[PICC_CB_RESULT_MAX_LEN];
        uint16 cbResultLen = 0U;

        /* Step 1: Store to mailbox FIRST — ensures polling-mode apps can read data */
        PICC_StoreToMailbox(header, payload, payloadLen, channelId);

        /* Step 2: Dispatch to service layer — callback may write cbResult
         * For Method REQUEST with registered handler: auto-send RESPONSE
         * For Event WITH_ACK: auto-send EVENT_ACK
         * For REQUEST_NO_RETURN_WITH_ACK: auto-send ACK */
        (void)PICC_ServiceProcessMessage(header, payload, payloadLen,
                                         instanceId, channelId,
                                         cbResult, &cbResultLen);

        /* Step 3: Store callback result into the same mailbox slot
         * This allows polling apps to retrieve callback-produced data */
        if (cbResultLen > 0U) {
            PICC_StoreCallbackResult(header, channelId, cbResult, cbResultLen);
        }
    }
}

/*==================================================================================================
 *                                         RX Message Task
 *==================================================================================================*/

/**
 * @brief Process received IPCF data — unpack stacked messages
 *
 * Called from the RX task for each received IPCF buffer.
 * Validates minimum size, then passes data to the Stack layer for
 * unstacking and individual message parsing.
 *
 * @param[in] instance  IPCF instance ID
 * @param[in] chan_id    IPCF channel ID
 * @param[in] buf       Pointer to received data buffer
 * @param[in] size      Data size in bytes
 * @return PICC_E_OK on success, PICC_E_PARAM on invalid data
 */
static sint8 PICC_ProcessRxData(const uint8 instance, uint8 chan_id,
                                 const void *buf, uint32 size)
{
    const uint8 *data;
    sint8 ret;

    if (buf == NULL) {
        return PICC_E_PARAM;
    }

    data = (const uint8 *)buf;

    if (size >= PICC_STACK_OVERHEAD_SIZE) {
        ret = PICC_StackProcessRx(data, size, instance, chan_id);
        if (ret >= 0) {
            return PICC_E_OK;
        }
        return PICC_E_PARAM;
    }

    return PICC_E_PARAM;
}

/**
 * @brief RX message processing task (event-driven, queue blocking)
 *
 * Blocks on g_rxQueue waiting for IPCF messages from the ISR callback.
 * For each received message:
 *   1. Calls PICC_ProcessRxData() to unpack stacked messages
 *   2. Releases the IPCF managed buffer back to the driver
 *
 * @param[in] params  FreeRTOS task parameter (unused)
 */
void PICC_Rx_Msg_10ms_Task(void *params)
{
    App_RxMsg_t rxMsg;
    sint8 err;

    (void)params;
    while (1) {
        if (xQueueReceive(g_rxQueue, &rxMsg, portMAX_DELAY) == pdPASS) {
            
            (void)PICC_ProcessRxData(rxMsg.instance, rxMsg.chanId, rxMsg.buf, rxMsg.size);
            
            if (rxMsg.isManaged != FALSE) {
                err = ipc_shm_release_buf(rxMsg.instance, rxMsg.chanId, rxMsg.buf);
                if (err != 0) {
                    /* Log release error */
                }
            }
            
            g_appData.tx_count++;
        }
    }
}

/*==================================================================================================
 *                                         Error Handling
 *==================================================================================================*/

void PICC_handle_error(sint8 error, const char *file, int line)
{
    g_appData.last_error = error;
    g_appData.error_file = file;
    g_appData.error_line = line;
    g_appData.error_count++;
}

#if defined(__cplusplus)
}
#endif
