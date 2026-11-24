# Hardware-in-the-loop tests

Host-side pytest checks that run against a console on USB through the openlifu
SDK. They need hardware, so CI does not run them. Record the output as bench
evidence in the PR or issue they verify.

```
pip install openlifu-sdk pytest
pytest tests/hil -v
```

Every test skips unless a console answers on USB and runs the FDA (clinical)
build with presets stored in its preset page (page 61). The version string is
the same for every build of a release, so the build is told by an empty
`PRESET_WRITE`: only the FDA build refuses it as read-only (0xF8). On any other
build these commands would run.

**HV.** Tests marked `hv` turn the HV rail on. They skip unless you pass
`--hv-dummy-load`, so put the HV output on a dummy load first:

```
pytest tests/hil -v --hv-dummy-load
pytest tests/hil -v --hv-dummy-load --tx-sensitivity 1.23
```

The preset tests run once per preset the page holds (`preset0` for the
released set, `preset0`–`preset3` for the test set), each held to that preset's
own range.
`--tx-sensitivity` adds a select at that transmitter sensitivity to the one at
the preset's own (a 1:1 rescale). Pick one that keeps every preset's rescaled
voltage inside its range, or run one preset with `-k preset3`.

| File | Bench step | HV | Checks |
|---|---|---|---|
| `test_fda_service_refusals.py` | 4.07 | off | `USR_CFG` write (reserved 1, the config as read) refused with 0xF8 and the config unchanged; `USR_CFG` read works; `DFU` with reserved 0x77 (the STM32 ROM loader) refused and the board still answers as the same image (reserved 0, the signed bootloader's DFU, is allowed and not run) |
| `test_preset_table.py` | 4.08 | off | the console serves the presets its preset page holds; each reports its own range, positive and no higher than 45 V; an index past the last is refused |
| `test_fda_hv_presets.py` | 4.08 | on (`--hv-dummy-load`) | per preset: `SET_HV`, `SET_DACS`, `RAW_DAC` refused; a select with the wrong settings CRC refused (`0xF6`) and HV on then refused; HV on with nothing selected refused (`0xF7`); a select above or below the preset's range refused (`0xF1`); the applied voltage is the preset voltage × preset sensitivity ÷ TX sensitivity, the output settles there, and a select under a live rail is refused |
