"""An FDA console refuses user-config writes and DFU entry (SR-021).

Bench step 4.07. Presets-only images only, +fda or +test (the `fda` fixture):
on another image these commands would run, and DFU would leave the board in its
bootloader. HV stays off.
"""

import pytest
from conftest import send_raw
from openlifu_sdk.io.LIFUConfig import (
    OW_CMD,
    OW_CMD_DFU,
    OW_CMD_USR_CFG,
    OW_ERROR,
    OW_HV_FDA_REFUSED,
)


def _read_config_wire(console) -> bytes:
    r = send_raw(console, OW_CMD, OW_CMD_USR_CFG, reserved=0)
    assert r is not None and r.packet_type != OW_ERROR, "USR_CFG read refused"
    assert r.data_len > 0
    return bytes(r.data)


def test_user_config_read_still_works(console, fda):
    assert _read_config_wire(console)


def test_user_config_write_is_refused(console, fda):
    before = _read_config_wire(console)

    # The config as it stands: if the write ever ran, nothing would change but its seq.
    r = send_raw(console, OW_CMD, OW_CMD_USR_CFG, reserved=1, data=bytearray(before))

    assert r is not None, "no reply"
    assert (r.packet_type, r.reserved) == (OW_ERROR, OW_HV_FDA_REFUSED)
    assert _read_config_wire(console) == before, "the config changed (seq included)"


@pytest.mark.parametrize("reserved", [0x00, 0x77], ids=["dfu", "rom-loader"])
def test_dfu_is_refused(console, fda, reserved):
    r = send_raw(console, OW_CMD, OW_CMD_DFU, reserved=reserved)

    assert r is not None, "no reply"
    assert (r.packet_type, r.reserved) == (OW_ERROR, OW_HV_FDA_REFUSED)
    # Still the application, not a bootloader.
    assert console.ping()
    assert console.get_version() == fda
