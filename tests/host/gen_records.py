# SPDX-License-Identifier: Apache-2.0
"""Reference writer for the console's preset headers (PRESET_HV_RECORD in
Core/Inc/hv_presets.h).

The openlifu SDK owns the real generators (LIFUTXPresets.console_preset_record,
generate_console_fda_preset, generate_console_presets). This mirrors them so
the host tests, and a bench build before the SDK catches up, can write the
same bytes:

* one preset  -> presets/fda_preset.h, one v1 record (byte-identical to the
  SDK's console_preset_record), linked by an "fda" image;
* N presets   -> presets/preset_hv_table.h, N v2 records back to back, each
  with its preset's voltage_range, linked by a "test" image.

Usage, from a diathermy preset context folder (presets.json and
<id>/<id>_settings.json):

    python tests/host/gen_records.py CONTEXT_DIR Core/Inc/presets
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import zlib
from pathlib import Path

MAGIC = b"OWHV"


def f32(v: float) -> float:
    return struct.unpack("<f", struct.pack("<f", v))[0]


def record(pid: str, settings_crc: int, voltage: float, sensitivity: float,
           voltage_range: tuple[float, float] | None = None, version: int | None = None) -> bytes:
    """One PRESET_HV_RECORD: v1 without a range, v2 with one."""
    if version is None:
        version = 1 if voltage_range is None else 2
    head = MAGIC + bytes([version, len(pid), 0, 0]) + struct.pack("<Iff", settings_crc, voltage, sensitivity)
    if version == 2:
        head += struct.pack("<ff", *voltage_range)
    head += pid.encode("ascii")
    return head + struct.pack("<I", zlib.crc32(head) & 0xFFFFFFFF)


def _bytes(data: bytes) -> list[str]:
    return ["\t" + " ".join("0x%02x," % b for b in data[i:i + 16]) for i in range(0, len(data), 16)]


def fda_preset_header(rec: bytes, pid: str) -> str:
    lines = [
        "// Generated from preset '%s'. Laid out as PRESET_HV_RECORD in hv_presets.h." % pid,
        "#ifndef FDA_PRESET_HV_H_",
        "#define FDA_PRESET_HV_H_",
        "",
        "#include <stdint.h>",
        "",
        '__attribute__((section(".fda_preset"), used))',
        "static const uint8_t FDA_PRESET_HV[%u] = {" % len(rec),
        *_bytes(rec),
        "};",
        "",
        "#endif  // FDA_PRESET_HV_H_",
        "",
    ]
    return "\n".join(lines)


def preset_hv_table_header(records: list[tuple[str, bytes]], context: str) -> str:
    guard = "PRESET_HV_TABLE_%s_H_" % context.upper()
    blob = b"".join(r for _, r in records)
    lines = [
        "// Generated for preset set '%s'. Laid out as PRESET_HV_RECORD v2 records" % context,
        "// in hv_presets.h, back to back, in the transmitter's preset order.",
        "#ifndef %s" % guard,
        "#define %s" % guard,
        "",
        "#include <stdint.h>",
        "",
        '#define PRESET_HV_CONTEXT_NAME "%s"' % context,
        "#define PRESET_HV_COUNT %uu" % len(records),
        "",
    ]
    lines += ["// %u: %s" % (i, pid) for i, (pid, _) in enumerate(records)]
    lines += [
        '__attribute__((section(".fda_preset"), used))',
        "static const uint8_t PRESET_HV_RECORDS[%u] = {" % len(blob),
        *_bytes(blob),
        "};",
        "",
        "#endif  // %s" % guard,
        "",
    ]
    return "\n".join(lines)


def from_context(context_dir: Path) -> list[tuple[str, bytes, dict]]:
    """(id, exact settings bytes, parsed settings) for each preset, in order."""
    manifest = json.loads((context_dir / "presets.json").read_text(encoding="utf-8"))
    out = []
    for pid in manifest["presets"]:
        raw = (context_dir / pid / ("%s_settings.json" % pid)).read_bytes()
        out.append((pid, raw, json.loads(raw)))
    return out


def write_headers(context_dir: Path, out_dir: Path) -> Path:
    presets = from_context(context_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for stale in ("fda_preset.h", "preset_hv_table.h"):
        (out_dir / stale).unlink(missing_ok=True)
    if len(presets) == 1:
        pid, raw, cfg = presets[0]
        rec = record(pid, zlib.crc32(raw) & 0xFFFFFFFF, f32(cfg["voltage"]), f32(cfg["sensitivity"]))
        path = out_dir / "fda_preset.h"
        path.write_text(fda_preset_header(rec, pid), encoding="utf-8", newline="\n")
        return path
    records = []
    for pid, raw, cfg in presets:
        lo, hi = cfg["voltage_range"]
        records.append((pid, record(pid, zlib.crc32(raw) & 0xFFFFFFFF, f32(cfg["voltage"]),
                                    f32(cfg["sensitivity"]), (f32(lo), f32(hi)))))
    path = out_dir / "preset_hv_table.h"
    path.write_text(preset_hv_table_header(records, context_dir.name), encoding="utf-8", newline="\n")
    return path


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("context_dir", type=Path)
    ap.add_argument("out_dir", type=Path)
    args = ap.parse_args()
    print(write_headers(args.context_dir, args.out_dir))
    sys.exit(0)
