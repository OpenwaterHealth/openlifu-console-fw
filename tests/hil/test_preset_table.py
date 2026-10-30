"""A presets-only console links the presets its suffix names (SR-003).

HV stays off. +fda links one preset, held to the FDA 35-45 V range. +test
links four, in the transmitter's order, each held to its own range: preset 0
is the FDA preset, range included, and no range leaves 15-45 V
(Core/Inc/hv_presets.h).
"""

import pytest
from conftest import preset_count
from openlifu_sdk.io.exceptions import LIFUDeviceError

FDA_WINDOW = (35.0, 45.0)
# The 45 V ceiling holds in every image; only +test may go below 35 V.
HARD_WINDOW = {"+fda": FDA_WINDOW, "+test": (15.0, 45.0)}


def test_the_image_links_the_presets_its_suffix_names(console, fda):
    count = preset_count(fda)
    suffix = "+" + fda.rsplit("+", 1)[1]
    low, high = HARD_WINDOW[suffix]
    presets = [console.get_preset(i) for i in range(count)]
    for i, p in enumerate(presets):
        print(f"{i}: {p['id']} {p['voltage']:g} V @ {p['sensitivity']:g}, "
              f"{p['min_voltage']:g}-{p['max_voltage']:g} V, crc 0x{p['settings_crc']:08x}")
        assert (p["count"], p["index"]) == (count, i)
        assert low <= p["min_voltage"] <= p["voltage"] <= p["max_voltage"] <= high
    assert (presets[0]["min_voltage"], presets[0]["max_voltage"]) == FDA_WINDOW
    assert len({p["settings_crc"] for p in presets}) == count

    with pytest.raises(LIFUDeviceError):
        console.get_preset(count)
