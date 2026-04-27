#!C:/LegacyApp/python/3.10.0/python.exe
# -*- coding: utf-8 -*-
"""
DBC to C Code Generator for Embedded CAN Applications (cantools-based)
======================================================================

Uses the `cantools` library for reliable DBC parsing and generates C
header/source files with:
  - Message struct definitions (with signal metadata as comments)
  - Pack functions  (struct -> CAN data bytes)  — TX messages only
  - Unpack functions (CAN data bytes -> struct) — RX messages only
  - IEEE float / double signal support
  - Motorola (big-endian) and Intel (little-endian) byte order
  - Factor / offset scaling
  - Signed integer two's complement handling
  - Value description enums

Usage:
    C:/LegacyApp/python/3.10.0/python.exe dbc_to_c_generator.py <input.dbc> <output_dir> [--node <node_name>]

Example (run from Tools/CAN_Tools):
    C:/LegacyApp/python/3.10.0/python.exe dbc_to_c_generator.py SOA_DBC_File/CANdbc_file.dbc D:/My_SandBox/NXP_S32G_Asample/sw-prj-SDV_HPC_BSW_Mcore/IPCF_FreeRTOS_S32G399A_M7_Oring/SWC/Soa_Adapter --node SDV_M_CORE0

Dependencies:
    C:/LegacyApp/python/3.10.0/python.exe -m pip install cantools
"""

import os
import sys
import re
import argparse
from pathlib import Path
from datetime import datetime

REQUIRED_PYTHON_EXE = Path("C:/Users/RogPlus/AppData/Local/Programs/Python/Python312/python.exe")

# =====================================================================
#  DBC Input File Path Configuration
#  Specify the absolute path to the input .dbc file here.
#  If set to None, the script will auto-detect from SOA_DBC_File/ subfolder.
# =====================================================================
DEFAULT_DBC_FILE_PATH = Path(
    r"D:\MySandbox\SDV_Project\IPCF_FreeRTOS_S32G399A_M7_Oring\Tools\CAN_Tools\SOA_DBC_File\CANdbc_file.dbc"
)

DEFAULT_OUTPUT_DIR = Path(
    r"D:\My_SandBox\NXP_S32G_Asample\sw-prj-SDV_HPC_BSW_Mcore\IPCF_FreeRTOS_S32G399A_M7_Oring\SWC\Soa_Adapter"
)
DEFAULT_OUTPUT_BASENAME = "SOA_CANdbc_Generated"


def validate_python_executable() -> None:
    """Fail fast when the script is not started with the required interpreter."""
    current_executable = Path(sys.executable).resolve()
    required_executable = REQUIRED_PYTHON_EXE.resolve()
    if current_executable != required_executable:
        print(
            "ERROR: Unsupported Python interpreter. "
            f"Expected: {required_executable}; current: {current_executable}.\n"
            "Run the generator with: "
            f"{required_executable} {Path(__file__).name}",
            file=sys.stderr,
        )
        sys.exit(1)

try:
    import cantools
except ImportError:
    print(
        "ERROR: 'cantools' library is required. Install via: "
        f"{sys.executable} -m pip install cantools",
        file=sys.stderr,
    )
    sys.exit(1)


# ===========================================================================
#  Helpers
# ===========================================================================

def upper_name(name: str) -> str:
    """Convert CamelCase / mixed to UPPER_CASE, collapsing double underscores."""
    s1 = re.sub("(.)([A-Z][a-z]+)", r"\1_\2", name)
    result = re.sub("([a-z0-9])([A-Z])", r"\1_\2", s1).upper()
    result = re.sub(r"_+", "_", result)
    return result


def c_type_for_signal(sig) -> str:
    """Return the best C type for a signal's struct member."""
    if sig.is_float:
        if sig.length <= 32:
            return "float"
        else:
            return "double"
    # integer
    if sig.is_signed:
        if sig.length <= 8:
            return "int8_t"
        elif sig.length <= 16:
            return "int16_t"
        elif sig.length <= 32:
            return "int32_t"
        else:
            return "int64_t"
    else:
        if sig.length <= 8:
            return "uint8_t"
        elif sig.length <= 16:
            return "uint16_t"
        elif sig.length <= 32:
            return "uint32_t"
        else:
            return "uint64_t"


def raw_uint_type(bit_width: int) -> str:
    """Return the unsigned integer type that can hold bit_width bits."""
    if bit_width <= 8:
        return "uint8_t"
    elif bit_width <= 16:
        return "uint16_t"
    elif bit_width <= 32:
        return "uint32_t"
    else:
        return "uint64_t"


def raw_uint_suffix(bit_width: int) -> str:
    """Return the integer literal suffix for a raw type."""
    if bit_width <= 16:
        return "u"
    elif bit_width <= 32:
        return "ul"
    else:
        return "ull"


def type_bits_for_length(length: int) -> int:
    """Return the C integer type bit-width for a given signal length."""
    if length <= 8:
        return 8
    elif length <= 16:
        return 16
    elif length <= 32:
        return 32
    else:
        return 64


# ---------------------------------------------------------------------------
#  Motorola / Intel byte-layout helpers
# ---------------------------------------------------------------------------

def _motorola_byte_segments(start_bit: int, length: int):
    """
    Yield (byte_idx, low_bit_in_byte, bits_count, raw_shift, mask) tuples
    from MSB to LSB for a Motorola-order (big-endian) signal.

    start_bit: MSB position in Vector DBC notation (row*8 + col, col=7 is MSB).
    """
    remaining = length
    raw_shift = length
    byte_idx = start_bit // 8
    bit_pos = start_bit % 8

    while remaining > 0:
        bits_here = min(bit_pos + 1, remaining)
        raw_shift -= bits_here
        low_bit = bit_pos - bits_here + 1
        mask = (1 << bits_here) - 1
        yield (byte_idx, low_bit, bits_here, raw_shift, mask)
        remaining -= bits_here
        byte_idx += 1
        bit_pos = 7


def _intel_byte_segments(start_bit: int, length: int):
    """
    Yield (byte_idx, low_bit_in_byte, bits_count, raw_shift, mask) tuples
    from LSB to MSB for an Intel-order (little-endian) signal.

    start_bit: LSB position.
    """
    remaining = length
    raw_shift = 0
    byte_idx = start_bit // 8
    bit_pos = start_bit % 8

    while remaining > 0:
        bits_here = min(8 - bit_pos, remaining)
        low_bit = bit_pos
        mask = (1 << bits_here) - 1
        yield (byte_idx, low_bit, bits_here, raw_shift, mask)
        raw_shift += bits_here
        remaining -= bits_here
        byte_idx += 1
        bit_pos = 0


def byte_segments(sig):
    """Get byte segments for a signal based on its byte order."""
    if sig.byte_order == "big_endian":
        return list(_motorola_byte_segments(sig.start, sig.length))
    else:
        return list(_intel_byte_segments(sig.start, sig.length))


# ===========================================================================
#  Direction helpers
# ===========================================================================

def is_tx(msg, node: str) -> bool:
    """Message is TX when the given node is the sender."""
    if node is None:
        return True
    return node in msg.senders


def is_rx(msg, node: str) -> bool:
    """Message is RX when the given node appears in any signal's receivers.

    Also treats 'Vector__XXX' as a wildcard meaning 'all nodes', which is
    the standard DBC convention.  Additionally, any message whose sender is
    NOT our target node is considered a candidate RX message (the target node
    must be receiving it if it is defined in the DBC and not sending it).
    """
    if node is None:
        return True
    for sig in msg.signals:
        if node in sig.receivers:
            return True
        # Vector__XXX is the DBC wildcard — treat as "every node receives this"
        if "Vector__XXX" in sig.receivers:
            return True
    # Fallback: if this node is NOT the sender, assume it is a receiver
    if not is_tx(msg, node):
        return True
    return False


def direction_tag(msg, node: str) -> str:
    """Return 'TX', 'RX', or 'TX/RX' direction tag."""
    parts = []
    if is_tx(msg, node):
        parts.append("TX")
    if is_rx(msg, node):
        parts.append("RX")
    return "/".join(parts) if parts else "N/A"


# ===========================================================================
#  C Code Generator
# ===========================================================================

def generate_header(db, basename: str, node: str) -> str:
    """Generate the .h header file content."""
    guard = basename.upper().replace(".", "_").replace("-", "_") + "_H"
    lines = []
    lines.append(f"/**")
    lines.append(f" * @file {basename}.h")
    lines.append(f" * @brief Auto-generated CAN message pack/unpack interface")
    lines.append(f" *")
    lines.append(f" * Generated from DBC by dbc_to_c_generator.py (cantools-based)")
    lines.append(f" * Date: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    lines.append(f" *")
    lines.append(f" * DO NOT EDIT — re-run the generator to update.")
    lines.append(f" */")
    lines.append(f"")
    lines.append(f"#ifndef {guard}")
    lines.append(f"#define {guard}")
    lines.append(f"")
    lines.append(f"#include <stdint.h>")
    lines.append(f"#include <string.h>")
    lines.append(f"")

    for msg in sorted(db.messages, key=lambda m: m.frame_id):
        msg_is_tx = is_tx(msg, node)
        msg_is_rx = is_rx(msg, node)
        if not (msg_is_tx or msg_is_rx):
            continue

        uname = upper_name(msg.name)
        sender_str = ", ".join(msg.senders) if msg.senders else "Vector__XXX"
        dtag = direction_tag(msg, node)

        lines.append(f"/* " + "=" * 70 + " */")
        lines.append(
            f"/* Message: {msg.name}  (0x{msg.frame_id:03X}, {msg.length} bytes, "
            f"{dtag}, sender={sender_str}) */"
        )
        lines.append(f"/* " + "=" * 70 + " */")
        lines.append(f"")
        lines.append(f"#define {uname}_ID          (0x{msg.frame_id:03X}U)")
        lines.append(f"#define {uname}_DLC         ({msg.length}U)")

        # Cycle time from DBC attribute
        cycle_time = msg.cycle_time
        if cycle_time is not None and cycle_time > 0:
            lines.append(f"#define {uname}_CYCLE_MS    ({cycle_time}U)")
        lines.append(f"")

        # Value-description enums
        for sig in msg.signals:
            if hasattr(sig, 'choices') and sig.choices:
                for val, desc in sorted(sig.choices.items()):
                    enum_name = f"{uname}_{upper_name(sig.name)}_{upper_name(str(desc).replace(' ', '_'))}"
                    lines.append(f"#define {enum_name}  ({val}U)")
                lines.append(f"")

        # Struct
        lines.append(f"typedef struct {{")
        for sig in msg.signals:
            ctype = c_type_for_signal(sig)
            comment_parts = []
            if sig.scale != 1.0 or sig.offset != 0.0:
                comment_parts.append(f"factor={sig.scale}, offset={sig.offset}")
            if sig.unit:
                comment_parts.append(f'unit="{sig.unit}"')
            comment_parts.append(f"[{sig.minimum}, {sig.maximum}]")
            if sig.comment:
                comment_parts.append(sig.comment)
            comment = ", ".join(comment_parts)
            lines.append(f"    {ctype:10s} {sig.name};  /* {comment} */")
        lines.append(f"}} {msg.name}_t;")
        lines.append(f"")

    # ---------------------------------------------------------------
    #  Dispatch table type + unified API declarations
    # ---------------------------------------------------------------
    lines.append(f"/* " + "=" * 70 + " */")
    lines.append(f"/*  DBC dispatch table & unified pack/unpack API                        */")
    lines.append(f"/* " + "=" * 70 + " */")
    lines.append(f"")
    lines.append(f"/** Function-pointer types used by the dispatch table */")
    lines.append(f"typedef int (*Dbc_PackFuncType)(uint8_t *data, const void *msg, uint8_t dlc);")
    lines.append(f"typedef int (*Dbc_UnpackFuncType)(void *msg, const uint8_t *data, uint8_t dlc);")
    lines.append(f"")
    lines.append(f"/** One entry in the auto-generated DBC dispatch table */")
    lines.append(f"typedef struct {{")
    lines.append(f"    uint32_t  msgId;    /**< CAN message ID */")
    lines.append(f"    uint8_t   dlc;      /**< Message DLC */")
    lines.append(f"    Dbc_PackFuncType   pack;    /**< NULL if RX-only */")
    lines.append(f"    Dbc_UnpackFuncType unpack;  /**< NULL if TX-only */")
    lines.append(f"}} Dbc_MessageHandlerType;")
    lines.append(f"")

    # Count TX / RX messages
    tx_msgs = [m for m in db.messages if is_tx(m, node)]
    rx_msgs = [m for m in db.messages if is_rx(m, node)]
    table_count = len(set(m.frame_id for m in tx_msgs + rx_msgs))

    lines.append(f"/** Number of entries in the dispatch table */")
    lines.append(f"#define DBC_MESSAGE_TABLE_COUNT  ({table_count}U)")
    lines.append(f"")
    lines.append(f"/** Auto-generated dispatch table (defined in {basename}.c) */")
    lines.append(f"extern const Dbc_MessageHandlerType g_dbcMessageTable[DBC_MESSAGE_TABLE_COUNT];")
    lines.append(f"")
    lines.append(f"/**")
    lines.append(f" * @brief Unified TX pack function — dispatches by CAN_ID")
    lines.append(f" *")
    lines.append(f" * @param[in]  CAN_ID  CAN message ID (e.g. 0x100U)")
    lines.append(f" * @param[out] data    Raw CAN data bytes (caller-allocated)")
    lines.append(f" * @param[in]  msg     Pointer to the typed DBC struct")
    lines.append(f" * @param[in]  dlc     Data length code")
    lines.append(f" * @return  0 on success, -1 on error")
    lines.append(f" */")
    lines.append(f"int Standard_Tx_pack(uint32_t CAN_ID, uint8_t *data, const void *msg, uint8_t dlc);")
    lines.append(f"")
    lines.append(f"/**")
    lines.append(f" * @brief Unified RX unpack function — dispatches by CAN_ID")
    lines.append(f" *")
    lines.append(f" * @param[in]  CAN_ID  CAN message ID (e.g. 0x200U)")
    lines.append(f" * @param[out] msg     Pointer to the typed DBC struct")
    lines.append(f" * @param[in]  data    Raw CAN data bytes")
    lines.append(f" * @param[in]  dlc     Data length code")
    lines.append(f" * @return  0 on success, -1 on error")
    lines.append(f" */")
    lines.append(f"int Standard_Rx_unpack(uint32_t CAN_ID, void *msg, const uint8_t *data, uint8_t dlc);")
    lines.append(f"")

    # ---------------------------------------------------------------
    #  Global struct instance extern declarations
    # ---------------------------------------------------------------
    lines.append(f"/* " + "=" * 70 + " */")
    lines.append(f"/*  Global message struct instances                                     */")
    lines.append(f"/* " + "=" * 70 + " */")
    lines.append(f"")
    for msg in sorted(db.messages, key=lambda m: m.frame_id):
        msg_is_tx = is_tx(msg, node)
        msg_is_rx = is_rx(msg, node)
        if not (msg_is_tx or msg_is_rx):
            continue
        if msg_is_tx:
            lines.append(
                f"extern {msg.name}_t g_tx_{msg.name};"
                f"  /**< TX 0x{msg.frame_id:03X} - DLC={msg.length} */"
            )
        if msg_is_rx:
            lines.append(
                f"extern {msg.name}_t g_rx_{msg.name};"
                f"  /**< RX 0x{msg.frame_id:03X} - DLC={msg.length} */"
            )
    lines.append(f"")
    lines.append(f"#endif /* {guard} */")
    lines.append(f"")
    return "\n".join(lines)


def _gen_pack(msg) -> list:
    """Generate pack function for a message (struct -> CAN data bytes)."""
    L = []
    L.append(f"int {msg.name}_pack(uint8_t *data, const {msg.name}_t *msg, uint8_t dlc)")
    L.append(f"{{")
    L.append(f"    if ((data == (void *)0) || (msg == (void *)0)) {{ return -1; }}")
    L.append(f"    if (dlc < {msg.length}U) {{ return -1; }}")
    L.append(f"")
    L.append(f"    (void)memset(data, 0, (size_t){msg.length}U);")
    L.append(f"")

    for sig in msg.signals:
        order_str = "Motorola" if sig.byte_order == "big_endian" else "Intel"
        float_str = ""
        if sig.is_float:
            float_str = ", float" if sig.length <= 32 else ", double"
        L.append(f"    /* {sig.name}: start_bit={sig.start}, len={sig.length}, "
                 f"{order_str}{float_str} */")

        segs = byte_segments(sig)

        if sig.is_float and sig.length <= 32:
            # IEEE float32 — memcpy to uint32_t, then pack bytes
            L.append(f"    {{")
            L.append(f"        uint32_t raw_val;")
            L.append(f"        (void)memcpy(&raw_val, &msg->{sig.name}, sizeof(uint32_t));")
            for byte_idx, low_bit, bits_here, rshift, mask in segs:
                if bits_here == 8 and low_bit == 0:
                    L.append(f"        data[{byte_idx}] = (uint8_t)(raw_val >> {rshift}u);")
                else:
                    L.append(
                        f"        data[{byte_idx}] |= (uint8_t)"
                        f"((raw_val >> {rshift}u) & 0x{mask:02X}u) << {low_bit}u;"
                    )
            L.append(f"    }}")

        elif sig.is_float and sig.length > 32:
            # IEEE float64 — memcpy to uint64_t
            L.append(f"    {{")
            L.append(f"        uint64_t raw_val;")
            L.append(f"        (void)memcpy(&raw_val, &msg->{sig.name}, sizeof(uint64_t));")
            for byte_idx, low_bit, bits_here, rshift, mask in segs:
                if bits_here == 8 and low_bit == 0:
                    L.append(f"        data[{byte_idx}] = (uint8_t)(raw_val >> {rshift}u);")
                else:
                    L.append(
                        f"        data[{byte_idx}] |= (uint8_t)"
                        f"(((uint8_t)(raw_val >> {rshift}u)) & 0x{mask:02X}u) << {low_bit}u;"
                    )
            L.append(f"    }}")

        else:
            # Integer signal
            rtype = raw_uint_type(sig.length)
            suf = raw_uint_suffix(sig.length)
            L.append(f"    {{")

            # Apply inverse scaling if needed
            if sig.scale != 1.0 or sig.offset != 0.0:
                L.append(f"        {rtype} raw_val = ({rtype})"
                         f"((msg->{sig.name} - ({sig.offset})) / ({sig.scale}));")
            else:
                L.append(f"        {rtype} raw_val = ({rtype})msg->{sig.name};")

            for byte_idx, low_bit, bits_here, rshift, mask in segs:
                if bits_here == 8 and low_bit == 0 and rshift == 0:
                    L.append(f"        data[{byte_idx}] = (uint8_t)raw_val;")
                elif bits_here == 8 and low_bit == 0:
                    L.append(f"        data[{byte_idx}] = (uint8_t)(raw_val >> {rshift}{suf});")
                elif rshift == 0:
                    L.append(
                        f"        data[{byte_idx}] |= (uint8_t)"
                        f"(((uint8_t)raw_val) & 0x{mask:02X}u) << {low_bit}u;"
                    )
                else:
                    L.append(
                        f"        data[{byte_idx}] |= (uint8_t)"
                        f"(((uint8_t)(raw_val >> {rshift}{suf})) & 0x{mask:02X}u) << {low_bit}u;"
                    )
            L.append(f"    }}")

    L.append(f"")
    L.append(f"    return 0;")
    L.append(f"}}")
    return L


def _gen_unpack(msg) -> list:
    """Generate unpack function for a message (CAN data bytes -> struct)."""
    L = []
    L.append(f"int {msg.name}_unpack({msg.name}_t *msg, const uint8_t *data, uint8_t dlc)")
    L.append(f"{{")
    L.append(f"    if ((data == (void *)0) || (msg == (void *)0)) {{ return -1; }}")
    L.append(f"    if (dlc < {msg.length}U) {{ return -1; }}")
    L.append(f"")

    for sig in msg.signals:
        L.append(f"    /* {sig.name} */")
        segs = byte_segments(sig)

        if sig.is_float and sig.length <= 32:
            # IEEE float32
            L.append(f"    {{")
            L.append(f"        uint32_t raw_val = 0u;")
            for byte_idx, low_bit, bits_here, rshift, mask in segs:
                if bits_here == 8 and low_bit == 0:
                    L.append(f"        raw_val |= ((uint32_t)data[{byte_idx}]) << {rshift}u;")
                else:
                    L.append(
                        f"        raw_val |= ((uint32_t)(data[{byte_idx}] >> {low_bit}u)"
                        f" & 0x{mask:02X}u) << {rshift}u;"
                    )
            L.append(f"        (void)memcpy(&msg->{sig.name}, &raw_val, sizeof(float));")
            L.append(f"    }}")

        elif sig.is_float and sig.length > 32:
            # IEEE float64
            L.append(f"    {{")
            L.append(f"        uint64_t raw_val = 0u;")
            for byte_idx, low_bit, bits_here, rshift, mask in segs:
                if bits_here == 8 and low_bit == 0:
                    L.append(f"        raw_val |= ((uint64_t)data[{byte_idx}]) << {rshift}u;")
                else:
                    L.append(
                        f"        raw_val |= ((uint64_t)(data[{byte_idx}] >> {low_bit}u)"
                        f" & 0x{mask:02X}u) << {rshift}u;"
                    )
            L.append(f"        (void)memcpy(&msg->{sig.name}, &raw_val, sizeof(double));")
            L.append(f"    }}")

        else:
            # Integer signal
            rtype = raw_uint_type(sig.length)
            suf = raw_uint_suffix(sig.length)
            ctype = c_type_for_signal(sig)
            L.append(f"    {{")
            L.append(f"        {rtype} raw_val = 0u;")

            for byte_idx, low_bit, bits_here, rshift, mask in segs:
                cast = f"({rtype})" if sig.length > 8 else ""
                if bits_here == 8 and low_bit == 0 and rshift == 0:
                    L.append(f"        raw_val |= {cast}data[{byte_idx}];")
                elif bits_here == 8 and low_bit == 0:
                    L.append(f"        raw_val |= {cast}data[{byte_idx}] << {rshift}{suf};")
                elif rshift == 0:
                    L.append(
                        f"        raw_val |= {cast}(data[{byte_idx}] >> {low_bit}u)"
                        f" & 0x{mask:02X}u;"
                    )
                else:
                    L.append(
                        f"        raw_val |= ({cast}(data[{byte_idx}] >> {low_bit}u)"
                        f" & 0x{mask:02X}u) << {rshift}{suf};"
                    )

            # Sign extension for signed integers
            if sig.is_signed:
                type_bits = type_bits_for_length(sig.length)
                if sig.length < type_bits:
                    L.append(f"        /* sign extension */")
                    L.append(
                        f"        if ((raw_val & (1{suf} << {sig.length - 1}u)) != 0u) {{"
                    )
                    fill_mask = ((1 << type_bits) - 1) ^ ((1 << sig.length) - 1)
                    L.append(f"            raw_val |= 0x{fill_mask:X}{suf};")
                    L.append(f"        }}")

            # Apply scaling
            if sig.scale != 1.0 or sig.offset != 0.0:
                if sig.is_signed:
                    signed_type = raw_uint_type(sig.length).replace("uint", "int")
                    L.append(
                        f"        msg->{sig.name} = ({ctype})"
                        f"(({ctype})({signed_type})raw_val"
                        f" * {sig.scale} + {sig.offset});"
                    )
                else:
                    L.append(
                        f"        msg->{sig.name} = ({ctype})"
                        f"(raw_val * {sig.scale} + {sig.offset});"
                    )
            else:
                L.append(f"        msg->{sig.name} = ({ctype})raw_val;")

            L.append(f"    }}")

    L.append(f"")
    L.append(f"    return 0;")
    L.append(f"}}")
    return L


def generate_source(db, basename: str, node: str) -> str:
    """Generate the .c source file content."""
    lines = []
    lines.append(f"/**")
    lines.append(f" * @file {basename}.c")
    lines.append(f" * @brief Auto-generated CAN message pack/unpack implementation")
    lines.append(f" *")
    lines.append(f" * Generated from DBC by dbc_to_c_generator.py (cantools-based)")
    lines.append(f" * Date: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    lines.append(f" *")
    lines.append(f" * DO NOT EDIT — re-run the generator to update.")
    lines.append(f" */")
    lines.append(f"")
    lines.append(f'#include "{basename}.h"')
    lines.append(f"")

    # Global struct instance definitions
    lines.append(f"/* " + "=" * 70 + " */")
    lines.append(f"/*  Global message struct instances                                     */")
    lines.append(f"/* " + "=" * 70 + " */")
    for msg in sorted(db.messages, key=lambda m: m.frame_id):
        msg_is_tx = is_tx(msg, node)
        msg_is_rx = is_rx(msg, node)
        if not (msg_is_tx or msg_is_rx):
            continue
        if msg_is_tx:
            lines.append(f"{msg.name}_t g_tx_{msg.name} = {{0}};")
        if msg_is_rx:
            lines.append(f"{msg.name}_t g_rx_{msg.name} = {{0}};")
    lines.append(f"")

    # ------------------------------------------------------------------
    # Per-message pack/unpack (static — internal use only)
    # ------------------------------------------------------------------
    lines.append(f"/* " + "=" * 70 + " */")
    lines.append(f"/*  Per-message pack/unpack (static helpers)                            */")
    lines.append(f"/* " + "=" * 70 + " */")
    lines.append(f"")

    for msg in sorted(db.messages, key=lambda m: m.frame_id):
        msg_is_tx = is_tx(msg, node)
        msg_is_rx = is_rx(msg, node)
        if not (msg_is_tx or msg_is_rx):
            continue

        # TX message: generate static pack
        if msg_is_tx:
            pack_lines = _gen_pack(msg)
            # Prepend "static " to the function signature line
            pack_lines[0] = "static " + pack_lines[0]
            lines.extend(pack_lines)
            lines.append(f"")

        # RX message: generate static unpack
        if msg_is_rx:
            unpack_lines = _gen_unpack(msg)
            unpack_lines[0] = "static " + unpack_lines[0]
            lines.extend(unpack_lines)
            lines.append(f"")

    # ------------------------------------------------------------------
    # Adapter wrappers (void* -> typed pointer cast)
    # ------------------------------------------------------------------
    lines.append(f"/* " + "=" * 70 + " */")
    lines.append(f"/*  Adapter wrappers (void* interface for dispatch table)               */")
    lines.append(f"/* " + "=" * 70 + " */")
    lines.append(f"")

    for msg in sorted(db.messages, key=lambda m: m.frame_id):
        msg_is_tx = is_tx(msg, node)
        msg_is_rx = is_rx(msg, node)
        if not (msg_is_tx or msg_is_rx):
            continue

        if msg_is_tx:
            adapter_name = f"Dbc_{msg.name}_PackAdapter"
            lines.append(f"static int {adapter_name}(uint8_t *data, const void *msg, uint8_t dlc)")
            lines.append(f"{{")
            lines.append(f"    return {msg.name}_pack(data, (const {msg.name}_t *)msg, dlc);")
            lines.append(f"}}")
            lines.append(f"")

        if msg_is_rx:
            adapter_name = f"Dbc_{msg.name}_UnpackAdapter"
            lines.append(f"static int {adapter_name}(void *msg, const uint8_t *data, uint8_t dlc)")
            lines.append(f"{{")
            lines.append(f"    return {msg.name}_unpack(({msg.name}_t *)msg, data, dlc);")
            lines.append(f"}}")
            lines.append(f"")

    # ------------------------------------------------------------------
    # Dispatch table
    # ------------------------------------------------------------------
    lines.append(f"/* " + "=" * 70 + " */")
    lines.append(f"/*  Auto-generated dispatch table                                      */")
    lines.append(f"/* " + "=" * 70 + " */")
    lines.append(f"")
    lines.append(f"const Dbc_MessageHandlerType g_dbcMessageTable[DBC_MESSAGE_TABLE_COUNT] = {{")

    # Collect unique messages (by frame_id) for the table
    seen_ids = set()
    table_entries = []
    for msg in sorted(db.messages, key=lambda m: m.frame_id):
        if msg.frame_id in seen_ids:
            continue
        msg_is_tx = is_tx(msg, node)
        msg_is_rx = is_rx(msg, node)
        if not (msg_is_tx or msg_is_rx):
            continue
        seen_ids.add(msg.frame_id)

        uname = upper_name(msg.name)
        pack_fn = f"Dbc_{msg.name}_PackAdapter" if msg_is_tx else "(Dbc_PackFuncType)0"
        unpack_fn = f"Dbc_{msg.name}_UnpackAdapter" if msg_is_rx else "(Dbc_UnpackFuncType)0"

        table_entries.append(
            f"    {{ {uname}_ID, {uname}_DLC, {pack_fn}, {unpack_fn} }}"
        )

    lines.append(",\n".join(table_entries))
    lines.append(f"}};")
    lines.append(f"")

    # ------------------------------------------------------------------
    # Unified dispatch functions
    # ------------------------------------------------------------------
    lines.append(f"/* " + "=" * 70 + " */")
    lines.append(f"/*  Unified dispatch functions                                          */")
    lines.append(f"/* " + "=" * 70 + " */")
    lines.append(f"")

    # Standard_Tx_pack
    lines.append(f"int Standard_Tx_pack(uint32_t CAN_ID, uint8_t *data, const void *msg, uint8_t dlc)")
    lines.append(f"{{")
    lines.append(f"    uint32_t idx;")
    lines.append(f"")
    lines.append(f"    if ((data == (void *)0) || (msg == (void *)0)) {{ return -1; }}")
    lines.append(f"")
    lines.append(f"    for (idx = 0U; idx < DBC_MESSAGE_TABLE_COUNT; idx++)")
    lines.append(f"    {{")
    lines.append(f"        if (g_dbcMessageTable[idx].msgId == CAN_ID)")
    lines.append(f"        {{")
    lines.append(f"            if (g_dbcMessageTable[idx].pack != (Dbc_PackFuncType)0)")
    lines.append(f"            {{")
    lines.append(f"                return g_dbcMessageTable[idx].pack(data, msg, dlc);")
    lines.append(f"            }}")
    lines.append(f"            return -1;  /* ID found but no pack function */")
    lines.append(f"        }}")
    lines.append(f"    }}")
    lines.append(f"")
    lines.append(f"    return -1;  /* CAN_ID not found */")
    lines.append(f"}}")
    lines.append(f"")

    # Standard_Rx_unpack
    lines.append(f"int Standard_Rx_unpack(uint32_t CAN_ID, void *msg, const uint8_t *data, uint8_t dlc)")
    lines.append(f"{{")
    lines.append(f"    uint32_t idx;")
    lines.append(f"")
    lines.append(f"    if ((data == (void *)0) || (msg == (void *)0)) {{ return -1; }}")
    lines.append(f"")
    lines.append(f"    for (idx = 0U; idx < DBC_MESSAGE_TABLE_COUNT; idx++)")
    lines.append(f"    {{")
    lines.append(f"        if (g_dbcMessageTable[idx].msgId == CAN_ID)")
    lines.append(f"        {{")
    lines.append(f"            if (g_dbcMessageTable[idx].unpack != (Dbc_UnpackFuncType)0)")
    lines.append(f"            {{")
    lines.append(f"                return g_dbcMessageTable[idx].unpack(msg, data, dlc);")
    lines.append(f"            }}")
    lines.append(f"            return -1;  /* ID found but no unpack function */")
    lines.append(f"        }}")
    lines.append(f"    }}")
    lines.append(f"")
    lines.append(f"    return -1;  /* CAN_ID not found */")
    lines.append(f"}}")
    lines.append(f"")

    return "\n".join(lines)


# ===========================================================================
#  Main
# ===========================================================================

def _auto_find_dbc(script_dir: Path) -> Path:
    """Search for .dbc files in the SOA_DBC_File subfolder next to this script."""
    dbc_dir = script_dir / "SOA_DBC_File"
    if not dbc_dir.is_dir():
        print(f"ERROR: DBC folder not found: {dbc_dir}", file=sys.stderr)
        sys.exit(1)
    dbc_files = list(dbc_dir.glob("*.dbc"))
    if len(dbc_files) == 0:
        print(f"ERROR: No .dbc files found in {dbc_dir}", file=sys.stderr)
        sys.exit(1)
    if len(dbc_files) > 1:
        print(f"WARNING: Multiple .dbc files found, using the first one: {dbc_files[0].name}")
    return dbc_files[0]


def _auto_select_node(db) -> str:
    """Auto-select the M-Core node from the DBC database by pattern matching."""
    node_names = [n.name for n in db.nodes]
    # Priority 1: match "M_CORE" (case-insensitive)
    for name in node_names:
        if "M_CORE" in name.upper():
            return name
    # Priority 2: match "M_Core" or "MCU" or "Mcore"
    for name in node_names:
        upper = name.upper()
        if "MCORE" in upper or "MCU" in upper:
            return name
    # Fallback: None (generate for all nodes)
    print("WARNING: No M-Core node found, generating for ALL nodes.")
    return None


def main():
    validate_python_executable()

    ap = argparse.ArgumentParser(
        description="Generate C pack/unpack code from a Vector DBC file (using cantools).\n"
                    "When run WITHOUT arguments, auto-detects DBC file from SOA_DBC_File/ subfolder,\n"
                    "outputs to DEFAULT_OUTPUT_DIR, and auto-selects the M_CORE node.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("dbc", nargs="?", default=None, help="Path to the input .dbc file (auto-detected if omitted)")
    ap.add_argument("outdir", nargs="?", default=None, help="Output directory for generated .h / .c (auto-detected if omitted)")
    ap.add_argument(
        "--node",
        default=None,
        help="Target ECU node name. Only messages TX/RX by this node are generated. (auto-detected if omitted)",
    )
    args = ap.parse_args()

    script_dir = Path(__file__).resolve().parent

    # --- Resolve DBC file path ---
    if args.dbc is not None:
        dbc_path = Path(args.dbc).resolve()
    elif DEFAULT_DBC_FILE_PATH is not None:
        dbc_path = DEFAULT_DBC_FILE_PATH.resolve()
        print(f"[Config] DBC file: {dbc_path}")
    else:
        dbc_path = _auto_find_dbc(script_dir)
        print(f"[Auto] DBC file: {dbc_path}")

    # --- Auto-detect output directory ---
    if args.outdir is not None:
        out_dir = Path(args.outdir).resolve()
    else:
        out_dir = DEFAULT_OUTPUT_DIR.resolve()
        print(f"[Auto] Output dir: {out_dir}")

    if not dbc_path.is_file():
        print(f"ERROR: DBC file not found: {dbc_path}", file=sys.stderr)
        sys.exit(1)

    out_dir.mkdir(parents=True, exist_ok=True)

    # Parse using cantools
    db = cantools.database.load_file(str(dbc_path))
    print(f"[cantools] Parsed {len(db.messages)} messages, nodes: {db.nodes}")

    # --- Auto-detect node ---
    if args.node is not None:
        node = args.node
    else:
        node = _auto_select_node(db)
        if node:
            print(f"[Auto] Selected node: {node}")

    # Generate
    basename = DEFAULT_OUTPUT_BASENAME.strip() or dbc_path.stem

    h_content = generate_header(db, basename, node)
    c_content = generate_source(db, basename, node)

    h_path = out_dir / f"{basename}.h"
    c_path = out_dir / f"{basename}.c"

    h_path.write_text(h_content, encoding="utf-8")
    c_path.write_text(c_content, encoding="utf-8")

    print(f"Generated: {h_path}")
    print(f"Generated: {c_path}")

    # Summary
    for msg in sorted(db.messages, key=lambda m: m.frame_id):
        tag = direction_tag(msg, node)
        sender_str = ", ".join(msg.senders) if msg.senders else "N/A"
        funcs = []
        if is_tx(msg, node):
            funcs.append("pack")
        if is_rx(msg, node):
            funcs.append("unpack")
        print(
            f"  0x{msg.frame_id:03X}  {msg.name:30s}  DLC={msg.length}  "
            f"[{tag}]  signals={len(msg.signals)}  "
            f"sender={sender_str}  functions=[{', '.join(funcs)}]"
        )


if __name__ == "__main__":
    main()
