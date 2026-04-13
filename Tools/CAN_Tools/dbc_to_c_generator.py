#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
DBC to C Code Generator for Embedded CAN Applications
======================================================

Parses Vector DBC files and generates C header/source files with:
  - Message struct definitions (with signal metadata as comments)
  - Pack functions  (struct -> CAN data bytes)
  - Unpack functions (CAN data bytes -> struct)
  - IEEE float / double signal support (SIG_VALTYPE_)
  - Motorola (big-endian) and Intel (little-endian) byte order
  - Factor / offset scaling
  - Signed integer two's complement handling
  - Value description enums

Usage:
  python dbc_to_c_generator.py <input.dbc> <output_dir> [--node <node_name>]

Example (run from Tools/CAN_Tools):
  python dbc_to_c_generator.py SOA_DBC_File\\CANdbc_file.dbc ..\\..\\Can_tools_generated --node SDV_M_CORE0
"""

import re
import os
import sys
import math
import argparse
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Tuple
from pathlib import Path
from datetime import datetime


# ===========================================================================
#  Data model
# ===========================================================================

@dataclass
class Signal:
    """Represents one CAN signal inside a message."""
    name: str
    start_bit: int
    length: int
    byte_order: str          # 'big_endian' (Motorola) | 'little_endian' (Intel)
    is_signed: bool
    factor: float
    offset: float
    minimum: float
    maximum: float
    unit: str
    receivers: List[str]
    comment: str = ""
    value_type: int = 0      # 0 = integer, 1 = IEEE float32, 2 = IEEE float64
    value_descriptions: Dict[int, str] = field(default_factory=dict)


@dataclass
class Message:
    """Represents one CAN message (BO_)."""
    msg_id: int
    name: str
    dlc: int
    sender: str
    signals: List[Signal] = field(default_factory=list)
    comment: str = ""
    cycle_time: int = 0
    send_type: str = "Cycle"


# ===========================================================================
#  DBC parser
# ===========================================================================

class DbcParser:
    """Minimal-but-correct Vector DBC parser."""

    def __init__(self):
        self.messages: Dict[int, Message] = {}
        self.nodes: List[str] = []
        self.version: str = ""
        self.value_tables: Dict[str, Dict[int, str]] = {}

    # ----- public ----------------------------------------------------------
    def parse(self, filepath: str):
        with open(filepath, "r", encoding="utf-8", errors="replace") as fh:
            content = fh.read()

        self._parse_version(content)
        self._parse_nodes(content)
        self._parse_value_tables(content)
        self._parse_messages(content)
        self._parse_signal_value_types(content)
        self._parse_value_descriptions(content)
        self._parse_comments(content)
        self._parse_attributes(content)

    # ----- private ---------------------------------------------------------
    def _parse_version(self, content: str):
        m = re.search(r'VERSION\s+"([^"]*)"', content)
        if m:
            self.version = m.group(1)

    def _parse_nodes(self, content: str):
        m = re.search(r"BU_\s*:\s*(.*)", content)
        if m:
            self.nodes = m.group(1).strip().split()

    def _parse_value_tables(self, content: str):
        pattern = re.compile(
            r"VAL_TABLE_\s+(\w+)\s+(.*?)\s*;", re.DOTALL
        )
        pair_pat = re.compile(r'(\d+)\s+"([^"]*)"')
        for m in pattern.finditer(content):
            tbl_name = m.group(1)
            pairs = {}
            for pm in pair_pat.finditer(m.group(2)):
                pairs[int(pm.group(1))] = pm.group(2)
            self.value_tables[tbl_name] = pairs

    def _parse_messages(self, content: str):
        msg_re = re.compile(
            r"BO_\s+(\d+)\s+(\w+)\s*:\s*(\d+)\s+(\w+)", re.MULTILINE
        )
        # Signal regex — handles scientific notation in min/max
        sig_re = re.compile(
            r"\s+SG_\s+(\w+)\s*:\s*(\d+)\|(\d+)@([01])([+-])\s+"
            r"\(([^,]+),([^)]+)\)\s+\[([^|]+)\|([^\]]+)\]\s+"
            r'"([^"]*)"\s+(.*)',
            re.MULTILINE,
        )

        msg_positions = [(m.start(), m) for m in msg_re.finditer(content)]
        for idx, (pos, mm) in enumerate(msg_positions):
            msg_id = int(mm.group(1))
            msg = Message(
                msg_id=msg_id,
                name=mm.group(2),
                dlc=int(mm.group(3)),
                sender=mm.group(4),
            )
            # Determine text region for this message's signals
            start = mm.end()
            end = msg_positions[idx + 1][0] if idx + 1 < len(msg_positions) else len(content)
            block = content[start:end]

            for sm in sig_re.finditer(block):
                try:
                    minimum = float(sm.group(8).strip())
                except ValueError:
                    minimum = 0.0
                try:
                    maximum = float(sm.group(9).strip())
                except ValueError:
                    maximum = 0.0

                receivers = [r.strip() for r in sm.group(11).strip().split(",") if r.strip()]
                sig = Signal(
                    name=sm.group(1),
                    start_bit=int(sm.group(2)),
                    length=int(sm.group(3)),
                    byte_order="big_endian" if sm.group(4) == "0" else "little_endian",
                    is_signed=(sm.group(5) == "-"),
                    factor=float(sm.group(6)),
                    offset=float(sm.group(7)),
                    minimum=minimum,
                    maximum=maximum,
                    unit=sm.group(10),
                    receivers=receivers,
                )
                msg.signals.append(sig)

            self.messages[msg_id] = msg

    def _parse_signal_value_types(self, content: str):
        pat = re.compile(r"SIG_VALTYPE_\s+(\d+)\s+(\w+)\s*:\s*(\d+)\s*;")
        for m in pat.finditer(content):
            mid = int(m.group(1))
            sname = m.group(2)
            vt = int(m.group(3))
            if mid in self.messages:
                for s in self.messages[mid].signals:
                    if s.name == sname:
                        s.value_type = vt
                        break

    def _parse_value_descriptions(self, content: str):
        pat = re.compile(r"VAL_\s+(\d+)\s+(\w+)\s+(.*?)\s*;", re.DOTALL)
        pair_pat = re.compile(r'(\d+)\s+"([^"]*)"')
        for m in pat.finditer(content):
            mid = int(m.group(1))
            sname = m.group(2)
            if mid in self.messages:
                for s in self.messages[mid].signals:
                    if s.name == sname:
                        for pm in pair_pat.finditer(m.group(3)):
                            s.value_descriptions[int(pm.group(1))] = pm.group(2)
                        break

    def _parse_comments(self, content: str):
        # Message comments
        pat_bo = re.compile(r'CM_\s+BO_\s+(\d+)\s+"([^"]*)"\s*;')
        for m in pat_bo.finditer(content):
            mid = int(m.group(1))
            if mid in self.messages:
                self.messages[mid].comment = m.group(2)
        # Signal comments
        pat_sg = re.compile(r'CM_\s+SG_\s+(\d+)\s+(\w+)\s+"([^"]*)"\s*;')
        for m in pat_sg.finditer(content):
            mid = int(m.group(1))
            sname = m.group(2)
            if mid in self.messages:
                for s in self.messages[mid].signals:
                    if s.name == sname:
                        s.comment = m.group(3)
                        break

    def _parse_attributes(self, content: str):
        # Cycle time
        pat_ct = re.compile(r'BA_\s+"GenMsgCycleTime"\s+BO_\s+(\d+)\s+(\d+)\s*;')
        for m in pat_ct.finditer(content):
            mid = int(m.group(1))
            if mid in self.messages:
                self.messages[mid].cycle_time = int(m.group(2))
        # Send type
        pat_st = re.compile(r'BA_\s+"GenMsgSendType"\s+BO_\s+(\d+)\s+(\d+)\s*;')
        for m in pat_st.finditer(content):
            mid = int(m.group(1))
            if mid in self.messages:
                self.messages[mid].send_type = (
                    "Cycle" if int(m.group(2)) == 0 else "Event"
                )


# ===========================================================================
#  C code helpers
# ===========================================================================

def c_type_for_signal(sig: Signal) -> str:
    """Return the best C type for a signal's struct member."""
    if sig.value_type == 1:
        return "float"
    if sig.value_type == 2:
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
    """Return the unsigned integer type that can hold *bit_width* bits."""
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


def upper_name(name: str) -> str:
    """Convert CamelCase / mixed to UPPER_CASE, collapsing double underscores."""
    s1 = re.sub("(.)([A-Z][a-z]+)", r"\1_\2", name)
    result = re.sub("([a-z0-9])([A-Z])", r"\1_\2", s1).upper()
    # Collapse consecutive underscores
    result = re.sub(r"_+", "_", result)
    return result


# ---------------------------------------------------------------------------
#  Motorola / Intel byte-layout helpers
# ---------------------------------------------------------------------------

def _motorola_byte_segments(start_bit: int, length: int):
    """
    Yield (byte_idx, low_bit_in_byte, bits_count, raw_shift) tuples
    from MSB to LSB for a Motorola-order signal.

    raw_shift: how many bits to right-shift the raw value to get the
    portion for this byte.
    """
    remaining = length
    raw_shift = length          # decremented before yield
    byte_idx = start_bit // 8
    bit_pos = start_bit % 8     # MSB position within byte

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
    Yield (byte_idx, low_bit_in_byte, bits_count, raw_shift) tuples
    from LSB to MSB for an Intel-order signal.
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


def byte_segments(sig: Signal):
    if sig.byte_order == "big_endian":
        return list(_motorola_byte_segments(sig.start_bit, sig.length))
    else:
        return list(_intel_byte_segments(sig.start_bit, sig.length))


# ===========================================================================
#  C code generator
# ===========================================================================

class CCodeGenerator:
    """Generates .h / .c from parsed DBC data."""

    def __init__(self, parser: DbcParser, node: Optional[str] = None):
        self.parser = parser
        self.node = node        # target ECU node (e.g. SDV_M_CORE0)

    # ----- direction helpers -----------------------------------------------
    def is_tx(self, msg: Message) -> bool:
        """Message is TX when this node is the sender."""
        if self.node is None:
            return True
        return msg.sender == self.node

    def is_rx(self, msg: Message) -> bool:
        """Message is RX when this node appears in any signal's receivers."""
        if self.node is None:
            return True
        for sig in msg.signals:
            if self.node in sig.receivers:
                return True
        return False

    def direction_tag(self, msg: Message) -> str:
        parts = []
        if self.is_tx(msg):
            parts.append("TX")
        if self.is_rx(msg):
            parts.append("RX")
        return "/".join(parts) if parts else "N/A"

    # ----- header generation -----------------------------------------------
    def generate_header(self, basename: str) -> str:
        guard = basename.upper().replace(".", "_").replace("-", "_") + "_H"
        lines = []
        lines.append(f"/**")
        lines.append(f" * @file {basename}.h")
        lines.append(f" * @brief Auto-generated CAN message pack/unpack interface")
        lines.append(f" *")
        lines.append(f" * Generated from DBC by dbc_to_c_generator.py")
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

        for msg in sorted(self.parser.messages.values(), key=lambda m: m.msg_id):
            if not (self.is_tx(msg) or self.is_rx(msg)):
                continue
            uname = upper_name(msg.name)
            lines.append(f"/* " + "=" * 70 + " */")
            lines.append(
                f"/* Message: {msg.name}  (0x{msg.msg_id:03X}, {msg.dlc} bytes, "
                f"{self.direction_tag(msg)}, sender={msg.sender}) */"
            )
            lines.append(f"/* " + "=" * 70 + " */")
            lines.append(f"")
            lines.append(f"#define {uname}_ID          (0x{msg.msg_id:03X}U)")
            lines.append(f"#define {uname}_DLC         ({msg.dlc}U)")
            if msg.cycle_time > 0:
                lines.append(f"#define {uname}_CYCLE_MS    ({msg.cycle_time}U)")
            lines.append(f"")

            # Value-description enums
            for sig in msg.signals:
                if sig.value_descriptions:
                    for val, desc in sorted(sig.value_descriptions.items()):
                        enum_name = f"{uname}_{upper_name(sig.name)}_{upper_name(desc.replace(' ', '_'))}"
                        lines.append(f"#define {enum_name}  ({val}U)")
                    lines.append(f"")

            # Struct
            lines.append(f"typedef struct {{")
            for sig in msg.signals:
                ctype = c_type_for_signal(sig)
                comment_parts = []
                if sig.factor != 1.0 or sig.offset != 0.0:
                    comment_parts.append(f"factor={sig.factor}, offset={sig.offset}")
                if sig.unit:
                    comment_parts.append(f'unit="{sig.unit}"')
                comment_parts.append(f"[{sig.minimum}, {sig.maximum}]")
                if sig.comment:
                    comment_parts.append(sig.comment)
                comment = ", ".join(comment_parts)
                lines.append(f"    {ctype:10s} {sig.name};  /* {comment} */")
            lines.append(f"}} {msg.name}_t;")
            lines.append(f"")

            # Function prototypes
            lines.append(
                f"int {msg.name}_pack(uint8_t *data, const {msg.name}_t *msg, uint8_t dlc);"
            )
            lines.append(
                f"int {msg.name}_unpack({msg.name}_t *msg, const uint8_t *data, uint8_t dlc);"
            )
            lines.append(f"")

        lines.append(f"#endif /* {guard} */")
        lines.append(f"")
        return "\n".join(lines)

    # ----- source generation -----------------------------------------------
    def generate_source(self, basename: str) -> str:
        lines = []
        lines.append(f"/**")
        lines.append(f" * @file {basename}.c")
        lines.append(f" * @brief Auto-generated CAN message pack/unpack implementation")
        lines.append(f" *")
        lines.append(f" * Generated from DBC by dbc_to_c_generator.py")
        lines.append(f" * Date: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        lines.append(f" *")
        lines.append(f" * DO NOT EDIT — re-run the generator to update.")
        lines.append(f" */")
        lines.append(f"")
        lines.append(f'#include "{basename}.h"')
        lines.append(f"")

        for msg in sorted(self.parser.messages.values(), key=lambda m: m.msg_id):
            if not (self.is_tx(msg) or self.is_rx(msg)):
                continue
            lines.extend(self._gen_pack(msg))
            lines.append(f"")
            lines.extend(self._gen_unpack(msg))
            lines.append(f"")

        return "\n".join(lines)

    # ----- pack ------------------------------------------------------------
    def _gen_pack(self, msg: Message) -> List[str]:
        L = []
        L.append(f"int {msg.name}_pack(uint8_t *data, const {msg.name}_t *msg, uint8_t dlc)")
        L.append(f"{{")
        L.append(f"    if ((data == (void *)0) || (msg == (void *)0)) {{ return -1; }}")
        L.append(f"    if (dlc < {msg.dlc}U) {{ return -1; }}")
        L.append(f"")
        L.append(f"    (void)memset(data, 0, (size_t)dlc);")
        L.append(f"")

        for sig in msg.signals:
            L.append(f"    /* {sig.name}: start_bit={sig.start_bit}, len={sig.length}, "
                     f"{'Motorola' if sig.byte_order == 'big_endian' else 'Intel'}"
                     f"{', float' if sig.value_type == 1 else ', double' if sig.value_type == 2 else ''} */")

            segs = byte_segments(sig)

            if sig.value_type == 1:
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

            elif sig.value_type == 2:
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
                if sig.factor != 1.0 or sig.offset != 0.0:
                    L.append(f"        {rtype} raw_val = ({rtype})"
                             f"((msg->{sig.name} - ({sig.offset})) / ({sig.factor}));")
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

    # ----- unpack ----------------------------------------------------------
    def _gen_unpack(self, msg: Message) -> List[str]:
        L = []
        L.append(f"int {msg.name}_unpack({msg.name}_t *msg, const uint8_t *data, uint8_t dlc)")
        L.append(f"{{")
        L.append(f"    if ((data == (void *)0) || (msg == (void *)0)) {{ return -1; }}")
        L.append(f"    if (dlc < {msg.dlc}U) {{ return -1; }}")
        L.append(f"")

        for sig in msg.signals:
            L.append(f"    /* {sig.name} */")
            segs = byte_segments(sig)

            if sig.value_type == 1:
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

            elif sig.value_type == 2:
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
                if sig.is_signed and sig.length < {8: 8, 16: 16, 32: 32, 64: 64}.get(sig.length, 64):
                    type_bits = 8
                    if sig.length > 8:
                        type_bits = 16
                    if sig.length > 16:
                        type_bits = 32
                    if sig.length > 32:
                        type_bits = 64
                    if sig.length < type_bits:
                        L.append(f"        /* sign extension */")
                        L.append(
                            f"        if ((raw_val & (1{suf} << {sig.length - 1}u)) != 0u) {{"
                        )
                        fill_mask = ((1 << type_bits) - 1) ^ ((1 << sig.length) - 1)
                        L.append(f"            raw_val |= 0x{fill_mask:X}{suf};")
                        L.append(f"        }}")

                # Apply scaling
                if sig.factor != 1.0 or sig.offset != 0.0:
                    if sig.is_signed:
                        L.append(
                            f"        msg->{sig.name} = ({ctype})"
                            f"(({ctype})({raw_uint_type(sig.length).replace('u','')}_t)raw_val"
                            f" * {sig.factor} + {sig.offset});"
                        )
                    else:
                        L.append(
                            f"        msg->{sig.name} = ({ctype})"
                            f"(raw_val * {sig.factor} + {sig.offset});"
                        )
                else:
                    L.append(f"        msg->{sig.name} = ({ctype})raw_val;")

                L.append(f"    }}")

        L.append(f"")
        L.append(f"    return 0;")
        L.append(f"}}")
        return L


# ===========================================================================
#  Main
# ===========================================================================

def main():
    ap = argparse.ArgumentParser(
        description="Generate C pack/unpack code from a Vector DBC file."
    )
    ap.add_argument("dbc", help="Path to the input .dbc file")
    ap.add_argument("outdir", help="Output directory for generated .h / .c")
    ap.add_argument(
        "--node",
        default=None,
        help="Target ECU node name. Only messages TX/RX by this node are generated.",
    )
    args = ap.parse_args()

    dbc_path = Path(args.dbc).resolve()
    out_dir = Path(args.outdir).resolve()

    if not dbc_path.is_file():
        print(f"ERROR: DBC file not found: {dbc_path}", file=sys.stderr)
        sys.exit(1)

    out_dir.mkdir(parents=True, exist_ok=True)

    # Parse
    parser = DbcParser()
    parser.parse(str(dbc_path))
    print(f"Parsed {len(parser.messages)} messages, nodes: {parser.nodes}")

    # Generate
    basename = dbc_path.stem  # e.g. "CANdbc_file"
    gen = CCodeGenerator(parser, node=args.node)

    h_content = gen.generate_header(basename)
    c_content = gen.generate_source(basename)

    h_path = out_dir / f"{basename}.h"
    c_path = out_dir / f"{basename}.c"

    h_path.write_text(h_content, encoding="utf-8")
    c_path.write_text(c_content, encoding="utf-8")

    print(f"Generated: {h_path}")
    print(f"Generated: {c_path}")

    # Summary
    for msg in sorted(parser.messages.values(), key=lambda m: m.msg_id):
        tag = gen.direction_tag(msg)
        print(
            f"  0x{msg.msg_id:03X}  {msg.name:30s}  DLC={msg.dlc}  "
            f"[{tag}]  signals={len(msg.signals)}"
        )


if __name__ == "__main__":
    main()
