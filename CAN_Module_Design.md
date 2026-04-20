# CAN Module Architecture and Design

This document details the architectural design and functional calling relationships of the CAN communication module within the M-Core platform.

## 1. System Overview

The CAN module is responsible for handling cyclic transmission and reception of physical CAN messages. It bridges the Application Layer (where physical values are used) with the low-level hardware `FlexCAN_Ip` driver via generated DBC pack/unpack functions. 

The module is strictly driven by the centralized RTOS periodic task architecture, specifically running within the unified 10ms FreeRTOS thread.

## 2. Module Architecture

The architecture is divided into three distinct layers:
1. **Application Layer:** Sets and reads physical variables (e.g., Vehicle Speed, Battery, Gear) from globally exposed structures generated from DBC.
2. **Intermediate Packing/Unpacking Layer & Cyclic Dispatcher:** Maps physical variables to/from raw `uint8_t` byte sequences, implementing endianness and scaling conversions. The cyclic dispatcher (`AINFC_Can_Cyclic_10ms`) orchestrates this packing and interfaces with the underlying FlexCAN wrappers.
3. **Hardware Abstraction (FlexCAN IP):** Uses NXP RTD `FlexCAN_Ip` APIs to interact physically with the CAN controller's Message Buffers (MBs).

```mermaid
graph TD
    classDef app fill:#e1f5fe,stroke:#03a9f4,stroke-width:2px;
    classDef mid fill:#fff3e0,stroke:#ff9800,stroke-width:2px;
    classDef hw fill:#e8f5e9,stroke:#4caf50,stroke-width:2px;
    
    subgraph "1. Application Layer"
        APP[App Logic]:::app
        DBCTXStruct[g_tx_Standard_100_Tx]:::app
        DBCRXStruct[g_rx_Standard_200_Rx]:::app
    end

    subgraph "2. Intermediate Layer (Can_tools_generated & FlexCAN_Ip_main)"
        DBC_Unpack[Standard_200_Rx_unpack]:::mid
        DBC_Pack[Standard_100_Tx_pack]:::mid
        Cyclic[AINFC_Can_Cyclic_10ms]:::mid
        Wrap_RX[AINFC_Can_RxMsgL]:::mid
        Wrap_TX[AINFC_Can_TxMsg]:::mid
    end

    subgraph "3. FlexCAN IP / Hardware Layer"
        FlexCAN[FlexCAN_Ip Driver APIs]:::hw
        CAN_HW((CAN Controller MBs)):::hw
    end

    APP -->|Write physical values| DBCTXStruct
    DBCTXStruct -.->|Read by| DBC_Pack
    DBC_Pack -->|Raw Tx Data| Wrap_TX
    
    Wrap_RX -->|Raw Rx Data| DBC_Unpack
    DBC_Unpack -.->|Writes to| DBCRXStruct
    DBCRXStruct -->|Read physical values| APP

    %% Control Flow
    Cyclic -->|1. Polling Call| Wrap_RX
    Cyclic -->|2. Convert Rx| DBC_Unpack
    Cyclic -->|3. Convert Tx| DBC_Pack
    Cyclic -->|4. Send Tx| Wrap_TX
    
    Wrap_RX <--> FlexCAN
    Wrap_TX <--> FlexCAN
    FlexCAN <--> CAN_HW
```

## 3. Cyclic Processing Walkthrough (10ms)

The core driver for the CAN module is the unified 10ms thread `OS_10ms` in `Ostask_main.c`. The processing follows a strict execution path executed every 10 milliseconds.

```mermaid
sequenceDiagram
    participant OS as OS_10ms_Thread (Ostask_main.c)
    participant Dispatcher as AINFC_Can_Cyclic_10ms (FlexCAN_Ip_main.c)
    participant Wrapper as FlexCAN Wrapper (AINFC_Can_Rx/Tx)
    participant DBC as CANdbc_file (Pack/Unpack)
    participant HW as FlexCAN_Ip Driver

    Note over OS, HW: Start 10ms Cycle Context
    OS->>Dispatcher: Call TASK_M0_10MS()

    rect rgb(232, 245, 233)
        Note over Dispatcher, HW: Phase 1: Receive (MB0 - ID: 0x200)
        Dispatcher->>Wrapper: AINFC_Can_RxMsgL()
        Wrapper->>HW: FlexCAN_Ip_MainFunctionRead()
        HW-->>Wrapper: FLEXCAN_STATUS_SUCCESS
        Wrapper->>Wrapper: Copy data to g_RxData0
        Wrapper->>HW: FlexCAN_Ip_Receive() [Re-arm MB]
        Wrapper-->>Dispatcher: AINFC_CAN_OK
        
        Dispatcher->>DBC: Standard_200_Rx_unpack(g_rx_Standard_200_Rx, g_RxData0)
        DBC-->>Dispatcher: Unpack Complete
    end

    rect rgb(255, 243, 224)
        Note over Dispatcher, HW: Phase 2: Transmit (MB0 - ID: 0x100)
        Dispatcher->>DBC: Standard_100_Tx_pack(g_txData0, g_tx_Standard_100_Tx)
        DBC-->>Dispatcher: Pack Complete
        
        Dispatcher->>Wrapper: AINFC_Can_TxMsg(g_txData0)
        Wrapper->>HW: FlexCAN_Ip_GetTransferStatus() [Check busy]
        HW-->>Wrapper: FLEXCAN_STATUS_SUCCESS [MB Free]
        Wrapper->>HW: FlexCAN_Ip_Send()
        HW-->>Wrapper: Started
        Wrapper-->>Dispatcher: AINFC_CAN_OK
    end

    Dispatcher-->>OS: Return to OS
```

## 4. Key Components and Files

- **`Ostask_main.c / .h` (Task Management)**
  - Implements the FreeRTOS `OsTask_10ms_Thread`.
  - Agregates the calls within `TASK_M0_10MS()`, executing `AINFC_Can_Cyclic_10ms()` immediately at the start of the timeframe.

- **`FlexCAN_Ip_main.c / .h` (CAN Business Logic)**
  - Implements `AINFC_Can_Cyclic_10ms()` which handles cyclic polling.
  - Implements `AINFC_Can_TxMsg()` and `AINFC_Can_RxMsgL()` wrappers.
  - Manages internal states (`g_txBusy[]`, `g_rxConfigured[]`) and CAN Mailbox IDs assignment.

- **`CANdbc_file.c / .h` (Auto-Generated DBC Layer)**
  - Exposes globally available physical measurement structs: `g_tx_Standard_100_Tx` and `g_rx_Standard_200_Rx`.
  - Implements signal bit-masking, byte alignment, shifting, factor scaling, and endianness logic within the `_pack` and `_unpack` macros.

- **`NXP RTD FlexCAN_Ip` (Underlying Platform Code)**
  - Handles actual hardware registers, located in `RTD/src/FlexCAN_Ip.c` (Read-Only).
