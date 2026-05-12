# SOA Adapter Test Cases

## Protocol Constants Reference

| Field | Value | Description |
|-------|-------|-------------|
| ProviderID | `0x47` (71) | M-Core SOA Provider |
| ConsumerID | `0x4C` (76) | A-Core SOA Consumer |
| IPC EventID (Notifier) | `0x03` | Fixed for all Notifier/Event |
| IPC MethodID (Getter/Setter) | `0x01` | Fixed for all Getter/Setter |
| Channel | 2 | IPCF data channel |

## Signal Data Source Summary

| Service | Entry | Type | CAN Signal | Source Struct |
|---------|-------|------|------------|---------------|
| DriSpeedSt | [0] | Notifier | VehicleSpeed | `g_rx_Standard_200_Rx` (RX 0x200) |
| ParkingSt | [1] | Getter | ParkingSts | `g_rx_Standard_200_Rx` (RX 0x200) |
| HVBatterySt | [2] | Getter | HighVoltageBatterySts | `g_rx_Standard_200_Rx` (RX 0x200) |
| IgnitionSt | [3] | Getter | IgnitionSts | `g_tx_Standard_100_Tx` (TX 0x100) |
| VehicleMode | [4] | Setter | VehicleMode | `g_tx_Standard_100_Tx` (TX 0x100) |
| VehicleModeSt | [5] | Notifier | VehicleMode | `g_tx_Standard_100_Tx` (TX 0x100) |

> [!IMPORTANT]
> Entry [5] Notifier reads from the **same TX variable** that Entry [4] Setter writes to (`g_tx_Standard_100_Tx.VehicleMode`), NOT from RX feedback. When Setter writes a new value, Notifier change detection will detect it and notify A-Core.

## Byte Layout Reference

### SOA 12-Byte Header (inside IPCF Payload, Big-Endian)

| Byte Offset | Field | Size |
|-------------|-------|------|
| 0-1 | SOA_ServiceID | 2B |
| 2-3 | SOA_MethodID | 2B |
| 4-5 | SOA_InstanceID | 2B |
| 6-7 | SOA_SessionID | 2B |
| 8-9 | SOA_ReturnCode | 2B |
| 10-11 | SOA_Length | 2B |
| 12+ | SOA Data | variable |

---

## TC-01: Notifier Initial Value Sync — Batched Send After Link Established

### Purpose
When PICC link transitions from DISCONNECTED to CONNECTED, M-Core calls `SOA_SendAllNotifierInitValues()` to send **all** Notifier initial values in a **single batched** `PICC_SendEvent()` call. Multiple SOA messages are concatenated into one IPC payload.

### Preconditions
- Link state = `DISCONNECTED`
- `g_rx_Standard_200_Rx.VehicleSpeed = 0x0064` (100 km/h)
- `g_tx_Standard_100_Tx.VehicleMode = 0x00` (Normal)

### Input Trigger
- Link state changes to `CONNECTED`
- `SoaAdapter_Main()` detects edge: `prevLinkState != CONNECTED` && `curLinkState == CONNECTED`

### Expected Output — Single Batched IPC Payload

The two Notifier SOA messages are **concatenated** into `s_soaBatchBuf` and sent as one call:

```
PICC_SendEvent(PICC_APP_SOA, 3, s_soaBatchBuf, 27, PICC_EVENT_WITHOUT_ACK)
```

**Batched payload (27 bytes = 14B + 13B):**
```
[Notifier 1: DriSpeedSt, 14B]  [Notifier 2: VehicleModeSt, 13B]
00 01 80 01 00 01 00 00 00 00 00 02 00 64 | 00 05 80 01 00 01 00 00 00 00 00 01 00
```

Detail breakdown:

**Notifier 1: DriSpeedSt (VehicleSpeed, from RX 0x200) — offset 0..13**

| SOA Field | Value (Hex) | Description |
|-----------|-------------|-------------|
| SOA_ServiceID | `00 01` | Atom_VCU_DriSpeedSt |
| SOA_MethodID | `80 01` | ntfVehSpeedSt |
| SOA_InstanceID | `00 01` | Default |
| SOA_SessionID | `00 00` | Always 0 for Notifier |
| SOA_ReturnCode | `00 00` | OK |
| SOA_Length | `00 02` | 2 bytes |
| Data | `00 64` | 100 km/h |

**Notifier 2: VehicleModeSt (VehicleMode, from TX 0x100) — offset 14..26**

| SOA Field | Value (Hex) | Description |
|-----------|-------------|-------------|
| SOA_ServiceID | `00 05` | Atom_BCM_VehicleModeSt |
| SOA_MethodID | `80 01` | ntfVehicleModeSt |
| SOA_InstanceID | `00 01` | Default |
| SOA_SessionID | `00 00` | Always 0 |
| SOA_ReturnCode | `00 00` | OK |
| SOA_Length | `00 01` | 1 byte |
| Data | `00` | Normal mode (from TX struct) |

### Pass Criteria
- **Only 1** `PICC_SendEvent()` call made (batched), total payload length = 27
- `SoaAdapter_CanRxProcess()` called before batching to get fresh CAN data
- Notifier caches populated after sending (`isValid = TRUE`)

---

## TC-02: Notifier Change Detection — VehicleSpeed Changed

### Preconditions
- Link = `CONNECTED`, cached VehicleSpeed = `0x0064`
- CAN RX 0x200 updated: `VehicleSpeed = 0x00C8` (200 km/h)

### Expected Output
Full payload (single Notifier, non-batched via `SOA_SendNotifier`):
```
00 01 80 01 00 01 00 00 00 00 00 02 00 C8
```
Sent via: `PICC_SendEvent(PICC_APP_SOA, 3, s_soaTxBuf, 14, PICC_EVENT_WITHOUT_ACK)`

### Pass Criteria
- Only DriSpeedSt Notifier sent (VehicleModeSt unchanged → NOT sent)
- Cache updated to `00 C8`
- Periodic change detection sends **individually** (not batched), unlike TC-01

---

## TC-03: Notifier No Change — No Transmission

### Preconditions
- All signal values same as cached

### Expected Output
- `PICC_SendEvent()` is **NOT called**

---

## TC-04: Getter Request — ParkingSts (ServiceID=0x0002)

### Preconditions
- Link = `CONNECTED`, `g_rx_Standard_200_Rx.ParkingSts = 0x02` (S gear)

### Input — A-Core REQUEST via `PICC_GetMethodData(PICC_APP_SOA, 1, ...)`
```
00 02 50 01 00 01 00 01 00 00 00 00
```

### Expected Response via `PICC_MethodResponse(PICC_APP_SOA, 1, sessionId=0x01, returnCode=0x00, ...)`
```
00 02 50 01 00 01 00 01 00 00 00 01 02
```

| SOA Field | Value | Description |
|-----------|-------|-------------|
| SOA_SessionID | `00 01` | Echoed from request |
| SOA_ReturnCode | `00 00` | Success |
| SOA_Length | `00 01` | 1 byte |
| Data | `02` | ParkingSts = S gear |

---

## TC-05: Getter Request — HighVoltageBatterySts (ServiceID=0x0003)

### Preconditions
- `g_rx_Standard_200_Rx.HighVoltageBatterySts = 0x1F40` (raw 8000 = 80.00%)

### Input — A-Core REQUEST
```
00 03 50 01 00 01 00 02 00 00 00 00
```

### Expected Response
```
00 03 50 01 00 01 00 02 00 00 00 02 1F 40
```

---

## TC-06: Getter Request — IgnitionSts (ServiceID=0x0004)

### Preconditions
- `g_tx_Standard_100_Tx.IgnitionSts = 0x01` (ON)

### Input — A-Core REQUEST
```
00 04 50 01 00 01 00 03 00 00 00 00
```

### Expected Response
```
00 04 50 01 00 01 00 03 00 00 00 01 01
```

> [!NOTE]
> IgnitionSts reads from TX struct `g_tx_Standard_100_Tx`, not RX.

---

## TC-07: Setter Request — VehicleMode (ServiceID=0x0005, with linked Notifier)

### Purpose
Verify Setter writes to TX struct and returns current value via linked Notifier [5]'s `SOA_ReadWorkVehicleMode()`.

### Preconditions
- Link = `CONNECTED`
- `g_tx_Standard_100_Tx.VehicleMode = 0x00` (Normal, before Setter)

### Input — A-Core Setter REQUEST
```
00 05 50 01 00 01 00 04 00 00 00 01 02
```

| SOA Field | Value | Description |
|-----------|-------|-------------|
| SOA_ServiceID | `00 05` | Atom_BCM_VehicleMode |
| SOA_MethodID | `50 01` | SetOper |
| SOA_SessionID | `00 04` | Session 4 |
| SOA_Length | `00 01` | 1 byte |
| Data | `02` | Set VehicleMode = OTA (2) |

### Expected Behavior
1. `SOA_FindService(0x0005, 0x5001, 0x0001)` → index 4 (Setter)
2. Entry [4] `SOA_WriteVehicleMode(&reqData[12], 1)` → `g_tx_Standard_100_Tx.VehicleMode = 0x02`, returns 0
3. `hasLinkedNotifier = TRUE` → lookup Entry [5] via `linkedNotifierIdx = 5`
4. Entry [5] `SOA_ReadWorkVehicleMode()` → reads `g_tx_Standard_100_Tx.VehicleMode = 0x02`

### Expected Response
```
00 05 50 01 00 01 00 04 00 00 00 01 02
```

| SOA Field | Value | Description |
|-----------|-------|-------------|
| SOA_SessionID | `00 04` | Echoed |
| SOA_ReturnCode | `00 00` | Success |
| SOA_Length | `00 01` | 1 byte |
| Data | `02` | **Current VehicleMode from TX struct (= written value)** |

> [!IMPORTANT]
> The response reads from the **same TX variable** that was just written. So the response data (`02`) will always match the written value.

### Pass Criteria
- `g_tx_Standard_100_Tx.VehicleMode` updated to `0x02`
- Response data = `02` (read via linked Notifier [5] from TX struct)
- IPC SessionID echoed
- `PICC_MethodResponse()` called with `ipcReturnCode = 0x00`

---

## TC-08: Getter Request — Unknown Service (Error Case)

### Input — A-Core REQUEST with unknown ServiceID
```
FF FF 50 01 00 01 00 05 00 00 00 00
```

### Expected Behavior
- `SOA_FindService(0xFFFF, 0x5001, 0x0001)` returns `0xFF` (not found)

### Expected Response
```
FF FF 50 01 00 01 00 05 00 01 00 00
```

| SOA Field | Value | Description |
|-----------|-------|-------------|
| SOA_ReturnCode | `00 01` | E_NOT_OK |
| SOA_Length | `00 00` | No data |

- `PICC_MethodResponse()` called with `ipcReturnCode = 0x01`

---

## TC-09: Link Disconnect — Stop Business Data

### Preconditions
- Link = `CONNECTED`, Notifier caches valid

### Input Trigger
- Link state changes to `DISCONNECTED`

### Expected Behavior
1. All Notifier caches invalidated (`isValid = FALSE`)
2. Function returns early — NO CAN RX, NO Notifier, NO Method poll
3. `PICC_SendEvent()` NOT called
4. `PICC_GetMethodData()` NOT called

---

## TC-10: Reconnect After Disconnect — Batched Re-sync

### Purpose
After disconnect → reconnect, `SOA_SendAllNotifierInitValues()` re-sends all Notifier values in a single batched call, regardless of whether values changed since last disconnect.

### Preconditions
- Previous state: `DISCONNECTED` (cache invalid, `isValid = FALSE`)
- `g_rx_Standard_200_Rx.VehicleSpeed = 0x012C` (300 km/h)
- `g_tx_Standard_100_Tx.VehicleMode = 0x02` (OTA)

### Expected Output — Single Batched IPC Payload

```
PICC_SendEvent(PICC_APP_SOA, 3, s_soaBatchBuf, 27, PICC_EVENT_WITHOUT_ACK)
```

**Batched payload (27 bytes):**
```
00 01 80 01 00 01 00 00 00 00 00 02 01 2C | 00 05 80 01 00 01 00 00 00 00 00 01 02
```

### Pass Criteria
- **Only 1** `PICC_SendEvent()` call (batched)
- Both Notifiers sent regardless of whether values changed since last disconnect
- Cache re-initialized (`isValid = TRUE`)
- `SoaAdapter_CanRxProcess()` called before batching

---

## TC-11: Setter Write Failure — ReturnCode E_NOT_OK

### Input — A-Core Setter REQUEST with empty data (SOA_Length=0)
```
00 05 50 01 00 01 00 06 00 00 00 00
```

### Expected Behavior
- `reqHdr.SOA_Length = 0` → condition `(reqHdr.SOA_Length > 0U)` fails
- `writeResult` stays at `1U` (initial value) → write not attempted

### Expected Response
```
00 05 50 01 00 01 00 06 00 01 00 00
```

| SOA Field | Value | Description |
|-----------|-------|-------------|
| SOA_ReturnCode | `00 01` | Write failed |
| SOA_Length | `00 00` | No data |

- `PICC_MethodResponse()` called with `ipcReturnCode = 0x01`
- `g_tx_Standard_100_Tx.VehicleMode` **NOT changed**

---

## TC-12: Request Before Link Connected

### Preconditions
- Link = `CONNECTING` (not yet CONNECTED)

### Expected Behavior
- `SoaAdapter_Main()` returns early after saving `prevLinkState`
- No `PICC_GetMethodData` / `PICC_SendEvent` calls
- No CAN RX/TX processing

---

## TC-13: Notifier Type Received as Method Request — Error Path

### Purpose
Verify that if A-Core sends a Method REQUEST whose ServiceID+MethodID+InstanceID matches a Notifier entry (not Getter/Setter), the default branch returns E_NOT_OK.

### Input — A-Core REQUEST matching Notifier entry [0] DriSpeedSt
```
00 01 80 01 00 01 00 07 00 00 00 00
```

| SOA Field | Value | Description |
|-----------|-------|-------------|
| SOA_ServiceID | `00 01` | Atom_VCU_DriSpeedSt |
| SOA_MethodID | `80 01` | Notifier MethodID |

### Expected Behavior
- `SOA_FindService(0x0001, 0x8001, 0x0001)` → index 0 (Notifier)
- `serviceType == SOA_SERVICE_NOTIFIER` → falls into `default` branch

### Expected Response
```
00 01 80 01 00 01 00 07 00 01 00 00
```

| SOA Field | Value | Description |
|-----------|-------|-------------|
| SOA_ReturnCode | `00 01` | E_NOT_OK |
| SOA_Length | `00 00` | No data |

- `PICC_MethodResponse()` called with `ipcReturnCode = 0x01`

---

## TC-14: Getter Read Failure — readFunc Returns 0

### Purpose
Verify Getter returns E_NOT_OK when `readFunc` returns 0 (read failure).

### Preconditions
- A service entry with `readFunc` that returns 0 (simulated failure condition)
- In practice, this can happen if `readFunc` receives `maxLen < required` (but current `SOA_MAX_DATA_SIZE=256` prevents this under normal operation)

### Expected Behavior
- `rspDataLen == 0U` → `rspHdr.SOA_ReturnCode = 1U`, `ipcReturnCode = 0x01`

### Expected Response
SOA header echoed with `ReturnCode = 00 01`, `Length = 00 00`, no data.

> [!NOTE]
> This path is defensive — under current configuration (all readFuncs need ≤2B, `SOA_MAX_DATA_SIZE=256`), this path is not normally reachable. It guards against future signals with larger data requirements.

---

## TC-15: Method Request Too Short — Less Than SOA_HEADER_SIZE

### Purpose
Verify `SOA_HandleMethodRequest` silently discards requests with less than 12 bytes.

### Input
- `PICC_GetMethodData` returns `actualLen = 8` (less than `SOA_HEADER_SIZE = 12`)

### Expected Behavior
- `SOA_HandleMethodRequest` returns immediately (first guard: `reqLen < SOA_HEADER_SIZE`)
- No `PICC_MethodResponse()` called
- No crash or buffer overflow

---

## TC-16: Module Not Initialized — Early Return

### Preconditions
- `SoaAdapter_Init()` NOT called (or `isInitialized = FALSE`)

### Expected Behavior
- `SoaAdapter_Main()` returns immediately at first guard check
- No PICC calls, no CAN processing

---

## Test Execution Summary

| TC | Scenario | Key Verification |
|----|----------|-----------------|
| TC-01 | Initial value sync (batched) | **Single** `PICC_SendEvent` call, payload=27B, CAN RX before batch |
| TC-02 | Notifier changed | Only changed signal sent, **individual** (non-batched) |
| TC-03 | Notifier unchanged | No transmission |
| TC-04 | Getter ParkingSts | SessionID echoed, data from RX |
| TC-05 | Getter HVBattery | 2B Big-Endian uint16 from RX |
| TC-06 | Getter IgnitionSts | Data from **TX** struct |
| TC-07 | Setter VehicleMode | Writes TX, reads back via linked Notifier [5] `SOA_ReadWorkVehicleMode()` |
| TC-08 | Unknown ServiceID | ReturnCode = 0x01, ipcReturnCode = 0x01 |
| TC-09 | Disconnect | Cache invalidated, no traffic |
| TC-10 | Reconnect (batched) | **Single** batched re-sync, VehicleModeSt from **TX** |
| TC-11 | Setter empty data | Write not attempted, fails gracefully |
| TC-12 | Pre-link | Early return, no processing |
| TC-13 | Notifier as Method | Default branch → E_NOT_OK |
| TC-14 | Getter read failure | Defensive error path → E_NOT_OK |
| TC-15 | Request too short | Silent discard, no response sent |
| TC-16 | Not initialized | Early return guard |
