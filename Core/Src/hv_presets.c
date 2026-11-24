#include "hv_presets.h"

#include "hv_monitor.h"
#include "hv_supply.h"
#include "memory_map.h"
#include "preset_store.h"

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

// Walk the stored records. With want below the count, fills *out with record
// want. Returns how many records the body holds, or 0 if any record fails to
// parse or they do not fill the body exactly: a stored set is all or nothing.
static uint8_t walk_presets(uint8_t want, HvPreset *out)
{
	uint32_t size;
	const uint8_t *body = preset_store_body(&size);
	if (body == NULL) return 0U;
	uint32_t offset = 0U;
	uint8_t count = 0U;
	HvPreset p;
	while (offset < size) {
		if (count == HV_PRESET_NONE || !parse_record(&body[offset], size - offset, &p)) return 0U;
		if (count == want && out != NULL) *out = p;
		offset += p.size;
		count++;
	}
	return count;
}

static uint8_t preset_count(void)
{
	return walk_presets(HV_PRESET_NONE, NULL);
}

// Preset index's record, if the stored set is valid and holds it.
static bool stored_preset(uint8_t index, HvPreset *out)
{
	return index < walk_presets(index, out);
}

static void put_u32(uint8_t *p, uint32_t v)
{
	for (int i = 0; i < 4; i++) {
		p[i] = (uint8_t)(v >> (8 * i));
	}
}

static void deselect(void)
{
	selected = HV_PRESET_NONE;
	applied = 0.0f;
	hv_monitor_set_point(0.0f, 0.0f, 0.0f);
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
	uint8_t count = preset_count();

	if (count == 0U) {
		ow_refuse(uartResp, OW_CODE_DATA_ERROR, "no preset stored");
		return;
	}
	if (cmd->reserved >= count || !stored_preset(cmd->reserved, &p)) {
		ow_refuse(uartResp, OW_CODE_DATA_ERROR, "no such preset");
		return;
	}
	reply[0] = count;
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
		ow_refuse(uartResp, OW_CODE_FDA_REFUSED, "HV is on, turn it off to select a preset");
		return;
	}
	// Anything but a successful select leaves nothing selected. Only a
	// successful one, deselect included, clears a latched HV fault.
	deselect();
	// No payload: the host is clearing the selection.
	if (cmd->data_len == 0U) {
		hv_monitor_clear_fault();
		return;
	}

	HvPreset p;
	if (cmd->data == NULL || cmd->data_len != 8U) {
		ow_refuse(uartResp, OW_CODE_DATA_ERROR, "need a preset index, its settings_crc and the transmitter sensitivity");
		return;
	}
	if (!stored_preset(cmd->reserved, &p)) {
		ow_refuse(uartResp, OW_CODE_DATA_ERROR, "no such preset stored");
		return;
	}
	if (rd_u32(&cmd->data[0]) != p.settings_crc) {
		ow_refuse(uartResp, OW_CODE_PRESET_CRC, "settings_crc is not this preset's");
		return;
	}
	float transmitter;
	memcpy(&transmitter, &cmd->data[4], 4);
	if (!positive(transmitter)) {
		ow_refuse(uartResp, OW_CODE_DATA_ERROR, "transmitter sensitivity must be a positive number");
		return;
	}
	// A less sensitive transmitter needs proportionally more voltage for the same output.
	float volts = p.voltage * p.sensitivity / transmitter;
	if (!(volts >= p.window[0] && volts <= p.window[1])) {
		ow_refuse(uartResp, OW_CODE_HV_RANGE, "the voltage for this transmitter is outside the preset's range");
		return;
	}
	hv_set_voltage(volts);
	hv_monitor_set_point(volts, p.window[0], p.window[1]);
	hv_monitor_clear_fault();
	selected = cmd->reserved;
	applied = volts;
	uartResp->data = (uint8_t *)&applied;
	uartResp->data_len = (uint16_t)sizeof(applied);
}

// reserved: PRESET_INFO_*; data: the package header when OK, straight from
// flash, so the host can see which preset set (and which signature) the unit
// holds with no RAM copy.
static void preset_info(UartPacket *uartResp)
{
	const uint8_t *page = preset_store_page();
	if (page == NULL) {
		uartResp->reserved = PRESET_INFO_NO_PAGE;
	} else if (preset_count() == 0U) {
		uartResp->reserved = PRESET_INFO_EMPTY;
	} else {
		uartResp->reserved = PRESET_INFO_OK;
		uartResp->data = (uint8_t *)page;
		uartResp->data_len = (uint16_t)PRESET_PKG_HEADER_LEN;
	}
}

#ifndef FDA_MODE
// A preset write in progress: its first chunk erased the page.
static bool writing;

// [0..1] offset u16 LE, [2..] bytes. The first chunk erases the page; the host
// sends the header chunk last, so a write that stops half-way leaves no
// package. Programs straight into flash: the console has no RAM for a page.
static void preset_write(UartPacket *uartResp, const UartPacket *cmd)
{
	if (getHVOnStatus()) {
		ow_refuse(uartResp, OW_CODE_ERROR, "HV is on, turn it off to write presets");
		return;
	}
	if (preset_store_page() == NULL) {
		ow_refuse(uartResp, OW_CODE_DATA_ERROR, "no preset page: update the bootloader (>= 1.1.0-rc.2)");
		return;
	}
	if (cmd->data == NULL || cmd->data_len < 3U) {
		ow_refuse(uartResp, OW_CODE_DATA_ERROR, "need an offset and data");
		return;
	}
	uint32_t offset = (uint32_t)cmd->data[0] | ((uint32_t)cmd->data[1] << 8);
	if (!writing) {
		deselect();
		if (!preset_store_erase()) {
			ow_refuse(uartResp, OW_CODE_ERROR, "preset page erase failed");
			return;
		}
		writing = true;
	}
	if (!preset_store_program(offset, &cmd->data[2], (uint32_t)cmd->data_len - 2U)) {
		ow_refuse(uartResp, OW_CODE_DATA_ERROR, "chunk not written: unaligned, outside the page, or over written bytes");
	}
}

// [0..3] package length u32 LE, [4..7] CRC-32 of the package. The page must
// now hold exactly that package: a PRS1 header whose body size matches, and
// records that fill the body. Otherwise the page is erased.
static void preset_commit(UartPacket *uartResp, const UartPacket *cmd)
{
	bool was_writing = writing;
	writing = false;
	if (!was_writing || cmd->data == NULL || cmd->data_len != 8U) {
		ow_refuse(uartResp, OW_CODE_DATA_ERROR, "need a write, then the package length and CRC-32");
		return;
	}
	uint32_t len = rd_u32(&cmd->data[0]);
	uint32_t size;
	const uint8_t *page = preset_store_page();
	bool ok = page != NULL && len > PRESET_PKG_HEADER_LEN && len <= PRESET_STORE_SIZE
	          && crc32(page, len) == rd_u32(&cmd->data[4])
	          && preset_store_body(&size) != NULL && PRESET_PKG_HEADER_LEN + size == len
	          && preset_count() != 0U;
	if (!ok) {
		(void)preset_store_erase();
		ow_refuse(uartResp, OW_CODE_DATA_ERROR, "the page does not hold a valid package of that length and CRC; erased");
	}
}
#endif /* FDA_MODE */

// Commands answered here. The rest go to normal processing.
static bool handled_here(const UartPacket *cmd)
{
	switch (cmd->command) {
	case OW_POWER_PRESET_GET:
	case OW_POWER_PRESET_SELECT:
	case OW_POWER_PRESET_INFO:
	case OW_POWER_PRESET_WRITE:
	case OW_POWER_PRESET_COMMIT:
		return true;
#ifdef FDA_MODE
	case OW_POWER_SET_HV:
	case OW_POWER_SET_DACS:
	case OW_POWER_RAW_DAC:
		return true;
	case OW_POWER_HV_ON:
		return selected == HV_PRESET_NONE;
	case OW_POWER_HV_ENABLE:
		// Disabling is always allowed.
		return cmd->addr == 1U && selected == HV_PRESET_NONE;
#endif
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
	case OW_POWER_PRESET_GET:
		preset_get(uartResp, cmd);
		break;
	case OW_POWER_PRESET_SELECT:
		preset_select(uartResp, cmd);
		break;
	case OW_POWER_PRESET_INFO:
		preset_info(uartResp);
		break;
#ifdef FDA_MODE
	case OW_POWER_PRESET_WRITE:
	case OW_POWER_PRESET_COMMIT:
		ow_refuse(uartResp, OW_CODE_FDA_REFUSED, "presets are read-only in FDA mode");
		break;
	case OW_POWER_SET_HV:
		ow_refuse(uartResp, OW_CODE_FDA_REFUSED, "the voltage comes from the selected preset");
		break;
	case OW_POWER_SET_DACS:
	case OW_POWER_RAW_DAC:
		ow_refuse(uartResp, OW_CODE_FDA_REFUSED, "raw DAC writes are refused in FDA mode");
		break;
	default:   // HV on with nothing selected
		ow_refuse(uartResp, OW_CODE_NO_PRESET, "no preset selected");
		break;
#else
	case OW_POWER_PRESET_WRITE:
		preset_write(uartResp, cmd);
		break;
	case OW_POWER_PRESET_COMMIT:
		preset_commit(uartResp, cmd);
		break;
	default:
		break;
#endif
	}
	return true;
}
