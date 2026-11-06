#include "hv_presets.h"

#ifdef FDA_MODE

#include "hv_supply.h"

#include <float.h>
#include <string.h>

// A parsed PRESET_HV_RECORD. id points into it.
typedef struct {
	const char *id;
	uint8_t id_len;
	uint32_t settings_crc;
	float voltage;          // V
	float sensitivity;      // the transmitter sensitivity that voltage suits
	float window[2];        // lowest, highest voltage a select may arrive at
	uint32_t size;          // bytes the record takes, CRC included
} HvPreset;

static void refuse(UartPacket *uartResp, uint8_t code, const char *why)
{
	uartResp->packet_type = OW_ERROR;
	uartResp->reserved = code;
	uartResp->data = (uint8_t *)why;
	uartResp->data_len = (uint16_t)strlen(why);
}

static uint32_t rd_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// CRC-32/ISO-HDLC (zlib's), a bit at a time.
static uint32_t crc32(const uint8_t *p, uint32_t n)
{
	uint32_t crc = 0xFFFFFFFFu;
	while (n--) {
		crc ^= *p++;
		for (int k = 0; k < 8; k++) {
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
		}
	}
	return ~crc;
}

// Written so a NaN fails it too.
static bool positive(float v)
{
	return v > 0.0f && v <= FLT_MAX;
}

// A whole PRESET_HV_RECORD within room bytes, its CRC intact, whose range is
// positive, in order and no higher than HV_PRESET_CEILING_VOLTS.
static bool parse_record(const uint8_t *r, uint32_t room, HvPreset *out)
{
	if (room < PRESET_HV_HEADER + 4U || memcmp(r, "OWHV", 4U) != 0 || r[4] != PRESET_HV_VERSION) return false;
	uint8_t id_len = r[5];
	if (id_len < 1U || id_len > HV_PRESET_ID_MAX) return false;
	uint32_t len = PRESET_HV_HEADER + id_len;
	if (room < len + 4U || crc32(r, len) != rd_u32(&r[len])) return false;
	out->settings_crc = rd_u32(&r[8]);
	memcpy(&out->voltage, &r[12], 4);
	memcpy(&out->sensitivity, &r[16], 4);
	memcpy(out->window, &r[20], 8);
	if (!positive(out->voltage) || !positive(out->sensitivity)) return false;
	// Also false for a NaN bound.
	if (!(positive(out->window[0]) && out->window[0] <= out->window[1]
	      && out->window[1] <= HV_PRESET_CEILING_VOLTS)) return false;
	out->id = (const char *)&r[PRESET_HV_HEADER];
	out->id_len = id_len;
	out->size = len + 4U;
	return true;
}

// HV_PRESET_NONE until a select succeeds.
static uint8_t selected = HV_PRESET_NONE;
// The voltage that select applied.
static float applied = 0.0f;

// The linker places the records at page 61, and they are read from there.
#ifdef HV_PRESET_TABLE_IMAGE
#include "presets/preset_hv_table.h"
_Static_assert(PRESET_HV_COUNT >= 1U && PRESET_HV_COUNT < HV_PRESET_NONE, "PRESET_HV_COUNT out of range");
#define PRESETS_SIZE sizeof(PRESET_HV_TABLE)
#else
#include "presets/fda_preset.h"
#define PRESET_HV_COUNT 1U
#define PRESETS_SIZE sizeof(FDA_PRESET_HV)
#endif
extern const uint8_t _sfda_preset[];

// Preset index's record. Every record before it must parse too, since each
// starts where the one before it ends.
static bool stored_preset(uint8_t index, HvPreset *out)
{
	if (index >= PRESET_HV_COUNT) return false;
	uint32_t offset = 0U;
	for (uint8_t i = 0U; i <= index; i++) {
		if (!parse_record(&_sfda_preset[offset], PRESETS_SIZE - offset, out)) return false;
		offset += out->size;
	}
	return true;
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
	HvPreset p;

	if (cmd->reserved >= PRESET_HV_COUNT) {
		refuse(uartResp, OW_CODE_DATA_ERROR, "no such preset");
		return;
	}
	if (!stored_preset(cmd->reserved, &p)) {
		refuse(uartResp, OW_CODE_DATA_ERROR, "no preset stored");
		return;
	}
	reply[0] = (uint8_t)PRESET_HV_COUNT;
	reply[1] = cmd->reserved;
	reply[2] = selected;
	put_u32(&reply[3], p.settings_crc);
	memcpy(&reply[7], &p.voltage, 4);
	memcpy(&reply[11], &p.sensitivity, 4);
	memcpy(&reply[15], &applied, 4);
	memcpy(&reply[19], p.window, 8);
	reply[27] = p.id_len;
	memcpy(&reply[28], p.id, p.id_len);
	uartResp->data = reply;
	uartResp->data_len = (uint16_t)(28U + p.id_len);
}

static void preset_select(UartPacket *uartResp, const UartPacket *cmd)
{
	// Never under a live rail, where it would change the output.
	if (getHVOnStatus()) {
		refuse(uartResp, OW_CODE_FDA_REFUSED, "HV is on, turn it off to select a preset");
		return;
	}
	// Anything but a successful select leaves nothing selected.
	selected = HV_PRESET_NONE;
	applied = 0.0f;
	// No payload: the host is clearing the selection.
	if (cmd->data_len == 0U) return;

	HvPreset p;
	if (cmd->data == NULL || cmd->data_len != 8U || cmd->reserved >= PRESET_HV_COUNT) {
		refuse(uartResp, OW_CODE_DATA_ERROR, "need a preset index, its settings_crc and the transmitter sensitivity");
		return;
	}
	if (!stored_preset(cmd->reserved, &p)) {
		refuse(uartResp, OW_CODE_DATA_ERROR, "no preset stored");
		return;
	}
	if (rd_u32(&cmd->data[0]) != p.settings_crc) {
		refuse(uartResp, OW_CODE_PRESET_CRC, "settings_crc is not this preset's");
		return;
	}
	float transmitter;
	memcpy(&transmitter, &cmd->data[4], 4);
	if (!positive(transmitter)) {
		refuse(uartResp, OW_CODE_DATA_ERROR, "transmitter sensitivity must be a positive number");
		return;
	}
	// A less sensitive transmitter needs proportionally more voltage for the same output.
	float volts = p.voltage * p.sensitivity / transmitter;
	if (!(volts >= p.window[0] && volts <= p.window[1])) {
		refuse(uartResp, OW_CODE_HV_RANGE, "the voltage for this transmitter is outside the preset's range");
		return;
	}
	hv_set_voltage(volts);
	selected = cmd->reserved;
	applied = volts;
	uartResp->data = (uint8_t *)&applied;
	uartResp->data_len = (uint16_t)sizeof(applied);
}

// Commands answered here. The rest go to normal processing.
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
