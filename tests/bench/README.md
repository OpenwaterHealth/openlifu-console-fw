# Bench fault injection

`hv_fault_inject.c` fakes HV rail monitor faults (SR-025,
`Core/Inc/hv_monitor.h`) so the trip, latch and watchdog paths can be
checked on hardware. It is not built by default, and no released or
committed build may include it.

## Linking it in

1. Set `INJECT` and `INJECT_AFTER_MS` at the top of `hv_fault_inject.c`.
2. In `CMakeLists.txt`, add these lines just above the
   `# OW_CMD_VERSION returns FW_VERSION` comment:

   ```cmake
   target_sources(${CMAKE_PROJECT_NAME} PRIVATE tests/bench/hv_fault_inject.c)
   target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
       -Wl,--wrap=hv_read_rails -Wl,--wrap=hv_monitor_hv_enabled)
   set(FW_GIT_DESCRIBE "${FW_GIT_DESCRIBE}-inject")
   ```

   The last line puts `-inject` in the version string, so the image can be
   told apart from a real one.
3. Build and flash. Revert `CMakeLists.txt` when you are done.

Works on FDA and RUO images. On FDA, select a preset before HV on.

## What to expect

Turn HV on and leave it on past `INJECT_AFTER_MS`.

| `INJECT` | Expected |
|---|---|
| `HV_FAULT_HVP_HIGH` .. `HV_FAULT_ADC_FAIL` | About `INJECT_AFTER_MS` + 150 ms after the ramp, HV turns off and the LED goes green. `OW_POWER_GET_HV.reserved` and `OW_POWER_STATUS` byte 1 hold the cause. HV on is refused with `OW_CODE_HV_FAULT` (0xF0) until a successful preset select or deselect (FDA) or `OW_POWER_SET_HV` (RUO). |
| `INJECT_HANG` | The console stops answering and resets within about 5 s, coming back with HV off. |
