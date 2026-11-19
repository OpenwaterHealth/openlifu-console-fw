# SPDX-License-Identifier: Apache-2.0
"""Preset records and packages for the host tests (PRESET_HV_RECORD in
Core/Inc/hv_presets.h, the PRS1 package in Core/Inc/preset_store.h).

The openlifu SDK writes the real ones (LIFUTXPresets.console_preset_record,
LIFUPresetPackage.build_preset_package). This writes the same bytes from
plain values, so the tests can also write records and packages the SDK never
would; test_hv_presets.py checks the records agree when the SDK is installed.
The firmware does not check the package signature (the bootloader does, on a
DFU write), so these packages carry a zero signature.
"""

from __future__ import annotations

import hashlib
import struct
import zlib

MAGIC = b"OWHV"
PKG_MAGIC = b"PRS1"
HEADER_LEN = 320          # the body starts here
PAGE_SIZE = 2048
CONTEXTS = {"fda": 1, "test": 2}


def record(pid: str, settings_crc: int, voltage: float, sensitivity: float,
           voltage_range: tuple[float, float], version: int = 2) -> bytes:
    """One PRESET_HV_RECORD. version=1 writes the retired layout, with no range."""
    head = MAGIC + bytes([version, len(pid), 0, 0]) + struct.pack("<Iff", settings_crc, voltage, sensitivity)
    if version == 2:
        head += struct.pack("<ff", *voltage_range)
    head += pid.encode("ascii")
    return head + struct.pack("<I", zlib.crc32(head) & 0xFFFFFFFF)


def package(records: list[bytes], context: str = "test", diathermy_sha: str = "ab" * 20,
            body_size: int | None = None) -> bytes:
    """A PRS1 package: the SFU1-layout header (zero signature), then the records.
    body_size overrides the header's body size, to write a mismatched one."""
    body = b"".join(records)
    tag = hashlib.sha256(body).digest()
    size = len(body) if body_size is None else body_size
    reserved = bytes.fromhex(diathermy_sha) + bytes([CONTEXTS[context]]) + bytes(7)
    auth = struct.pack("<4sHHIII32s32s16s28s", PKG_MAGIC, 1, 0, size, 0, size, tag, tag,
                       bytes(16), reserved)
    header = auth + bytes(64) + b"\xFF" * 96 + bytes(32)
    assert len(header) == HEADER_LEN
    return header + body
