"""Hardware-in-the-loop fixtures: an FDA console on USB.

Run from the repo root with the openlifu SDK installed:

    pytest tests/hil -v                    # bench step 4.07; HV stays off
    pytest tests/hil -v --hv-dummy-load    # also 4.08: turns HV on into a dummy load

Skipped unless a console answers on USB and runs an FDA image. Tests marked
`hv` turn the HV rail on and skip unless --hv-dummy-load is given.
"""

import pytest
from openlifu_sdk.io.LIFUConfig import OW_POWER, OW_POWER_PRESET_GET, OW_RESP
from openlifu_sdk.io.LIFUHVController import LIFUHVController


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
    """Skip unless the console runs an FDA image; returns its version.

    Only an FDA image answers PRESET_GET with its preset. Anything else would
    run the DFU and HV commands these tests send.
    """
    version = console.get_version()
    if version.endswith(("+dev", "+ruo")):
        pytest.skip(f"not an FDA image ({version})")
    r = send_raw(console, OW_POWER, OW_POWER_PRESET_GET)
    if r is None or r.packet_type != OW_RESP or r.data_len == 0:
        pytest.skip(f"not an FDA image with a preset ({version})")
    return version
