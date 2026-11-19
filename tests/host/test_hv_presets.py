# SPDX-License-Identifier: Apache-2.0
"""Host tests of Core/Src/hv_presets.c: presets read from the preset page
(page 61), one preset and four, in the FDA (clinical) build and the research
build.

Each test writes a preset package (gen_records.py) into the simulated page,
compiles hv_presets_host.c with the host C compiler (CC, default gcc), with or
without FDA_MODE, and drives it line by line. Run with:

    pytest tests/host -v
"""

from __future__ import annotations

import os
import shutil
import struct
import subprocess
import zlib
from dataclasses import dataclass
from pathlib import Path

import pytest
from gen_records import package, record

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
CC = os.environ.get("CC", "gcc")

OW_ERROR = 0xEF
HV_RANGE, PRESET_CRC, NO_PRESET, FDA_REFUSED, DATA_ERROR = 0xF1, 0xF6, 0xF7, 0xF8, 0xFE
GENERIC_ERROR = 0xFF
# Core/Inc/hv_presets.h: OW_POWER_PRESET_INFO reply reserved byte
INFO_OK, INFO_EMPTY, INFO_NO_PAGE = 0x00, 0x01, 0x02
# Core/Inc/hv_presets.h
CEILING_V = 45.0

needs_cc = pytest.mark.skipif(shutil.which(CC) is None, reason=f"no host C compiler ({CC})")

SENS = 3728.0
FDA = ("diathermy_34ele_40V_50pct_25mm", 0x1111AAAA, 40.0, SENS, (35.0, 45.0))
TEST_SET = [
    FDA,
    ("diathermy_34ele_40V_50pct_25mm_extended", 0x2222BBBB, 40.0, SENS, (35.0, 45.0)),
    ("diathermy_34ele_40V_50pct_25mm_short", 0x3333CCCC, 40.0, SENS, (35.0, 45.0)),
    ("diathermy_34ele_40V_50pct_25mm_lowv", 0x4444DDDD, 20.0, SENS, (15.0, 25.0)),
]


@dataclass
class Reply:
    handled: bool
    packet_type: int
    reserved: int
    data: bytes
    last_set: float

    def refused(self, code: int) -> bool:
        return (self.packet_type, self.reserved) == (OW_ERROR, code)

    @property
    def ok(self) -> bool:
        return self.handled and self.packet_type != OW_ERROR

    @property
    def applied(self) -> float:
        return struct.unpack("<f", self.data)[0]

    @property
    def preset(self) -> dict:
        d = self.data
        n = d[27]
        return {
            "count": d[0], "index": d[1], "selected": d[2],
            "settings_crc": struct.unpack_from("<I", d, 3)[0],
            "voltage": struct.unpack_from("<f", d, 7)[0],
            "sensitivity": struct.unpack_from("<f", d, 11)[0],
            "applied": struct.unpack_from("<f", d, 15)[0],
            "window": struct.unpack_from("<2f", d, 19),
            "id": d[28:28 + n].decode("ascii"),
        }


class Console:
    def __init__(self, exe: Path, page: Path | None = None):
        args = [str(exe)] + ([str(page)] if page is not None else [])
        self.p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)

    def __call__(self, line: str) -> Reply | None:
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()
        if line.startswith(("rail", "nopage")):
            return None
        if line == "page":
            return bytes.fromhex(self.p.stdout.readline().strip())
        handled, ptype, res, data, last = self.p.stdout.readline().split(" ")
        return Reply(handled == "1", int(ptype, 16), int(res, 16), bytes.fromhex(data), float(last))

    def close(self):
        self.p.stdin.close()
        assert self.p.wait(timeout=5) == 0


_EXES: dict[bool, Path] = {}


def _exe(tmp_path_factory, fda_mode: bool) -> Path:
    """hv_presets_host, built once per session for each build."""
    if fda_mode not in _EXES:
        out = tmp_path_factory.mktemp("fda" if fda_mode else "research")
        exe = out / "hv_presets_host"
        cmd = [CC, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
               *(["-DFDA_MODE"] if fda_mode else []),
               f"-I{HERE / 'stubs'}", f"-I{ROOT / 'Core' / 'Inc'}",
               str(HERE / "hv_presets_host.c"), "-o", str(exe)]
        subprocess.run(cmd, check=True)
        _EXES[fda_mode] = exe
    return _EXES[fda_mode]


def build(tmp_path: Path, pkg: bytes | None, fda_mode: bool = True) -> Console:
    """A console whose preset page holds pkg (None: an erased page)."""
    exe = _exe(_FACTORY, fda_mode)
    if pkg is None:
        return Console(exe)
    path = tmp_path / "page.bin"
    path.write_bytes(pkg)
    return Console(exe, path)


def fda_console(tmp_path, preset=FDA, rec: bytes | None = None) -> Console:
    rec = rec if rec is not None else record(*preset)
    return build(tmp_path, package([rec], "fda"))


def table_console(tmp_path, presets=TEST_SET, records: list[bytes] | None = None) -> Console:
    records = records if records is not None else [record(*p) for p in presets]
    return build(tmp_path, package(records, "test"))


@pytest.fixture(autouse=True)
def _factory(tmp_path_factory):
    global _FACTORY
    _FACTORY = tmp_path_factory


@pytest.fixture
def fda(tmp_path):
    c = fda_console(tmp_path)
    yield c
    c.close()


@pytest.fixture
def test_set(tmp_path):
    c = table_console(tmp_path)
    yield c
    c.close()


def sens_for(volts: float, preset=FDA) -> float:
    """The transmitter sensitivity that rescales preset to volts."""
    return preset[2] * preset[3] / volts


def test_records_match_the_sdk():
    sdk = pytest.importorskip("openlifu_sdk.io.LIFUTXPresets")
    for pid, crc, v, s, rng in TEST_SET:
        mc = {"id": pid, "settings_crc": crc, "voltage": v, "sensitivity": s, "voltage_range": list(rng)}
        assert sdk.console_preset_record(mc, version=2) == record(pid, crc, v, s, rng)


# --- N = 1, the released (fda) preset set -----------------------------------

@needs_cc
def test_fda_get_reports_one_preset_and_its_range(fda):
    r = fda("get 0")
    assert r.ok
    p = r.preset
    assert (p["count"], p["index"], p["selected"]) == (1, 0, 0xFF)
    assert (p["id"], p["settings_crc"], p["voltage"], p["sensitivity"]) == FDA[:4]
    assert p["window"] == FDA[4]
    assert fda("get 1").refused(DATA_ERROR)


@needs_cc
def test_fda_select_applies_rescaled_voltage(fda):
    r = fda(f"select 0 {FDA[1]:x} {SENS}")
    assert r.ok and r.applied == pytest.approx(40.0) and r.last_set == r.applied
    assert fda("get 0").preset["selected"] == 0
    assert fda("hvon").handled is False  # passes to normal processing


@needs_cc
@pytest.mark.parametrize("volts", [34.9, 45.1])
def test_fda_select_outside_its_range_is_refused(fda, volts):
    assert fda(f"select 0 {FDA[1]:x} {sens_for(volts)}").refused(HV_RANGE)
    assert fda("get 0").preset["selected"] == 0xFF
    assert fda("hvon").refused(NO_PRESET)


@needs_cc
def test_fda_select_index_1_wrong_crc_and_live_rail(fda):
    assert fda(f"select 1 {FDA[1]:x} {SENS}").refused(DATA_ERROR)
    assert fda(f"select 0 {FDA[1] ^ 1:x} {SENS}").refused(PRESET_CRC)
    assert fda(f"select 0 {FDA[1]:x} {SENS}").ok
    fda("rail 1")
    assert fda(f"select 0 {FDA[1]:x} {SENS}").refused(FDA_REFUSED)
    assert fda("get 0").preset["selected"] == 0  # unchanged under a live rail
    assert fda("sethv").refused(FDA_REFUSED)


@needs_cc
def test_fda_range_comes_from_the_record(tmp_path):
    """No floor is compiled in: a 30 V FDA preset runs at 30 V."""
    pid, crc, _, s, _ = FDA
    c = fda_console(tmp_path, (pid, crc, 30.0, s, (25.0, 35.0)))
    assert c("get 0").preset["window"] == (25.0, 35.0)
    assert c(f"select 0 {crc:x} {s}").applied == pytest.approx(30.0)
    assert c(f"select 0 {crc:x} {30.0 * s / 36.0}").refused(HV_RANGE)
    c.close()


@needs_cc
def test_fda_v1_record_is_not_loaded(tmp_path):
    c = fda_console(tmp_path, rec=record(*FDA, version=1))
    assert c("get 0").refused(DATA_ERROR)
    assert c(f"select 0 {FDA[1]:x} {SENS}").refused(DATA_ERROR)
    c.close()


# --- N = 4, the test preset set ---------------------------------------------

@needs_cc
def test_table_get_reports_each_preset_and_its_range(test_set):
    for i, (pid, crc, v, s, rng) in enumerate(TEST_SET):
        p = test_set(f"get {i}").preset
        assert (p["count"], p["index"], p["selected"]) == (4, i, 0xFF)
        assert (p["id"], p["settings_crc"], p["voltage"], p["sensitivity"]) == (pid, crc, v, s)
        assert p["window"] == rng
    assert test_set("get 4").refused(DATA_ERROR)


@needs_cc
@pytest.mark.parametrize("index", range(4))
def test_table_select_each_preset(test_set, index):
    pid, crc, v, s, _ = TEST_SET[index]
    r = test_set(f"select {index} {crc:x} {s}")
    assert r.ok and r.applied == pytest.approx(v) and r.last_set == r.applied
    assert test_set(f"get {index}").preset["selected"] == index


@needs_cc
def test_table_select_needs_that_presets_crc(test_set):
    assert test_set(f"select 3 {TEST_SET[0][1]:x} {SENS}").refused(PRESET_CRC)
    assert test_set(f"select 4 {TEST_SET[0][1]:x} {SENS}").refused(DATA_ERROR)
    assert test_set("hvon").refused(NO_PRESET)


@needs_cc
@pytest.mark.parametrize("index, volts", [(3, 14.9), (3, 25.1), (0, 34.9), (0, 45.1), (3, 40.0)])
def test_table_select_held_to_the_records_own_range(test_set, index, volts):
    pid, crc, v, s, _ = TEST_SET[index]
    assert test_set(f"select {index} {crc:x} {v * s / volts}").refused(HV_RANGE)
    assert test_set(f"get {index}").preset["selected"] == 0xFF


@needs_cc
def test_table_lowv_rescales_inside_its_range(test_set):
    lowv = TEST_SET[3]
    r = test_set(f"select 3 {lowv[1]:x} {sens_for(24.0, lowv)}")
    assert r.applied == pytest.approx(24.0, rel=1e-5)


@needs_cc
def test_table_reselect_and_clear(test_set):
    assert test_set(f"select 1 {TEST_SET[1][1]:x} {SENS}").ok
    test_set("rail 1")
    assert test_set(f"select 3 {TEST_SET[3][1]:x} {SENS}").refused(FDA_REFUSED)
    assert test_set("get 0").preset["selected"] == 1
    test_set("rail 0")
    assert test_set(f"select 3 {TEST_SET[3][1]:x} {SENS}").ok
    assert test_set("clear").ok
    assert test_set("get 0").preset["selected"] == 0xFF


# --- mis-written records: fail closed ---------------------------------------

@needs_cc
@pytest.mark.parametrize("rng", [(0.0, 25.0), (35.0, CEILING_V + 0.5), (40.0, 35.0), (float("nan"), 25.0)],
                         ids=["zero-floor", "above-ceiling", "inverted", "nan"])
def test_table_record_with_a_bad_range_hides_the_whole_set(tmp_path, rng):
    """A stored set is all or nothing: one bad record and none are loaded."""
    presets = [*TEST_SET[:3], (*TEST_SET[3][:4], rng)]
    c = table_console(tmp_path, presets)
    for i in range(4):
        assert c(f"get {i}").refused(DATA_ERROR)
    assert c(f"select 0 {TEST_SET[0][1]:x} {SENS}").refused(DATA_ERROR)
    assert c("info").reserved == INFO_EMPTY
    c.close()


@needs_cc
def test_table_range_up_to_the_ceiling_loads(tmp_path):
    presets = [*TEST_SET[:3], (*TEST_SET[3][:4], (1.0, CEILING_V))]
    c = table_console(tmp_path, presets)
    assert c("get 3").preset["window"] == (1.0, CEILING_V)
    c.close()


@needs_cc
def test_table_corrupt_record_hides_the_whole_set(tmp_path):
    records = [record(*p) for p in TEST_SET]
    bad = bytearray(records[1])
    bad[12] ^= 0x01  # voltage, under the CRC
    records[1] = bytes(bad)
    c = table_console(tmp_path, records=records)
    for i in range(4):
        assert c(f"get {i}").refused(DATA_ERROR)
    c.close()


# --- the preset page: INFO, and writes (research builds) ---------------------

def _write(c, pkg: bytes, chunk: int = 64):
    """The SDK's order: body chunks first, the header chunk last."""
    replies = []
    offsets = list(range(320, len(pkg), chunk)) + list(range(0, 320, chunk))
    for off in offsets:
        end = min(off + chunk, len(pkg) if off >= 320 else 320)
        replies.append(c(f"write {off} {pkg[off:end].hex()}"))
    return replies


@needs_cc
def test_info_reports_the_package_header(tmp_path):
    pkg = package([record(*p) for p in TEST_SET], "test", diathermy_sha="12" * 20)
    c = build(tmp_path, pkg)
    r = c("info")
    assert r.ok and r.reserved == INFO_OK
    assert r.data == pkg[:320]                      # the host parses it (LIFUPresetPackage)
    assert r.data[100:120] == bytes.fromhex("12" * 20)   # Reserved: the diathermy SHA
    c.close()


@needs_cc
def test_info_empty_page_and_no_page(tmp_path):
    c = build(tmp_path, None)
    assert c("info").reserved == INFO_EMPTY
    assert c("get 0").refused(DATA_ERROR)
    c("nopage")                                     # a bootloader before 1.1.0-rc.2
    assert c("info").reserved == INFO_NO_PAGE
    c.close()


@needs_cc
def test_body_size_must_match_the_records(tmp_path):
    records = [record(*p) for p in TEST_SET]
    for size in (sum(map(len, records)) - 1, sum(map(len, records)) + 4):
        c = build(tmp_path, package(records, body_size=size))
        assert c("get 0").refused(DATA_ERROR)
        assert c("info").reserved == INFO_EMPTY
        c.close()


@needs_cc
def test_research_build_writes_a_package(tmp_path):
    c = build(tmp_path, package([record(*FDA)], "fda"), fda_mode=False)
    assert c(f"select 0 {FDA[1]:x} {SENS}").ok
    pkg = package([record(*p) for p in TEST_SET], "test")
    assert all(r.ok for r in _write(c, pkg))
    assert c(f"commit {len(pkg)} {zlib.crc32(pkg):x}").ok
    assert c("info").data == pkg[:320]
    assert c("get 3").preset["id"] == TEST_SET[3][0]
    assert c("get 0").preset["selected"] == 0xFF     # writing deselected
    assert c("page")[:len(pkg)] == pkg
    c.close()


@needs_cc
def test_research_build_leaves_hv_to_the_host(tmp_path):
    c = build(tmp_path, None, fda_mode=False)
    assert c("sethv").handled is False              # normal processing sets it
    assert c("hvon").handled is False
    c.close()


@needs_cc
@pytest.mark.parametrize("fault", ["crc", "length", "short-body"])
def test_a_bad_commit_erases_the_page(tmp_path, fault):
    c = build(tmp_path, None, fda_mode=False)
    pkg = package([record(*p) for p in TEST_SET], "test")
    if fault == "short-body":
        pkg = pkg[:-4]                              # the records do not fill the body
    assert all(r.ok for r in _write(c, pkg))
    crc = zlib.crc32(pkg) ^ (1 if fault == "crc" else 0)
    n = len(pkg) + (4 if fault == "length" else 0)
    assert c(f"commit {n} {crc:x}").refused(DATA_ERROR)
    assert c("page") == b"\xFF" * 2048
    assert c("info").reserved == INFO_EMPTY
    c.close()


@needs_cc
def test_commit_without_a_write_is_refused(tmp_path):
    pkg = package([record(*FDA)], "fda")
    c = build(tmp_path, pkg, fda_mode=False)
    assert c(f"commit {len(pkg)} {zlib.crc32(pkg):x}").refused(DATA_ERROR)
    assert c("get 0").ok                            # the stored set is untouched
    c.close()


@needs_cc
def test_write_refused_under_a_live_rail_or_without_a_page(tmp_path):
    pkg = package([record(*FDA)], "fda")
    c = build(tmp_path, pkg, fda_mode=False)
    c("rail 1")
    assert c(f"write 0 {pkg[:64].hex()}").refused(GENERIC_ERROR)
    c("rail 0")
    c("nopage")
    assert c(f"write 0 {pkg[:64].hex()}").refused(DATA_ERROR)
    c.close()


@needs_cc
def test_write_over_written_bytes_is_refused(tmp_path):
    c = build(tmp_path, None, fda_mode=False)
    assert c("write 320 00112233").ok
    assert c("write 320 44556677").refused(DATA_ERROR)
    assert c("write 322 0011").refused(DATA_ERROR)  # unaligned
    c.close()


@needs_cc
def test_fda_build_refuses_writes(tmp_path):
    pkg = package([record(*FDA)], "fda")
    c = build(tmp_path, pkg)
    other = package([record(*p) for p in TEST_SET], "test")
    assert c(f"write 0 {other[:64].hex()}").refused(FDA_REFUSED)
    assert c(f"commit {len(other)} {zlib.crc32(other):x}").refused(FDA_REFUSED)
    assert c("page")[:len(pkg)] == pkg              # unchanged
    assert c("get 0").ok
    c.close()
