/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bench fault injection for the HV rail monitor (Core/Inc/hv_monitor.h).
 * Not part of any build: tests/bench/README.md says how to link it in for a
 * bench session. Never commit a build that does.
 *
 * Linked with -Wl,--wrap=hv_read_rails -Wl,--wrap=hv_monitor_hv_enabled, it
 * sits between the monitor and the ADC read. INJECT_AFTER_MS after HV on's
 * ramp returns it fakes the fault INJECT selects, so the real monitor, latch
 * and watchdog paths are what the bench exercises.
 */

#include "main.h"
#include "hv_supply.h"
#include "hv_monitor.h"
#include "dbg_print.h"

#include <math.h>

/* Edit these two for the session. */
#define INJECT           HV_FAULT_HVP_HIGH   // an HvFault cause, or INJECT_HANG
#define INJECT_AFTER_MS  30000u

// The main loop stops, so it stops checking in and the watchdog resets the console.
#define INJECT_HANG 6

_Static_assert(INJECT >= HV_FAULT_HVP_HIGH && INJECT <= INJECT_HANG, "INJECT out of range");

bool __real_hv_read_rails(float *hvp, float *hvm);
void __real_hv_monitor_hv_enabled(void);

static uint32_t ramped_at;

void __wrap_hv_monitor_hv_enabled(void)
{
	__real_hv_monitor_hv_enabled();
	ramped_at = HAL_GetTick();
	DBG_PRINTF("BENCH: HV fault %u injected in %u ms\r\n", (unsigned)INJECT, (unsigned)INJECT_AFTER_MS);
}

bool __wrap_hv_read_rails(float *hvp, float *hvm)
{
	bool ok = __real_hv_read_rails(hvp, hvm);
	if (!ok || HAL_GetTick() - ramped_at < INJECT_AFTER_MS) return ok;

	switch (INJECT) {
	case HV_FAULT_HVP_HIGH: *hvp += 20.0f; break;
	case HV_FAULT_HVP_LOW: *hvp *= 0.5f; break;
	case HV_FAULT_HVM_HIGH: *hvm = copysignf(fabsf(*hvm) + 20.0f, *hvm); break;
	case HV_FAULT_HVM_LOW: *hvm *= 0.5f; break;
	case HV_FAULT_ADC_FAIL: return false;
	default:   // INJECT_HANG
		for (;;) {
		}
	}
	return true;
}
