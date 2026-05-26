# 存储中间件 (STM) TRACE32 交互式测试指南

本文档描述了如何使用集成在 **S32G399A M7 微控制器** 平台存储中间件 (STM) 模块中的交互式测试框架。通过在 **Lauterbach TRACE32** 调试器中修改全局变量 `NVM_test_flag` 的值（范围 1 到 7），开发人员可以动态执行本地 NVM 读写操作、格式化 EEPROM 持久化存储，并触发跨核（M 核到 A 核）的通信数据验证，而无需重新烧录芯片。

---

## 1. 测试环境搭建与调试变量

STM 模块向调试器暴露了一组全局变量。这些变量在 `Stm_Main()` 任务（10ms 周期）的每个周期内通过助手函数 `Stm_ProcessTest()` 进行更新。

### 1.1 观测变量列表

| 变量名称 | 类型 | 作用域 | 用途/说明 |
| :--- | :--- | :--- | :--- |
| **`NVM_test_flag`** | `volatile uint8` | 全局变量 | **测试用例选择器**。当设置为非零值（1~7）时触发对应的测试分支。执行完成后会自动重置为 `0`。 |
| **`NVM_test_write_val`** | `uint8` | 全局变量 | 写入测试的**种子值**。写入数据块的数据从此值开始逐字节递增。（默认值：`0xAA`） |
| **`NVM_test_read_buf[16]`**| `uint8` | 全局变量 | **读取缓冲区**。在读取测试分支中接收来自本地 NVM 的数据。 |
| **`NVM_test_read_len`** | `uint16` | 全局变量 | **实际读取长度**。指示成功加载到 `NVM_test_read_buf` 中的有效字节数。 |
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
PRINT "=========================================================="

ENDDO
```

---

## 3. 详细测试用例（用例序号与 NVM_test_flag 完美对应）

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
* **预期结果**：
  * `NVM_test_flag` 在 10ms 内自动恢复为 `0`。
  * `NVM_test_result` 显示 `0x00` (`E_OK`)。
  * 数据块 1 的 RAM 镜像更新为：`[0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C]`。
    > [!TIP]
    > **如何在调试器中查看此 RAM 镜像**：由于 RAM 镜像是静态（`static`）全局变量，定义在 `stm_nvm.c` 文件中。您可以在 TRACE32 命令行中输入以下命令直接将其以结构体形式展开观测：
    > ```orcas
    > Var.View \\stm_nvm\g_nvmBlocks[0]
    > ```
    > 展开后可查看其各成员变量：`data`（即本地缓存数据 `0x55..`）、`dataLen`（当前长度 `8`）、`valid`（是否有效 `1`）、`dirty`（是否为脏数据 `0`）等字段。
  * 物理 EEPROM 对应数据块 1 的地址空间显示完全相同的 8 字节数据，且前缀有正确的头部：`[0x01] [0x08]` (Valid=TRUE, Length=8)。

---

### 测试用例 2 (NVM_test_flag = 2) ：本地 NVM 读取（数据块 1）
* **测试目的**：从 RAM 镜像中检索数据块 1 已持久化的数据，并验证其数据完整性。
* **操作步骤**：
  1. 触发 Case 2：
     ```orcas
     Var.set NVM_test_flag = 2
     ```
* **预期结果**：
  * `NVM_test_flag` 自动清零。
  * `NVM_test_result` 显示 `0x00` (`E_OK`)。
  * `NVM_test_read_len` 显示为 `8`（十进制）。
  * `NVM_test_read_buf` 被填充为：`0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x00...`。

---

### 测试用例 3 (NVM_test_flag = 3) ：跨核异步读取请求（Method 0x05 / 0x03 统一入口）
* **测试目的**：测试 M 核作为 Client 端的跨核读取能力，验证重构后的统一读请求 API `Stm_RequestReadFromA(methodId, dataId)`。此 API 统一支持 Method 0x03（读取最新数据）与 Method 0x05（读取滚动数据）的数据拉取。
* **操作步骤**：
  1. 确保 M 核与 A 核的跨核通信链路已建立成功，且状态机处于 `RUNNING`（运行）状态。
  2. 触发 Case 3（代码内部会调用 `Stm_RequestReadFromA(STM_METHOD_M_ASYNC_READ, 0x0002U)`）：
     ```orcas
     Var.set NVM_test_flag = 3
     ```
* **预期结果**：
  * M 核将组装一个 Method 0x05 (`STM_METHOD_M_ASYNC_READ`) 的请求报文，并使用递增的 `SessionID`。
  * 请求报文通过 IPCF 通道成功发送给 A 核。
  * 当 A 核作为 Provider 回复 `RESPONSE`（包含请求的数据负载）后，M 核接收中断会解析该 `SessionID` 进行匹配，自动更新本地的 RAM 镜像与 EEPROM 存储。之后可通过触发**测试用例 6**来验证接收到的新数据。
  * > [!NOTE]
  * > **接口统一重构说明**：为了优化代码存储空间并增强复用性，`Stm_RequestReadFromA` 已经升级为二合一的通用读取函数。
  * > * 调用 `Stm_RequestReadFromA(STM_METHOD_M_READ_FROM_A, dataId)` 即发起 **Method 0x03** 读取请求；
  * > * 调用 `Stm_RequestReadFromA(STM_METHOD_M_ASYNC_READ, dataId)` 即发起 **Method 0x05** 读取请求。
  * > 无论哪种请求，均公用同一套挂起追踪结构体 `Stm_PendingReadReq` 进行 SessionID 的自动应答匹配与接收，让读取流程高度解耦且维护成本极低。

---

### 测试用例 4 (NVM_test_flag = 4) ：持久化 EEPROM 格式化测试
* **测试目的**：强制清空 EEPROM 格式并清除 RAM 镜像，验证系统的初始化回退与首飞（First-run）逻辑。
* **操作步骤**：
  1. 触发 Case 4：
     ```orcas
     Var.set NVM_test_flag = 4
     ```
* **预期结果**：
  * EEPROM 地址 `0x10` 的 Magic Byte 重新写入为 `0xA5`。
  * EEPROM 中所有 5 个已配置数据块的 valid 标志被设置为 `FALSE` (`0x00`)，擦除其校验状态。
  * 所有的 RAM 镜像清空为 `0`，并标记为 clean。
  * 执行**测试用例 2**，由于数据块 1 已失效，API 应返回 `E_NOT_OK` (`0x01`)。

---

### 测试用例 5 (NVM_test_flag = 5) ：本地 RAM 与 EEPROM 写入（数据块 2）
* **测试目的**：验证 NVM 数据块 2（`dataId = 0x0002`，最大长度 16 字节）的分段物理写入安全性。
* **操作步骤**：
  1. 设置写入种子值：
     ```orcas
     Var.set NVM_test_write_val = 0x10
     ```
  2. 触发 Case 5：
     ```orcas
     Var.set NVM_test_flag = 5
     ```
* **预期结果**：
  * `NVM_test_flag` 自动清零。
  * `NVM_test_result` 显示 `0x00` (`E_OK`)。
  * RAM 镜像及 EEPROM 写入内容更新为：`[0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F]`。
    > [!TIP]
    > **查看 Block 2 的 RAM 镜像**：可以在 TRACE32 命令行输入以下命令以结构体展开观测：
    > ```orcas
    > Var.View \\stm_nvm\g_nvmBlocks[1]
    > ```

---

### 测试用例 6 (NVM_test_flag = 6) ：本地 NVM 读取（数据块 2）
* **测试目的**：读取并检索数据块 2 存储的 16 字节持久化数据。
* **操作步骤**：
  1. 触发 Case 6：
     ```orcas
     Var.set NVM_test_flag = 6
     ```
* **预期结果**：
  * `NVM_test_flag` 自动清零。
  * `NVM_test_result` 显示 `0x00` (`E_OK`)。
  * `NVM_test_read_len` 显示为 `16`（十进制）。
  * `NVM_test_read_buf` 成功获取序列 `0x10` 至 `0x1F` 的全部内容。

---

### 测试用例 7 (NVM_test_flag = 7) ：强制触发 Method 0x04 同步到 A 核测试
* **测试目的**：验证所有本地修改（dirty，脏数据）的数据块是否能够安全、准确地同步到 A 核存储中间件中。
* **操作步骤**：
  1. 触发 Case 7：
     ```orcas
     Var.set NVM_test_flag = 7
     ```
* **预期结果**：
  * 调用 `StmNvm_SetAllValidDirty()`，它将扫描所有配置，把包含有效数据（`valid == TRUE`）的数据块的 `dirty` 标志全部置为 `TRUE`。
  * 在接下来的主循环 10ms 周期任务中，同步子任务被唤醒，开始通过 IPCF 发送 Method 0x04 同步报文（符合防风暴限制：每个 10ms 周期最多发送 2 个同步包）。
  * 一旦收到 A 核的响应，`dirty` 标志被清除。可以在调试器中验证所有数据块最终恢复为 clean（干净）状态。

---

## 4. 栈空间与运行安全性评估

为了防止在 M7 实时内核上发生系统崩溃或硬件错误（HardFault），本测试框架在设计上严格遵循了以下安全准则：
1. **禁止在栈上分配大数组**：临时缓冲区 `tempWriteBuf[16]` 仅占用 16 字节栈空间。这确保了在 FreeRTOS 任务上下文中调用时不会引发任务栈溢出，完美保持在 `OSTASK_10MS_STACK_SIZE` 安全阈值之内。
2. **自动清零触发机制**：触发变量 `NVM_test_flag` 在执行动作 of 周期末尾会被自动清零，防止测试循环往复触发，确保每次手动赋值仅执行一次动作。
3. **严格的越界和长度检查**：所有底层读写 API 均对数据项的长度进行严格越界和一致性校验，防止由于人为写入不合规的长度数据导致系统发生内存覆写或损坏。
