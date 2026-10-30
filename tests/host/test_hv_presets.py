# SPDX-License-Identifier: Apache-2.0
"""Host tests of Core/Src/hv_presets.c, one preset (+fda) and four (+test).

Each test writes the preset header gen_records.py would, compiles
hv_presets_host.c against it with the host C compiler (CC, default gcc), and
drives it line by line. Run with:

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
from gen_records import fda_preset_header, preset_hv_table_header, record

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
CC = os.environ.get("CC", "gcc")

OW_ERROR = 0xEF
OW_RESP = 0xE3
HV_RANGE, PRESET_CRC, NO_PRESET, FDA_REFUSED, DATA_ERROR = 0xF1, 0xF6, 0xF7, 0xF8, 0xFE

pytestmark = pytest.mark.skipif(shutil.which(CC) is None, reason=f"no host C compiler ({CC})")

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
    def __init__(self, exe: Path):
        self.p = subprocess.Popen([str(exe)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)

    def __call__(self, line: str) -> Reply | None:
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()
        if line.startswith("rail"):
            return None
        handled, ptype, res, data, last = self.p.stdout.readline().split(" ")
        return Reply(handled == "1", int(ptype, 16), int(res, 16), bytes.fromhex(data), float(last))

    def close(self):
        self.p.stdin.close()
        assert self.p.wait(timeout=5) == 0


def build(tmp_path: Path, header_name: str, header: str, table: bool) -> Console:
    inc = tmp_path / "inc" / "presets"
    inc.mkdir(parents=True)
    (inc / header_name).write_text(header, encoding="utf-8")
    exe = tmp_path / "hv_presets_host"
    cmd = [CC, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-DFDA_MODE",
           *(["-DPRESET_HV_TABLE"] if table else []),
           f"-I{HERE / 'stubs'}", f"-I{tmp_path / 'inc'}", f"-I{ROOT / 'Core' / 'Inc'}",
           str(HERE / "hv_presets_host.c"), "-o", str(exe)]
    subprocess.run(cmd, check=True)
    return Console(exe)


def fda_console(tmp_path, rec: bytes | None = None) -> Console:
    pid, crc, v, s, _ = FDA
    rec = rec if rec is not None else record(pid, crc, v, s)
    return build(tmp_path, "fda_preset.h", fda_preset_header(rec, pid), table=False)


def table_console(tmp_path, presets=TEST_SET, records: list[bytes] | None = None) -> Console:
    if records is None:
        records = [record(pid, crc, v, s, rng) for pid, crc, v, s, rng in presets]
    named = [(p[0], r) for p, r in zip(presets, records)]
    return build(tmp_path, "preset_hv_table.h", preset_hv_table_header(named, "test"), table=True)


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


# --- N = 1, the +fda image: as before this change ---------------------------

def test_fda_record_matches_sdk_v1_layout():
    pid, crc, v, s, _ = FDA
    head = b"OWHV" + bytes([1, len(pid), 0, 0]) + struct.pack("<Iff", crc, v, s) + pid.encode()
    assert record(pid, crc, v, s) == head + struct.pack("<I", zlib.crc32(head))


def test_fda_get_reports_one_preset_and_fda_window(fda):
    r = fda("get 0")
    assert r.handled and r.packet_type != OW_ERROR
    p = r.preset
    assert (p["count"], p["index"], p["selected"]) == (1, 0, 0xFF)
    assert (p["id"], p["settings_crc"], p["voltage"], p["sensitivity"]) == FDA[:4]
    assert p["window"] == (35.0, 45.0)
    assert fda("get 1").refused(DATA_ERROR)


def test_fda_select_applies_rescaled_voltage(fda):
    r = fda(f"select 0 {FDA[1]:x} {SENS}")
    assert r.packet_type != OW_ERROR and r.applied == pytest.approx(40.0) and r.last_set == r.applied
    assert fda("get 0").preset["selected"] == 0
    assert fda("hvon").handled is False  # passes to normal processing


@pytest.mark.parametrize("volts", [34.9, 45.1])
def test_fda_select_outside_35_45_is_refused(fda, volts):
    assert fda(f"select 0 {FDA[1]:x} {sens_for(volts)}").refused(HV_RANGE)
    assert fda("get 0").preset["selected"] == 0xFF
    assert fda("hvon").refused(NO_PRESET)


def test_fda_select_index_1_wrong_crc_and_live_rail(fda):
    assert fda(f"select 1 {FDA[1]:x} {SENS}").refused(DATA_ERROR)
    assert fda(f"select 0 {FDA[1] ^ 1:x} {SENS}").refused(PRESET_CRC)
    assert fda(f"select 0 {FDA[1]:x} {SENS}").packet_type != OW_ERROR
    fda("rail 1")
    assert fda(f"select 0 {FDA[1]:x} {SENS}").refused(FDA_REFUSED)
    assert fda("get 0").preset["selected"] == 0  # unchanged under a live rail
    assert fda("sethv").refused(FDA_REFUSED)


def test_fda_image_refuses_a_low_v2_record(tmp_path):
    """An fda image keeps the 35 V floor even for a record that asks for less."""
    pid, crc, _, s, _ = FDA
    c = fda_console(tmp_path, record(pid, crc, 20.0, s, (15.0, 25.0)))
    assert c("get 0").refused(DATA_ERROR)
    assert c(f"select 0 {crc:x} {s}").refused(DATA_ERROR)
    c.close()


# --- N = 4, the +test image -------------------------------------------------

def test_table_get_reports_each_preset_and_its_range(test_set):
    for i, (pid, crc, v, s, rng) in enumerate(TEST_SET):
        p = test_set(f"get {i}").preset
        assert (p["count"], p["index"], p["selected"]) == (4, i, 0xFF)
        assert (p["id"], p["settings_crc"], p["voltage"], p["sensitivity"]) == (pid, crc, v, s)
        assert p["window"] == rng
    assert test_set("get 4").refused(DATA_ERROR)


@pytest.mark.parametrize("index", range(4))
def test_table_select_each_preset(test_set, index):
    pid, crc, v, s, _ = TEST_SET[index]
    r = test_set(f"select {index} {crc:x} {s}")
    assert r.packet_type != OW_ERROR and r.applied == pytest.approx(v) and r.last_set == r.applied
    assert test_set(f"get {index}").preset["selected"] == index


def test_table_select_needs_that_presets_crc(test_set):
    assert test_set(f"select 3 {TEST_SET[0][1]:x} {SENS}").refused(PRESET_CRC)
    assert test_set(f"select 4 {TEST_SET[0][1]:x} {SENS}").refused(DATA_ERROR)
    assert test_set("hvon").refused(NO_PRESET)


@pytest.mark.parametrize("index, volts", [(3, 14.9), (3, 25.1), (0, 34.9), (0, 45.1), (3, 40.0)])
def test_table_select_held_to_the_records_own_range(test_set, index, volts):
    pid, crc, v, s, _ = TEST_SET[index]
    assert test_set(f"select {index} {crc:x} {v * s / volts}").refused(HV_RANGE)
    assert test_set(f"get {index}").preset["selected"] == 0xFF


def test_table_lowv_rescales_inside_its_range(test_set):
    lowv = TEST_SET[3]
    r = test_set(f"select 3 {lowv[1]:x} {sens_for(24.0, lowv)}")
    assert r.applied == pytest.approx(24.0, rel=1e-5)


def test_table_reselect_and_clear(test_set):
    assert test_set(f"select 1 {TEST_SET[1][1]:x} {SENS}").packet_type != OW_ERROR
    test_set("rail 1")
    assert test_set(f"select 3 {TEST_SET[3][1]:x} {SENS}").refused(FDA_REFUSED)
    assert test_set("get 0").preset["selected"] == 1
    test_set("rail 0")
    assert test_set(f"select 3 {TEST_SET[3][1]:x} {SENS}").packet_type != OW_ERROR
    assert test_set("clear").packet_type != OW_ERROR
    assert test_set("get 0").preset["selected"] == 0xFF


# --- mis-written records: fail closed ---------------------------------------

@pytest.mark.parametrize("rng", [(14.0, 25.0), (35.0, 45.5), (40.0, 35.0)],
                         ids=["below-floor", "above-ceiling", "inverted"])
def test_table_record_outside_the_hard_window_is_not_loaded(tmp_path, rng):
    presets = [*TEST_SET[:3], (*TEST_SET[3][:4], rng)]
    c = table_console(tmp_path, presets)
    assert c("get 2").packet_type != OW_ERROR
    assert c("get 3").refused(DATA_ERROR)
    assert c(f"select 3 {TEST_SET[3][1]:x} {SENS}").refused(DATA_ERROR)
    c.close()


def test_table_corrupt_record_hides_it_and_those_after(tmp_path):
    records = [record(pid, crc, v, s, rng) for pid, crc, v, s, rng in TEST_SET]
    bad = bytearray(records[1])
    bad[12] ^= 0x01  # voltage, under the CRC
    records[1] = bytes(bad)
    c = table_console(tmp_path, records=records)
    assert c("get 0").packet_type != OW_ERROR
    for i in (1, 2, 3):
        assert c(f"get {i}").refused(DATA_ERROR)
    c.close()
