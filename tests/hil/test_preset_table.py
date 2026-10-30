"""A presets-only console links the presets its suffix names (SR-003).

HV stays off. +fda links one preset and +test four, in the transmitter's
order (index 0 is the FDA preset). Each reports its own voltage range, which
no image lets reach above 45 V (Core/Inc/hv_presets.h).
"""

import pytest
from conftest import preset_count
from openlifu_sdk.io.exceptions import LIFUDeviceError

CEILING_V = 45.0


def test_the_image_links_the_presets_its_suffix_names(console, fda):
    count = preset_count(fda)
    presets = [console.get_preset(i) for i in range(count)]
    for i, p in enumerate(presets):
        print(f"{i}: {p['id']} {p['voltage']:g} V @ {p['sensitivity']:g}, "
              f"{p['min_voltage']:g}-{p['max_voltage']:g} V, crc 0x{p['settings_crc']:08x}")
        assert (p["count"], p["index"]) == (count, i)
        assert 0.0 < p["min_voltage"] <= p["voltage"] <= p["max_voltage"] <= CEILING_V
    assert len({p["settings_crc"] for p in presets}) == count

    with pytest.raises(LIFUDeviceError):
        console.get_preset(count)
