# STM (Storage Middleware) — Software Design Document

> **Module**: STM (Storage Middleware)  
> **Version**: 1.0  
> **Date**: 2026/05/25  
> **Author**: Li Song (uic59152)  
> **Target**: S32G399A M7 Core + FreeRTOS  

---

## 1. Overview

### 1.1 Purpose

STM provides **non-volatile data management** and **M↔A core data synchronization** for the S32G3 M-core. It bridges the gap between:

- **Local persistence** — Data stored in EEPROM with a RAM mirror for zero-latency reads
- **Cross-core sync** — Data changes are automatically propagated to/from A-core via PICC (IPCF shared memory)

### 1.2 Key Characteristics

| Property | Value |
|----------|-------|
| Task period | 10ms (called in `TASK_M0_10MS`) |
| Transport | IPCF Channel 1 (HP) via PICC middleware |
| PICC roles | Dual: **Provider (Server)** + **Consumer (Client)** |
| Persistence | I2C EEPROM with segmented writes (≤16B/transfer) |
| Max data items | 5 |
| Max total EEPROM data | 64 bytes |
| Retry support | Method 0x04 only (4 retries, stepped intervals) |

### 1.3 File Structure

```
SWC/Stm/
├── stm.h            — Public API (Stm_Init, Stm_Main, Stm_WriteLocal, Stm_ReadLocal, Stm_RequestReadFromA)
├── stm.c            — Core logic: state machine + 5 sub-task handlers + PICC interaction
├── stm_cnf.h        — Configuration header: PICC IDs, Method IDs, NVM params, state machine enum
├── stm_cnf.c        — Configuration instances: data item table, retry interval table
├── stm_nvm.h        — NVM management interface
└── stm_nvm.c        — NVM implementation: EEPROM read/write, RAM mirror, segmented I2C
```

---

## 2. Architecture

### 2.1 Layer Diagram

```
┌─────────────────────────────────────────────────────┐
│                   Application (SWC)                 │
│  Stm_WriteLocal() / Stm_ReadLocal() /              │
│  Stm_RequestReadFromA()                            │
└──────────────┬──────────────────────┬───────────────┘
               │                      │
┌──────────────▼──────────┐  ┌────────▼──────────────┐
│     stm.c (Core)        │  │   stm_nvm.c (NVM)     │
│  ┌───────────────────┐  │  │  ┌─────────────────┐ │
│  │  State Machine    │  │  │  │  RAM Mirror      │ │
│  │  (5 states)       │  │  │  │  g_nvmBlocks[]  │ │
│  ├───────────────────┤  │  │  ├─────────────────┤ │
│  │  Sub-tasks:       │  │  │  │  EEPROM I/O     │ │
│  │  - CheckLink      │  │  │  │  (segmented)    │ │
│  │  - ProcessRxFromA │  │  │  └─────────────────┘ │
│  │  - ProcessSyncToA │  │  └───────────────────────┘
│  │  - ProcessAppRead │  │
│  └───────────────────┘  │
└──────────────┬──────────┘
               │
┌──────────────▼──────────┐
│   PICC Middleware       │
│   (IPCF Channel 1)      │
└─────────────────────────┘
```

### 2.2 Dual-Role PICC Registration

STM registers **two** PICC endpoints on Channel 1:

| Role | PICC App Index | localId | remoteId | Purpose |
|------|---------------|---------|----------|---------|
| **Provider (Server)** | `PICC_APP_STORAGE` | 41 (0x29) | 47 (0x2F) | Receive Method 0x01/0x02 from A-core |
| **Consumer (Client)** | `PICC_APP_STM_CLI` | 42 (0x2A) | 46 (0x2E) | Send Method 0x03/0x04/0x05 to A-core |

Both links must be connected before STM can operate (state `WAIT_LINK` → `WAIT_CONSISTENCY`).

---

## 3. State Machine

### 3.1 State Transition Diagram

```
                    Stm_Init()
                        │
                        ▼
                ┌───────────────┐
                │   UNINIT      │
                └───────┬───────┘
                        │
                        ▼
                ┌───────────────┐    link lost
                │  WAIT_LINK    │◄──────────────────────────┐
                └───────┬───────┘                            │
                        │ both links connected               │
                        ▼                                    │
                ┌───────────────┐                            │
                │WAIT_CONSISTENCY│                           │
                │  (TODO)       │                            │
                └───────┬───────┘                            │
                        │                                    │
                        ▼                                    │
                ┌───────────────┐                            │
                │  SYNC_TO_A    │──── link lost ─────────────┤
                └───────┬───────┘                            │
                        │ all dirty data synced              │
                        ▼                                    │
                ┌───────────────┐                            │
                │  RUNNING      │──── link lost ─────────────┘
                └───────────────┘
```

### 3.2 State Descriptions

| State | Entry Condition | Active Sub-tasks | Exit Condition |
|-------|----------------|-----------------|----------------|
| `UNINIT` | Power-on default | None | `Stm_Init()` called → `WAIT_LINK` |
| `WAIT_LINK` | After init or link lost | None (PICC auto-handles link requests) | Both Provider + Consumer links connected → `WAIT_CONSISTENCY` |
| `WAIT_CONSISTENCY` | Links established | None (currently immediate transition) | TODO: wait for A-core consistency check → `SYNC_TO_A` |
| `SYNC_TO_A` | Consistency passed | `Stm_ProcessSyncToA()`, `Stm_ProcessRxFromA()` | No more dirty blocks + no retry in progress → `RUNNING` |
| `RUNNING` | All data synced | All 4 sub-tasks active | Link lost → `WAIT_LINK` |

### 3.3 State Reset on Disconnect

When either PICC link is lost, the following reset actions are taken:

| Item | Action | Rationale |
|------|--------|-----------|
| State | → `WAIT_LINK` | Must re-establish link before any operation |
| `Stm_SessionId` | Reset to 0 | Per protocol: session IDs reset on disconnect |
| `Stm_RetryState.active` | Cleared | Cancel in-flight sync |
| `Stm_PendingReadReq.active` | Cleared | Cancel pending read request |
| `Stm_SyncScanIndex` | Reset to 0 | Restart round-robin scan |
| NVM dirty flags | **Cleared** | Pending syncs abandoned; data preserved in RAM/EEPROM |
| NVM data (RAM/EEPROM) | **Preserved** | Data remains valid locally |
| CRC/send counter | **Not reset** | Handled by PICC layer, persists across disconnects |

---

## 4. Method Protocol

### 4.1 Method Overview

| Method ID | Name | Direction | PICC Role | Retry | Description |
|-----------|------|-----------|-----------|-------|-------------|
| 0x01 | Consistency Check | A→M | Server | No | A-core asks M-core to verify/send local data |
| 0x02 | A-core Write | A→M | Server | No | A-core pushes data to M-core NVM |
| 0x03 | M-core Read from A | M→A | Client | No | M-core reads data from A-core (sync) |
| 0x04 | M-core Sync to A | M→A | Client | **Yes** (4 retries) | M-core pushes dirty data to A-core |
| 0x05 | M-core Async Read from A | M→A | Client | No | M-core reads data from A-core (async) |

### 4.2 Method 0x01 — Consistency Check (A→M)

```
Request from A-core:
  Payload: [dataId_H][dataId_L]
  Length: 2 bytes

Response from M-core:
  Payload: [dataId_H][dataId_L][status_H][status_H][data...]
  Length: 4 + dataLen bytes
  ReturnCode: 0x00 (OK) or 0x01 (NOT_OK)

  Status values:
    0x0000 = OK (data found and returned)
    0x0001 = NOT_OK (dataId not found or block invalid)
```

### 4.3 Method 0x02 — A-core Write to M-core (A→M)

```
Request from A-core:
  Payload: [dataId_H][dataId_L][data...]
  Length: 2 + dataLen bytes

Response from M-core:
  Payload: [dataId_H][dataId_L][status_H][status_L]
  Length: 4 bytes
  ReturnCode: 0x00 (OK) or 0x01 (NOT_OK)

  Status values:
    0x0000 = OK (data written to NVM)
    0x0001 = NOT_OK (invalid dataId, length mismatch, or EEPROM failure)
```

### 4.4 Method 0x04 — M-core Sync to A (M→A, with retry)

```
Request from M-core:
  Payload: [dataId_H][dataId_L][data...]
  Length: 2 + dataLen bytes
  PICC MethodType: PICC_METHOD_WITH_RESPONSE

Response from A-core:
  (Application-specific; M-core only checks that a response was received)

Retry Logic:
  - Only one 0x04 sync can be in-flight at a time
  - Stepped retry intervals: 100ms → 200ms → 400ms → 800ms
  - Max 4 retries before giving up (clearing dirty flag)
  - Anti-storm: max 2 sync messages per 10ms cycle
```

### 4.5 Method 0x05 — M-core Async Read from A (M→A, no retry)

```
Request from M-core:
  Payload: [dataId_H][dataId_L][0x00][0x00]
  Length: 4 bytes (2B dataId + 2B reserved)
  PICC MethodType: PICC_METHOD_WITH_RESPONSE

Response from A-core:
  (Handled in Stm_ProcessAppReadReq, matched by sessionId)

Constraints:
  - Only one read request can be pending at a time
  - No retry on failure or timeout
  - Must be in RUNNING state
```

---

## 5. NVM Layer Design

### 5.1 Memory Architecture

```
┌─────────────────────────────────────────────┐
│              RAM Mirror (g_nvmBlocks[])      │
│                                             │
│  Block 0: [data(64B)][dataLen][valid][dirty][eepromOffset] │
│  Block 1: [data(64B)][dataLen][valid][dirty][eepromOffset] │
│  Block 2: [data(64B)][dataLen][valid][dirty][eepromOffset] │
│  Block 3: [data(64B)][dataLen][valid][dirty][eepromOffset] │
│  Block 4: [data(64B)][dataLen][valid][dirty][eepromOffset] │
│                                             │
│  Read path:  Direct from RAM (zero latency) │
│  Write path: RAM mirror → EEPROM (immediate)│
└──────────────────┬──────────────────────────┘
                   │ I2C (segmented, ≤16B/transfer)
                   ▼
┌─────────────────────────────────────────────┐
│              I2C EEPROM                    │
│                                             │
│  Addr 0x10: [magic = 0xA5]                 │
│  Addr 0x11: [valid][len][data...]  Block 0 │
│  Addr 0x1B: [valid][len][data...]  Block 1 │
│  Addr 0x2D: [valid][len][data...]  Block 2 │
│  Addr 0x33: [valid][len][data...]  Block 3 │
│  Addr 0x41: [valid][len][data...]  Block 4 │
│  Addr 0x50: ──── END ────                  │
└─────────────────────────────────────────────┘
```

### 5.2 EEPROM Block Format

Each data block stored in EEPROM has the following format:

```
Offset   Field    Size   Description
─────    ─────    ────   ───────────
+0       valid    1B     TRUE(1) if block has valid data, FALSE(0) if empty
+1       len      1B     Actual data length (0..maxDataLen)
+2       data     NB     Persistent data bytes (N = maxDataLen from config)

Total per block = 2 + maxDataLen bytes
```

### 5.3 EEPROM Address Map

| EEPROM Address | Content | Size |
|---------------|---------|------|
| `0x10` | Magic byte (0xA5) | 1B |
| `0x11` ~ `0x50` | Data area (5 blocks) | 64B |

### 5.4 Data Item Configuration

Current configuration (5 items, total 58B ≤ 64B):

| Index | dataId | maxDataLen | EEPROM Size | Example Usage |
|-------|--------|-----------|-------------|---------------|
| 0 | 0x0001 | 8 | 2+8=10 | Calibration data |
| 1 | 0x0002 | 16 | 2+16=18 | Config block |
| 2 | 0x0003 | 4 | 2+4=6 | Status flags |
| 3 | 0x0004 | 12 | 2+12=14 | Sensor offsets |
| 4 | 0x0005 | 8 | 2+8=10 | Runtime params |
| | | **Total** | **58** | |

### 5.5 Init Flow

```
StmNvm_Init()
     │
     ├─ memset(g_nvmBlocks, 0)          ← Clear all RAM mirror blocks
     │
     ├─ StmNvm_ComputeOffsets()          ← Assign EEPROM offsets from config
     │
     ├─ Eeprom_ReadBytes(0x10, &magic)   ← Read magic byte
     │
     ├── magic != 0xA5 ?                 ← First boot or corrupted?
     │    │
     │    └─ YES → StmNvm_FormatEeprom() ← Write magic + clear all blocks
     │
     └── magic == 0xA5                   ← EEPROM is valid
          │
          └─ For each block: StmNvm_ReadBlockFromEeprom()
                              ├─ Read [valid][len] header (2B)
                              ├─ Validate len ≤ maxDataLen
                              └─ If valid && len > 0: read data
```

### 5.6 Write Path (Local or from A-core)

```
StmNvm_Write() / StmNvm_WriteFromA()
     │
     ├─ Validate: NVM ready, data != NULL, len ≤ maxDataLen
     │
     ├─ memcpy(data) to RAM mirror         ← Immediate update
     ├─ Set valid=TRUE, dirty=TRUE         ← Mark for A-core sync
     │
     ├─ StmNvm_WriteBlockToEeprom()        ← Persist to EEPROM
     │    ├─ Write [valid][len] header (2B)
     │    └─ Write data in segments ≤ 16B   ← I2C transaction size limit
     │
     └─ If EEPROM OK: dirty=FALSE          ← Data persisted, clear dirty
```

### 5.7 Read Path (Local)

```
StmNvm_Read()
     │
     ├─ Validate: NVM ready, data != NULL, block valid
     │
     └─ memcpy from RAM mirror             ← Zero latency (no EEPROM access)
```

---

## 6. Sub-task Design

### 6.1 Sub-task Execution Matrix

| Sub-task | SYNC_TO_A | RUNNING | Period | Description |
|----------|-----------|---------|--------|-------------|
| `Stm_CheckLinkState()` | — | ✅ | 10ms | Monitor both PICC links |
| `Stm_ProcessRxFromA()` | ✅ | ✅ | 10ms | Handle Method 0x01/0x02 |
| `Stm_ProcessSyncToA()` | ✅ | ✅ | 10ms | Sync dirty data via Method 0x04 |
| `Stm_ProcessAppReadReq()` | — | ✅ | 10ms | Handle Method 0x03/0x05 responses |

### 6.2 Stm_ProcessSyncToA — Retry Flow

```
Stm_ProcessSyncToA()
     │
     ├── Retry in progress (Stm_RetryState.active)?
     │    │
     │    ├── retryCount ≥ 4? → ClearDirty, stop retry
     │    │
     │    ├── Response received? → ClearDirty, stop retry
     │    │
     │    ├── tickCounter < interval[retryCount]? → Wait
     │    │
     │    └── Interval elapsed? → Re-send, retryCount++
     │
     ├── Anti-storm: syncCount ≥ 2? → Skip this cycle
     │
     └── Find next dirty block (round-robin)
          │
          ├── Found? → Build payload, PICC_MethodRequest(0x04)
          │           Set up retry state, advance scan index
          │
          └── Not found? → Reset scan index to 0
```

### 6.3 Retry Interval Table

| Retry # | Interval | Ticks (10ms) |
|---------|----------|-------------|
| 0 (initial) | 100ms | 10 |
| 1 | 200ms | 20 |
| 2 | 400ms | 40 |
| 3 | 800ms | 80 |

Max retries: **4**. After exhaustion, the dirty flag is cleared and the item is abandoned until next write.

---

## 7. Session ID Management

### 7.1 Rules

- **Range**: 0x01 ~ 0xFF (wraps to 0x01, never uses 0x00)
- **Scope**: Global within STM module (not per-Provider)
- **Reset on disconnect**: Yes, per protocol requirement
- **Usage**: Only for async Method requests (0x03, 0x05)

### 7.2 Matching Logic

```
M-core sends:  PICC_MethodRequest() → returns sessionId
M-core stores:  Stm_PendingReadReq.sessionId = sessionId
M-core polls:   PICC_GetResponseData(sessionId) → match response
```

---

## 8. Integration Points

### 8.1 Init Sequence

In `EcuM_main_init.c` → `App_Init_All()`:

```c
Hm_Init();
Stm_Init();    // ← Added after Hm_Init()
```

### 8.2 Task Integration

In `Ostask_main.c` → `TASK_M0_10MS`:

```c
Hm_Main();
Stm_Main();    // ← Added after Hm_Main()
```

### 8.3 PICC App Index Mapping

| Index | Enum | Module |
|-------|------|--------|
| 0 | `PICC_APP_PWSM_SRV` | Power Management Server |
| 1 | `PICC_APP_PWSM_CLI` | Power Management Client |
| 2 | `PICC_APP_DIAG` | Diagnostics |
| **3** | **`PICC_APP_STM_CLI`** | **STM Consumer (Client)** |
| 4 | `PICC_APP_STORAGE` | STM Provider (Server) |

### 8.4 PICC RX Buffer

`picc_mailbox.c`: `PICC_RX_MAX_DATA_LEN` changed from **32** to **80** bytes to support NVM blocks up to 64 bytes + protocol overhead.

### 8.5 Task Stack

`Ostask_main.c`: `OSTASK_10MS_STACK_SIZE` changed from **256** to **320** words to accommodate `Stm_Main()` processing.

---

## 9. Data Flow Examples

### 9.1 Local Write + Sync to A-core

```
Application calls Stm_WriteLocal(0x0003, data, 4)
     │
     ├─ StmNvm_Write(0x0003, data, 4)
     │    ├─ Update RAM mirror
     │    ├─ dirty = TRUE
     │    ├─ Write to EEPROM (segmented)
     │    └─ dirty = FALSE (EEPROM OK)
     │
     └─ (Next 10ms cycle in Stm_Main())
        │
        └─ Stm_ProcessSyncToA() finds dirty block
           ├─ Build payload: [0x00][0x03][data...]
           ├─ PICC_MethodRequest(0x04)
           └─ Set up retry state

     (Response from A-core)
        │
        └─ StmNvm_ClearDirty(0x0003)
```

### 9.2 A-core Writes to M-core

```
A-core sends Method 0x02: [0x00][0x01][8 bytes of data]
     │
     └─ Stm_ProcessRxFromA()
        ├─ Parse dataId=0x0001, dataLen=8
        ├─ StmNvm_WriteFromA(0x0001, data, 8)
        │    ├─ Update RAM mirror + EEPROM
        │    └─ dirty = TRUE (will be synced back to A-core later)
        │
        └─ PICC_MethodResponse(0x00, [0x00][0x01][0x00][0x00])
```

### 9.3 Disconnect → Reconnect Sequence

```
1. Link lost (heartbeat timeout or disconnect notification)
2. Stm_CheckLinkState() detects link down
3. State → WAIT_LINK
4. Reset actions:
   - StmNvm_ResetOnDisconnect() → clear dirty flags, preserve data
   - Stm_ResetSessionId() → sessionId = 0
   - Cancel retry/pending requests
5. PICC auto-sends link requests (Client role)
6. Both links reconnect → WAIT_CONSISTENCY
7. Immediate transition → SYNC_TO_A
8. Dirty data from new writes is synced to A-core
9. All synced → RUNNING
```

---

## 10. Configuration Guide

### 10.1 Adding a New Data Item

1. **Increase `STM_MAX_DATA_ITEMS`** in `stm_cnf.h` (if > 5)
2. **Add entry** to `g_StmDataItemCfg[]` in `stm_cnf.c`:
   ```c
   { 0x0006U, 4U  },  /* Item 6: new data */
   ```
3. **Verify EEPROM capacity**: `SUM(2 + maxDataLen) ≤ 64`
4. **Adjust `STM_EEPROM_DATA_END_ADDR`** if more space is needed
5. **Increase `PICC_RX_MAX_DATA_LEN`** if max payload exceeds 80 bytes

### 10.2 Changing Retry Behavior

Modify in `stm_cnf.h` / `stm_cnf.c`:

| Parameter | File | Default | Description |
|-----------|------|---------|-------------|
| `STM_RETRY_MAX_COUNT` | `stm_cnf.h` | 4 | Max retry attempts for 0x04 |
| `g_StmRetryIntervals[]` | `stm_cnf.c` | {10,20,40,80} | Retry intervals in 10ms ticks |
| `STM_SYNC_MAX_PER_CYCLE` | `stm_cnf.h` | 2 | Max sync messages per 10ms cycle |

### 10.3 Changing PICC IDs

Modify in `stm_cnf.h`:

| Parameter | Default | Note |
|-----------|---------|------|
| `STM_PROVIDER_ID` | 41 (0x29) | M-core Server ID, must be unique system-wide |
| `STM_CONSUMER_ID` | 42 (0x2A) | M-core Client ID, must be unique system-wide |
| `STM_PROVIDER_REMOTE_ID` | 46 (0x2E) | A-core Server that M-core Client talks to |
| `STM_CONSUMER_REMOTE_ID` | 47 (0x2F) | A-core Client that talks to M-core Server |

---

## 11. Known Limitations & TODOs

| # | Item | Status | Note |
|---|------|--------|------|
| 1 | `WAIT_CONSISTENCY` state | **Not implemented** | Currently transitions immediately to `SYNC_TO_A` |
| 2 | Method 0x03 (M read from A, sync) | **Not fully implemented** | Only 0x05 (async) is used; 0x03 response parsing is TODO |
| 3 | Read request timeout | **Not implemented** | If A-core never responds, request stays pending until disconnect |
| 4 | EEPROM write error recovery | **Basic** | Returns E_NOT_OK but does not retry EEPROM write |
| 5 | Method 0x04 retry after max retries | **Gives up** | Dirty flag cleared; data lost for sync until next write |
| 6 | Multiple concurrent sync requests | **Not supported** | Only one 0x04 sync in-flight at a time |

---

## 12. Stack Usage Considerations

All large buffers in `stm.c` and `stm_nvm.c` are declared `static` to avoid stack overflow:

| Variable | File | Size | Reason |
|----------|------|------|--------|
| `s_methodBuf` | stm.c | 68B | Method 0x01/0x02 RX buffer |
| `s_respBuf` | stm.c | 68B | Method 0x01/0x02 TX buffer |
| `s_syncBuf` | stm.c | 68B | Sync 0x04 RX buffer |
| `s_txPayload` | stm.c | 66B | Sync 0x04 TX payload |
| `s_reqPayload` | stm.c | 4B | Read 0x05 request payload |
| `s_readRspBuf` | stm.c | 68B | Read 0x03/0x05 response buffer |
| `s_writeBuf` | stm_nvm.c | 16B | EEPROM segmented write buffer |

**Task stack**: `OSTASK_10MS_STACK_SIZE = 320 words (1.25KB)` — sufficient for all STM processing.
