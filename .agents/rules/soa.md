---
trigger: always_on
---


以下需求是SOA 的客户需求，当前只需要关注M核；
[REQ-SOA-010] 基础数据类型支持： M核SOA代理模块必须支持将C/C++原生基础数据类型作为业务功能的载体。支持的类型必须包括但不限于：int8 , int16, uint32  int64  uint8,uint16,uint32,uint64,q  float, double, bool，以及 enum（枚举）。
[REQ-SOA-011] 复合数据类型支持： M核SOA代理模块必须支持对复合数据类型的代理，包括允许嵌套定义的结构体（struct）以及定长数组（Fixed-length Array）。
[REQ-SOA-012] 跨核序列化与反序列化： M核SOA模块必须负责将上述数据类型序列化为字节流，填入核间私有通信协议的 Payload 字段，并在接收时进行反序列化。在转换结构体时，必须处理并保证严格的字节对齐（Byte Alignment）规则。
[REQ-SOA-013] 字节序转换（大端约束）： 根据核间通信规范，M核SOA模块在打包 int16/uint16 及以上宽度的多字节数据类型（含浮点数）时，必须将其转换为**大端字节序（Big-Endian）**再填入 Payload 发送；在接收A核数据时，也必须按照大端规则解包转换为M核本地字节序。
2. 连接状态感知与链路管理联动需求
本部分定义M核SOA服务发布与核间物理链路状态的强关联性。

[REQ-SOA-020] 握手状态依赖： M核SOA代理模块在系统启动后，必须等待底层通信链路通过 LINK_AVAILABLE 报文与A核完成握手（收到 ReturnCode 为 0x00）。在握手成功之前，SOA模块禁止启动服务发布或发送任何业务报文。
[REQ-SOA-021] 连接断开感知与挂起： 当M核检测到A核发来的链路断开请求（Payload 0x02）、重启通知（Payload 0x03）或底层上报心跳超时（连续3次Pong丢失）时，SOA代理模块必须立即挂起所有代理的A核 SOME/IP 服务，并停止 CAN 信号到服务的转换。
[REQ-SOA-022] 链路恢复重连： 当由于重启或心跳超时导致链路断开，且随后M核与A核重新建立连接并完成握手后，SOA模块必须能自动恢复服务的发布与代理状态。
3. SOME/IP服务语义到核间私有协议的映射需求
本部分定义M核抽象的 SOME/IP 概念如何具体落地到现有的异步 IPCF 堆叠报文协议上。

[REQ-SOA-030] 单向通知服务映射 (Notifier/Event)： 对于配置为 Field Notifier 和 Event 类型的 SOA 服务，M核SOA代理必须将其映射为核间通信协议中的 NOTIFICATION_WITHOUT_ACK (0x09) 报文进行发送。受限于M核异步特性，M核不可要求此类型消息的中间件ACK。
[REQ-SOA-031] 方法调用请求映射 (Method/Getter/Setter)： 对于配置为 R/R Method, Field Getter, Field Setter 的请求动作，M核SOA代理必须将其映射为核间通信协议中的 REQUEST (0x05) 报文接收。
[REQ-SOA-032] 方法响应匹配 (SessionID回传)： 针对 REQ-SOA-031 接收到的 REQUEST 报文，M核在完成业务处理（如读取 CAN 数据）后，必须通过 RESPONSE (0x80) 报文进行回复，并且必须将发出该请求的原报文中的 SessionID 无修改地填入响应报文中，以支持 A 核的异步匹配。
4. CAN 信号的 SOA 化包装需求 (Signal-to-Service)
本部分定义M核作为网关，实现传统 CAN 矩阵与 SOA 服务的双向数据流转。

[REQ-SOA-040] CAN信号汇聚与发布 (Signal to Event)： M核SOA代理应当具备缓存物理 CAN 总线接收信号的能力。当组合定义的 CAN 信号发生变化或满足触发条件时，SOA模块必须将其打包组装为对应的 Field Notifier 或 Event Payload 发送给 A 核。
[REQ-SOA-041] CAN信号读取代理 (Getter Proxy)： 当 A 核通过 Field Getter 请求读取相关的 CAN 信号时，M核SOA代理必须能够获取对应 CAN 信号的最新缓存值，并通过 Response 报文及时回复。
[REQ-SOA-042] 服务转 CAN 控制指令 (Method/Setter to Signal)： 当 M 核接收到 A 核发起的 Field Setter 或 Method REQUEST，且该服务对应到底层 CAN 控制时，M核SOA代理必须解析 Payload 中的意图，提取出控制数值，并触发底层 CAN 驱动发送对应的物理 CAN 报文。
5. 工具化支持与配置一致性需求
本部分定义代码生成工具（Creator）的边界，以保证核间硬编码不出错。

[REQ-SOA-050] 服务矩阵导入与代码生成： M核SOA代理的代码架构必须支持由外部工具（Creator）导入标准服务矩阵（如 ARXML, Excel,  DBC）后直接生成接口与实例化配置代码，严格避免手工编写路由逻辑。
[REQ-SOA-051] 核间路由 ID 一致性绑定： 工具生成的代码必须自动为每个 SOA 服务分配核间通信专用的 ProviderID、ConsumerID (取值1-254) 以及 MethodID，并保证其在整个系统中具有绝对的唯一性与两端（A/M）的一致性。
[REQ-SOA-052] 数据结构隐式约束： 工具生成的 Payload 结构体代码必须符合 REQ-SOA-012 中关于强制字节对齐设定的宏声明，从而确保内存排布与 A 核生成的协议结构毫无偏差。