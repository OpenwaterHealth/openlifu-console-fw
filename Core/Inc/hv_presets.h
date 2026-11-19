/*
 * hv_presets.h
 *
 * Presets: each one's HV entry — its voltage, the transmitter sensitivity
 * that voltage suits, the settings_crc of its .json, and its voltage range —
 * read from the preset page (page 61, preset_store.h), not linked into the
 * image. One firmware release serves every build:
 *
 *   all builds   OW_POWER_PRESET_GET / _SELECT / _INFO read and select presets.
 *   research,    OW_POWER_PRESET_WRITE / _COMMIT write a package into the page.
 *   bare metal   The host may still set the HV voltage directly.
 *   FDA_MODE     The clinical build: the page is read-only, the HV setpoint
 *                comes only from the selected preset, HV stays off until a
 *                select succeeds, and OW_POWER_SET_HV and the raw DAC
 *                commands are refused.
 *
 * No record may reach above HV_PRESET_CEILING_VOLTS.
 *
 * OW_POWER_PRESET_SELECT (reserved = preset index) gives the settings_crc and
 * the connected transmitter's sensitivity. The console then applies
 *
 *     voltage x preset sensitivity / transmitter sensitivity
 *
 * if that is inside the preset's range. The selection cannot change while HV
 * is on. A refused select, or one with no payload, leaves nothing selected.
 */

#ifndef INC_HV_PRESETS_H_
#define INC_HV_PRESETS_H_

#include "common.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * PRESET_HV_RECORD, little-endian (the SDK's console_preset_record(version=2)):
 *   u8  magic[4]      "OWHV"
 *   u8  version       PRESET_HV_VERSION
 *   u8  id_len        1..HV_PRESET_ID_MAX
 *   u8  reserved[2]   0
 *   u32 settings_crc  CRC-32 of the preset .json
 *   f32 voltage       V, for a transmitter of this sensitivity
 *   f32 sensitivity
 *   f32 voltage_min   the preset's voltage_range, V
 *   f32 voltage_max
 *   u8  id[id_len]    ASCII, not NUL-terminated
 *   u32 crc           CRC-32 of everything before it
 */
#define PRESET_HV_VERSION    2u
#define PRESET_HV_HEADER     28u
#define HV_PRESET_ID_MAX     64u

// The highest voltage any record's range may reach, in every image; one that
// reaches higher is not loaded. The lower bound is each record's own (P-10,
// console-fw#21).
#define HV_PRESET_CEILING_VOLTS 45.0f

#define HV_PRESET_NONE 0xFFu

/* OW_POWER_PRESET_INFO: reply reserved byte; data = the 320-byte package
 * header when PRESET_INFO_OK. */
#define PRESET_INFO_OK       0x00u  /* a package whose records all parse */
#define PRESET_INFO_EMPTY    0x01u  /* no valid package in the page */
#define PRESET_INFO_NO_PAGE  0x02u  /* no preset page: bootloader before 1.1.0-rc.2 */

/** Answer the preset commands and refuse what FDA mode forbids. False lets the command through. */
bool hv_presets_process(UartPacket *uartResp, const UartPacket *cmd);

#endif /* INC_HV_PRESETS_H_ */
