"""An FDA console's HV setpoint comes only from its preset (SR-003, SR-006).

Bench step 4.08. Every test here may turn the HV rail on, so all of them skip
unless --hv-dummy-load is given (conftest.py): put the console's HV output on a
dummy load first. FDA images only. Each test starts and ends with HV off and
nothing selected.
"""

import struct

import pytest
from conftest import send_raw
from openlifu_sdk.io.exceptions import LIFUDeviceError
from openlifu_sdk.io.LIFUConfig import (
    OW_ERROR,
    OW_HV_FDA_REFUSED,
    OW_HV_NO_PRESET,
    OW_HV_PRESET_CRC,
    OW_HV_RANGE,
    OW_POWER,
    OW_POWER_RAW_DAC,
    OW_POWER_SET_DACS,
    OW_POWER_SET_HV,
)

pytestmark = pytest.mark.hv

PRESET_INDEX = 0
# How close the measured output must settle to the applied setpoint.
SETTLE_RANGE_V = 2.0
# Below this the rail counts as off.
OFF_V = 5.0


@pytest.fixture
def preset(console, fda):
    """The console's preset, with HV off and nothing selected before and after."""
    console.turn_hv_off()
    console.deselect_preset()
    p = console.get_preset(PRESET_INDEX)
    assert p is not None
    yield p
    console.turn_hv_off()
    console.deselect_preset()


def _assert_hv_off(console):
    assert not console.get_hv_status()
    assert console.get_voltage() < OFF_V


def _assert_hv_on_refused(console, code):
    with pytest.raises(LIFUDeviceError) as e:
        console.turn_hv_on()
    assert e.value.device_error_code == code
    _assert_hv_off(console)


@pytest.mark.parametrize(
    "command, addr, data",
    [
        (OW_POWER_SET_HV, 0, struct.pack(">f", 40.0)),
        (OW_POWER_SET_DACS, 0, bytes(8)),
        (OW_POWER_RAW_DAC, 0, bytes(2)),
    ],
    ids=["SET_HV", "SET_DACS", "RAW_DAC"],
)
def test_host_set_voltage_is_refused(console, preset, command, addr, data):
    r = send_raw(console, OW_POWER, command, addr=addr, data=bytearray(data))

    assert r is not None, "no reply"
    assert (r.packet_type, r.reserved) == (OW_ERROR, OW_HV_FDA_REFUSED)
    assert console.get_preset(PRESET_INDEX)["selected"] is None
    _assert_hv_off(console)


def test_hv_on_with_no_preset_is_refused(console, preset):
    _assert_hv_on_refused(console, OW_HV_NO_PRESET)


def test_a_select_with_the_wrong_settings_crc_leaves_hv_off(console, preset):
    with pytest.raises(LIFUDeviceError) as e:
        wrong_crc = preset["settings_crc"] ^ 0xFFFFFFFF
        console.select_preset(PRESET_INDEX, wrong_crc, preset["sensitivity"])
    assert e.value.device_error_code == OW_HV_PRESET_CRC
    assert console.get_preset(PRESET_INDEX)["selected"] is None

    _assert_hv_on_refused(console, OW_HV_NO_PRESET)


def test_a_select_outside_the_window_leaves_hv_off(console, preset):
    # A transmitter this much less sensitive needs more than the window's top.
    too_weak = preset["sensitivity"] * preset["voltage"] / (preset["max_voltage"] * 1.5)
    with pytest.raises(LIFUDeviceError) as e:
        console.select_preset(PRESET_INDEX, preset["settings_crc"], too_weak)
    assert e.value.device_error_code == OW_HV_RANGE
    assert console.get_preset(PRESET_INDEX)["selected"] is None

    _assert_hv_on_refused(console, OW_HV_NO_PRESET)


def _sensitivities(config):
    yield pytest.param(None, id="preset-own")
    if config.getoption("--tx-sensitivity") is not None:
        yield pytest.param(config.getoption("--tx-sensitivity"), id="tx-given")


def pytest_generate_tests(metafunc):
    if "tx_sensitivity" in metafunc.fixturenames:
        metafunc.parametrize("tx_sensitivity", list(_sensitivities(metafunc.config)))


def test_the_applied_voltage_is_the_preset_rescaled(console, preset, tx_sensitivity):
    s_tx = preset["sensitivity"] if tx_sensitivity is None else tx_sensitivity
    expected = preset["voltage"] * preset["sensitivity"] / s_tx
    assert 35.0 <= expected <= 45.0, f"that sensitivity gives {expected:.2f} V, outside 35-45 V"

    applied = console.select_preset(PRESET_INDEX, preset["settings_crc"], s_tx)

    assert applied == pytest.approx(expected, rel=1e-5)
    assert 35.0 <= applied <= 45.0
    state = console.get_preset(PRESET_INDEX)
    assert state["selected"] == PRESET_INDEX
    assert state["applied_voltage"] == pytest.approx(applied, rel=1e-6)

    console.turn_hv_on()
    console.wait_for_settle(range_volts=SETTLE_RANGE_V)
    measured = console.get_voltage()
    print(f"applied {applied:.2f} V, measured {measured:.2f} V on the dummy load")
    assert measured == pytest.approx(applied, abs=SETTLE_RANGE_V)

    # The selection cannot change under a live rail.
    with pytest.raises(LIFUDeviceError) as e:
        console.select_preset(PRESET_INDEX, preset["settings_crc"], s_tx)
    assert e.value.device_error_code == OW_HV_FDA_REFUSED

    console.turn_hv_off()
    console.wait_for_settle(range_volts=OFF_V)
    _assert_hv_off(console)
