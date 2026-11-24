"""Hardware-in-the-loop fixtures: an FDA console on USB.

Run from the repo root with the openlifu SDK installed:

    pytest tests/hil -v                    # bench step 4.07; HV stays off
    pytest tests/hil -v --hv-dummy-load    # also 4.08: turns HV on into a dummy load

Skipped unless a console answers on USB and runs the FDA (clinical) build with
presets stored in its preset page (page 61). Tests marked `hv` turn the HV
rail on and skip unless --hv-dummy-load is given.
"""

import pytest
from openlifu_sdk.io.LIFUConfig import (
    OW_ERROR,
    OW_HV_FDA_REFUSED,
    OW_POWER,
    OW_POWER_PRESET_GET,
    OW_RESP,
)
from openlifu_sdk.io.LIFUHVController import LIFUHVController

# Core/Inc/common.h. An empty write: the FDA build refuses it with 0xF8, a
# research build with a data error, before either touches the preset page.
OW_POWER_PRESET_WRITE = 0x47


def pytest_addoption(parser):
    parser.addoption(
        "--hv-dummy-load",
        action="store_true",
        help="The console's HV output is on a dummy load: run the tests that turn HV on.",
    )
    parser.addoption(
        "--tx-sensitivity",
        type=float,
        default=None,
        help="Transmitter sensitivity to select the preset with, as well as the preset's own.",
    )


def pytest_configure(config):
    config.addinivalue_line("markers", "hv: turns the console HV on; needs --hv-dummy-load")


def pytest_collection_modifyitems(config, items):
    if config.getoption("--hv-dummy-load"):
        return
    skip = pytest.mark.skip(reason="turns HV on: needs --hv-dummy-load, output on a dummy load")
    for item in items:
        if "hv" in item.keywords:
            item.add_marker(skip)


@pytest.fixture(scope="session")
def console():
    # The research class, so the test can put commands on the wire that the
    # FDA HVController would refuse to send: the point is that the firmware refuses.
    hv = LIFUHVController()
    if not hv.connect():
        pytest.skip("no console on USB")
    yield hv
    hv.close()


def send_raw(console, packet_type, command, addr=0, reserved=0, data=None):
    """Put a packet on the wire as is, past the SDK's own command checks."""
    return console._uart.send_packet(
        packet_type=packet_type, command=command, addr=addr, reserved=reserved, data=data
    )


@pytest.fixture(scope="session")
def fda(console) -> str:
    """Skip unless the console runs the FDA (clinical) build with presets
    stored; returns its version.

    The version string is the same for every build of a release, so the
    build is told by an empty OW_POWER_PRESET_WRITE: only the FDA build
    refuses it as read-only (0xF8). Any other build would run the HV commands
    these tests send.
    """
    version = console.get_version()
    r = send_raw(console, OW_POWER, OW_POWER_PRESET_WRITE)
    if r is None or (r.packet_type, r.reserved) != (OW_ERROR, OW_HV_FDA_REFUSED):
        pytest.skip(f"not the FDA build ({version})")
    r = send_raw(console, OW_POWER, OW_POWER_PRESET_GET)
    if r is None or r.packet_type != OW_RESP or r.data_len == 0:
        pytest.skip(f"no presets stored in the preset page ({version})")
    return version


def preset_count(console) -> int:
    """How many presets the console's preset page holds (PRESET_GET's count)."""
    r = send_raw(console, OW_POWER, OW_POWER_PRESET_GET)
    return r.data[0]
