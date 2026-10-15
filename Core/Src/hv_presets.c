#include "hv_presets.h"

#ifdef FDA_MODE

#include "hv_supply.h"

#include <float.h>
#include <string.h>

#include "presets/preset_hv_table.h"

_Static_assert(FDA_HV_MIN_VOLTS > 0.0f && FDA_HV_MIN_VOLTS <= FDA_HV_MAX_VOLTS,
               "the FDA voltage window is empty");

// Index of the preset whose voltage is applied; none until the host selects one.
static uint8_t selected = HV_PRESET_NONE;
// The voltage that select arrived at: the preset's, rescaled to the transmitter.
static float applied = 0.0f;

static void refuse(UartPacket *uartResp, uint8_t code, const char *why)
{
	uartResp->packet_type = OW_ERROR;
	uartResp->reserved = code;
	uartResp->data = (uint8_t *)why;
	uartResp->data_len = (uint16_t)strlen(why);
}

static void put_u32(uint8_t *p, uint32_t v)
{
	for (int i = 0; i < 4; i++) {
		p[i] = (uint8_t)(v >> (8 * i));
	}
}

static void preset_get(UartPacket *uartResp, const UartPacket *cmd)
{
	// [0] count [1] index [2] selected index (0xFF: none)
	// [3..6] settings_crc  [7..10] voltage  [11..14] sensitivity
	// [15..18] voltage applied by the last select (0: none)
	// [19..22] lowest, [23..26] highest voltage a select may arrive at
	// [27] id_len [28..] id                          (all LE, voltages f32)
	static uint8_t reply[28U + HV_PRESET_ID_MAX];
	static const float window[2] = { FDA_HV_MIN_VOLTS, FDA_HV_MAX_VOLTS };

	if (cmd->reserved >= PRESET_HV_COUNT) {
		refuse(uartResp, OW_CODE_DATA_ERROR, "no such preset");
		return;
	}
	const HvPreset *p = &hv_preset_table[cmd->reserved];
	size_t id_len = strlen(p->id);
	if (id_len > HV_PRESET_ID_MAX) id_len = HV_PRESET_ID_MAX;

	reply[0] = (uint8_t)PRESET_HV_COUNT;
	reply[1] = cmd->reserved;
	reply[2] = selected;
	put_u32(&reply[3], p->settings_crc);
	memcpy(&reply[7], &p->voltage, 4);
	memcpy(&reply[11], &p->sensitivity, 4);
	memcpy(&reply[15], &applied, 4);
	memcpy(&reply[19], window, 8);
	reply[27] = (uint8_t)id_len;
	memcpy(&reply[28], p->id, id_len);
	uartResp->data = reply;
	uartResp->data_len = (uint16_t)(28U + id_len);
}

static void preset_select(UartPacket *uartResp, const UartPacket *cmd)
{
	// A new setpoint, or none, under a live rail would change the output
	// mid-sonication, so what is selected stays as it is.
	if (getHVOnStatus()) {
		refuse(uartResp, OW_CODE_FDA_REFUSED, "HV is on; turn it off to select a preset");
		return;
	}
	// Anything short of a select that succeeds leaves nothing selected, so HV
	// cannot come on for an earlier preset the host no longer matches.
	selected = HV_PRESET_NONE;
	applied = 0.0f;
	// No payload is the host clearing the selection: its transmitter did not load.
	if (cmd->data_len == 0U) return;

	if (cmd->data == NULL || cmd->data_len != 8U || cmd->reserved >= PRESET_HV_COUNT) {
		refuse(uartResp, OW_CODE_DATA_ERROR, "need a preset index, its settings_crc and the transmitter sensitivity");
		return;
	}
	uint32_t crc = 0U;
	for (int i = 0; i < 4; i++) {
		crc |= (uint32_t)cmd->data[i] << (8 * i);
	}
	const HvPreset *p = &hv_preset_table[cmd->reserved];
	if (crc != p->settings_crc) {
		refuse(uartResp, OW_CODE_PRESET_CRC, "settings_crc is not this preset's");
		return;
	}
	float transmitter;
	memcpy(&transmitter, &cmd->data[4], 4);
	// Written so a NaN fails it too.
	if (!(transmitter > 0.0f && transmitter <= FLT_MAX)) {
		refuse(uartResp, OW_CODE_DATA_ERROR, "transmitter sensitivity must be a positive number");
		return;
	}
	// A less sensitive transmitter needs proportionally more voltage for the same output.
	float volts = p->voltage * p->sensitivity / transmitter;
	if (!(volts >= FDA_HV_MIN_VOLTS && volts <= FDA_HV_MAX_VOLTS)) {
		refuse(uartResp, OW_CODE_HV_RANGE, "the voltage for this transmitter is outside the FDA window");
		return;
	}
	hv_set_voltage(volts);
	selected = cmd->reserved;
	applied = volts;
	uartResp->data = (uint8_t *)&applied;
	uartResp->data_len = (uint16_t)sizeof(applied);
}

// Which commands this module answers; the rest go to normal processing untouched.
static bool handled_here(const UartPacket *cmd)
{
	switch (cmd->command) {
	case OW_POWER_SET_HV:
	case OW_POWER_SET_DACS:
	case OW_POWER_RAW_DAC:
	case OW_POWER_PRESET_GET:
	case OW_POWER_PRESET_SELECT:
		return true;
	case OW_POWER_HV_ON:
		return selected == HV_PRESET_NONE;
	case OW_POWER_HV_ENABLE:
		// Disabling is always allowed.
		return cmd->addr == 1U && selected == HV_PRESET_NONE;
	default:
		return false;
	}
}

bool hv_presets_process(UartPacket *uartResp, const UartPacket *cmd)
{
	if (!handled_here(cmd)) return false;

	uartResp->command = cmd->command;
	uartResp->addr = cmd->addr;
	uartResp->reserved = cmd->reserved;
	uartResp->data_len = 0;

	switch (cmd->command) {
	case OW_POWER_SET_HV:
		refuse(uartResp, OW_CODE_FDA_REFUSED, "the voltage comes from the selected preset");
		break;
	case OW_POWER_SET_DACS:
	case OW_POWER_RAW_DAC:
		refuse(uartResp, OW_CODE_FDA_REFUSED, "raw DAC writes are refused in FDA mode");
		break;
	case OW_POWER_PRESET_GET:
		preset_get(uartResp, cmd);
		break;
	case OW_POWER_PRESET_SELECT:
		preset_select(uartResp, cmd);
		break;
	default:   // HV on with nothing selected
		refuse(uartResp, OW_CODE_NO_PRESET, "no preset selected");
		break;
	}
	return true;
}

#endif /* FDA_MODE */
