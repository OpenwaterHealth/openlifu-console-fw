# Hardware-in-the-loop tests

Host-side pytest checks that run against a console on USB through the openlifu
SDK. They need hardware, so CI does not run them. Record the output as bench
evidence in the PR or issue they verify.

```
pip install openlifu-sdk pytest
pytest tests/hil -v
```

Every test skips unless a console answers on USB and runs a presets-only image:
a `+fda` (one preset) or `+test` (four presets) version that answers
`PRESET_GET` with its first preset. A plain image has no suffix. On any other image these commands
would run, and DFU would leave the board in its bootloader.

**HV.** Tests marked `hv` turn the HV rail on. They skip unless you pass
`--hv-dummy-load`, so put the HV output on a dummy load first:

```
pytest tests/hil -v --hv-dummy-load
pytest tests/hil -v --hv-dummy-load --tx-sensitivity 1.23
```

The preset tests run once per preset the image links (`preset0` on `+fda`,
`preset0`–`preset3` on `+test`), each held to that preset's own range.
`--tx-sensitivity` adds a select at that transmitter sensitivity to the one at
the preset's own (a 1:1 rescale). Pick one that keeps every preset's rescaled
voltage inside its range, or run one preset with `-k preset3`.

| File | Bench step | HV | Checks |
|---|---|---|---|
| `test_fda_service_refusals.py` | 4.07 | off | `USR_CFG` write (reserved 1, the config as read) refused with 0xF8 and the config unchanged; `USR_CFG` read works; `DFU` with reserved 0 and 0x77 refused, and the board still answers as the same image |
| `test_preset_table.py` | 4.08 | off | the image links as many presets as its suffix names; each reports its own range, positive and no higher than 45 V; an index past the last is refused |
| `test_fda_hv_presets.py` | 4.08 | on (`--hv-dummy-load`) | per preset: `SET_HV`, `SET_DACS`, `RAW_DAC` refused; a select with the wrong settings CRC refused (`0xF6`) and HV on then refused; HV on with nothing selected refused (`0xF7`); a select above or below the preset's range refused (`0xF1`); the applied voltage is the preset voltage × preset sensitivity ÷ TX sensitivity, the output settles there, and a select under a live rail is refused |
