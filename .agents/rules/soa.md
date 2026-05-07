---
trigger: always_on
---

底层 IPC 协议头 (8-Byte Header)
所有报文使用大端字节序 (Big-Endian)，每个独立的报文开头为 8字节 Header [20, 21]：
*   Byte 1: ProviderID
*   Byte 2: MethodID (IPC 层级的标识，非 SOA Method)
*   Byte 3: ConsumerID
*   Byte 4: SessionID
*   Byte 5: MessageType (如 0x05 REQUEST, 0x09 NOTIF_WITHOUT_ACK)
*   Byte 6: ReturnCode (0x00 正常, 0x01 异常)
*   Byte 7-8: Length (Payload 长度)

--------------------------------------------------------------------------------
4. SOA 协议映射与序列化需求 (SOA Serialization)

4.1 基础数据类型支持
根据服务矩阵定义，M核 SOA Adapter 必须支持将以下类型进行内存序列化并填入 Payload [3]：
*   基础类型：int8~64, uint8~64, float, double, bool, 枚举类型。
*   复杂类型：允许嵌套的结构体 (Struct)，定长数组 (Array) [3]。

4.2 SOA 私有协议头 (14-Byte SOA Header inside IPC Payload)
紧跟在底层 IPC 8字节 Header 之后，业务 Payload 内部必须采用严格的 14字节格式 [22, 23]：
*   Byte 1-2: ServiceID (来自服务矩阵)
*   Byte 3-4: MethodID (来自服务矩阵，AP 事件/方法 ID)
*   Byte 5-6: InstanceID (来自服务矩阵)
*   Byte 7-8: SessionID (请求时递增匹配，Event/Notifier 固定为 0)
*   Byte 9-10: ReturnCode (有响应的调用使用，成功为 0，失败非 0)
*   Byte 11-12: Length (后续实际业务参数的长度)
*   Byte 13+: Actual Data (实际应用参数值，即 4.1 中的数据)



--------------------------------------------------------------------------------
5. SOA 核心服务代理业务逻辑 (M核开发重点)

M核负责把内部业务变量和 MCU 的 CAN 信号，包装成以下逻辑代理给 A 核：

5.1 单向通知类：Event 与 Field Notifier
*   底层映射：使用 IPC Event 机制，固定底层 IPC `EventID=3`，`MessageType=0x09` (NOTIFICATION_WITHOUT_ACK) [23]。
*   ACK 约束：根据 M 核异步机制限制，**M核只发送不带 ACK 的 EVENT (0x09)**。如果上层应用配置了需要带 ACK 的事件，M核核间组件会主动丢弃并**不做处理** [5, 24]。
*   **初值同步 (必须实现的关键逻辑)**：一旦底层建链握手成功，M核**必须立刻主动向 A核发送一次所有 Notifier 的当前初值**。完成前 A核服务不可用 [23, 25]。
*   支持组包：多个 Event/Notifier 的 14 字节数据体可拼接成一个长 Payload 放在同一个底层报文内进行发送以提高效率 [23, 26]。

5.2 请求响应类：Field Getter, Field Setter 与 R/R Method
*   底层映射：使用 IPC Method 机制，固定底层 IPC `MethodID=1`，请求的 MessageType 为 `REQUEST` (0x05)，响应为 `RESPONSE` (0x80) [26, 27]。
*   SessionID 匹配：M核收到 A核异步发来的 REQUEST 后，必须解析出 `SessionID`，处理完业务后，**必须将相同的 SessionID 填入 RESPONSE 报文返回** [27]。由于需要区分同一类型消息的不同报文，断开重连后此全局（或局部）变量的 SessionID 重置 [6]。
*   **Setter 联动约束 (最易错点)**：
    *   当 Field **同时**定义了 Setter 和 Notifier：M核在完成 Setter 的目标值写入后，必须立刻**重新读取该 Field 当前的 Notifier 值**，并将这个最新状态放入 RESPONSE 返回给 A核，以确保状态一致 [27, 28]。
*   当 Field **仅**定义 Setter：写入完成后，M核返回 `Length=0` 的空数据 RESPONSE 报文 [27]。

如果M核收到来自A核发来的Field Setter指令，且当前需要SETTER 的报文对M核来说是接收报文，那么M核的RESPONSE 是NOT OK


FF Method 当前不支持

5.3 S2S (Signal-to-Service) 代理处理
*   M核接收到 Setter 控制指令后，如果该 Field 绑定了 CAN 信号，SOA Agent 必须通过 RTE 将 Payload 转交 S2S Agent，调用 COM 接口改变物理 CAN 矩阵信号 [1-3]。

--------------------------------------------------------------------------------
1. 异常边界与状态机管理 (Edge Cases & State Machine)

6.1 链路状态机强关联
*   建链前：即便底层 IPCF 初始化完毕，在握手成功前，M核绝对**禁止**向 A核发送任何业务数据 [25]。
*   断链时：一旦 A核发送 0x02 断开报文或发生心跳超时，M核必须立即停止该链路上的 SOA 业务报文发送，并作废正在处理的未完成 REQUEST 请求 [25, 29]。
6.2 返回码与校验
*   业务失败：若 M核应用层在处理 Setter 或 Method 时发生错误，必须向 A核回复 `RESPONSE` 报文，并将其中的 `ReturnCode` 字段设置为 `0x01` (E_NOT_OK) [30]。
*   超时防范：M核中间件必须保证业务分发调用的实时性。若长时间阻塞导致未能及时回复，A核会直接判定本次调用超时失效 [27]。