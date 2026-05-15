# PICC 架构重构 v4（Final）

## 修订历史
| 版本 | 关键变更 |
|------|---------|
| v3→v4 | **实际 PICC APP 仅 ~20 个**（SOA 150 服务复用 1 个 APP）→ 数组大幅缩减; picc_id_map.h 命名修正; channelId 校验纳入 v4; PICC_RX_MAX_DATA_LEN 保持 32B |

---

## 1. 关键发现：实际 PICC APP 数量

### 1.1 当前已注册 APP（从代码实证）

| APP | localId | remoteId | Role | Ch | 文件 |
|-----|---------|----------|------|----|------|
| PWR | 1 | 6 | SERVER | 2 | pwsm.c:70 |
| DM(pwsm) | 2 | 7 | SERVER | 2 | pwsm.c:79 |
| HEALTH HB | 21 | 26 | SERVER | 1 | hm.c:167 |
| HEALTH Reporter | 81 | 91 | SERVER | 1 | hm.c:177 |
| DiagMgmt | 52 | 60 | SERVER | 1 | diag_mgmt.c:102 |
| SOA Adapter | 71 | 76 | SERVER | 2 | soa_adapter_main.c:464 |

**当前：6 个 PICC APP。**

### 1.2 "150" 的真实含义

从 soa.md 可知：SOA Adapter 内部通过 14B SOA Header（ServiceID+MethodID+InstanceID）做二次分发，150+ SOA 服务全部复用 **1 个 PICC APP**（localId=71）。

**因此**：即使未来扩展到 8 个功能域 × 每域 2~3 个子模块，PICC APP 总数也仅 **~20~30 个**。

### 1.3 数组规模决策

| 资源 | v3(150 APP) | **v4(30 APP)** | 节省 |
|------|-------------|----------------|------|
| g_appRegistry | [256] = 5,120B | **[128]** = 2,560B | 2,560B |
| g_slotPool | [256] = 12,288B | **[128]** = 6,144B | 6,144B |
| g_remoteToLocal | [256] = 256B | **[128]** = 128B | 128B |
| appLinkStateById | [256] = 256B | **[128]** = 128B | 128B |
| g_linkSharedPool | [20] = 560B | **[10]** = 280B | 280B |

> [!TIP]
> 使用 128 而非 30 是因为 ID 范围 1~127 可以直接做数组下标，且 128 是 2 的幂，有利于编译器优化。ID 范围 128~254 暂不使用，未来如果需要可扩展到 256。

---

## 2. 数据结构

### 2.1 PICC_AppEntry_t

```c
#define PICC_REGISTRY_SIZE    (128U)  /* 支持 localId 1~127 */

typedef struct {
    uint8    isRegistered;       /* 1B */
    uint8    remoteId;           /* 1B */
    uint8    role;               /* 1B */
    uint8    channelId;          /* 1B */
    uint16   linkReqPeriodMs;    /* 2B */
    uint16   methodSlotStart;    /* 2B — uint16 防越界 */
    uint8    methodSlotCount;    /* 1B */
    uint16   responseSlotStart;  /* 2B */
    uint8    responseSlotCount;  /* 1B */
    uint16   eventSlotStart;     /* 2B */
    uint8    eventSlotCount;     /* 1B */
    uint8    methodVictim;       /* 1B */
    uint8    responseVictim;     /* 1B */
    uint8    eventVictim;        /* 1B */
    uint8    linkSharedIdx;      /* 1B — 0xFF=未分配 */
    uint8    _pad;               /* 1B — 对齐到 20B */
} PICC_AppEntry_t;               /* 20B */

static PICC_AppEntry_t g_appRegistry[PICC_REGISTRY_SIZE]; /* 128×20 = 2,560B */

STATIC_ASSERT(sizeof(PICC_AppEntry_t) == 20U, "size mismatch");
```

### 2.2 Slot 池

```c
#define PICC_SLOT_POOL_SIZE   (128U)
#define PICC_RX_MAX_DATA_LEN  (32U)  /* 不可压缩：SOA头14B+业务数据18B=32B */

static PICC_RxSlot_t g_slotPool[PICC_SLOT_POOL_SIZE];  /* 128×48 = 6,144B */
static uint16        g_slotPoolNextFree;
```

**分配边界检查**：
```c
if ((g_slotPoolNextFree + totalNeeded) > PICC_SLOT_POOL_SIZE) {
    return PICC_E_RESOURCE;
}
```

### 2.3 反向映射 + 冲突检测

```c
static uint8 g_remoteToLocal[PICC_REGISTRY_SIZE]; /* 128B, 初始化0xFF */

static sint8 PICC_RegisterRemoteMapping(uint8 remoteId, uint8 localId) {
    if (remoteId >= PICC_REGISTRY_SIZE) return PICC_E_PARAM;
    if (g_remoteToLocal[remoteId] != 0xFFU &&
        g_remoteToLocal[remoteId] != localId) {
        PICC_HANDLE_ERROR(-30);
        return PICC_E_REMOTE_ID_CONFLICT;
    }
    g_remoteToLocal[remoteId] = localId;
    return PICC_E_OK;
}
```

### 2.4 Link 共享池

```c
#define PICC_LINK_SHARED_POOL_SIZE   (10U)

typedef struct {
    PICC_LinkConfig_t config;
    volatile PICC_LinkState_e state;
    uint8    isInitialized;
    uint16   periodCounter;
    uint8    backoffCounter;
    uint8    refCount;
} PICC_LinkShared_t;               /* ~28B */

static PICC_LinkShared_t g_linkSharedPool[PICC_LINK_SHARED_POOL_SIZE]; /* 10×28=280B */
```

**共享语义**：`config.localId` 取首个注册者的值，后续共享者不再发握手报文。

---

## 3. RX 路由逻辑（修复所有 P0 bug）

```c
void PICC_StoreToMailbox(const PICC_MsgHeader_t *header,
                          const uint8 *payload, uint16 payloadLen,
                          uint8 channelId)  /* v4: 新增 channelId 参数 */
{
    uint8 localId = 0xFFU;
    PICC_AppEntry_t *entry;

    switch (header->msgType) {
        case (uint8)PICC_MSG_REQUEST:
        case (uint8)PICC_MSG_REQUEST_NO_RETURN_WITH_ACK:
        case (uint8)PICC_MSG_REQUEST_NO_RETURN_WITHOUT_ACK:
            localId = header->providerId;
            break;
        case (uint8)PICC_MSG_RESPONSE:
            localId = header->consumerId;
            break;
        case (uint8)PICC_MSG_NOTIFICATION_WITH_ACK:
        case (uint8)PICC_MSG_NOTIFICATION_WITHOUT_ACK:
            if (header->providerId < PICC_REGISTRY_SIZE) {
                localId = g_remoteToLocal[header->providerId];
            }
            if (localId == 0xFFU) return;  /* 无映射，丢弃 */
            break;
        default:
            return;
    }

    /* 边界+注册校验 */
    if (localId == 0x00U || localId >= PICC_REGISTRY_SIZE) return;
    entry = &g_appRegistry[localId];
    if (!entry->isRegistered) return;

    /* channelId 防御校验 */
    if (entry->channelId != channelId) return;

    /* 路由到对应 slot 段 */
    switch (header->msgType) {
        case (uint8)PICC_MSG_REQUEST:
        case (uint8)PICC_MSG_REQUEST_NO_RETURN_WITH_ACK:
        case (uint8)PICC_MSG_REQUEST_NO_RETURN_WITHOUT_ACK:
            PICC_StoreToSlot(&g_slotPool[entry->methodSlotStart],
                             entry->methodSlotCount,
                             header->methodId, header->sessionId,
                             0U, payload, payloadLen,
                             &entry->methodVictim);
            break;
        case (uint8)PICC_MSG_RESPONSE:
            PICC_StoreResponseToSlot(&g_slotPool[entry->responseSlotStart],
                                      entry->responseSlotCount,
                                      header->methodId, header->sessionId,
                                      header->returnCode,
                                      payload, payloadLen,
                                      &entry->responseVictim);
            break;
        case (uint8)PICC_MSG_NOTIFICATION_WITH_ACK:
        case (uint8)PICC_MSG_NOTIFICATION_WITHOUT_ACK:
            PICC_StoreToSlot(&g_slotPool[entry->eventSlotStart],
                             entry->eventSlotCount,
                             header->methodId, 0U,
                             0U, payload, payloadLen,
                             &entry->eventVictim);
            break;
        default: break;
    }
}
```

---

## 4. API 变更

```c
/* 新增全局初始化（替代原 PICC_InfraInit 中的部分逻辑） */
void PICC_GlobalInit(void);

/* 删除 appIndex 参数 */
sint8 PICC_Init(const PICC_AppConfig_t *config);

/* 所有 API: PICC_AppIndex_e → uint8 localId */
sint8 PICC_SendEvent(uint8 localId, uint8 eventId, ...);
uint8 PICC_MethodRequest(uint8 localId, uint8 methodId, ...);
sint8 PICC_MethodResponse(uint8 localId, uint8 methodId, ...);
sint8 PICC_GetMethodData(uint8 localId, uint8 methodId, ...);
sint8 PICC_GetResponseData(uint8 localId, uint8 methodId, ...);
sint8 PICC_GetEventData(uint8 localId, uint8 eventId, ...);
PICC_LinkState_e PICC_GetAppLinkState(uint8 localId);
```

**PICC_AppConfig_t** 新增可选 slot 配置：
```c
typedef struct {
    uint8                    localId;
    uint8                    remoteId;
    uint8                    role;
    uint8                    channelId;
    uint16                   Client_linkReq_PeriodMs;
    PICC_MethodCallback_t    methodHandler;
    PICC_EventCallback_t     eventHandler;
    uint8                    methodSlots;    /* 0=默认2 */
    uint8                    responseSlots;  /* 0=默认2 */
    uint8                    eventSlots;     /* 0=默认2 */
} PICC_AppConfig_t;
```

---

## 5. picc_id_map.h（命名修正）

```c
#ifndef PICC_ID_MAP_H
#define PICC_ID_MAP_H

/* ===== 命名规则 =====
 * PICC_ID_<MODULE>_LOCAL  = M-Core 自身的 ProviderID/ConsumerID (用作 localId)
 * PICC_ID_<MODULE>_REMOTE = A-Core 对端的 ConsumerID/ProviderID (用作 remoteId)
 */

/* 电源管理 (M-Core=Server) */
#define PICC_ID_PWR_LOCAL           (1U)    /* M-Core ProviderID */
#define PICC_ID_PWR_REMOTE          (6U)    /* A-Core ConsumerID */

/* 设备管理 (M-Core=Server) */
#define PICC_ID_DM_LOCAL            (2U)
#define PICC_ID_DM_REMOTE           (7U)

/* 健康管理-心跳 (M-Core=Server) */
#define PICC_ID_HEALTH_HB_LOCAL     (21U)
#define PICC_ID_HEALTH_HB_REMOTE    (26U)

/* 诊断管理 (M-Core=Server) */
#define PICC_ID_DIAGMGMT_LOCAL      (52U)   /* 0x34 */
#define PICC_ID_DIAGMGMT_REMOTE     (60U)   /* 0x3C */

/* SOA Adapter (M-Core=Server) */
#define PICC_ID_SOA_LOCAL           (71U)   /* 0x47 */
#define PICC_ID_SOA_REMOTE          (76U)   /* 0x4C */

/* 健康管理-Reporter (M-Core=Server) */
#define PICC_ID_HEALTH_RPT_LOCAL    (81U)
#define PICC_ID_HEALTH_RPT_REMOTE   (91U)

/* 编译时去重断言 */
STATIC_ASSERT(PICC_ID_PWR_LOCAL != PICC_ID_DM_LOCAL, "localId conflict");
STATIC_ASSERT(PICC_ID_PWR_LOCAL != PICC_ID_HEALTH_HB_LOCAL, "localId conflict");
/* ... 更多组合 ... */

#endif /* PICC_ID_MAP_H */
```

---

## 6. 内存预算（v4 Final）

| 组件 | 大小 | 说明 |
|------|------|------|
| g_appRegistry[128] | **2,560B** | 20B × 128 |
| g_slotPool[128] | **6,144B** | 48B × 128 |
| g_linkSharedPool[10] | **280B** | 28B × 10 |
| g_remoteToLocal[128] | **128B** | 反向映射 |
| appLinkStateById[128] | **128B** | 诊断 |
| Service Handlers (24+24) | **384B** | 扩容 |
| **合计** | **~9.6 KB** | 仅比当前 9.3KB 多 **~300B** |

> [!IMPORTANT]
> **v4 方案仅增加约 300B RAM，就将支持容量从 10 扩展到 127 个 PICC APP。**
> 这是因为 Slot 池从当前的 `10×18×48B = 8,640B` 压缩到 `128×48B = 6,144B`（按需分配 vs 全量预分配）。

---

## 7. 文件变更清单

### PICC Daemon

| 操作 | 文件 | 变更 |
|------|------|------|
| MODIFY | picc_api.h | 删除枚举; 签名改 uint8; 新增 PICC_GlobalInit; AppConfig 加 slot 字段 |
| MODIFY | picc_api.c | 实现适配 |
| MODIFY | picc_mailbox.h | 接口改 uint8; 新增 channelId 参数; slot 池声明 |
| MODIFY | picc_mailbox.c | **核心重构**: g_appRegistry[128] + g_slotPool[128] + g_remoteToLocal; 路由修复 |
| MODIFY | picc_link.h | 新增 PICC_LinkShared_t; PICC_LINK_SHARED_POOL_SIZE=10 |
| MODIFY | picc_link.c | 共享 Link 注册/查找; refCount |
| MODIFY | picc_service.h | Handler 扩容到 24 |
| MODIFY | picc_main.c | 诊断 uint8[128]; channelId 透传; PICC_GlobalInit 调用 |
| NEW | picc_id_map.h | ID 分配表 + LOCAL/REMOTE 命名 + STATIC_ASSERT |

### 应用层（仅签名变更）

| 文件 | 变更 |
|------|------|
| pwsm.c | `PICC_APP_PWR` → `PICC_ID_PWR_LOCAL`; `PICC_Init` 删第一参数 |
| hm.c | `PICC_APP_HEALTH` → `PICC_ID_HEALTH_HB_LOCAL` 等 |
| diag_mgmt.c | `PICC_APP_RSV0` → `PICC_ID_DIAGMGMT_LOCAL` |
| soa_adapter_main.c | `PICC_APP_SOA` → `PICC_ID_SOA_LOCAL` |

---

## 8. Verification Plan

1. **编译**：S32 Design Studio 无错误
2. **MAP 文件**：确认 .bss 增量 < 500B
3. **CMM 脚本**：验证 g_appRegistry/g_slotPool/g_linkSharedPool 状态
4. **回归**：PWR/HEALTH/SOA/DIAG 全部功能正常
