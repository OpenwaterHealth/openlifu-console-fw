/*
 * hv_presets.h
 *
 * FDA mode: the HV setpoint comes from the preset, not the host. The image
 * carries the preset's HV entry at page 61 (Core/Inc/presets/fda_preset.h):
 * its voltage, the transmitter sensitivity that voltage suits, and the
 * settings_crc of its .json.
 *
 * OW_POWER_PRESET_SELECT gives the settings_crc and the connected
 * transmitter's sensitivity. The console then applies
 *
 *     voltage x preset sensitivity / transmitter sensitivity
 *
 * if that is inside FDA_HV_MIN_VOLTS..FDA_HV_MAX_VOLTS. HV stays off until a
 * select succeeds, and the selection cannot change while HV is on. A refused
 * select, or one with no payload, leaves nothing selected. OW_POWER_SET_HV and
 * the raw DAC commands are refused.
 */

#ifndef INC_HV_PRESETS_H_
#define INC_HV_PRESETS_H_

#ifdef FDA_MODE

#include "common.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * PRESET_HV_RECORD, little-endian (the SDK's console_preset_record()):
 *   u8  magic[4]      "OWHV"
 *   u8  version       PRESET_HV_VERSION
 *   u8  id_len        1..HV_PRESET_ID_MAX
 *   u8  reserved[2]   0
 *   u32 settings_crc  CRC-32 of the preset .json
 *   f32 voltage       V, for a transmitter of this sensitivity
 *   f32 sensitivity
 *   u8  id[id_len]    ASCII, not NUL-terminated
 *   u32 crc           CRC-32 of everything before it
 */
#define PRESET_HV_VERSION    1u
#define PRESET_HV_HEADER     20u
#define HV_PRESET_ID_MAX     64u
#define PRESET_HV_RECORD_MAX (PRESET_HV_HEADER + HV_PRESET_ID_MAX + 4u)

// The only HV setpoints a select may arrive at, in volts.
#define FDA_HV_MIN_VOLTS 35.0f
#define FDA_HV_MAX_VOLTS 45.0f
#define HV_PRESET_NONE 0xFFu

/** Answer the preset commands and refuse what FDA mode forbids. False lets the command through. */
bool hv_presets_process(UartPacket *uartResp, const UartPacket *cmd);

#endif /* FDA_MODE */

#endif /* INC_HV_PRESETS_H_ */
