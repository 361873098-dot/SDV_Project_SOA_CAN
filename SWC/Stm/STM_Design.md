# STM（存储中间件）— 软件设计文档

> **模块**：STM（Storage Middleware，存储中间件）
> **版本**：1.1
> **日期**：2026/05/26
> **作者**：weizhichun(uix08649)
> **目标平台**：S32G399A M7 核 + FreeRTOS

---

## 1. 概述

### 1.1 目的

STM 为 S32G3 M 核提供**非易失性数据管理**和 **M↔A 核数据同步**功能。它桥接了以下两个领域：

- **本地持久化** — 数据存储在 EEPROM 中，同时维护 RAM 镜像以实现零延迟读取
- **跨核同步** — 数据变更通过 PICC（IPCF 共享内存）自动在 M 核与 A 核之间传播

### 1.2 关键特性

| 属性                | 值                                                      |
| ------------------- | ------------------------------------------------------- |
| 任务周期            | 10ms（在 `TASK_M0_10MS` 中调用）                        |
| 传输通道            | IPCF 通道 1（高优先级），通过 PICC 中间件               |
| PICC 角色           | 双角色：**Provider（服务端）** + **Consumer（客户端）** |
| 持久化              | I2C EEPROM，分段写入（≤16 字节/次传输）                 |
| 最大数据项数        | 5                                                       |
| EEPROM 数据总量上限 | 64 字节                                                 |
| 重试支持            | 仅 Method 0x04（4 次重试，阶梯间隔）                    |

### 1.3 文件结构

```
SWC/Stm/
├── stm.h            — 公共 API（Stm_Init, Stm_Main, Stm_WriteLocal, Stm_ReadLocal, Stm_RequestReadFromA）
├── stm.c            — 核心逻辑：状态机 + 5 个子任务处理器 + PICC 交互
├── stm_cnf.h        — 配置头文件：PICC ID、Method ID、NVM 参数、状态机枚举
├── stm_cnf.c        — 配置实例：数据项表、重试间隔表
├── stm_nvm.h        — NVM 管理接口
└── stm_nvm.c        — NVM 实现：EEPROM 读写、RAM 镜像、I2C 分段传输
```

---

## 2. 架构

### 2.1 分层图

```
┌─────────────────────────────────────────────────────┐
│                   应用层 (SWC)                       │
│  Stm_WriteLocal() / Stm_ReadLocal() /              │
│  Stm_RequestReadFromA()                            │
└──────────────┬──────────────────────┬───────────────┘
               │                      │
┌──────────────▼──────────┐  ┌────────▼──────────────┐
│     stm.c (核心层)       │  │   stm_nvm.c (NVM 层)  │
│  ┌───────────────────┐  │  │  ┌─────────────────┐ │
│  │  状态机            │  │  │  │  RAM 镜像       │ │
│  │  (5 个状态)        │  │  │  │  g_nvmBlocks[]  │ │
│  ├───────────────────┤  │  │  ├─────────────────┤ │
│  │  子任务:           │  │  │  │  EEPROM I/O     │ │
│  │  - CheckLink      │  │  │  │  (分段传输)      │ │
│  │  - ProcessRxFromA │  │  │  └─────────────────┘ │
│  │  - ProcessSyncToA │  │  └───────────────────────┘
│  │  - ProcessAppRead │  │
│  └───────────────────┘  │
└──────────────┬──────────┘
               │
┌──────────────▼──────────┐
│   PICC 中间件            │
│   (IPCF 通道 1)          │
└─────────────────────────┘
```

### 2.2 双角色 PICC 注册

STM 在通道 1 上注册**两个** PICC 端点：

| 角色                   | PICC App 索引      | localId   | remoteId  | 用途                              |
| ---------------------- | ------------------ | --------- | --------- | --------------------------------- |
| **Provider（服务端）** | `PICC_APP_STORAGE` | 41 (0x29) | 47 (0x2F) | 接收来自 A 核的 Method 0x01/0x02  |
| **Consumer（客户端）** | `PICC_APP_STM_CLI` | 42 (0x2A) | 46 (0x2E) | 向 A 核发送 Method 0x03/0x04/0x05 |

两个链路都必须连接成功后 STM 才能运行（状态 `WAIT_LINK` → `WAIT_CONSISTENCY`）。

### 2.3 M/A 核双端点交互模型与交互机理

STM 基于底层高优先级 IPCF 通道 1，在逻辑上注册了 Provider 和 Consumer 双角色端点，形成了一个**对称双通道交互模型**。这种设计既满足了 A 核对 M 核的控制与写入需求，也保障了 M 核主动同步和读取数据的实时性。

#### 2.3.1 逻辑通道与数据流向
交互模型由两个独立的逻辑通道构成，流向清晰且相互隔离：

1.  **下行控制通道 (A 核 -> M 核 服务端)**：
    *   **通道映射**：M 核以 `PICC_APP_STORAGE` (localId: 41, 角色为 `PICC_ROLE_SERVER`) 身份，与 A 核的客户端 (remoteId: 47) 进行绑定。
    *   **业务特征**：A 核发起控制或写入请求。主要用于承载 **Method 0x01 (一致性检查)** 和 **Method 0x02 (A 核数据写入 M 核)**。
2.  **上行数据通道 (M 核 客户端 -> A 核 服务端)**：
    *   **通道映射**：M 核以 `PICC_APP_STM_CLI` (localId: 42, 角色为 `PICC_ROLE_CLIENT`) 身份，与 A 核的服务端 (remoteId: 46) 进行绑定。
    *   **业务特征**：M 核主动同步本地数据或发起异步回读。主要用于承载 **Method 0x04 (M 核同步到 A 核)** 和 **Method 0x03/0x05 (M 核自 A 核读取)**。

```
       A 核 (Application Core)                 M 核 (S32G3 M7 Core)
   ┌─────────────────────────────┐        ┌─────────────────────────────┐
   │  A-Core Client (Consumer)   │        │   M-Core Server (Provider)  │
   │  ID: 47                     │        │   ID: 41 (PICC_APP_STORAGE) │
   └──────────────┬──────────────┘        └──────────────┬──────────────┘
                  │   下行通道 (Method 0x01/0x02)        ▲
                  └──────────────────────────────────────┘
                  
   ┌─────────────────────────────┐        ┌─────────────────────────────┐
   │  A-Core Server (Provider)   │        │   M-Core Client (Consumer)  │
   │  ID: 46                     │        │   ID: 42 (PICC_APP_STM_CLI) │
   └──────────────▲──────────────┘        └──────────────▲──────────────┘
                  │   上行通道 (Method 0x03/0x04/0x05)   │
                  └──────────────────────────────────────┘
```

#### 2.3.2 交互机理与链路生命周期
1.  **分时链路握手**：
    *   作为 **Server** (41 端口)，M 核处于被动监听状态，不会主动发出建链请求。
    *   作为 **Client** (42 端口)，M 核启动后或检测到断开时，会以 10ms 为周期主动向 A 核发送 `LINK_AVAILABLE` 连接请求，直到 A 核返回同意响应（ReturnCode = 0x00）。
    *   **严格双通关口**：STM 业务层代码会在主任务中强制校验两个端点链路。只有当两个逻辑通道同时处于 `PICC_LINK_STATE_CONNECTED` 时，STM 才会进入后续的一致性检查和数据同步状态，任何单向连接都无法启动业务。
2.  **双向心跳监控 (Heartbeat)**：
    *   在双通道建立后，无论当前是否存在业务读写，M 核与 A 核的通信组件均以 **2 秒** 为固定周期，在 Channel 1 的两个端口上同时双向发送和接收特殊的 `Ping/Pong` 心跳报文（Ping: `ff 00 ff 00 ff 00 00 01 00`，Pong: `ff 00 ff 00 ff 00 00 01 01`）。
    *   若任一逻辑端口连续 3 次未收到 Pong 应答，判定为该介质中断，触发链路断开重置流（ResetOnDisconnect）。
3.  **多包堆叠发送与数据完整性校验 (CRC16)**：
    *   为提高 IPCF 通信的吞吐量，M 核发送和接收的报文均为**堆叠报文**，支持将多个 Event/Method 业务包合并拼接发送。
    *   所有待发堆叠数据在送入 IPCF 驱动前，均会进行全局封装：
        *   **首字节**：CRC 使能标志位（固定为 `0x00` 代表使能）。
        *   **数据体**：堆叠合并后的 N 个私有协议包。
        *   **尾部字段 1**：2 字节的通道全局发送计数器（Counter），每个通道独立累计，且在链路断开/重连时**不会被重置清零**。
        *   **尾部字段 2**：对前面所有数据字节进行大端序 CRC16 校验计算（使用指定的 `startValue=0xFFFF`, `xorValue=0x0000` 查表法），将 2 字节校验和附加在帧末尾，以实现车规级的通信完整性保障。

---

## 3. 状态机

### 3.1 状态转换图

```
                    Stm_Init()
                        │
                        ▼
                ┌───────────────┐
                │   UNINIT      │
                └───────┬───────┘
                        │
                        ▼
                ┌───────────────┐    链路断开
                │  WAIT_LINK    │◄──────────────────────────┬──────────────────────────┐
                └───────┬───────┘                            │                          │
                        │ 两个链路均连接成功                    │                          │
                        ▼                                    │                          │
                ┌───────────────┐                            │                          │
                │WAIT_CONSISTENCY│────── 链路断开 ────────────┤                          │
                └───────┬───────┘                            │                          │
                        │                                    │                          │
                        ▼                                    │                          │
                ┌───────────────┐                            │                          │
                │  SYNC_TO_A    │──── 链路断开 ──────────────┘                          │
                └───────┬───────┘                                                       │
                        │ 所有脏数据同步完成                                              │
                        ▼                                                               │
                ┌───────────────┐                                                       │
                │  RUNNING      │──── 链路断开 ─────────────────────────────────────────┘
                └───────────────┘
```

### 3.2 状态说明

| 状态               | 进入条件             | 活跃子任务                                     | 退出条件                                                                  |
| ------------------ | -------------------- | ---------------------------------------------- | ------------------------------------------------------------------------- |
| `UNINIT`           | 上电默认             | 无                                             | 调用 `Stm_Init()` → `WAIT_LINK`                                           |
| `WAIT_LINK`        | 初始化后或链路断开后 | 无（PICC 自动处理链路请求）                    | Provider + Consumer 链路均连接成功 $\rightarrow$ `WAIT_CONSISTENCY`       |
| `WAIT_CONSISTENCY` | 链路建立             | 链路活性监测 + 轮询 Method 0x01 请求           | 收到 A 核 0x01 请求且 payload 为 `0x0000` $\rightarrow$ 回应 0x0000 成功，将本地有效块全部置脏，跳转至 `SYNC_TO_A` |
| `SYNC_TO_A`        | 一致性检查通过       | 链路活性监测 + `Stm_ProcessSyncToA()` (0x04)   | 所有脏数据（0x04）均发送完毕且当前无在途重试 $\rightarrow$ `RUNNING`      |
| `RUNNING`          | 所有数据已同步       | 链路活性监测 + 4 个子任务全部活跃              | 链路断开（心跳检测超时或收到断开连接通知） $\rightarrow$ `WAIT_LINK`      |

### 3.3 断开连接时的状态重置

当任一 PICC 链路断开时，执行以下重置操作：

| 项目                        | 操作          | 原因                                    |
| --------------------------- | ------------- | --------------------------------------- |
| 状态                        | → `WAIT_LINK` | 必须重新建立链路后才能操作              |
| `Stm_SessionId`             | 重置为 0      | 按协议要求：断开连接时 session ID 重置  |
| `Stm_RetryState.active`     | 清除          | 取消正在进行的同步                      |
| `Stm_PendingReadReq.active` | 清除          | 取消挂起的读取请求                      |
| `Stm_SyncScanIndex`         | 重置为 0      | 重新开始轮询扫描                        |
| NVM dirty 标志              | **清除**      | 放弃待同步数据；RAM/EEPROM 中的数据保留 |
| NVM 数据（RAM/EEPROM）      | **保留**      | 数据在本地仍然有效                      |
| CRC/发送计数器              | **不重置**    | 由 PICC 层管理，断开连接后保持          |

---

## 4. Method 协议

### 4.1 Method 总览

| Method ID | 名称                | 方向 | PICC 角色 | 重试               | 描述                           |
| --------- | ------------------- | ---- | --------- | ------------------ | ------------------------------ |
| 0x01      | 一致性检查          | A→M  | 服务端    | 否                 | A 核请求 M 核验证/发送本地数据 |
| 0x02      | A 核写入            | A→M  | 服务端    | 否                 | A 核向 M 核 NVM 推送数据       |
| 0x03      | M 核从 A 核读取     | M→A  | 客户端    | 否                 | M 核从 A 核读取数据（同步）    |
| 0x04      | M 核同步到 A 核     | M→A  | 客户端    | **是**（4 次重试） | M 核向 A 核推送脏数据          |
| 0x05      | M 核异步从 A 核读取 | M→A  | 客户端    | 否                 | M 核从 A 核读取数据（异步）    |

### 4.2 Method 0x01 — 一致性检查（A→M）

```
来自 A 核的请求：
  Payload：[dataId_H][dataId_L]
  长度：2 字节

来自 M 核的响应：
  Payload：[dataId_H][dataId_L][status_H][status_H][data...]
  长度：4 + dataLen 字节
  ReturnCode：0x00（OK）或 0x01（NOT_OK）

  状态值：
    0x0000 = OK（找到数据并返回）
    0x0001 = NOT_OK（dataId 未找到或块无效）
```

### 4.3 Method 0x02 — A 核写入 M 核（A→M）

```
来自 A 核的请求：
  Payload：[dataId_H][dataId_L][data...]
  长度：2 + dataLen 字节

来自 M 核的响应：
  Payload：[dataId_H][dataId_L][status_H][status_L]
  长度：4 字节
  ReturnCode：0x00（OK）或 0x01（NOT_OK）

  状态值：
    0x0000 = OK（数据已写入 NVM）
    0x0001 = NOT_OK（dataId 无效、长度不匹配或 EEPROM 故障）
```

### 4.4 Method 0x04 — M 核同步到 A 核（M→A，带重试）

```
来自 M 核的请求：
  Payload：[dataId_H][dataId_L][data...]
  长度：2 + dataLen 字节
  PICC MethodType：PICC_METHOD_WITH_RESPONSE

来自 A 核的响应：
  （应用层自定义；M 核仅检查是否收到了 RESPONSE）
  - M 核不检查响应中的 ReturnCode
  - M 核不解析响应 Payload 内容
  - 任何 RESPONSE（MessageType=0x80）都被视为成功

脏标志（dirty）生命周期：
  - StmNvm_Write() 设置 dirty=TRUE，即使 EEPROM 写入成功也保持 TRUE
  - dirty 仅在 A 核确认接收后由 StmNvm_ClearDirty() 清除
  - 断开连接时：dirty 标志被清除（放弃待同步数据）

重试逻辑：
  - 同一时间只能有一个 0x04 同步请求在途
  - 阶梯重试间隔：100ms → 200ms → 400ms → 800ms
  - 最多 4 次重试，超限后放弃（清除 dirty 标志）
  - 防风暴：每个 10ms 周期最多发送 2 条同步消息
```

### 4.5 Method 0x05 — M 核异步从 A 核读取（M→A，无重试）

```
来自 M 核的请求：
  Payload：[dataId_H][dataId_L][0x00][0x00]
  长度：4 字节（2 字节 dataId + 2 字节保留）
  PICC MethodType：PICC_METHOD_WITH_RESPONSE

来自 A 核的响应：
  （在 Stm_ProcessAppReadReq 中处理，通过 sessionId 匹配）

约束条件：
  - 同一时间只能有一个读取请求在途
  - 失败或超时不会重试
  - 必须处于 RUNNING 状态
```

---

## 5. NVM 层设计

### 5.1 存储架构

```
┌─────────────────────────────────────────────┐
│              RAM 镜像 (g_nvmBlocks[])         │
│                                             │
│  块 0：[data(64B)][dataLen][valid][dirty][eepromOffset] │
│  块 1：[data(64B)][dataLen][valid][dirty][eepromOffset] │
│  块 2：[data(64B)][dataLen][valid][dirty][eepromOffset] │
│  块 3：[data(64B)][dataLen][valid][dirty][eepromOffset] │
│  块 4：[data(64B)][dataLen][valid][dirty][eepromOffset] │
│                                             │
│  读取路径：直接从 RAM 读取（零延迟）           │
│  写入路径：RAM 镜像 → EEPROM（立即）          │
└──────────────────┬──────────────────────────┘
                   │ I2C（分段传输，≤16B/次）
                   ▼
┌─────────────────────────────────────────────┐
│              I2C EEPROM                      │
│                                             │
│  地址 0x10：[magic = 0xA5]                  │
│  地址 0x11：[valid][len][data...]  块 0      │
│  地址 0x1B：[valid][len][data...]  块 1      │
│  地址 0x2D：[valid][len][data...]  块 2      │
│  地址 0x33：[valid][len][data...]  块 3      │
│  地址 0x41：[valid][len][data...]  块 4      │
│  地址 0x50：──── END ────                    │
└─────────────────────────────────────────────┘
```

### 5.2 EEPROM 块格式

每个数据块在 EEPROM 中的存储格式如下：

```
偏移     字段     大小   描述
─────    ─────    ────   ───────────
+0       valid    1B     TRUE(1) 表示块中有有效数据，FALSE(0) 表示空
+1       len      1B     实际数据长度（0..maxDataLen）
+2       data     NB     持久化数据字节（N = 配置中的 maxDataLen）

每块总计 = 2 + maxDataLen 字节
```

### 5.3 EEPROM 地址映射

| EEPROM 地址     | 内容              | 大小 |
| --------------- | ----------------- | ---- |
| `0x10`          | Magic 字节 (0xA5) | 1B   |
| `0x11` ~ `0x50` | 数据区（5 个块）  | 64B  |

### 5.4 数据项配置

当前配置（5 个数据项，总计 58B ≤ 64B）：

| 索引 | dataId | maxDataLen | EEPROM 大小 | 示例用途   |
| ---- | ------ | ---------- | ----------- | ---------- |
| 0    | 0x0001 | 8          | 2+8=10      | 标定数据   |
| 1    | 0x0002 | 16         | 2+16=18     | 配置块     |
| 2    | 0x0003 | 4          | 2+4=6       | 状态标志   |
| 3    | 0x0004 | 12         | 2+12=14     | 传感器偏移 |
| 4    | 0x0005 | 8          | 2+8=10      | 运行时参数 |
|      |        | **合计**   | **58**      |            |

### 5.5 初始化流程

```
StmNvm_Init()
     │
     ├─ memset(g_nvmBlocks, 0)          ← 清除所有 RAM 镜像块
     │
     ├─ StmNvm_ComputeOffsets()          ← 根据配置分配 EEPROM 偏移
     │
     ├─ Eeprom_ReadBytes(0x10, &magic)   ← 读取 magic 字节
     │
     ├── magic != 0xA5 ?                 ← 首次开机或 EEPROM 损坏？
     │    │
     │    └─ 是 → StmNvm_FormatEeprom()  ← 写入 magic + 清除所有块
     │
     └── magic == 0xA5                   ← EEPROM 有效
          │
          └─ 逐块执行：StmNvm_ReadBlockFromEeprom()
                        ├─ 读取 [valid][len] 头部（2B）
                        ├─ 校验 len ≤ maxDataLen
                        └─ 如果 valid 且 len > 0：读取数据
```

### 5.6 写入路径（本地写入或来自 A 核）

```
StmNvm_Write() / StmNvm_WriteFromA()
     │
     ├─ 校验：NVM 就绪、data != NULL、len ≤ maxDataLen
     │
     ├─ memcpy(data) 到 RAM 镜像          ← 立即更新
     ├─ 设置 valid=TRUE, dirty=TRUE       ← 标记为待同步给 A 核
     │
     ├─ StmNvm_WriteBlockToEeprom()       ← 持久化到 EEPROM
     │    ├─ 写入 [valid][len] 头部（2B）
     │    └─ 以 ≤16B 分段写入数据          ← I2C 事务大小限制
     │
     └─ EEPROM 成功：dirty 保持 TRUE      ← 数据已在本地持久化，但仍需同步给 A 核
                                             ← dirty 仅在 A 核确认接收后由 StmNvm_ClearDirty() 清除
```

### 5.7 读取路径（本地读取）

```
StmNvm_Read()
     │
     ├─ 校验：NVM 就绪、data != NULL、块有效
     │
     └─ 从 RAM 镜像 memcpy               ← 零延迟（无需访问 EEPROM）
```

### 5.8 本地 RAM 与 EEPROM 协同管理机制

为了满足高实时性的数据读取需求，并确保关键数据能可靠地持久化，STM 模块采用了 **"RAM 镜像缓存 + EEPROM 实时持久化 + 异步跨核同步"** 的协同管理模式。

#### 5.8.1 RAM 镜像与零延迟读取
*   **设计目的**：由于 I2C EEPROM 属于慢速外设，若每次读取都触发硬件总线传输，会导致任务阻塞并带来极高的延迟。
*   **结构定义**：全局分配 `g_nvmBlocks[STM_MAX_DATA_ITEMS]` 的静态 RAM 镜像数组（类型为 `Stm_NvmBlock_t`）。每个数据块在内存中包含本地缓存 `data`、当前长度 `dataLen`、有效标志 `valid`、脏数据标志 `dirty` 以及自动计算出的 `eepromOffset`。
*   **读取机制**：`StmNvm_Read()` 所有的读操作只访问 RAM 镜像，通过 `memcpy` 在微秒级内直接返回，实现**零硬件延迟**。

#### 5.8.2 EEPROM 物理布局与基准校验
*   **起始物理地址**：魔术字节（Magic Byte）位于 `0x10`，数据区从 `0x11` 开始（上限为 64 字节数据空间）。
*   **连续自动分配**：初始化阶段，根据配置表中每个 `dataId` 对应的 `maxDataLen`，以 `[valid(1B)] + [len(1B)] + [data(NB)]` 格式连续累加分配 EEPROM 地址偏移量。
*   **首次上电格式化**：在 `StmNvm_Init()` 中读取 `0x10` 物理地址。若魔术字节不等于 `0xA5`，则说明 EEPROM 未初始化或损坏，自动调用 `StmNvm_FormatEeprom()` 写入魔术字节，并将 EEPROM 存储区和 RAM 镜像整体清零。若等于 `0xA5`，则直接通过 `StmNvm_ReadBlockFromEeprom()` 将持久化数据载入 RAM 镜像。

#### 5.8.3 物理 EEPROM 分段写入 (Segmented Write)
*   **硬件驱动交易限制**：由于物理层 I2C EEPROM 驱动对单次总线交易的最大字节数有限制（`EEPROM_WRITE_MAX_LEN` = 16 字节），数据包不能一次性发送。
*   **两阶段分段传输**：
    1.  **第一阶段（Header）**：将 2 字节的 `[valid, dataLen]` 头部写入 EEPROM 对应的数据块物理首地址。
    2.  **第二阶段（Payload）**：计算实际数据长度，使用静态安全缓冲区 `s_writeBuf[16]` 按照每次最大 16 字节分段（Segmented）循环调用 I2C 写入接口，直到数据完全写入物理介质，有效防止栈溢出并契合驱动限制。

#### 5.8.4 脏标志 (dirty) 的状态与生命周期
`dirty` 标志作为 M 核和 A 核之间**非对称数据同步**的关键媒介，其状态流转遵循严格的规则：
1.  **本地写入 (M->EEPROM)**：应用层调用 `Stm_WriteLocal()` 时，数据拷贝至 RAM，`dirty` 标志置为 `TRUE`，随即开始物理 EEPROM 持久化。当 EEPROM 写入成功后，**`dirty` 标志依然保持 `TRUE`**，以表明该数据虽然在本地完成了持久化，但尚未通过 Method 0x04 发送到 A 核。
2.  **跨核同步完成 (Method 0x04 成功)**：在 `Stm_ProcessSyncToA()` 轮询中成功将脏块数据发送给 A 核并接收到 A 核的响应后，调用 `StmNvm_ClearDirty()` 将 `dirty` 标志置为 `FALSE`。
3.  **A 核主动覆写 (WriteFromA)**：当 A 核通过 Method 0x02 主动向 M 核写入数据时，由于这本就源自 A 核，因此 M 核在将数据成功存入 RAM + EEPROM 后，**会立即显式清除脏标志（`dirty = FALSE`）**，防止产生不必要的 Method 0x04 同步回传。
4.  **断开连接 (ResetOnDisconnect)**：如果链路断开或心跳超时，为了防止重连时数据流向紊乱，M 核会调用 `StmNvm_ResetOnDisconnect()` **强制将所有 Block 的 `dirty` 标志清零**（放弃先前未完成的同步请求），但 RAM 镜像与 EEPROM 物理存储中的本地数据依然保持有效。

---

## 6. 子任务设计

### 6.1 子任务执行矩阵

| 子任务                    | SYNC_TO_A | RUNNING | 周期 | 描述                        |
| ------------------------- | --------- | ------- | ---- | --------------------------- |
| `Stm_CheckLinkState()`    | —         | ✅       | 10ms | 监控两个 PICC 链路          |
| `Stm_ProcessRxFromA()`    | ✅         | ✅       | 10ms | 处理 Method 0x01/0x02       |
| `Stm_ProcessSyncToA()`    | ✅         | ✅       | 10ms | 通过 Method 0x04 同步脏数据 |
| `Stm_ProcessAppReadReq()` | —         | ✅       | 10ms | 处理 Method 0x03/0x05 响应  |

### 6.2 Stm_ProcessSyncToA — 重试流程

```
Stm_ProcessSyncToA()
     │
     ├── 有正在进行的重试（Stm_RetryState.active）？
     │    │
     │    ├── retryCount ≥ 4？ → ClearDirty，停止重试
     │    │
     │    ├── 收到响应？ → ClearDirty，停止重试
     │    │
     │    ├── tickCounter < interval[retryCount]？ → 等待
     │    │
     │    └── 间隔时间到？ → 重新发送，retryCount++
     │
     ├── 防风暴：syncCount ≥ 2？ → 跳过本周期
     │
     └── 查找下一个脏块（轮询扫描）
          │
          ├── 找到？ → 构建 payload，PICC_MethodRequest(0x04)
          │           设置重试状态，推进扫描索引
          │
          └── 未找到？ → 重置扫描索引为 0
```

### 6.3 重试间隔表

| 重试序号  | 间隔  | Tick 数（10ms） |
| --------- | ----- | --------------- |
| 0（首次） | 100ms | 10              |
| 1         | 200ms | 20              |
| 2         | 400ms | 40              |
| 3         | 800ms | 80              |

最大重试次数：**4 次**。耗尽后 dirty 标志被清除，该数据项被放弃直到下次写入。

---

## 7. Session ID 管理

### 7.1 规则

- **范围**：0x01 ~ 0xFF（达到 0xFF 后回绕至 0x01，永不使用 0x00）
- **作用域**：STM 模块内全局（非按 Provider）
- **断开连接时重置**：是，按协议要求
- **使用场景**：仅用于异步 Method 请求（0x03、0x05）

### 7.2 匹配逻辑

```
M 核发送：  PICC_MethodRequest() → 返回 sessionId
M 核存储：  Stm_PendingReadReq.sessionId = sessionId
M 核轮询：  PICC_GetResponseData(sessionId) → 匹配响应
```

---

## 8. 集成点

### 8.1 初始化序列

在 `EcuM_main_init.c` → `App_Init_All()` 中：

```c
Hm_Init();
Stm_Init();    // ← 在 Hm_Init() 之后添加
```

### 8.2 任务集成

在 `Ostask_main.c` → `TASK_M0_10MS` 中：

```c
Hm_Main();
Stm_Main();    // ← 在 Hm_Main() 之后添加
```

### 8.3 PICC App 索引映射

| 索引  | 枚举                   | 模块                       |
| ----- | ---------------------- | -------------------------- |
| 0     | `PICC_APP_PWSM_SRV`    | 电源管理服务端             |
| 1     | `PICC_APP_PWSM_CLI`    | 电源管理客户端             |
| 2     | `PICC_APP_DIAG`        | 诊断                       |
| **3** | **`PICC_APP_STM_CLI`** | **STM Consumer（客户端）** |
| 4     | `PICC_APP_STORAGE`     | STM Provider（服务端）     |

### 8.4 PICC 接收缓冲区

`picc_mailbox.c`：`PICC_RX_MAX_DATA_LEN` 从 **32** 增加到 **80** 字节，以支持最大 64 字节的 NVM 块加上协议头开销。

### 8.5 任务栈

`Ostask_main.c`：`OSTASK_10MS_STACK_SIZE` 从 **256** 增加到 **320** 字（word），以容纳 `Stm_Main()` 的处理。

---

## 9. 数据流示例

### 9.1 本地写入 + 同步到 A 核

```
应用调用 Stm_WriteLocal(0x0003, data, 4)
     │
     ├─ StmNvm_Write(0x0003, data, 4)
     │    ├─ 更新 RAM 镜像
     │    ├─ dirty = TRUE
     │    ├─ 写入 EEPROM（分段）
     │    └─ dirty 保持 TRUE（EEPROM 成功，数据仍需同步给 A 核）
     │
     └─ （下一个 10ms 周期在 Stm_Main() 中）
        │
        └─ Stm_ProcessSyncToA() 发现脏块
           ├─ 构建 payload：[0x00][0x03][data...]
           ├─ PICC_MethodRequest(0x04)
           └─ 设置重试状态

     （A 核响应到达）
        │
        └─ StmNvm_ClearDirty(0x0003)
```

### 9.2 A 核写入 M 核

```
A 核发送 Method 0x02：[0x00][0x01][8 字节数据]
     │
     └─ Stm_ProcessRxFromA()
        ├─ 解析 dataId=0x0001, dataLen=8
        ├─ StmNvm_WriteFromA(0x0001, data, 8)
        │    ├─ 更新 RAM 镜像 + EEPROM
        │    └─ dirty = TRUE（稍后同步回 A 核）
        │
        └─ PICC_MethodResponse(0x00, [0x00][0x01][0x00][0x00])
```

### 9.3 断开 → 重连序列

```
1. 链路断开（心跳超时或断开通知）
2. Stm_CheckLinkState() 检测到链路断开
3. 状态 → WAIT_LINK
4. 重置操作：
   - StmNvm_ResetOnDisconnect() → 清除 dirty 标志，保留数据
   - Stm_ResetSessionId() → sessionId = 0
   - 取消重试/挂起请求
5. PICC 自动发送链路请求（客户端角色）
6. 两个链路重新连接 → WAIT_CONSISTENCY
7. A 核发起 Method 0x01 一致性检查请求 (Payload=0x0000)
8. M 核就绪校验通过，回复成功响应 ([0x0000]+[0x0000])，调用 StmNvm_SetAllValidDirty() 批量置脏
9. 状态 → SYNC_TO_A，通过 Method 0x04 主动同步所有脏数据
10. 同步完毕，状态 → RUNNING，进入正常运行期
```

---

## 10. 配置指南

### 10.1 添加新数据项

1. 在 `stm_cnf.h` 中**增大 `STM_MAX_DATA_ITEMS`**（如果 > 5）
2. 在 `stm_cnf.c` 的 `g_StmDataItemCfg[]` 中**添加条目**：
   ```c
   { 0x0006U, 4U  },  /* 数据项 6：新增数据 */
   ```
3. **验证 EEPROM 容量**：`SUM(2 + maxDataLen) ≤ 64`
4. 如果需要更多空间，**调整 `STM_EEPROM_DATA_END_ADDR`**
5. 如果最大 payload 超过 80 字节，**增大 `PICC_RX_MAX_DATA_LEN`**

### 10.2 修改重试行为

在 `stm_cnf.h` / `stm_cnf.c` 中修改：

| 参数                     | 文件        | 默认值        | 描述                            |
| ------------------------ | ----------- | ------------- | ------------------------------- |
| `STM_RETRY_MAX_COUNT`    | `stm_cnf.h` | 4             | Method 0x04 最大重试次数        |
| `g_StmRetryIntervals[]`  | `stm_cnf.c` | {10,20,40,80} | 重试间隔（以 10ms tick 为单位） |
| `STM_SYNC_MAX_PER_CYCLE` | `stm_cnf.h` | 2             | 每个 10ms 周期最大同步消息数    |

### 10.3 修改 PICC ID

在 `stm_cnf.h` 中修改：

| 参数                     | 默认值    | 说明                              |
| ------------------------ | --------- | --------------------------------- |
| `STM_PROVIDER_ID`        | 41 (0x29) | M 核服务端 ID，必须在全系统内唯一 |
| `STM_CONSUMER_ID`        | 42 (0x2A) | M 核客户端 ID，必须在全系统内唯一 |
| `STM_PROVIDER_REMOTE_ID` | 46 (0x2E) | M 核客户端通信的 A 核服务端 ID    |
| `STM_CONSUMER_REMOTE_ID` | 47 (0x2F) | 与 M 核服务端通信的 A 核客户端 ID |

---

## 11. 已知限制与待办事项

| #   | 项目                               | 状态           | 说明                                                                                        |
| --- | ---------------------------------- | -------------- | ------------------------------------------------------------------------------------------- |
| 1   | `WAIT_CONSISTENCY` 状态            | **已实现**     | 在建链后挂起等待 A 核发起 0x01 一致性检查，校验通过后调用 SetAllValidDirty 触发 0x04 主动同步 |
| 2   | Method 0x03（M 核从 A 核同步读取） | **未完全实现** | 当前仅使用 0x05（异步）；0x03 响应解析待实现                                                |
| 3   | 读取请求超时                       | **已实现**     | 如果 A 核始终不响应，请求将保持挂起直到断开连接                                             |
| 4   | EEPROM 写入错误恢复                | **基础**       | 返回 E_NOT_OK 但不重试 EEPROM 写入                                                          |
| 5   | Method 0x04 最大重试后             | **放弃**       | dirty 标志被清除；同步数据丢失直到下次写入                                                  |
| 6   | 多个并发同步请求                   | **不支持**     | 同一时间只能有一个 0x04 同步在途                                                            |

---

## 12. 栈使用考量

`stm.c` 和 `stm_nvm.c` 中所有大缓冲区都声明为 `static`，以避免栈溢出：

| 变量           | 文件      | 大小 | 原因                        |
| -------------- | --------- | ---- | --------------------------- |
| `s_methodBuf`  | stm.c     | 68B  | Method 0x01/0x02 接收缓冲区 |
| `s_respBuf`    | stm.c     | 68B  | Method 0x01/0x02 发送缓冲区 |
| `s_syncBuf`    | stm.c     | 68B  | 同步 0x04 接收缓冲区        |
| `s_txPayload`  | stm.c     | 66B  | 同步 0x04 发送 payload      |
| `s_reqPayload` | stm.c     | 4B   | 读取 0x05 请求 payload      |
| `s_readRspBuf` | stm.c     | 68B  | 读取 0x03/0x05 响应缓冲区   |
| `s_writeBuf`   | stm_nvm.c | 16B  | EEPROM 分段写入缓冲区       |

**任务栈**：`OSTASK_10MS_STACK_SIZE = 320 字（1.25KB）` — 足以支持所有 STM 处理。
