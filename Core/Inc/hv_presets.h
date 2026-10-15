/*
 * hv_presets.h
 *
 * FDA mode: the HV setpoint belongs to the preset, not the host. The image
 * carries the one preset's HV entry -- its voltage, the transmitter
 * sensitivity that voltage is right for, and the settings_crc of its .json --
 * in the last page of the application slot (page 61, the FDA linker scripts'
 * FDA_PRESET region), from Core/Inc/presets/fda_preset.h, which the openlifu
 * SDK's examples/generate_presets.py writes. Flashing or updating the image
 * writes it; the firmware only reads it.
 *
 * The host selects it with OW_POWER_PRESET_SELECT, giving its settings_crc, so
 * a host holding another .json is refused, and the connected transmitter's
 * sensitivity. The console applies
 *
 *     voltage x preset sensitivity / transmitter sensitivity
 *
 * unless that is outside FDA_HV_MIN_VOLTS..FDA_HV_MAX_VOLTS. OW_POWER_SET_HV
 * and the raw DAC commands are refused, HV cannot be turned on until the
 * preset is selected, and the selection cannot change while HV is on. A
 * select that is refused leaves nothing selected, and one with no payload
 * clears the selection: the host does that when the transmitter did not load
 * the preset, so HV never comes on for a preset only the console holds.
 *
 * Nothing here exists unless the image is built with FDA_MODE.
 */

#ifndef INC_HV_PRESETS_H_
#define INC_HV_PRESETS_H_

#ifdef FDA_MODE

#include "common.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * PRESET_HV_RECORD, little-endian, as the SDK writes it
 * (LIFUTXPresets.console_preset_record):
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

/**
 * @brief  Apply the FDA preset rules to a power command.
 *
 * Answers OW_POWER_PRESET_GET / OW_POWER_PRESET_SELECT and refuses what FDA
 * mode forbids.
 *
 * @return true if cmd was handled here and uartResp filled in; false to let
 *         the normal command processing run.
 */
bool hv_presets_process(UartPacket *uartResp, const UartPacket *cmd);

#endif /* FDA_MODE */

#endif /* INC_HV_PRESETS_H_ */
