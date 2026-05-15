# PICC_APP_MAX 从 10 扩展到 120 的可行性分析

## 结论：不能简单修改 `PICC_AppIndex_e` 到 120

直接将 `PICC_APP_MAX` 改为 120 会导致 **静态 RAM 占用增加约 100KB**，远超 M7 核心的内存能力。
需要进行架构重设计才能支持 120 个应用。

---

## 1. 当前架构中受 PICC_APP_MAX 影响的全部项

### 1.1 直接以 PICC_APP_MAX 为大小的静态数组

| 文件 | 变量名 | 类型 | 10个应用时大小 | 120个应用时大小 |
|------|--------|------|---------------|----------------|
| [picc_mailbox.c:73](file:///d:/MySandbox/SDV_Project/IPCF_FreeRTOS_S32G399A_M7_Oring/SWC/PICC/Picc_Deamon/picc_mailbox.c#L73) | `g_appContexts[PICC_APP_MAX]` | PICC_AppContext_t | ~240B | ~2,880B |
| [picc_mailbox.c:76](file:///d:/MySandbox/SDV_Project/IPCF_FreeRTOS_S32G399A_M7_Oring/SWC/PICC/Picc_Deamon/picc_mailbox.c#L76) | `g_rxMailbox[PICC_APP_MAX]` | PICC_RxMailbox_t | **~8,700B** | **~104,160B** |
| [picc_main.c:134](file:///d:/MySandbox/SDV_Project/IPCF_FreeRTOS_S32G399A_M7_Oring/SWC/PICC/Picc_Deamon/picc_main.c#L134) | `g_diagRecord_Debug.appLinkState[PICC_APP_MAX]` | PICC_LinkState_e | ~40B | ~480B |

### 1.2 独立但需同步的宏

| 文件 | 宏名 | 当前值 | 需改为 |
|------|------|--------|--------|
| [picc_link.h:34](file:///d:/MySandbox/SDV_Project/IPCF_FreeRTOS_S32G399A_M7_Oring/SWC/PICC/Picc_Deamon/picc_link.h#L34) | `PICC_MAX_LINK_APPS` | 10 | 120 |

对应数组：
| 文件 | 变量名 | 10个应用时大小 | 120个应用时大小 |
|------|--------|---------------|----------------|
| [picc_link.c:43](file:///d:/MySandbox/SDV_Project/IPCF_FreeRTOS_S32G399A_M7_Oring/SWC/PICC/Picc_Deamon/picc_link.c#L43) | `g_linkContexts[PICC_MAX_LINK_APPS]` | ~280B | ~3,360B |

### 1.3 Handler 注册表限制

| 文件 | 宏名 | 当前值 | 影响 |
|------|------|--------|------|
| [picc_service.h:96](file:///d:/MySandbox/SDV_Project/IPCF_FreeRTOS_S32G399A_M7_Oring/SWC/PICC/Picc_Deamon/picc_service.h#L96) | `PICC_MAX_EVENT_HANDLERS` | 4 | 最多4个应用能注册Event回调 |
| [picc_service.h:99](file:///d:/MySandbox/SDV_Project/IPCF_FreeRTOS_S32G399A_M7_Oring/SWC/PICC/Picc_Deamon/picc_service.h#L99) | `PICC_MAX_METHOD_HANDLERS` | 4 | 最多4个应用能注册Method回调 |

---

## 2. 内存影响计算（核心问题）

### 2.1 每个 PICC_RxMailbox_t 结构大小拆解

```
PICC_RxSlot_t (每个 slot):
├── boolean  ready            1B
├── uint8    msgId            1B
├── uint8    sessionId        1B
├── uint8    returnCode       1B
├── uint8    data[32]        32B     ← PICC_RX_MAX_DATA_LEN
├── uint16   dataLen          2B
├── uint8    cbResult[8]      8B     ← PICC_CB_RESULT_MAX_LEN
└── uint16   cbResultLen      2B
                             ≈ 48B per slot

PICC_RxMailbox_t (每个应用):
├── method[6]    = 6 × 48   = 288B   ← PICC_RX_MAX_SLOTS
├── response[6]  = 6 × 48   = 288B
├── event[6]     = 6 × 48   = 288B
├── methodVictim              1B
├── responseVictim            1B
└── eventVictim               1B
                             ≈ 867B per app（含对齐约 868B）
```

### 2.2 总内存对比

| 数组 | 10个应用（当前） | 120个应用 | 增量 |
|------|-----------------|----------|------|
| `g_rxMailbox[]` | 8,680 B | **104,160 B** | +95,480 B |
| `g_appContexts[]` | 240 B | 2,880 B | +2,640 B |
| `g_linkContexts[]` | 280 B | 3,360 B | +3,080 B |
| `appLinkState[]` (diag) | 40 B | 480 B | +440 B |
| **合计** | **~9.2 KB** | **~108 KB** | **+99 KB** |

> [!CAUTION]
> `configTOTAL_HEAP_SIZE` 仅为 **16 KB**，而仅 `g_rxMailbox[120]` 就需要 **~102 KB** 的静态 RAM。
> 即便这些是静态分配（不占 FreeRTOS 堆），M7 核心的 SRAM 总量也无法承受这种增长。

---

## 3. 除内存外的其他代码兼容性问题

### 3.1 ✅ uint8 溢出风险 — **无风险**
多处代码使用 `(uint8)appIndex >= (uint8)PICC_APP_MAX` 进行边界检查。
`PICC_APP_MAX = 120` 在 `uint8` 范围内（0-255），所以 **不会溢出**。

### 3.2 ✅ 循环变量 — **无风险**
`picc_mailbox.c:92` 循环使用 `uint8 a`，120 仍在 uint8 范围内。

### 3.3 ⚠️ 枚举定义方式 — **不实际**
当前 `PICC_AppIndex_e` 枚举逐个命名（PICC_APP_PWR=0, PICC_APP_OTA=1...），
要定义 120 个枚举值不现实。需要改为 **`#define` + `uint8` 参数类型**。

### 3.4 ⚠️ Event/Method Handler 注册表 — **瓶颈**
`PICC_MAX_EVENT_HANDLERS=4`、`PICC_MAX_METHOD_HANDLERS=4` 仅支持 4 个回调注册。
如果超过 4 个应用注册回调处理函数，`PICC_RegisterEventHandler()` / `PICC_RegisterMethodHandler()` 将返回 `-2`（registry full）。

---

## 4. 推荐的重构方案

> [!IMPORTANT]
> 核心思路：**将 appIndex 索引空间（0~119）与实际分配的 mailbox 槽位解耦**。
> 不为每个 appIndex 预分配完整的 mailbox，而是只为实际注册的应用分配资源。

### 方案：间接映射池化（Indirect Mapping + Pool）

```
新设计:
                                          实际分配的槽位（Pool）
  appIndex (0~119) ───┐                 ┌─────────────────────┐
                      │  映射表          │ Slot 0: mailbox     │
  appIndex 0 ─────→ [  0xFF (未注册) ]   │ Slot 1: mailbox     │
  appIndex 1 ─────→ [  0    ]─────────→ │ Slot 2: mailbox     │
  appIndex 2 ─────→ [  0xFF (未注册) ]   │ ...                 │
  ...                                    │ Slot N-1: mailbox   │
  appIndex 119 ───→ [  1    ]─────────→ └─────────────────────┘
                                          N = PICC_POOL_SIZE (实际同时活跃的应用数)
```

### 需要修改的项目清单

| # | 文件 | 修改项 | 说明 |
|---|------|--------|------|
| 1 | [picc_api.h](file:///d:/MySandbox/SDV_Project/IPCF_FreeRTOS_S32G399A_M7_Oring/SWC/PICC/Picc_Deamon/picc_api.h) | `PICC_AppIndex_e` → `#define PICC_APP_MAX 120U` + `typedef uint8 PICC_AppIndex_t` | 去掉穷举枚举，改用数字索引 |
| 2 | [picc_api.h](file:///d:/MySandbox/SDV_Project/IPCF_FreeRTOS_S32G399A_M7_Oring/SWC/PICC/Picc_Deamon/picc_api.h) | 所有 API 函数参数 `PICC_AppIndex_e` → `PICC_AppIndex_t` | 类型统一 |
| 3 | [picc_mailbox.c](file:///d:/MySandbox/SDV_Project/IPCF_FreeRTOS_S32G399A_M7_Oring/SWC/PICC/Picc_Deamon/picc_mailbox.c) | 新增映射表 `uint8 g_appToPool[PICC_APP_MAX]`=120B | appIndex → pool slot 映射 |
| 4 | [picc_mailbox.c](file:///d:/MySandbox/SDV_Project/IPCF_FreeRTOS_S32G399A_M7_Oring/SWC/PICC/Picc_Deamon/picc_mailbox.c) | `g_rxMailbox[PICC_POOL_SIZE]`（例如16~32） | 仅为实际注册的应用分配 mailbox |
| 5 | [picc_mailbox.c](file:///d:/MySandbox/SDV_Project/IPCF_FreeRTOS_S32G399A_M7_Oring/SWC/PICC/Picc_Deamon/picc_mailbox.c) | `g_appContexts[PICC_APP_MAX]` → 轻量化（仅存config指针/索引） | 120×24B=2.9KB，可接受 |
| 6 | [picc_link.h](file:///d:/MySandbox/SDV_Project/IPCF_FreeRTOS_S32G399A_M7_Oring/SWC/PICC/Picc_Deamon/picc_link.h) | `PICC_MAX_LINK_APPS` 改为与池大小匹配 | Link 上下文也池化 |
| 7 | [picc_service.h](file:///d:/MySandbox/SDV_Project/IPCF_FreeRTOS_S32G399A_M7_Oring/SWC/PICC/Picc_Deamon/picc_service.h) | `PICC_MAX_EVENT_HANDLERS` / `PICC_MAX_METHOD_HANDLERS` 增大 | 按实际需要 |
| 8 | [picc_main.c](file:///d:/MySandbox/SDV_Project/IPCF_FreeRTOS_S32G399A_M7_Oring/SWC/PICC/Picc_Deamon/picc_main.c) | 诊断数组改为池大小或按需查询 | 减少内存占用 |
| 9 | 所有应用层 (pwsm.c, hm.c 等) | `PICC_APP_PWR` 等枚举改为 `#define` 常量 | 兼容新类型 |

### 内存估算（池化方案，Pool=32）

| 数组 | 大小 |
|------|------|
| `g_appToPool[120]` (映射表) | 120 B |
| `g_appContexts[120]` (轻量) | ~2,880 B |
| `g_rxMailbox[32]` (池) | ~27,776 B |
| `g_linkContexts[32]` (池) | ~896 B |
| **合计** | **~31 KB** |

对比直接改120的 ~108KB，节省约 77KB，且仍可支持 120 个 appIndex 空间。

---

## 5. 总结

| 问题 | 能否直接改 PICC_APP_MAX=120？ |
|------|------|
| 内存占用 | ❌ g_rxMailbox 会膨胀到 ~104KB，无法承受 |
| 枚举定义 | ❌ 120 个枚举值不实际，需改为 #define |
| uint8 类型兼容 | ✅ 120 在 uint8 范围内 |
| PICC_MAX_LINK_APPS | ❌ 需要同步修改 |
| Handler 注册限制 | ⚠️ 当前最多 4 个，需按需增大 |
| 初始化循环 | ⚠️ 可接受但初始化时间增加 |

> [!IMPORTANT]
> **结论**：不能直接简单修改 `PICC_AppIndex_e` 支持 120。必须采用 **间接映射 + 池化** 的方案重构 mailbox 和 link 模块，
> 将 appIndex 索引空间与实际分配的资源（mailbox/link context）解耦，才能在有限的 M7 SRAM 内支持 120 个应用。

如果您确认要按此方案进行重构，请告知：
1. **实际同时活跃的应用数**（Pool 大小），是否 32 足够？
2. **哪些应用需要回调模式**（决定 Handler 注册表大小）
