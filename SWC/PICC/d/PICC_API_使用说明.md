# PICC API 使用说明 (v3.0 — 基于 localId 直索引架构)

> **本文档基于当前代码自动生成，对应 PICC v3.0 架构（localId 直索引、共享 Link 池、全局 Slot 池）。**

---

## 0. 核心通信方向与角色向导

### 1) EVENT 协议 (单向通知 / Fire-and-Forget)
*   **M核 → A核**：调用 `PICC_SendEvent()`。
*   **A核 → M核**：通过 `.eventHandler` 回调捕获；或周期轮询 `PICC_GetEventData()`。

### 2) METHOD 协议 (双向请求 / Request-Response)
*   **情形 A：M核是 Client（M核提问，A核作答）**
    *   M核 → A核：`PICC_MethodRequest()` 发送请求。
    *   A核 → M核：`PICC_GetResponseData()` 轮询获取响应。
*   **情形 B：M核是 Server（A核提问，M核作答）**
    *   A核 → M核：`.methodHandler` 回调捕获；或 `PICC_GetMethodData()` 轮询获取请求。
    *   M核 → A核：`PICC_MethodResponse()` 发送响应（回调模式下驱动自动发送）。

---

## 1. 概述

`picc_api.h` 是 M 核应用层与 PICC 驱动层交互的**唯一公共接口头文件**。
应用层只需 `#include "picc_api.h"`，通过以下 **9 个**公共 API 完成全部核间通信操作。

---

## 2. 公共 API 列表

| # | 函数 | 方向 | 用途 |
|---|------|------|------|
| 1 | `PICC_Init()` | 初始化 | 注册一个应用（localId 作为直索引） |
| 2 | `PICC_SendEvent()` | M→A | 发送 Event 通知 |
| 3 | `PICC_MethodRequest()` | M→A | 发送 Method 请求（返回 sessionId） |
| 4 | `PICC_MethodResponse()` | M→A | 发送 Method 响应 |
| 5 | `PICC_GetMethodData()` | A→M | 获取 A 核 Method 请求数据及回调结果 |
| 6 | `PICC_GetResponseData()` | A→M | 获取 A 核 Method 响应数据及回调结果 |
| 7 | `PICC_GetEventData()` | A→M | 获取 A 核 Event 通知数据及回调结果 |
| 8 | `PICC_GetLinkState()` | 查询 | 查询物理通道健康状态（基于心跳） |
| 9 | `PICC_GetAppLinkState()` | 查询 | 查询应用级链路连接状态（per-app） |

---

## 3. PICC_Init 配置说明

### 3.1 前置条件

在调用 `PICC_Init()` 之前，必须先调用 `PICC_PreOS_Init()`。该函数在 `main()` 中、FreeRTOS 调度器启动之前调用。

```c
int main(void)
{
    /* ... 其他初始化 ... */
    PICC_PreOS_Init();      /* 必须在任何 PICC_Init() 之前调用 */
    vTaskStartScheduler();  /* 启动 FreeRTOS 调度器 */
}
```

`PICC_PreOS_Init()` 内部执行顺序：
1. 创建 RX 消息队列
2. 初始化 IPCF 驱动 (`ipc_shm_init`)
3. 调用 `PICC_InfraInit()`（含 TraceInit → ServiceLayerInit → GlobalInit → MailboxReady → LinkLayerInit）
4. 注册 Stack 消息回调
5. 初始化 IPCF Channel 1 和 Channel 2（含 Stack + Heartbeat）

### 3.2 PICC_AppConfig_t 结构体

```c
typedef struct {
    uint8                    localId;                  /* 本地 ID (1~254)，用作 g_appRegistry 直索引 */
    uint8                    remoteId;                 /* 对端 ID (1~254) */
    PICC_Role_e              role;                     /* PICC_ROLE_SERVER 或 PICC_ROLE_CLIENT */
    uint8                    channelId;                /* IPCF 通道号 (1 或 2) */
    uint16                   Client_linkReq_PeriodMs;  /* CLIENT 连接请求周期(ms)，0=默认10ms，SERVER忽略 */
    PICC_MethodCallback_t    methodHandler;             /* Method 请求回调（NULL=轮询模式） */
    PICC_EventCallback_t     eventHandler;              /* Event 通知回调（NULL=轮询模式） */
} PICC_AppConfig_t;
```

**v3.0 架构关键变化：**
- **不再使用 `PICC_AppIndex_e` 枚举**。`localId` 直接作为内部数组索引（O(1) 查找）。
- **不再需要手动指定 Slot 数量**。Slot 由 `role` 字段自动推导：
  - SERVER：methodSlots=2, responseSlots=0, eventSlots=2
  - CLIENT：methodSlots=0, responseSlots=2, eventSlots=2
- **新增 `Client_linkReq_PeriodMs`**：可自定义 CLIENT 发连接请求的周期。

### 3.3 ID 分配表（picc_id_map.h）

| 模块 | LOCAL (M核) | REMOTE (A核) | 角色 |
|------|:-----------:|:------------:|:----:|
| 电源管理 | 1 | 6 | Server |
| 设备管理 | 2 | 7 | Server |
| 健康管理-心跳 | 21 | 26 | Server |
| 诊断管理 | 52 | 60 | Server |
| SOA Adapter | 71 | 76 | Server |
| 健康管理-上报 | 81 | 91 | Server |

应用模块通过 `#include "picc_id_map.h"` 使用 `PICC_ID_xxx_LOCAL` / `PICC_ID_xxx_REMOTE` 宏。

### 3.4 两个回调字段说明

两个回调字段均为**可选**。传 `NULL` = 纯轮询模式，传函数指针 = 即时回调 + 轮询混合模式。

> ⚠️ **ISR 上下文警告**：回调直接运行在 IPCF 硬件 RX 中断中！执行时间严禁超过 **50μs**，严禁调用任何阻塞 OS API！

#### methodHandler

```c
typedef uint8 (*PICC_MethodCallback_t)(uint8 consumerId, uint8 methodId,
                                       const uint8 *reqData, uint16 reqLen,
                                       uint8 *rspData, uint16 *rspLen,
                                       uint8 *cbResult, uint16 *cbResultLen);
```

- `NULL`：纯轮询，通过 `PICC_GetMethodData()` 读取请求，**必须手动调用 `PICC_MethodResponse()` 回复**。
- 函数指针：请求到达时立即调用，驱动**自动发送 RESPONSE**（填写 rspData/rspLen），**禁止再手动调用 `PICC_MethodResponse()`**。

#### eventHandler

```c
typedef void (*PICC_EventCallback_t)(uint8 providerId, uint8 eventId,
                                     const uint8 *data, uint16 len,
                                     uint8 *cbResult, uint16 *cbResultLen);
```

- `NULL`：纯轮询，通过 `PICC_GetEventData()` 读取。
- 函数指针：事件到达时立即调用。适合微秒精度操作（如时间戳采集）。

---

## 4. API 详细说明

### 4.1 PICC_Init — 注册应用

```c
sint8 PICC_Init(const PICC_AppConfig_t *config);
```

**参数**：`config` — 应用配置指针

**返回值**：

| 返回码 | 含义 |
|--------|------|
| `PICC_E_OK` (0) | 注册成功 |
| `PICC_E_PARAM` (-2) | config=NULL 或 localId/remoteId 超出 [1,254] 范围 |
| `PICC_E_NOT_INIT` (-1) | PICC 基础设施未初始化（未调用 PICC_PreOS_Init） |
| `PICC_E_DUPLICATE` (-7) | localId 已被注册 |
| `PICC_E_NOMEM` (-3) | Slot 池耗尽 |
| `PICC_E_REMOTE_ID_CONFLICT` (-8) | remoteId 映射冲突 |

**内部流程**（自动完成，应用无需关心）：
1. 在 Mailbox 注册 app（包含 Slot 池分配）
2. 在共享 Link 池注册链路（Server 初始 DISCONNECTED，Client 初始 CONNECTING）
3. 注册 Method handler（如非 NULL）
4. 注册 Event handler（如非 NULL）

### 4.2 PICC_SendEvent — 发送 Event (M→A)

```c
sint8 PICC_SendEvent(uint8 localId, uint8 eventId,
                     const uint8 *data, uint16 len, PICC_EventType_e withAck);
```

| 参数 | 说明 |
|------|------|
| `localId` | 应用本地 ID（如 `PICC_ID_PWR_LOCAL`） |
| `eventId` | Event ID (1~254) |
| `data` | 负载数据指针 |
| `len` | 负载长度 |
| `withAck` | `PICC_EVENT_WITH_ACK` 或 `PICC_EVENT_WITHOUT_ACK` |

> 注：M核因实时性限制，建议使用 `PICC_EVENT_WITHOUT_ACK`。即使配置为 WITH_ACK，M核也不会处理返回的 ACK。

**返回值**：`PICC_E_OK` 成功 / `PICC_E_NOT_CONNECTED` 链路未连接 / 其他错误码

### 4.3 PICC_MethodRequest — 发送 Method 请求 (M→A, Client)

```c
uint8 PICC_MethodRequest(uint8 localId, uint8 methodId,
                         const uint8 *data, uint16 len,
                         PICC_MethodType_e type);
```

**返回值**：成功返回非零 SessionID (1~255)，失败返回 0。

SessionID 用于后续 `PICC_GetResponseData()` 匹配异步响应。

| MethodType | 说明 |
|------------|------|
| `PICC_METHOD_WITH_RESPONSE` | 需要 A 核返回业务 Response |
| `PICC_METHOD_NO_RETURN_WITH_ACK` | 不需业务 Response，但中间件返回 ACK |
| `PICC_METHOD_NO_RETURN_WITHOUT_ACK` | 即发即忘 |

### 4.4 PICC_MethodResponse — 发送 Method 响应 (M→A, Server)

```c
sint8 PICC_MethodResponse(uint8 localId, uint8 methodId,
                          uint8 sessionId, uint8 returnCode,
                          const uint8 *data, uint16 len);
```

> ⚠️ **仅在纯轮询模式（`methodHandler=NULL`）下使用！** 注册了回调的场景下，驱动自动发送 RESPONSE，禁止手动调用。

### 4.5 PICC_GetMethodData — 获取 Method 请求 (A→M)

```c
sint8 PICC_GetMethodData(uint8 localId, uint8 methodId,
                         uint8 *data, uint16 maxLen, uint16 *actualLen,
                         uint8 *outSessionId,
                         uint8 *cbResult, uint16 *cbResultLen);
```

| 出参 | 说明 |
|------|------|
| `data` | A核请求原始负载 |
| `actualLen` | 实际负载长度 |
| `outSessionId` | A核请求的 SessionID（回复 Response 时必须原样回传） |
| `cbResult` | 回调产生的结果（无回调时 cbResultLen=0），可传 NULL |
| `cbResultLen` | 回调结果长度，可传 NULL |

**返回值**：`PICC_E_OK` = 有新数据 / `PICC_E_NO_DATA` = 无新数据

### 4.6 PICC_GetResponseData — 获取 Method 响应 (A→M)

```c
sint8 PICC_GetResponseData(uint8 localId, uint8 methodId,
                           uint8 sessionId, uint8 *returnCode,
                           uint8 *data, uint16 maxLen, uint16 *actualLen,
                           uint8 *cbResult, uint16 *cbResultLen);
```

`sessionId` 必须与 `PICC_MethodRequest()` 返回的值一致，用于匹配异步响应。

### 4.7 PICC_GetEventData — 获取 Event 通知 (A→M)

```c
sint8 PICC_GetEventData(uint8 localId, uint8 eventId,
                        uint8 *data, uint16 maxLen, uint16 *actualLen,
                        uint8 *cbResult, uint16 *cbResultLen);
```

### 4.8 PICC_GetLinkState — 物理通道健康状态

```c
PICC_LinkState_e PICC_GetLinkState(uint8 channelId);
```

基于心跳（Ping/Pong）判断物理通道是否可用：
- 心跳周期：2000ms
- 失效条件：连续 3 次未收到 Pong

返回值：`PICC_LINK_STATE_CONNECTED` 或 `PICC_LINK_STATE_DISCONNECTED`。

### 4.9 PICC_GetAppLinkState — 应用级链路状态

```c
PICC_LinkState_e PICC_GetAppLinkState(uint8 localId);
```

返回该应用的连接握手状态：

| 状态 | 说明 |
|------|------|
| `PICC_LINK_STATE_DISCONNECTED` | 未连接 / 已断开 |
| `PICC_LINK_STATE_CONNECTING` | 正在连接（Client 周期发送连接请求） |
| `PICC_LINK_STATE_CONNECTED` | 已连接（握手完成） |

> 发送类 API 内部已自动检查应用级链路状态，未连接时返回 `PICC_E_NOT_CONNECTED`。应用层通常无需在发送前手动检查。

---

## 5. 错误码一览

| 宏 | 值 | 含义 |
|----|:--:|------|
| `PICC_E_OK` | 0 | 成功 |
| `PICC_E_NOT_INIT` | -1 | 基础设施未初始化 |
| `PICC_E_PARAM` | -2 | 参数无效 |
| `PICC_E_NOMEM` | -3 | 内存/Slot 不足 |
| `PICC_E_SEND` | -4 | 发送失败 |
| `PICC_E_NOT_CONNECTED` | -5 | 链路未连接 |
| `PICC_E_NO_DATA` | -6 | 无新数据 |
| `PICC_E_DUPLICATE` | -7 | localId 重复注册 |
| `PICC_E_REMOTE_ID_CONFLICT` | -8 | remoteId 映射冲突 |

---

## 6. 使用示例

### 6.1 场景一：纯轮询（电源管理模型，M核=Server）

```c
#include "picc_api.h"

void Pwsm_Init(void)
{
    static const PICC_AppConfig_t cfg = {
        .localId                = PICC_ID_PWR_LOCAL,     /* 1 */
        .remoteId               = PICC_ID_PWR_REMOTE,    /* 6 */
        .role                   = PICC_ROLE_SERVER,
        .channelId              = 2U,
        .Client_linkReq_PeriodMs = 0U,       /* Server 忽略此字段 */
        .methodHandler          = NULL,       /* 纯轮询模式 */
        .eventHandler           = NULL
    };
    (void)PICC_Init(&cfg);
}

void Pwsm_Main_10ms(void)
{
    uint8 buf[8];
    uint16 len;
    uint8 sessionId;

    /* 发送 Event (M→A) */
    uint8 payload[1] = { 0x04 };
    (void)PICC_SendEvent(PICC_ID_PWR_LOCAL, 1U,
                         payload, 1U, PICC_EVENT_WITHOUT_ACK);

    /* 轮询获取 Method 请求 (A→M) */
    if (PICC_GetMethodData(PICC_ID_PWR_LOCAL, 2U,
                           buf, sizeof(buf), &len,
                           &sessionId,
                           NULL, NULL) == PICC_E_OK) {
        /* 处理业务... */

        /* 纯轮询模式必须手动回复 Response */
        uint8 rsp[2] = { 0x03, 0x04 };
        (void)PICC_MethodResponse(PICC_ID_PWR_LOCAL, 2U,
                                  sessionId, 0x00, rsp, 2U);
    }
}
```

### 6.2 场景二：回调模式（时钟同步时间戳，M核=Client）

```c
static void TimeSync_EventCb(uint8 providerId, uint8 eventId,
                              const uint8 *data, uint16 len,
                              uint8 *cbResult, uint16 *cbResultLen)
{
    /* ISR 上下文 — 立即采集硬件时间戳 */
    uint32 ts = STM_GetCounter();
    cbResult[0] = (uint8)(ts >> 24);
    cbResult[1] = (uint8)(ts >> 16);
    cbResult[2] = (uint8)(ts >>  8);
    cbResult[3] = (uint8)(ts);
    *cbResultLen = 4U;
}

void TimeSync_Init(void)
{
    static const PICC_AppConfig_t cfg = {
        .localId                = 61U,
        .remoteId               = 66U,
        .role                   = PICC_ROLE_CLIENT,
        .channelId              = 1U,
        .Client_linkReq_PeriodMs = 10U,      /* 10ms 周期发连接请求 */
        .methodHandler          = NULL,
        .eventHandler           = TimeSync_EventCb
    };
    (void)PICC_Init(&cfg);
}

void TimeSync_Main_10ms(void)
{
    uint8 rxData[8]; uint16 rxLen;
    uint8 cbRes[8];  uint16 cbLen;

    if (PICC_GetEventData(61U, 0x01,
                          rxData, sizeof(rxData), &rxLen,
                          cbRes, &cbLen) == PICC_E_OK) {
        /* rxData = A核原始数据, cbRes = 回调中采集的时间戳 */
        uint32 localTs = ((uint32)cbRes[0] << 24) | ((uint32)cbRes[1] << 16) |
                         ((uint32)cbRes[2] <<  8) | ((uint32)cbRes[3]);
        /* 执行时钟对齐... */
    }
}
```

### 6.3 场景三：链路状态查询

```c
void MyApp_Main(void)
{
    /* 方式 1：查物理通道（心跳级别） */
    if (PICC_GetLinkState(2U) != PICC_LINK_STATE_CONNECTED) {
        /* 物理通道不健康，跳过所有业务 */
        return;
    }

    /* 方式 2：查应用级链路（握手级别） */
    if (PICC_GetAppLinkState(PICC_ID_PWR_LOCAL) != PICC_LINK_STATE_CONNECTED) {
        /* 该应用尚未完成握手 */
        return;
    }

    /* 链路已连接，执行业务... */
}
```

---

## 7. 数据流总结

```
A核数据到达 → PICC_StoreToMailbox(payload)    ── 存入 slot.data
           → 调用底层回调(产生 cbResult)       ── 存入 slot.cbResult
           │
           ▼
  应用层周期拉取 PICC_Get*Data(data, cbResult)
           │
           ├─ data     = A核原始数据
           └─ cbResult = 回调产生的结果（无回调时 cbResultLen=0）
```

---

## 8. 模块架构概览

```
┌─────────────────────────────────────────────────┐
│                   应用层 (SWC)                   │
│  Pwsm / OTA / Health / SOA / Diag / TimeSync    │
│         仅 #include "picc_api.h"                 │
└───────────────────┬─────────────────────────────┘
                    │ PICC_Init / SendEvent / MethodRequest / Get*Data
                    ▼
┌─────────────────────────────────────────────────┐
│              picc_api.c (公共 API 层)             │
│  参数校验 → 委托内部模块                          │
└───────┬──────────┬───────────┬──────────────────┘
        │          │           │
        ▼          ▼           ▼
  picc_mailbox   picc_service   picc_link
  (邮箱/注册)   (Event/Method)  (连接管理)
        │          │           │
        └────┬─────┘           │
             ▼                 │
        picc_stack ◄───────────┘
      (消息堆叠/CRC)
             │
             ▼
        picc_protocol          picc_heartbeat
      (协议编解码)           (心跳 Ping/Pong)
             │                      │
             └──────────┬───────────┘
                        ▼
                   IPCF Driver
                  (ipc-shm.h)
```

**各模块职责**：

| 模块 | 文件 | 职责 |
|------|------|------|
| API 层 | picc_api.h/c | 唯一公共接口，参数校验 + 委托 |
| 邮箱 | picc_mailbox.h/c | App 注册、Slot 池管理、消息路由存储、轮询读取 |
| 服务 | picc_service.h/c | Event/Method 发送、自动 ACK/Response、回调分发 |
| 链路 | picc_link.h/c | 共享 Link 池、连接握手、断开/重连处理 |
| 堆叠 | picc_stack.h/c | 消息堆叠发送（10ms 周期或缓冲区满）、Counter+CRC 封包/解包 |
| 协议 | picc_protocol.h/c | 8 字节协议头编解码、CRC16、字节序转换 |
| 心跳 | picc_heartbeat.h/c | 双向 Ping/Pong（2s 周期）、通道健康监测 |
| 追踪 | picc_trace.h/c | TX/RX 数据记录（TRACE32 调试用） |
| 入口 | picc_main.h/c | PreOS 初始化、IPCF 回调、RX 任务、消息分发 |
| ID表 | picc_id_map.h | 集中定义所有应用 LOCAL/REMOTE ID |

---

## 9. FAQ

### Q1：注册了回调后，还能调用 `PICC_Get*Data()` 轮询吗？

**可以，且推荐。** 邮箱始终存储 A 核原始数据 + 回调结果。回调用于微秒级即时操作，轮询用于业务逻辑处理。

### Q2：`PICC_MethodResponse` 什么时候用？

- `methodHandler=NULL`：**必须手动调用**，与 `PICC_GetMethodData` 成对使用。
- `methodHandler≠NULL`：**禁止手动调用**，驱动自动发送 Response。

### Q3：回调执行时间限制？

**严禁超过 50μs！** 回调运行在 IPCF RX ISR 中，禁止调用 `vTaskDelay`、Mutex、Semaphore 等阻塞 API。

### Q4：`PICC_GetLinkState` 和 `PICC_GetAppLinkState` 区别？

| API | 层级 | 判断依据 | 适用场景 |
|-----|------|----------|----------|
| `PICC_GetLinkState(channelId)` | 物理通道 | 心跳 Ping/Pong | 整体通道是否可用 |
| `PICC_GetAppLinkState(localId)` | 应用级 | 连接握手状态 | 特定应用是否已建链 |

### Q5：v3.0 相比旧版本的主要变化？

1. **移除 `PICC_AppIndex_e` 枚举** — `localId` 直接作为数组索引，支持 120+ 应用。
2. **`PICC_Init()` 参数简化** — 不再需要 `appIndex` 参数，只传 `config` 指针。
3. **新增 `PICC_GetAppLinkState()`** — 支持 per-app 链路状态查询。
4. **新增 `Client_linkReq_PeriodMs`** — CLIENT 可自定义连接请求周期。
5. **Slot 自动分配** — 根据 `role` 自动推导，无需手动指定。
6. **共享 Link 池** — 相同 (remoteId, channelId, role) 的多个应用共享一个链路上下文。
7. **`PICC_GetMethodData` 新增 `outSessionId` 出参** — 用于纯轮询模式下获取请求的 SessionID 以正确回复 Response。
