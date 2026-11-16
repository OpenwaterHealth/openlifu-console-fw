# SPDX-License-Identifier: Apache-2.0
"""The console's preset headers for the host tests (PRESET_HV_RECORD in
Core/Inc/hv_presets.h).

The openlifu SDK writes the real ones (LIFUTXPresets.console_preset_record,
generate_console_fda_preset, generate_console_presets). This writes the same
bytes from plain values, so the tests can also write records the SDK never
would; test_hv_presets.py checks the two agree when the SDK is installed.

* one preset  -> fda_preset.h, FDA_PRESET_HV[], linked by an "fda" image;
* N presets   -> preset_hv_table.h, PRESET_HV_TABLE[], the records back to
  back, linked by a "test" image.
"""

from __future__ import annotations

import struct
import zlib

MAGIC = b"OWHV"


def record(pid: str, settings_crc: int, voltage: float, sensitivity: float,
           voltage_range: tuple[float, float], version: int = 2) -> bytes:
    """One PRESET_HV_RECORD. version=1 writes the retired layout, with no range."""
    head = MAGIC + bytes([version, len(pid), 0, 0]) + struct.pack("<Iff", settings_crc, voltage, sensitivity)
    if version == 2:
        head += struct.pack("<ff", *voltage_range)
    head += pid.encode("ascii")
    return head + struct.pack("<I", zlib.crc32(head) & 0xFFFFFFFF)


def _bytes(data: bytes) -> list[str]:
    return ["\t" + " ".join("0x%02x," % b for b in data[i:i + 16]) for i in range(0, len(data), 16)]


def fda_preset_header(rec: bytes) -> str:
    return "\n".join([
        "#include <stdint.h>",
        '__attribute__((section(".fda_preset"), used))',
        "static const uint8_t FDA_PRESET_HV[%u] = {" % len(rec),
        *_bytes(rec),
        "};",
        "",
    ])


def preset_hv_table_header(records: list[bytes], context: str) -> str:
    blob = b"".join(records)
    return "\n".join([
        "#include <stdint.h>",
        '#define PRESET_HV_CONTEXT_NAME "%s"' % context,
        "#define PRESET_HV_COUNT %uu" % len(records),
        '__attribute__((section(".fda_preset"), used))',
        "static const uint8_t PRESET_HV_TABLE[%u] = {" % len(blob),
        *_bytes(blob),
        "};",
        "",
    ])
