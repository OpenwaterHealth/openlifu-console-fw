"""An FDA console refuses user-config writes and the STM32 ROM loader (SR-021).

Bench step 4.07. FDA build only (the `fda` fixture): on another build the
config write would run. HV stays off.

The FDA build enters the signed bootloader's DFU (reserved 0), so the clinical
firmware updater can update it over USB; that path is not run here, since it
leaves the board in its bootloader. It refuses the STM32 ROM loader (0x77).
"""

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


def test_rom_loader_is_refused(console, fda):
    r = send_raw(console, OW_CMD, OW_CMD_DFU, reserved=0x77)

    assert r is not None, "no reply"
    assert (r.packet_type, r.reserved) == (OW_ERROR, OW_HV_FDA_REFUSED)
    # Still the application, not a bootloader.
    assert console.ping()
    assert console.get_version() == fda
