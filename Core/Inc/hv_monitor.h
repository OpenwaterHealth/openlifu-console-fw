/*
 * hv_monitor.h
 *
 * SR-025: while HV is on, the console samples both HV rails and turns HV off
 * when either leaves its allowed window. The host contract (fault causes,
 * OW_POWER_GET_HV.reserved, the OW_POWER_STATUS layout, OW_CODE_HV_FAULT) is
 * console-fw#43's "Host contract".
 *
 * Window. V_set is the voltage the console last applied: a successful
 * OW_POWER_PRESET_SELECT on an FDA image, the last OW_POWER_SET_HV on an RUO
 * image. The window is V_set +/- HV_MONITOR_TOL, clamped to the preset's
 * voltage_range when one applies. HVP and |HVM| are each held to it.
 * HVP is ADS8678 ch1 (HVP_2, what OW_POWER_GET_HV reports) and HVM is ch3
 * (HVM_1).
 *
 * Arming. Sampling starts HV_MONITOR_SETTLE_MS after HV_Enable()'s ramp
 * returns (hv_monitor_hv_enabled()); an OW_POWER_SET_HV with HV on restarts
 * the settle period. No V_set (nothing selected, or an RUO raw DAC write)
 * leaves the monitor idle.
 *
 * Trip. HV_MONITOR_TRIP_COUNT consecutive samples out of the window, a failed
 * read counting as one, turn HV off and latch the cause, V_set, the window,
 * the rails at the trip and the trip tick. While latched, OW_POWER_HV_ON and
 * OW_POWER_HV_ENABLE(addr=1) are refused with OW_CODE_HV_FAULT; HV off is
 * always allowed. A successful preset select or deselect (FDA) or
 * OW_POWER_SET_HV (RUO) clears the latch, as does a reset.
 *
 * Context. hv_monitor_poll() runs from the comms loop's idle (comms_idle()),
 * the same context as command processing, so its ADS8678 reads never meet the
 * DAC or another reader on hspi1. A main loop that stops checking in stops
 * the watchdog refresh (main.c), so a hang resets the console with HV off.
 *
 * tests/bench/hv_fault_inject.c fakes faults on the bench; it is not built
 * by default (tests/bench/README.md).
 */

#ifndef INC_HV_MONITOR_H_
#define INC_HV_MONITOR_H_

#include "common.h"

#include <stdbool.h>
#include <stdint.h>

#define HV_MONITOR_PERIOD_MS   50u
#define HV_MONITOR_SETTLE_MS   1000u
#define HV_MONITOR_TRIP_COUNT  3u
#define HV_MONITOR_TOL         0.03f

#define HV_MONITOR_STATUS_VERSION 1u
#define HV_MONITOR_STATUS_LEN     36u

typedef enum {
	HV_FAULT_NONE = 0,
	HV_FAULT_HVP_HIGH = 1,   // positive rail above the window
	HV_FAULT_HVP_LOW = 2,    // positive rail below the window
	HV_FAULT_HVM_HIGH = 3,   // negative rail magnitude above the window
	HV_FAULT_HVM_LOW = 4,    // negative rail magnitude below the window
	HV_FAULT_ADC_FAIL = 5,   // the rails could not be read
} HvFault;

typedef struct {
	float low;
	float high;
} HvWindow;

/* --- Decision: pure, so it can be tested on the host. --- */

/**
 * The allowed window around v_set. range_max > 0 clamps it to the preset's
 * [range_min, range_max]; range_max == 0 means no preset range (RUO).
 */
HvWindow hv_monitor_window(float v_set, float range_min, float range_max);

/** HV_FAULT_NONE when the sample is inside w, else the cause. A NaN is outside. */
HvFault hv_monitor_classify(HvWindow w, bool read_ok, float hvp, float hvm_mag);

/* --- The monitor. --- */

/** V_set and its preset range (range_max 0: none); v_set 0 leaves the monitor idle. */
void hv_monitor_set_point(float v_set, float range_min, float range_max);

/** Drop the latched fault (a successful select or deselect, or an RUO SET_HV). */
void hv_monitor_clear_fault(void);

/** HV_Enable()'s ramp has returned with HV on: the settle period starts now. */
void hv_monitor_hv_enabled(void);

/** Sample the rails if one is due. Main context only. */
void hv_monitor_poll(void);

/** The latched cause, HV_FAULT_NONE when none. */
HvFault hv_monitor_fault(void);

/**
 * OW_POWER_STATUS and the latch's refusals. True when it answered cmd; false
 * lets the command through.
 */
bool hv_monitor_process(UartPacket *uartResp, const UartPacket *cmd);

#endif /* INC_HV_MONITOR_H_ */
