/*
 * hv_presets.h
 *
 * FDA mode: the HV setpoint comes from the preset, not the host. The image
 * carries each preset's HV entry at page 61: its voltage, the transmitter
 * sensitivity that voltage suits, and the settings_crc of its .json.
 *
 *   +fda  one PRESET_HV_RECORD (Core/Inc/presets/fda_preset.h).
 *   +test PRESET_HV_COUNT records back to back, in the transmitter's preset
 *         order (Core/Inc/presets/preset_hv_table.h).
 *
 * Each record carries its preset's voltage_range, and a select is held to it.
 * No record may reach above HV_PRESET_CEILING_VOLTS.
 *
 * OW_POWER_PRESET_SELECT (reserved = preset index) gives the settings_crc and
 * the connected transmitter's sensitivity. The console then applies
 *
 *     voltage x preset sensitivity / transmitter sensitivity
 *
 * if that is inside the preset's range. HV stays off until a select succeeds,
 * and the selection cannot change while HV is on. A refused select, or one
 * with no payload, leaves nothing selected. OW_POWER_SET_HV and the raw DAC
 * commands are refused.
 */

#ifndef INC_HV_PRESETS_H_
#define INC_HV_PRESETS_H_

#ifdef FDA_MODE

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

/** Answer the preset commands and refuse what FDA mode forbids. False lets the command through. */
bool hv_presets_process(UartPacket *uartResp, const UartPacket *cmd);

#endif /* FDA_MODE */

#endif /* INC_HV_PRESETS_H_ */
