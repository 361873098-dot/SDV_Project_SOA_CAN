# CAN Message Unit Test Cases (Standard_100_Tx & Standard_200_Rx)

本文档根据最新 `CANdbc_file.c` / `CANdbc_file.h`（生成时间 2026-04-17 10:00:03）中的 `pack/unpack` 代码编写。
用于验证全局变量 `g_tx_Standard_100_Tx` 和 `g_rx_Standard_200_Rx` 的组包与解包行为。

---

## 1. 发送报文测试：`Standard_100_Tx` (CAN ID: 0x100, DLC=1, Cycle=100ms)

**结构体定义**：
```c
typedef struct {
    uint8_t Power_Msgstate;  /* [0, 1] */
} Standard_100_Tx_t;
```

**Pack 逻辑** (摘自代码)：
- `data[0] = msg->Power_Msgstate` （Motorola, start_bit=7, len=8）

### TC-TX-001: Power_Msgstate = IDLE (0)

| 项目 | 内容 |
|------|------|
| **输入** | `g_tx_Standard_100_Tx.Power_Msgstate = 0;` (宏: `PWSM_MSG_STATE_IDLE`) |
| **执行** | `ret = Standard_100_Tx_pack(data, &g_tx_Standard_100_Tx, 1);` |
| **期望 data** | `data[0] == 0x00` |
| **期望返回值** | `ret == 0` |

### TC-TX-002: Power_Msgstate = TX_ID1 (17)

| 项目 | 内容 |
|------|------|
| **输入** | `g_tx_Standard_100_Tx.Power_Msgstate = 17;` (宏: `PWSM_STATE_MSG_TX_ID1`) |
| **执行** | `ret = Standard_100_Tx_pack(data, &g_tx_Standard_100_Tx, 1);` |
| **期望 data** | `data[0] == 0x11` |
| **期望返回值** | `ret == 0` |

### TC-TX-003: Power_Msgstate = RX_ID2 (18)

| 项目 | 内容 |
|------|------|
| **输入** | `g_tx_Standard_100_Tx.Power_Msgstate = 18;` (宏: `PWSM_STATE_MSG_RX_ID2`) |
| **执行** | `ret = Standard_100_Tx_pack(data, &g_tx_Standard_100_Tx, 1);` |
| **期望 data** | `data[0] == 0x12` |
| **期望返回值** | `ret == 0` |

### TC-TX-004: Power_Msgstate = RX_ID8 (19)

| 项目 | 内容 |
|------|------|
| **输入** | `g_tx_Standard_100_Tx.Power_Msgstate = 19;` (宏: `PWSM_STATE_MSG_RX_ID8`) |
| **执行** | `ret = Standard_100_Tx_pack(data, &g_tx_Standard_100_Tx, 1);` |
| **期望 data** | `data[0] == 0x13` |
| **期望返回值** | `ret == 0` |

### TC-TX-005: Power_Msgstate = TX_ID4 (20)

| 项目 | 内容 |
|------|------|
| **输入** | `g_tx_Standard_100_Tx.Power_Msgstate = 20;` (宏: `PWSM_STATE_MSG_TX_ID4`) |
| **执行** | `ret = Standard_100_Tx_pack(data, &g_tx_Standard_100_Tx, 1);` |
| **期望 data** | `data[0] == 0x14` |
| **期望返回值** | `ret == 0` |

### TC-TX-006: Power_Msgstate = RX_ID11 (21)

| 项目 | 内容 |
|------|------|
| **输入** | `g_tx_Standard_100_Tx.Power_Msgstate = 21;` (宏: `PWSM_STATE_MSG_RX_ID11`) |
| **执行** | `ret = Standard_100_Tx_pack(data, &g_tx_Standard_100_Tx, 1);` |
| **期望 data** | `data[0] == 0x15` |
| **期望返回值** | `ret == 0` |

### TC-TX-007: Power_Msgstate = NO_RESPONSE (22)

| 项目 | 内容 |
|------|------|
| **输入** | `g_tx_Standard_100_Tx.Power_Msgstate = 22;` (宏: `PWSM_MSG_STATE_NO_RESPONSE`) |
| **执行** | `ret = Standard_100_Tx_pack(data, &g_tx_Standard_100_Tx, 1);` |
| **期望 data** | `data[0] == 0x16` |
| **期望返回值** | `ret == 0` |

### TC-TX-008: Power_Msgstate = SHUTDOWN_DONE (23)

| 项目 | 内容 |
|------|------|
| **输入** | `g_tx_Standard_100_Tx.Power_Msgstate = 23;` (宏: `PWSM_STATE_MSG_SHUTDOWN_DONE`) |
| **执行** | `ret = Standard_100_Tx_pack(data, &g_tx_Standard_100_Tx, 1);` |
| **期望 data** | `data[0] == 0x17` |
| **期望返回值** | `ret == 0` |

### TC-TX-009: 异常 — data 指针为 NULL

| 项目 | 内容 |
|------|------|
| **输入** | `g_tx_Standard_100_Tx.Power_Msgstate = 0;` |
| **执行** | `ret = Standard_100_Tx_pack(NULL, &g_tx_Standard_100_Tx, 1);` |
| **期望返回值** | `ret == -1` |

### TC-TX-010: 异常 — msg 指针为 NULL

| 项目 | 内容 |
|------|------|
| **执行** | `ret = Standard_100_Tx_pack(data, NULL, 1);` |
| **期望返回值** | `ret == -1` |

### TC-TX-011: 异常 — DLC 过小 (dlc=0)

| 项目 | 内容 |
|------|------|
| **输入** | `g_tx_Standard_100_Tx.Power_Msgstate = 0;` |
| **执行** | `ret = Standard_100_Tx_pack(data, &g_tx_Standard_100_Tx, 0);` |
| **期望返回值** | `ret == -1` |

---

## 2. 接收报文测试：`Standard_200_Rx` (CAN ID: 0x200, DLC=6, Cycle=100ms)

**结构体定义**：
```c
typedef struct {
    uint8_t    Gear;           /* [0, 4], 枚举: P=0, R=1, N=2, D=3, INVALID=4 */
    uint8_t    Battery;        /* unit="%", [0, 100] */
    int16_t    Vehicle_Speed;  /* factor=0.01, offset=0, unit="km/h", [-327.68, 327.67] */
} Standard_200_Rx_t;
```

**Unpack 逻辑** (摘自代码)：
- `Gear     ← data[0]`
- `Battery  ← data[1]`
- `Vehicle_Speed ← (int16_t)((int16_t)raw_val * 0.01)`, 其中 `raw_val = (data[2] << 8) | data[3]`

> [!IMPORTANT]
> `Vehicle_Speed` 的类型是 `int16_t`，经过 `factor=0.01` 运算后**转为整数会截断小数部分**。
> 例如：raw=12000 → 12000×0.01=120.0 → 存入 120；raw=5001 → 50.01 → 存入 50。

### TC-RX-001: 常规值 — 车速 120 km/h, 挡位 D, 电量 85%

| 项目 | 内容 |
|------|------|
| **输入 data[]** | `{0x03, 0x55, 0x2E, 0xE0, 0x00, 0x00}` |
| **执行** | `ret = Standard_200_Rx_unpack(&g_rx_Standard_200_Rx, data, 6);` |
| **期望 Gear** | `3` (D 挡, 宏: `STANDARD_200_RX_GEAR_D`) |
| **期望 Battery** | `85` |
| **期望 Vehicle_Speed** | `120` (raw=0x2EE0=12000, 12000×0.01=120) |
| **期望返回值** | `ret == 0` |

### TC-RX-002: 车速 50 km/h, 挡位 P, 电量 100%

| 项目 | 内容 |
|------|------|
| **输入 data[]** | `{0x00, 0x64, 0x13, 0x88, 0x00, 0x00}` |
| **执行** | `ret = Standard_200_Rx_unpack(&g_rx_Standard_200_Rx, data, 6);` |
| **期望 Gear** | `0` (P 挡, 宏: `STANDARD_200_RX_GEAR_P`) |
| **期望 Battery** | `100` |
| **期望 Vehicle_Speed** | `50` (raw=0x1388=5000, 5000×0.01=50) |
| **期望返回值** | `ret == 0` |

### TC-RX-003: 车速 0 km/h, 挡位 N, 电量 0%

| 项目 | 内容 |
|------|------|
| **输入 data[]** | `{0x02, 0x00, 0x00, 0x00, 0x00, 0x00}` |
| **执行** | `ret = Standard_200_Rx_unpack(&g_rx_Standard_200_Rx, data, 6);` |
| **期望 Gear** | `2` (N 挡, 宏: `STANDARD_200_RX_GEAR_N`) |
| **期望 Battery** | `0` |
| **期望 Vehicle_Speed** | `0` (raw=0x0000=0, 0×0.01=0) |
| **期望返回值** | `ret == 0` |

### TC-RX-004: 车速 1 km/h, 挡位 R, 电量 50%

| 项目 | 内容 |
|------|------|
| **输入 data[]** | `{0x01, 0x32, 0x00, 0x64, 0x00, 0x00}` |
| **执行** | `ret = Standard_200_Rx_unpack(&g_rx_Standard_200_Rx, data, 6);` |
| **期望 Gear** | `1` (R 挡, 宏: `STANDARD_200_RX_GEAR_R`) |
| **期望 Battery** | `50` |
| **期望 Vehicle_Speed** | `1` (raw=0x0064=100, 100×0.01=1.0 → 截断为 1) |
| **期望返回值** | `ret == 0` |

### TC-RX-005: 挡位 INVALID (4), 电量 1%

| 项目 | 内容 |
|------|------|
| **输入 data[]** | `{0x04, 0x01, 0x03, 0xE8, 0x00, 0x00}` |
| **执行** | `ret = Standard_200_Rx_unpack(&g_rx_Standard_200_Rx, data, 6);` |
| **期望 Gear** | `4` (INVALID, 宏: `STANDARD_200_RX_GEAR_INVALID`) |
| **期望 Battery** | `1` |
| **期望 Vehicle_Speed** | `10` (raw=0x03E8=1000, 1000×0.01=10) |
| **期望返回值** | `ret == 0` |

### TC-RX-006: Vehicle_Speed 正向最大值 (327 km/h)

| 项目 | 内容 |
|------|------|
| **输入 data[]** | `{0x03, 0x64, 0x7F, 0xFF, 0x00, 0x00}` |
| **执行** | `ret = Standard_200_Rx_unpack(&g_rx_Standard_200_Rx, data, 6);` |
| **期望 Gear** | `3` |
| **期望 Battery** | `100` |
| **期望 Vehicle_Speed** | `327` (raw=0x7FFF=32767, 32767×0.01=327.67 → 截断为 327) |
| **期望返回值** | `ret == 0` |

### TC-RX-007: 异常 — data 指针为 NULL

| 项目 | 内容 |
|------|------|
| **执行** | `ret = Standard_200_Rx_unpack(&g_rx_Standard_200_Rx, NULL, 6);` |
| **期望返回值** | `ret == -1` |

### TC-RX-008: 异常 — msg 指针为 NULL

| 项目 | 内容 |
|------|------|
| **执行** | `ret = Standard_200_Rx_unpack(NULL, data, 6);` |
| **期望返回值** | `ret == -1` |

### TC-RX-009: 异常 — DLC 过小 (dlc=5)

| 项目 | 内容 |
|------|------|
| **输入 data[]** | `{0x00, 0x32, 0x00, 0x64, 0x00}` (只有 5 字节) |
| **执行** | `ret = Standard_200_Rx_unpack(&g_rx_Standard_200_Rx, data, 5);` |
| **期望返回值** | `ret == -1` |

---

## 3. 信号映射速查表

### Standard_100_Tx (DLC=1) — Pack 方向

| data 字节 | 信号名 | 类型 | 取值范围 | 编码方式 |
|-----------|--------|------|----------|----------|
| data[0] | Power_Msgstate | uint8_t | 0~23 (枚举) | 直接赋值 |

### Standard_200_Rx (DLC=6) — Unpack 方向

| data 字节 | 信号名 | 类型 | 物理范围 | 解码公式 |
|-----------|--------|------|----------|----------|
| data[0] | Gear | uint8_t | 0~4 (枚举 P/R/N/D/INVALID) | 直接读取 |
| data[1] | Battery | uint8_t | 0~100 (%) | 直接读取 |
| data[2..3] | Vehicle_Speed | int16_t | [-327.68, 327.67] km/h | `(int16_t)(raw * 0.01)`, raw = `(data[2]<<8) \| data[3]` |
| data[4..5] | — | — | 未使用 | — |
