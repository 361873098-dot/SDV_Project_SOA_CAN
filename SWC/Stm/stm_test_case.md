# 存储中间件 (STM) TRACE32 交互式测试指南

本文档描述了如何使用集成在 **S32G399A M7 微控制器** 平台存储中间件 (STM) 模块中的交互式测试框架。通过在 **Lauterbach TRACE32** 调试器中修改全局变量 `NVM_test_flag` 的值（范围 1 到 9），开发人员可以动态执行本地 NVM 读写操作、格式化 EEPROM 持久化存储、触发跨核（M 核到 A 核）的通信数据验证，以及直接读取 EEPROM 原始字节进行页边界诊断，而无需重新烧录芯片。

---

## 1. 测试环境搭建与调试变量

STM 模块向调试器暴露了一组全局变量。这些变量在 `Stm_Main()` 任务（10ms 周期）的每个周期内通过助手函数 `Stm_ProcessTest()` 进行更新。

### 1.1 观测变量列表

| 变量名称 | 类型 | 作用域 | 用途/说明 |
| :--- | :--- | :--- | :--- |
| **`NVM_test_flag`** | `volatile uint8` | 全局变量 | **测试用例选择器**。当设置为非零值（1~9）时触发对应的测试分支。执行完成后会自动重置为 `0`。 |
| **`NVM_test_write_val`** | `uint8` | 全局变量 | 写入测试的**种子值**。写入数据块的数据从此值开始逐字节递增。（默认值：`0xAA`） |
| **`NVM_test_read_buf[16]`**| `uint8` | 全局变量 | **读取缓冲区**。在读取测试分支中接收来自本地 NVM 的数据。 |
| **`NVM_test_read_len`** | `uint16` | 全局变量 | **实际读取长度**。指示成功加载 to `NVM_test_read_buf` 中的有效字节数。 |
| **`NVM_test_result`** | `Std_ReturnType` | 全局变量 | **操作结果**。存储上一次执行的测试用例的状态（`0x00` = `E_OK`，`0x01` = `E_NOT_OK`）。 |

---

## 2. TRACE32 CMM 自动化脚本

以下是用于自动配置测试窗口的 Lauterbach CMM 脚本（`stm_test.cmm`）。

> [!IMPORTANT]
> 为了符合**规则 16 (Rule 16)**的要求，该脚本使用 `AREA.Create` 和 `AREA.View` 命令**仅打开一个专用的控制台显示窗口**，从而避免屏幕窗口过多和杂乱，同时确保能够直观地看到 `PRINT` 输出的内容。

```orcas
; ==============================================================================
; COPYRIGHT (C) Vitesco Technologies 2026
; File name: stm_test.cmm
; Description: 用于 STM NVM 交互式测试的 Lauterbach TRACE32 自动化脚本。
; ==============================================================================

; 清除旧的窗口布局
WinCLEAR

; 1. 创建且仅打开一个专用的日志输出区域，避免屏幕杂乱
AREA.Create STM_TEST_LOG 100. 30.
AREA.Select STM_TEST_LOG
AREA.View STM_TEST_LOG

PRINT "=========================================================="
PRINT "      STM NVM M7内核交互式测试控制台                      "
PRINT "=========================================================="
PRINT "正在检查符号可用性..."

; 2. 验证调试符号是否已加载
IF !symbol.exist(NVM_test_flag)
(
    PRINT "[-] 错误：未找到 NVM_test_flag 的调试符号。"
    PRINT "    请确保在 Debug 模式下编译项目并加载了 ELF 文件。"
    ENDDO
)

PRINT "[+] 符号验证成功！"
PRINT ""
PRINT "以下是十六进制格式 of STM 观测变量窗口："

; 3. 在屏幕上方打开一个变量监视窗口
Var.View %HEX NVM_test_flag NVM_test_write_val NVM_test_read_buf NVM_test_read_len NVM_test_result

PRINT ""
PRINT "操作指南："
PRINT "  - Case 1: 写入本地数据块 1 (8字节)，使用 NVM_test_write_val 作为种子值"
PRINT "  - Case 2: 读取本地数据块 1 到 NVM_test_read_buf 中"
PRINT "  - Case 3: 触发 Method 0x05 异步请求，要求 A 核读取数据块 2 (dataId 0x0002)"
PRINT "  - Case 4: 格式化 EEPROM 持久化存储（重新初始化所有数据块）"
PRINT "  - Case 5: 写入本地数据块 2 (16字节)，使用 NVM_test_write_val 作为种子值"
PRINT "  - Case 6: 读取本地数据块 2 到 NVM_test_read_buf 中"
PRINT "  - Case 7: 强制将所有有效数据块标记为 dirty，触发 Method 0x04 同步到 A 核"
PRINT "  - Case 8: 读取 EEPROM 地址 0x10 的原始 magic byte（页边界诊断）"
PRINT "  - Case 9: 从 EEPROM 地址 0x10 开始 dump 16 字节原始数据"
PRINT "=========================================================="

ENDDO
```

---

## 3. 详细测试用例与预期数据流（含具体 IPCF 报文）

### 测试用例 1 (NVM_test_flag = 1) ：本地 RAM 与 EEPROM 写入（数据块 1）
* **测试目的**：验证写入数据块 1（`dataId = 0x0001`，最大长度 8 字节）是否能正确更新 RAM 镜像并即时写入物理 EEPROM。
* **操作步骤**：
  1. 在 TRACE32 中设置写入种子值：
     ```orcas
     Var.set NVM_test_write_val = 0x55
     ```
  2. 触发 Case 1：
     ```orcas
     Var.set NVM_test_flag = 1
     ```
* **预期结果（具体变量与存储镜像）**：
  * **全局调试变量状态**：
    * `NVM_test_flag` 自动清零恢复为 `0`。
    * `NVM_test_result` 显示 `0x00` (`E_OK`)。
  * **RAM 镜像状态 (`g_nvmBlocks[0]`)**：
    * `data` 数组被填充为：`[0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C]`。
    * `dataLen` 成员更新为：`8`（十进制）。
    * `valid` 标志更新为：`0x01` (`TRUE`)。
    * `dirty` 标志更新为：`0x01` (`TRUE`)（指示本地有更改，需要通过 Method 0x04 同步给 A 核）。
    * `eepromOffset` 保持为：`0`（静态计算分配）。
    > [!TIP]
    > **如何在调试器中查看此 RAM 镜像**：可以在 TRACE32 命令行输入以下命令直接将其以结构体形式展开观测：
    > ```orcas
    > Var.View \\stm_nvm\g_nvmBlocks[0]
    > ```
  * **物理 EEPROM 状态（地址偏移 `0x11` 开始）**：
    * `0x11`（Valid）= `0x01`
    * `0x12`（Length）= `0x08`
    * `0x13 ~ 0x1A`（Payload）= `0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C`

---

### 测试用例 2 (NVM_test_flag = 2) ：本地 NVM 读取（数据块 1）
* **测试目的**：从 RAM 镜像中检索数据块 1 已持久化的数据，并验证其数据完整性。
* **操作步骤**：
  1. 触发 Case 2：
     ```orcas
     Var.set NVM_test_flag = 2
     ```
* **预期结果（具体变量）**：
  * **全局调试变量状态**：
    * `NVM_test_flag` 自动清零恢复为 `0`。
    * `NVM_test_result` 显示 `0x00` (`E_OK`)。
    * `NVM_test_read_len` 显示为 `8`（十进制）。
    * `NVM_test_read_buf[0..7]` 被精确填充为：`0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C`，其余字节自动清零。

---

### 测试用例 3 (NVM_test_flag = 3) ：跨核异步读取请求（Method 0x05 / 0x03 统一入口）
* **测试目的**：测试 M 核作为 Client 端的跨核读取能力，验证重写后的统一读请求 API `Stm_RequestReadFromA(methodId, dataId)`。此 API 统一支持 Method 0x03（读取最新数据）与 Method 0x05（读取滚动数据）的数据拉取。
* **操作步骤**：
  1. 确保 M 核与 A 核的跨核通信链路已建立成功，且状态机处于 `RUNNING`（运行）状态。
  2. 触发 Case 3（代码内部会调用 `Stm_RequestReadFromA(STM_METHOD_M_ASYNC_READ, 0x0002U)`）：
     ```orcas
     Var.set NVM_test_flag = 3
     ```
* **预期结果（具体变量与 IPCF 物理帧）**：
  * **全局调试变量及挂起状态**：
    * `NVM_test_flag` 自动恢复为 `0`，`NVM_test_result` 显示 `0x00` (`E_OK`)。
    * 挂起追踪结构体 `Stm_PendingReadReq` 状态更新为：
      * `dataId` = `0x0002`，`methodId` = `0x05`，`active` = `1U`。
      * `sessionId` = 获取到当前分配的唯一 SessionID（假设为首个请求 `0x01`）。
  * **IPCF 通道 1 发送物理帧数据（M核Client $\rightarrow$ A核Server）**：
    * **底层的 8 字节私有 IPC Header**：
      * `ProviderID` (A核Server) = `0x2F` (47)
      * `MethodID` (业务Method) = `0x05` (Method 0x05)
      * `ConsumerID` (M核Client) = `0x2A` (42)
      * `SessionID` = `0x01`
      * `MessageType` = `0x05` (REQUEST)
      * `ReturnCode` = `0x00`
      * `Length` (2B 负载长度) = `0x00 0x04` (4字节)
    * **4 字节业务 Payload**：
      * `dataId` (2B 大端) = `0x00 0x02`
      * `Reserved` (2B) = `0x00 0x00`
    * **单包私有协议数据**：`2F 05 2A 01 05 00 00 04 00 02 00 00`
    * **最终网络上传输的物理通道堆叠帧**：
      * 格式：`[1B CRC使能] [N字节私有包] [2B 通道发送Counter] [2B CRC16]`
      * 假设此时通道 Counter 累加为 `0x000A`，则发送帧数据为：
        `00 2F 05 2A 01 05 00 00 04 00 02 00 00 00 0A [CRC_H] [CRC_L]`
  * **IPCF 通道 1 接收物理帧数据（A核Server $\rightarrow$ M核Client）**：
    * A 核处理请求后，向 M 核回传包含 16 字节数据内容的 RESPONSE 帧。
    * **底层的 8 字节私有 IPC Header**：
      * `ProviderID` = `0x2F`，`MethodID` = `0x05`，`ConsumerID` = `0x2A`。
      * `SessionID` = `0x01` (匹配请求)。
      * `MessageType` = `0x80` (RESPONSE)
      * `ReturnCode` = `0x00` (成功处理)
      * `Length` (2B 负载长度) = `0x00 0x14` (20字节)
    * **20 字节业务 Payload**：
      * `dataId` (2B) = `0x00 0x02`
      * `status` (2B 成功) = `0x00 0x00`
      * `data` (16B 业务数据，假设为) = `0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F`
    * **最终网络上收到的物理通道堆叠帧**：
      * 格式同上，假设通道接收 Counter 为 `0x000B`：
        `00 2F 05 2A 01 80 00 00 14 00 02 00 00 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F 00 0B [CRC_H] [CRC_L]`
  * **接收解析**：M 核接收中断解析该报文并发现 `SessionID` 匹配 `0x01` 成功，会将 16 字节数据更新写入本地 Block 2 的 RAM 及 EEPROM 中，并将 `Stm_PendingReadReq.active` 重置为 `0`。

---

### 测试用例 4 (NVM_test_flag = 4) ：持久化 EEPROM 格式化测试
* **测试目的**：强制清空 EEPROM 格式并清除 RAM 镜像，验证系统的初始化回退与首飞（First-run）逻辑。
* **操作步骤**：
  1. 触发 Case 4：
     ```orcas
     Var.set NVM_test_flag = 4
     ```
* **预期结果（具体变量与存储空间变化）**：
  * **全局调试变量状态**：
    * `NVM_test_flag` 恢复为 `0`，`NVM_test_result` 显示 `0x00` (`E_OK`)。
  * **EEPROM 物理存储空间状态**：
    * `0x10`（Magic Byte）写入为首飞标志 `0xA5`。
    * 数据存储区 `0x11 ~ 0x50` 范围的所有 64 字节**全部被格式化清空为 `0x00`**。
  * **RAM 镜像状态 (`g_nvmBlocks[0..4]`)**：
    * 5 个块的数据缓存 `data` 全部填充为 `0`。
    * `valid` 强制标记为 `0x00` (`FALSE`)，`dirty` 强制标记为 `0x00` (`FALSE`)，`dataLen` 清零。
  * **安全联动测试**：
    * 再次在 TRACE32 中执行测试用例 2（读取数据块 1）。
    * 由于数据块 1 镜像的 `valid` 为 `FALSE`，`StmNvm_Read()` 触发安全防错，**`NVM_test_result` 预期显示为 `0x01` (`E_NOT_OK`)**，完美阻止无效数据输出。

---

### 测试用例 5 (NVM_test_flag = 5) ：本地 RAM 与 EEPROM 写入（数据块 2）+ 自动 Method 0x04 同步到 A 核
* **测试目的**：验证 NVM 数据块 2（`dataId = 0x0002`，最大长度 16 字节）的分段物理写入安全性，以及写入后 dirty 标记触发的 Method 0x04 跨核同步完整数据流。
* **前提条件**：M 核与 A 核的跨核通信链路已建立成功，且 STM 状态机处于 `RUNNING`（运行）状态。若未建链，写入仍会成功，但 Method 0x04 不会发送。
* **操作步骤**：
  1. 设置写入种子值：
     ```orcas
     Var.set NVM_test_write_val = 0x10
     ```
  2. 触发 Case 5：
     ```orcas
     Var.set NVM_test_flag = 5
     ```
* **预期结果（具体变量、存储镜像与 IPCF 报文数据流）**：

  #### 阶段一：本地 RAM + EEPROM 写入（Case 5 直接触发）

  由 `Stm_ProcessTest()` → `Stm_WriteLocal(0x0002, tempWriteBuf, 16)` 立即执行：

  * **全局调试变量状态**：
    * `NVM_test_flag` 自动清零恢复为 `0`。
    * `NVM_test_result` 显示 `0x00` (`E_OK`)。
  * **RAM 镜像状态 (`g_nvmBlocks[1]`)**：
    * `data` 前 16 字节被填充为：`[0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F]`。
    * `dataLen` 成员更新为：`16`（十进制）。
    * `valid` 标志更新为：`0x01` (`TRUE`)。
    * `dirty` 标志更新为：`0x01` (`TRUE`)（触发下一阶段 Method 0x04 同步）。
  * **物理 EEPROM 状态（地址偏移 `0x1B` 开始，计算规则：Block 0 占用 10 字节）**：
    * `0x1B`（Valid）= `0x01`
    * `0x1C`（Length）= `0x10`（即十进制 16）
    * `0x1D ~ 0x2C`（Payload）= `0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F`

  #### 阶段二：Method 0x04 自动同步到 A 核（下一 10ms 周期触发）

  在 Case 5 执行完毕后的下一个 `Stm_Main()` 10ms 周期中，状态机处于 `RUNNING`，`Stm_ProcessSyncToA()` 检测到 `g_nvmBlocks[1].dirty == TRUE`，自动发起 Method 0x04 请求：

  * **M 核发出数据：M 核 Client（ConsumerID=0x2A）→ A 核 Server（ProviderID=0x2F）**
  * **业务 Method：Method 0x04（STM_METHOD_M_SYNC_TO_A，M 核同步数据到 A 核）**
  * **IPCF 通道 1 发送物理帧数据（M核Client $\rightarrow$ A核Server）**：
    * **底层的 8 字节私有 IPC Header**：
      * `ProviderID` (A核Server) = `0x2F` (47)
      * `MethodID` (业务Method) = `0x04` (Method 0x04)
      * `ConsumerID` (M核Client) = `0x2A` (42)
      * `SessionID` = 自增的唯一 SessionID（假设累加为 `0x01`）
      * `MessageType` = `0x05` (REQUEST)
      * `ReturnCode` = `0x00`
      * `Length` (2B 负载长度) = `0x00 0x12` (18字节: 2B dataId + 16B data)
    * **18 字节业务 Payload**：
      * `dataId` (2B 大端) = `0x00 0x02`
      * `data` (16B 业务数据) = `0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F`
    * **单包私有协议数据**：`2F 04 2A 01 05 00 00 12 00 02 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F`
    * **最终网络上传输的物理通道堆叠帧**：
      * 格式：`[1B CRC使能] [N字节私有包] [2B 通道发送Counter] [2B CRC16]`
      * 假设此时通道 Counter 累加为 `0x0014`：
        `00 2F 04 2A 01 05 00 00 12 00 02 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F 00 14 [CRC_H] [CRC_L]`
  * **IPCF 通道 1 接收确认帧数据（A核Server $\rightarrow$ M核Client）**：
    * A 核收到 Method 0x04 同步数据后，向 M 核回传确认应答。
    * **底层的 8 字节私有 IPC Header**：
      * `ProviderID` = `0x2F`，`MethodID` = `0x04`，`ConsumerID` = `0x2A`。
      * `SessionID` = `0x01` (与请求完全一致)。
      * `MessageType` = `0x80` (RESPONSE)
      * `ReturnCode` = `0x00` (接收成功)
      * `Length` (2B 负载长度) = `0x00 0x04` (4字节: 2B dataId + 2B status)
    * **4 字节业务 Payload**：
      * `dataId` (2B) = `0x00 0x02`
      * `status` (2B 成功) = `0x00 0x00`
    * **最终网络上收到的物理通道堆叠应答帧**：
      * 假设通道接收 Counter 为 `0x0015`：
        `00 2F 04 2A 01 80 00 00 04 00 02 00 00 00 15 [CRC_H] [CRC_L]`
  * **同步完成**：M 核比对 `SessionID = 0x01` 响应成功，清除脏标志：`g_nvmBlocks[1].dirty = FALSE`。重试状态复位：`Stm_RetryState.active = 0`。

  > [!NOTE]
  > 如果 A 核未回复 RESPONSE，`Stm_RetryState` 将进入重试流程（100ms → 200ms → 400ms → 800ms，最多 4 次重试），可通过 `Stm_RetryState.active / retryCount / sessionId` 观察追踪。

---

### 测试用例 6 (NVM_test_flag = 6) ：本地 NVM 读取（数据块 2）
* **测试目的**：读取并检索数据块 2 存储的 16 字节持久化数据。
* **操作步骤**：
  1. 触发 Case 6：
     ```orcas
     Var.set NVM_test_flag = 6
     ```
* **预期结果（具体变量）**：
  * **全局调试变量状态**：
    * `NVM_test_flag` 自动清零恢复为 `0`。
    * `NVM_test_result` 显示 `0x00` (`E_OK`)。
    * `NVM_test_read_len` 显示为 `16`（十进制）。
    * `NVM_test_read_buf[0..15]` 被精确填充为：`0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F`。

---

### 测试用例 7 (NVM_test_flag = 7) ：强制触发 Method 0x04 同步到 A 核测试
* **测试目的**：验证所有本地修改（dirty，脏数据）的数据块是否能够安全、准确地同步到 A 核存储中间件中。
* **操作步骤**：
  1. 触发 Case 7：
     ```orcas
     Var.set NVM_test_flag = 7
     ```
* **预期结果（以同步 Case 1 写入的数据块 1 为例）**：
  * **全局调试变量及 RAM 标记**：
    * `NVM_test_flag` 恢复为 `0`，`NVM_test_result` 显示 `0x00` (`E_OK`)。
    * 扫描所有有效数据块，由于块 1 和块 2 的 `valid` 为 `TRUE`，对应的脏标志被唤醒置为 `TRUE`（例如 `g_nvmBlocks[0].dirty = 0x01`）。
  * **IPCF 通道 1 同步发送帧数据（M核Client $\rightarrow$ A核Server）**：
    * 同步子任务被唤醒。以数据块 1（`dataId = 0x0001`，长度 8 字节）的发送帧为例：
    * **底层的 8 字节私有 IPC Header**：
      * `ProviderID` (A核Server) = `0x2F` (47)
      * `MethodID` (业务Method) = `0x04` (Method 0x04)
      * `ConsumerID` (M核Client) = `0x2A` (42)
      * `SessionID` = 自增的唯一 SessionID（假设累加为 `0x15`）
      * `MessageType` = `0x05` (REQUEST)
      * `ReturnCode` = `0x00`
      * `Length` (2B 负载长度) = `0x00 0x0A` (10字节: 2B dataId + 8B data)
    * **10 字节业务 Payload**：
      * `dataId` (2B 大端) = `0x00 0x01`
      * `data` (8B 内容) = `0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C`
    * **单包私有协议数据**：`2F 04 2A 15 05 00 00 0A 00 01 55 56 57 58 59 5A 5B 5C`
    * **最终网络上传输的物理通道堆叠帧**：
      * 假设此时通道 Counter 累加为 `0x0012`：
        `00 2F 04 2A 15 05 00 00 0A 00 01 55 56 57 58 59 5A 5B 5C 00 12 [CRC_H] [CRC_L]`
  * **IPCF 通道 1 同步接收确认帧数据（A核Server $\rightarrow$ M核Client）**：
    * A 核收到同步数据后，向 M 核回传 Method 0x04 确认应答报文。
    * **底层的 8 字节私有 IPC Header**：
      * `ProviderID` = `0x2F`，`MethodID` = `0x04`，`ConsumerID` = `0x2A`。
      * `SessionID` = `0x15` (与发送请求完全一致)。
      * `MessageType` = `0x80` (RESPONSE)
      * `ReturnCode` = `0x00` (接收成功)
      * `Length` (2B 负载长度) = `0x00 0x04` (4字节: 2B dataId + 2B status)
    * **4 字节业务 Payload**：
      * `dataId` (2B) = `0x00 0x01`
      * `status` (2B 成功) = `0x00 0x00`
    * **最终网络上收到的物理通道堆叠应答帧**：
      * 假设通道接收 Counter 为 `0x0013`：
        `00 2F 04 2A 15 80 00 00 04 00 01 00 00 00 13 [CRC_H] [CRC_L]`
  * **同步完成**：M 核比对 `SessionID = 0x15` 响应成功，立刻清除该数据块的脏标志：`g_nvmBlocks[0].dirty = FALSE`。数据块正式恢复为 clean（干净）状态。

---

### 测试用例 8 (NVM_test_flag = 8) ：EEPROM Magic Byte 原始读取（页边界诊断）

* **测试目的**：直接从 EEPROM 物理地址 `0x10` 读取 1 字节的 Magic Byte 原始值，用于诊断是否存在页边界写入回绕导致 Magic Byte 被覆盖损坏的问题。
* **背景说明**：
  * EEPROM 的 Magic Byte 存储在地址 `0x10`（`STM_EEPROM_MAGIC_ADDR = 0x10`），正常值为 `0xA5`（`STM_EEPROM_MAGIC_VALUE`）。
  * 由于 EEPROM 页大小为 8 字节，地址 `0x10` 是页 2（`0x10~0x17`）的起始地址。如果向地址 `0x18~0x1F`（页 3）写入数据时发生跨页回绕 Bug，Magic Byte 不会被影响。但如果向地址 `0x10~0x17`（页 2）写入超出页边界的数据，回绕会覆盖该页开头，即 Magic Byte 本身。
  * 该测试绕过 NVM 层，直接调用 `Eeprom_ReadBytes()` 读取原始物理字节，因此即使 NVM 层的 RAM 镜像或格式化状态异常，也能获取到 EEPROM 的真实物理内容。
* **操作步骤**：
  1. 触发 Case 8：
     ```orcas
     Var.set NVM_test_flag = 8
     ```
* **预期结果（具体变量）**：
  * **全局调试变量状态**：
    * `NVM_test_flag` 自动清零恢复为 `0`。
    * `NVM_test_result` 显示 `0x00` (`E_OK`)（读取 I2C 操作成功）。
    * `NVM_test_read_buf[0]` = Magic Byte 原始值。
    * `NVM_test_read_len` = `0xA5xx`，其中 `xx` 为读到的 Magic Byte 值（编码格式：高字节固定 `0xA5` 作为标识，低字节为实际读到的值）。
  * **典型场景判断**：

    | `NVM_test_read_buf[0]` | `NVM_test_read_len` | 含义 |
    | :--- | :--- | :--- |
    | `0xA5` | `0xA5A5` | ✅ 正常：Magic Byte 未损坏，EEPROM 已格式化 |
    | `0x00` | `0xA500` | ⚠️ 未格式化：EEPROM 为出厂空白状态或被擦除 |
    | `0xFF` | `0xA5FF` | ⚠️ 未格式化：EEPROM 为出厂空白状态（0xFF 填充） |
    | 其他值 | `0xA5xx` | ❌ 异常：Magic Byte 被覆盖，可能存在页边界回绕 Bug |

  > [!TIP]
  > **诊断流程**：如果 Case 8 读到非 `0xA5` 值，可先执行 Case 4（格式化 EEPROM）再重新执行 Case 8 验证 Magic Byte 是否被正确写入为 `0xA5`。如果格式化后 Case 8 仍读不到 `0xA5`，则说明 `Eeprom_WriteBytes` 的跨页保护存在 Bug，需要检查底层 I2C 写入时序。

---

### 测试用例 9 (NVM_test_flag = 9) ：EEPROM 原始数据 Dump（Magic + Block[0] 区域）

* **测试目的**：从 EEPROM 物理地址 `0x10` 开始连续读取 16 字节原始数据，将 Magic Byte + Block[0] 的完整存储布局一次性 dump 到 `NVM_test_read_buf` 中，用于直观诊断 EEPROM 的物理存储内容是否正确。
* **背景说明**：
  * EEPROM 地址 `0x10~0x1F` 包含：
    * `0x10`：Magic Byte（应为 `0xA5`）
    * `0x11`：Block[0] Valid 标志（`0x01` = 已写入，`0x00` = 未写入）
    * `0x12`：Block[0] 数据长度（例如 `0x08` 表示 8 字节）
    * `0x13~0x1A`：Block[0] 数据内容（最多 8 字节，取决于 `maxDataLen`）
    * `0x1B~0x1F`：Block[1] 的 Valid + Length 字段（Block[1] 起始于 `0x1B`，占用 `0x1B~0x2C`）
  * 该测试绕过 NVM 层的 RAM 镜像，直接读取 EEPROM 的物理内容，适用于验证底层存储一致性。
* **操作步骤**：
  1. 触发 Case 9：
     ```orcas
     Var.set NVM_test_flag = 9
     ```
* **预期结果（具体变量）**：

  * **全局调试变量状态**：
    * `NVM_test_flag` 自动清零恢复为 `0`。
    * `NVM_test_result` 显示 `0x00` (`E_OK`)（读取 I2C 操作成功）。
    * `NVM_test_read_len` = `16`（固定读取 16 字节）。
    * `NVM_test_read_buf[0..15]` 包含地址 `0x10~0x1F` 的原始字节。

  * **典型场景：已执行 Case 1（写入 Block[0]）+ Case 4（格式化）后的 Dump**：

    假设先执行 Case 4 格式化，再执行 Case 1 写入 `NVM_test_write_val = 0x55`，则 `NVM_test_read_buf` 内容如下：

    | 偏移 | EEPROM 地址 | 字段含义 | 预期值 | 说明 |
    | :--- | :--- | :--- | :--- | :--- |
    | `[0]` | `0x10` | Magic Byte | `0xA5` | 格式化写入的标识 |
    | `[1]` | `0x11` | Block[0] Valid | `0x01` | Case 1 写入后 valid=TRUE |
    | `[2]` | `0x12` | Block[0] Length | `0x08` | Block[0] 数据长度 8 字节 |
    | `[3]` | `0x13` | Block[0] Data[0] | `0x55` | 种子值 0x55 |
    | `[4]` | `0x14` | Block[0] Data[1] | `0x56` | 0x55+1 |
    | `[5]` | `0x15` | Block[0] Data[2] | `0x57` | 0x55+2 |
    | `[6]` | `0x16` | Block[0] Data[3] | `0x58` | 0x55+3 |
    | `[7]` | `0x17` | Block[0] Data[4] | `0x59` | 0x55+4 |
    | `[8]` | `0x18` | Block[0] Data[5] | `0x5A` | 0x55+5 |
    | `[9]` | `0x19` | Block[0] Data[6] | `0x5B` | 0x55+6 |
    | `[10]` | `0x1A` | Block[0] Data[7] | `0x5C` | 0x55+7 |
    | `[11]` | `0x1B` | Block[1] Valid | `0x00` | Block[1] 未写入 |
    | `[12]` | `0x1C` | Block[1] Length | `0x00` | Block[1] 无数据 |
    | `[13]` | `0x1D` | Block[1] Data[0] | `0x00` | 未使用 |
    | `[14]` | `0x1E` | Block[1] Data[1] | `0x00` | 未使用 |
    | `[15]` | `0x1F` | Block[1] Data[2] | `0x00` | 未使用 |

  * **典型场景：仅格式化后未写入任何 Block 的 Dump**：

    | 偏移 | EEPROM 地址 | 预期值 | 说明 |
    | :--- | :--- | :--- | :--- |
    | `[0]` | `0x10` | `0xA5` | Magic Byte |
    | `[1]~[15]` | `0x11~0x1F` | `0x00` | 格式化清零的数据区 |

  > [!IMPORTANT]
  > **与 Case 2 / Case 6 的区别**：Case 2 和 Case 6 是通过 NVM 层（`StmNvm_ReadFromEeprom`）读取，会先检查 `valid` 标志、进行长度校验等安全处理；Case 8 和 Case 9 是直接调用 `Eeprom_ReadBytes` 读取原始物理字节，不经过任何 NVM 层校验，适合做底层硬件级诊断。

  > [!TIP]
  > **TRACE32 中查看 dump 结果**：执行 Case 9 后，可以在 TRACE32 命令行输入：
  > ```orcas
  > Var.View %HEX NVM_test_read_buf
  > ```
  > 直接以十六进制查看 16 字节 dump 内容，直观比对 EEPROM 物理布局。

---

## 4. 栈空间与运行安全性评估

为了防止在 M7 实时内核上发生系统崩溃或硬件错误（HardFault），本测试框架在设计上严格遵循了以下安全准则：
1. **禁止在栈上分配大数组**：临时缓冲区 `tempWriteBuf[16]` 仅占用 16 字节栈空间。这确保了在 FreeRTOS 任务上下文中调用时不会引发任务栈溢出，完美保持在 `OSTASK_10MS_STACK_SIZE` 安全阈值之内。
2. **自动清零触发机制**：触发变量 `NVM_test_flag` 在执行动作 of 周期末尾会被自动清零，防止测试循环往复触发，确保每次手动赋值仅执行一次动作。
3. **严格的越界和长度检查**：所有底层读写 API 均对数据项的长度进行严格越界和一致性校验，防止由于人为写入不合规的长度数据导致系统发生内存覆写或损坏。
