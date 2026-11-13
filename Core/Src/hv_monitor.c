#include "hv_monitor.h"

#include "main.h"
#include "hv_supply.h"
#include "rgb.h"
#include "dbg_print.h"

#include <math.h>
#include <string.h>

HvWindow hv_monitor_window(float v_set, float range_min, float range_max)
{
	HvWindow w = { (1.0f - HV_MONITOR_TOL) * v_set, (1.0f + HV_MONITOR_TOL) * v_set };
	if (range_max > 0.0f) {
		if (w.low < range_min) w.low = range_min;
		if (w.high > range_max) w.high = range_max;
	}
	return w;
}

HvFault hv_monitor_classify(HvWindow w, bool read_ok, float hvp, float hvm_mag)
{
	if (!read_ok) return HV_FAULT_ADC_FAIL;
	// Each test is written so a NaN fails it.
	if (!(hvp <= w.high)) return HV_FAULT_HVP_HIGH;
	if (!(hvp >= w.low)) return HV_FAULT_HVP_LOW;
	if (!(hvm_mag <= w.high)) return HV_FAULT_HVM_HIGH;
	if (!(hvm_mag >= w.low)) return HV_FAULT_HVM_LOW;
	return HV_FAULT_NONE;
}

static struct {
	float v_set;            // 0: nothing to hold the rails to
	bool preset_range;      // the window is clamped to a preset's range
	HvWindow window;
	bool enabled;           // HV_Enable() returned with HV on
	uint32_t enabled_at;    // tick the settle period started
	uint32_t last_sample;
	uint8_t out_count;      // consecutive samples out of the window
	float hvp, hvm;         // latest sample, |HVM|
	HvFault fault;
	float hvp_at_trip, hvm_at_trip;
	uint32_t trip_tick;
} mon;

void hv_monitor_set_point(float v_set, float range_min, float range_max)
{
	mon.v_set = v_set;
	mon.preset_range = range_max > 0.0f;
	mon.window = v_set > 0.0f ? hv_monitor_window(v_set, range_min, range_max) : (HvWindow){ 0.0f, 0.0f };
	mon.out_count = 0U;
	// A new setpoint under a live rail (RUO SET_HV) restarts the settle period.
	mon.enabled_at = HAL_GetTick();
}

void hv_monitor_clear_fault(void)
{
	mon.fault = HV_FAULT_NONE;
	mon.hvp_at_trip = 0.0f;
	mon.hvm_at_trip = 0.0f;
	mon.trip_tick = 0U;
}

void hv_monitor_hv_enabled(void)
{
	mon.enabled = getHVOnStatus();
	mon.enabled_at = HAL_GetTick();
	mon.out_count = 0U;
}

HvFault hv_monitor_fault(void)
{
	return mon.fault;
}

static bool armed(uint32_t now)
{
	return mon.enabled && mon.v_set > 0.0f && getHVOnStatus()
	       && now - mon.enabled_at >= HV_MONITOR_SETTLE_MS;
}

static void trip(HvFault cause, uint32_t now)
{
	HV_Disable();   // HV_SHUTDOWN first, then the DACs
	mon.fault = cause;
	mon.hvp_at_trip = mon.hvp;
	mon.hvm_at_trip = mon.hvm;
	mon.trip_tick = now;
	mon.enabled = false;
	RGB_Set(RGB_GREEN);   // the HV-off color, as OW_POWER_HV_OFF sets; no fault color in FDA
	DBG_PRINTF("HV monitor: fault %u, HV off\r\n", (unsigned)cause);
}

void hv_monitor_poll(void)
{
	if (!getHVOnStatus()) {
		mon.enabled = false;
		mon.out_count = 0U;
		mon.hvp = 0.0f;   // no sample of a rail that is off
		mon.hvm = 0.0f;
		return;
	}
	if (mon.fault != HV_FAULT_NONE) {   // HV on is refused while latched; keep it off
		HV_Disable();
		return;
	}
	uint32_t now = HAL_GetTick();
	if (!armed(now) || now - mon.last_sample < HV_MONITOR_PERIOD_MS) return;
	mon.last_sample = now;

	float hvp = 0.0f, hvm = 0.0f;
	bool read_ok = hv_read_rails(&hvp, &hvm);
	hvm = fabsf(hvm);
	mon.hvp = read_ok ? hvp : 0.0f;
	mon.hvm = read_ok ? hvm : 0.0f;

	HvFault cause = hv_monitor_classify(mon.window, read_ok, hvp, hvm);
	if (cause == HV_FAULT_NONE) {
		mon.out_count = 0U;
	} else if (++mon.out_count >= HV_MONITOR_TRIP_COUNT) {
		trip(cause, now);
	}
}

static void put_f32(uint8_t *p, float v)
{
	memcpy(p, &v, 4);
}

static void status_reply(UartPacket *uartResp)
{
	static uint8_t reply[HV_MONITOR_STATUS_LEN];
	uint32_t now = HAL_GetTick();
	bool faulted = mon.fault != HV_FAULT_NONE;
	uint32_t ms_since_trip = faulted ? now - mon.trip_tick : 0U;

	reply[0] = HV_MONITOR_STATUS_VERSION;
	reply[1] = (uint8_t)mon.fault;
	reply[2] = getHVOnStatus() ? 1U : 0U;
	reply[3] = (uint8_t)((armed(now) ? 0x01U : 0U) | (mon.preset_range ? 0x02U : 0U));
	put_f32(&reply[4], mon.v_set);
	put_f32(&reply[8], mon.window.low);
	put_f32(&reply[12], mon.window.high);
	put_f32(&reply[16], mon.hvp);
	put_f32(&reply[20], mon.hvm);
	put_f32(&reply[24], mon.hvp_at_trip);
	put_f32(&reply[28], mon.hvm_at_trip);
	memcpy(&reply[32], &ms_since_trip, 4);
	uartResp->data = reply;
	uartResp->data_len = HV_MONITOR_STATUS_LEN;
}

bool hv_monitor_process(UartPacket *uartResp, const UartPacket *cmd)
{
	bool hv_on_request = cmd->command == OW_POWER_HV_ON
	                     || (cmd->command == OW_POWER_HV_ENABLE && cmd->addr == 1U);
	if (cmd->command != OW_POWER_STATUS && !(hv_on_request && mon.fault != HV_FAULT_NONE)) {
		return false;
	}

	uartResp->command = cmd->command;
	uartResp->addr = cmd->addr;
	uartResp->reserved = cmd->reserved;
	uartResp->data_len = 0;

	if (cmd->command == OW_POWER_STATUS) {
		status_reply(uartResp);
	} else {
		ow_refuse(uartResp, OW_CODE_HV_FAULT, "an HV fault is latched: select a preset (or set the voltage) to clear it");
	}
	return true;
}
