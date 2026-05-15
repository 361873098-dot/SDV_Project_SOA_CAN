# PICC 扩展性分析：从 20-30 APP 扩展到 120+ APP

## 问题根因分析

当前系统受到 **3层硬上限** 的约束，任何一层都会成为瓶颈：

| 层级 | 关键变量 | 当前上限 | 瓶颈原因 |
|------|---------|---------|---------|
| **API 层** | `PICC_APP_MAX = 10` | 10个APP | Enum + 静态数组索引 |
| **Mailbox 层** | `g_rxMailbox[PICC_APP_MAX]` | 10个APP | 静态二维数组 |
| **Link 层** | `PICC_MAX_LINK_APPS = 10` | 10个Link | 静态数组 |
| **Service 层** | `PICC_MAX_EVENT_HANDLERS = 4` `PICC_MAX_METHOD_HANDLERS = 4` | 4个回调 | 静态回调表 |

> [!IMPORTANT]
> 协议层面 ProviderID/ConsumerID 有效范围是 **0x01~0xFE**，即最多支持 **254个不同 ID**。
> 真正的限制完全在 M 核中间件的静态数组分配，而不在协议本身。

### 当前内存占用计算

每个 APP 的 Mailbox 占用：
```
PICC_RxMailbox_t = 3 × PICC_RX_MAX_SLOTS × sizeof(PICC_RxSlot_t)
                 = 3 × 6 × (1+1+1+1+32+2+8+2) = 3 × 6 × 48 = 864 bytes/APP
```
当前 10 个 APP = **8640 bytes**。

若扩展到 120 APP 且维持相同设计 = **103,680 bytes = ~100 KB**
→ 这在 FreeRTOS/M核 环境中**不可接受**（SRAM 通常 < 512 KB，还有 Stack/Heap 竞争）。

---

## 三套扩展方案对比

### 方案 A：纯静态扩容（简单粗暴，不推荐）

直接把所有 `_MAX` 宏从 10 改到 120。

**优点：** 代码改动最小  
**缺点：**
- Mailbox RAM 爆炸：120 × 864 = **103 KB**，不可接受
- 枚举 `PICC_AppIndex_e` 需要手动列出 120 个条目
- 完全不可扩展

❌ **直接否决**

---

### 方案 B：动态哈希注册表（推荐核心方向）

**核心思想：** 用 `localId`（即 ProviderID 或 ConsumerID）作为**唯一 Key** 直接路由，彻底废弃 `appIndex` 枚举。

#### B.1 关键设计改变

```
旧设计：应用层 → appIndex(enum) → 静态数组[appIndex]
新设计：应用层 → localId(uint8) → 哈希查找 → 动态注册表
```

**路由逻辑**（ProviderID 0x01~0xFE 范围内的哈希）：

```c
// 简单模运算哈希，避免用昂贵的通用哈希
#define PICC_APP_BUCKET_COUNT   (32U)   /* 质数更好，用 31 */
#define PICC_APP_BUCKET_CHAIN   (4U)    /* 每桶最多 4 个碰撞链条目 */

// 最多支持：32 桶 × 4 链 = 128 个 APP（覆盖 120+ 需求）
```

#### B.2 Mailbox 按需分配（关键内存节省）

> [!WARNING]
> 这是内存节省的核心：**当前每个 APP 固定分配 6个Method + 6个Response + 6个Event = 18个槽**，
> 但实际上大多数 APP 只需要 1-2 种消息类型。

改为**按需注册时声明需要哪些类型**：

```c
typedef struct {
    uint8   localId;
    uint8   remoteId;
    PICC_Role_e role;
    uint8   channelId;
    uint16  Client_linkReq_PeriodMs;
    PICC_MethodCallback_t methodHandler;
    PICC_EventCallback_t  eventHandler;

    /* 新增：声明此APP需要的槽数（节省内存）*/
    uint8   methodSlots;    /* 需要缓存的 Method 槽数 (0-6)，0=不接收 Method */
    uint8   responseSlots; /* 需要缓存的 Response 槽数 (0-6)，0=不发送 MethodRequest */
    uint8   eventSlots;    /* 需要缓存的 Event 槽数 (0-6)，0=不接收 Event */
} PICC_AppConfig_t;
```

**内存计算**（典型配置）：
- 纯 Server（只接收 Method）：`methodSlots=2, responseSlots=0, eventSlots=0` → **2×48 = 96 bytes**
- 纯 Client（只发 Method，收 Response）：`methodSlots=0, responseSlots=2, eventSlots=1` → **3×48 = 144 bytes**
- 典型混合 APP：约 **200-300 bytes**

120 个 APP × 平均 200 bytes = **24 KB**（vs 当前方案的 103 KB）✅

但这需要动态内存分配（FreeRTOS heap），或预留一个大的静态 slot pool。

#### B.3 静态 Slot Pool + 动态分配（推荐，避免 malloc）

```c
/* 全局 Slot Pool，按需从 pool 分配槽给每个 APP */
#define PICC_TOTAL_SLOT_COUNT   (128U)  /* 全局 pool，128个槽 */
static PICC_RxSlot_t g_slotPool[PICC_TOTAL_SLOT_COUNT];
static boolean       g_slotUsed[PICC_TOTAL_SLOT_COUNT];
```

每个 APP 注册时，根据 `methodSlots/responseSlots/eventSlots` 从 pool 借出对应数量的槽，
存储槽指针（非数据拷贝）到 APP 上下文中。

**内存总量：**
- Slot Pool：128 × 48 = **6,144 bytes = 6 KB**（固定上限，可控）
- APP 注册表：128 × sizeof(PICC_AppContext_t) ≈ 128 × 32 = **4 KB**
- 哈希表：32桶 × 4链 × 4字节(指针) = **512 bytes**
- **合计约 10-11 KB**（对比当前 8 KB，增加很少，但支持 120+ APP）

---

### 方案 C：消息队列分发架构（适合超高并发，过度设计）

用 FreeRTOS Queue 替换 Mailbox，每个 APP 有自己的接收队列。

**优点：** 天然 RTOS 集成，支持 blocking 接收  
**缺点：** 
- 每个 Queue 需要额外 FreeRTOS 开销（~40-80 bytes 元数据 + 深度×消息大小）
- 120 个 APP 的队列开销 = 120×(64+48)= **13 KB** 开销，还有 TCB 等
- 当前设计中 PICC 接收在 ISR 中完成，FreeRTOS Queue 在 ISR 里只能用 `xQueueSendFromISR`，有中断延迟
- 架构复杂度大幅提升

⚠️ **当前规模不需要，暂不推荐**

---

## 推荐方案：方案 B 精简版（最小改动，最大收益）

结合实际情况，建议采用以下**渐进式改造**：

### 第一步：解耦 appIndex，改用 localId 直接索引

**核心改变：`PICC_Init` 去掉 `appIndex` 参数。**

```c
/* 旧接口 */
sint8 PICC_Init(PICC_AppIndex_e appIndex, const PICC_AppConfig_t *config);

/* 新接口 —— appIndex 去掉，内部用 localId 作为索引键 */
sint8 PICC_Init(const PICC_AppConfig_t *config);
```

其他 API 同样用 `localId` 替代 `appIndex`：

```c
/* 旧接口 */
sint8 PICC_SendEvent(PICC_AppIndex_e appIndex, ...);
uint8 PICC_MethodRequest(PICC_AppIndex_e appIndex, ...);

/* 新接口 —— 直接传 localId，不需要枚举 */
sint8 PICC_SendEvent(uint8 localId, ...);
uint8 PICC_MethodRequest(uint8 localId, ...);
sint8 PICC_MethodResponse(uint8 localId, ...);
sint8 PICC_GetMethodData(uint8 localId, ...);
sint8 PICC_GetResponseData(uint8 localId, ...);
sint8 PICC_GetEventData(uint8 localId, ...);
PICC_LinkState_e PICC_GetAppLinkState(uint8 localId);
```

> [!NOTE]
> `appIndex` 的**唯一用途**是作为 `g_appContexts[appIndex]` 和 `g_rxMailbox[appIndex]` 的数组下标（O(1) 查找）。
> 去掉后，改为**按 localId 线性搜索**（O(n)），对于 n<128 且调用频率 10ms/次的场景，**开销完全可以接受**。
> 如果需要 O(1)，可用 `localId` 作为数组下标（0x01~0xFE → 数组下标 1~254），但需要 254 个槽的数组（约 254 × sizeof(PICC_AppContext_t)），见下方 "方案 B2"。

### 第二步：扩大静态表上限

**最简单实现：**

```c
/* picc_mailbox.c */
#define PICC_APP_MAX_DYNAMIC    (128U)   /* 原来的 10 改为 128 */

/* picc_link.h */
#define PICC_MAX_LINK_APPS      (128U)   /* 原来的 10 改为 128 */

/* picc_service.h */
#define PICC_MAX_EVENT_HANDLERS  (32U)   /* 原来的 4 改为 32 */
#define PICC_MAX_METHOD_HANDLERS (32U)   /* 原来的 4 改为 32 */
```

但 Mailbox 内存问题仍然存在（见方案 A 的分析），需要同时削减每个 APP 的槽数量。

### 第三步：压缩单 APP Mailbox 开销

```c
/* 将每个 APP 的 slot 数从 6 降到 3，大多数 APP 足够用 */
#define PICC_RX_MAX_SLOTS       (3U)    /* 从 6 改为 3 */

/* 同时将每个 slot 的 payload 从 32 bytes 压缩到实际需要的大小 */
/* 当前协议 payload 实际很少超过 16 bytes */
#define PICC_RX_MAX_DATA_LEN    (16U)   /* 从 32 改为 16 */
```

单 APP Mailbox 新大小：
```
3 × 3 × (1+1+1+1+16+2+8+2) = 3 × 3 × 32 = 288 bytes/APP
```

128 个 APP 合计：128 × 288 = **36 KB**（可接受）

> [!CAUTION]
> 将 `PICC_RX_MAX_DATA_LEN` 从 32 改为 16 之前，**必须确认所有 APP 的实际 payload 不超过 16 字节**。
> 请检查 SOA Adapter 的实际数据大小。如不确定，保留 32 bytes，128 APP × 6 slots × 32 bytes = 73 KB，仍然偏大。
> **建议先用方案 B2（localId直接索引）+ slot压缩到3个，再根据实际 APP 数量调整。**

---

## 方案 B2：localId 直接数组索引（最高效，推荐）

ProviderID/ConsumerID 范围是 0x01~0xFE（1~254），直接用 localId 做数组下标：

```c
/* APP 上下文表：下标 = localId，0x00 和 0xFF 不合法，故下标 0 和 255 不用 */
static PICC_AppContext_t g_appContexts[256U];  /* 256 × sizeof(PICC_AppContext_t) */
static PICC_RxMailbox_t  g_rxMailbox[256U];    /* 256 × sizeof(PICC_RxMailbox_t)  */
```

**内存占用（slot压缩后）：**
```
256 × (sizeof(PICC_AppContext_t) + sizeof(PICC_RxMailbox_t))
= 256 × (32 + 288)
= 256 × 320
= 81,920 bytes ≈ 80 KB
```

注意：这里 256 个槽是**全部可能的 ID 空间**，绝大多数不会被注册（isRegistered=FALSE），
仅占内存不占用任何运行时开销。

**优点：**
- O(1) 查找，零哈希碰撞
- localId 直接是下标，逻辑极简单
- 支持全部 254 个可能的 APP，远超 120 需求

**缺点：**
- 静态预留 80 KB（实际使用率可能只有 10-20%，有浪费）

> [!TIP]
> 如果 M 核 SRAM 充裕（≥512 KB），推荐 **方案 B2（localId直接索引）**。
> 如果 SRAM 紧张（< 256 KB），推荐 **方案 B（哈希 + Slot Pool）**。

---

## API 接口变更方案（保持最小变化）

### picc_api.h 变化

```c
/* ===== 删除 appIndex 枚举 ===== */
// 整个 PICC_AppIndex_e 枚举删除（或保留用于文档目的，但不再传入 API）

/* ===== 接口函数签名变化 ===== */

// BEFORE:
sint8 PICC_Init(PICC_AppIndex_e appIndex, const PICC_AppConfig_t *config);
// AFTER:
sint8 PICC_Init(const PICC_AppConfig_t *config);   // 内部用 config->localId 索引

// BEFORE:
sint8 PICC_SendEvent(PICC_AppIndex_e appIndex, uint8 eventId, ...);
// AFTER:
sint8 PICC_SendEvent(uint8 localId, uint8 eventId, ...);

// BEFORE:
uint8 PICC_MethodRequest(PICC_AppIndex_e appIndex, uint8 methodId, ...);
// AFTER:
uint8 PICC_MethodRequest(uint8 localId, uint8 methodId, ...);

// BEFORE:
sint8 PICC_MethodResponse(PICC_AppIndex_e appIndex, uint8 methodId, ...);
// AFTER:
sint8 PICC_MethodResponse(uint8 localId, uint8 methodId, ...);

// BEFORE:
sint8 PICC_GetMethodData(PICC_AppIndex_e appIndex, uint8 methodId, ...);
// AFTER:
sint8 PICC_GetMethodData(uint8 localId, uint8 methodId, ...);

// BEFORE:
sint8 PICC_GetResponseData(PICC_AppIndex_e appIndex, uint8 methodId, ...);
// AFTER:
sint8 PICC_GetResponseData(uint8 localId, uint8 methodId, ...);

// BEFORE:
sint8 PICC_GetEventData(PICC_AppIndex_e appIndex, uint8 eventId, ...);
// AFTER:
sint8 PICC_GetEventData(uint8 localId, uint8 eventId, ...);

// BEFORE:
PICC_LinkState_e PICC_GetAppLinkState(PICC_AppIndex_e appIndex);
// AFTER:
PICC_LinkState_e PICC_GetAppLinkState(uint8 localId);
```

### PICC_AppConfig_t 变化

```c
typedef struct {
    uint8                    localId;           /* 不变 */
    uint8                    remoteId;          /* 不变 */
    PICC_Role_e              role;              /* 不变 */
    uint8                    channelId;         /* 不变 */
    uint16                   Client_linkReq_PeriodMs; /* 不变 */
    PICC_MethodCallback_t    methodHandler;     /* 不变 */
    PICC_EventCallback_t     eventHandler;      /* 不变 */
    /* 可选新增（如需内存优化）: */
    /* uint8 methodSlots;   */  /* 0-6，0=不需要 */
    /* uint8 responseSlots; */  /* 0-6，0=不需要 */
    /* uint8 eventSlots;    */  /* 0-6，0=不需要 */
} PICC_AppConfig_t;
```

---

## 调用方（SWC）代码变更示例

```c
/* ===== 旧调用方式 ===== */
PICC_AppConfig_t cfg = {
    .localId  = 0x01,
    .remoteId = 0x06,
    .role     = PICC_ROLE_SERVER,
    .channelId = 1U,
    ...
};
PICC_Init(PICC_APP_PWR, &cfg);               // 需要枚举
PICC_SendEvent(PICC_APP_PWR, eventId, ...);  // 需要枚举
PICC_GetMethodData(PICC_APP_PWR, methodId, ...); // 需要枚举

/* ===== 新调用方式 ===== */
#define PWR_LOCAL_ID   (0x01U)  /* 用宏替代枚举，更灵活 */

PICC_AppConfig_t cfg = {
    .localId  = PWR_LOCAL_ID,
    .remoteId = 0x06,
    ...
};
PICC_Init(&cfg);                             // 更简单
PICC_SendEvent(PWR_LOCAL_ID, eventId, ...);  // 直接用 ID
PICC_GetMethodData(PWR_LOCAL_ID, methodId, ...);  // 直接用 ID
```

---

## 开问题（需要您决策）

> [!IMPORTANT]
> **问题 1：SRAM 容量确认**
> 方案 B2（localId直接索引，256个槽）需要约 **80 KB** 静态 RAM。
> 当前系统 SRAM 还有多少可用？如果不够，需要用哈希方案（B1）或进一步压缩 slot 数量。

> [!IMPORTANT]
> **问题 2：Slot 大小确认**
> `PICC_RX_MAX_DATA_LEN = 32` 可以压缩到 16 吗？
> SOA Adapter 的实际 payload 大小是多少？如果有超过 16 字节的 payload，不能压缩。

> [!IMPORTANT]  
> **问题 3：Service 回调表扩容**
> `PICC_MAX_EVENT_HANDLERS=4, PICC_MAX_METHOD_HANDLERS=4` 也需要扩大。
> 建议改为 32，是否可以接受？

> [!NOTE]
> **问题 4：是否保留 PICC_AppIndex_e 枚举（仅用于文档/注释）？**
> 去掉 appIndex 后，各 APP 的 localId 建议统一放在一个 `picc_id_map.h` 头文件中定义为宏，
> 方便管理，避免 ID 冲突。是否需要我同时设计这个头文件？

