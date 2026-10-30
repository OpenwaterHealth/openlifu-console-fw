/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host build of Core/Src/hv_presets.c for test_hv_presets.py. Reads one
 * command per line on stdin and prints the reply:
 *
 *   get INDEX                      OW_POWER_PRESET_GET
 *   select INDEX CRC SENSITIVITY   OW_POWER_PRESET_SELECT (CRC hex)
 *   clear                          OW_POWER_PRESET_SELECT, no payload
 *   hvon                           OW_POWER_HV_ON
 *   sethv                          OW_POWER_SET_HV
 *   rail 0|1                       what getHVOnStatus() reports
 *
 * Each reply is "<handled> <packet_type> <reserved> <data hex>"; select also
 * prints the voltage hv_set_voltage() was last given.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hv_supply.h"

static bool rail_on;
static float last_set = -1.0f;

bool getHVOnStatus(void) { return rail_on; }
bool hv_set_voltage(float value) { last_set = value; return true; }

// The firmware reads the records from the linker's _sfda_preset; here that is
// the generated array itself.
#ifdef HV_PRESET_TABLE_IMAGE
#define _sfda_preset PRESET_HV_TABLE
#else
#define _sfda_preset FDA_PRESET_HV
#endif
#include "../../Core/Src/hv_presets.c"

static void reply(bool handled, const UartPacket *r)
{
	printf("%d %02x %02x ", handled ? 1 : 0, r->packet_type, r->reserved);
	for (uint16_t i = 0; i < r->data_len; i++) printf("%02x", r->data[i]);
	printf(" %.6f\n", (double)last_set);
	fflush(stdout);
}

int main(void)
{
	char line[128];
	while (fgets(line, sizeof line, stdin)) {
		UartPacket cmd = { .packet_type = OW_CMD, .command = 0 };
		UartPacket resp = { .packet_type = OW_RESP };
		uint8_t payload[8];
		unsigned index = 0, crc = 0;
		float sens = 0.0f;
		int on = 0;

		if (sscanf(line, "get %u", &index) == 1) {
			cmd.command = OW_POWER_PRESET_GET;
			cmd.reserved = (uint8_t)index;
		} else if (sscanf(line, "select %u %x %f", &index, &crc, &sens) == 3) {
			cmd.command = OW_POWER_PRESET_SELECT;
			cmd.reserved = (uint8_t)index;
			put_u32(payload, crc);
			memcpy(&payload[4], &sens, 4);
			cmd.data = payload;
			cmd.data_len = 8;
		} else if (strncmp(line, "clear", 5) == 0) {
			cmd.command = OW_POWER_PRESET_SELECT;
		} else if (strncmp(line, "hvon", 4) == 0) {
			cmd.command = OW_POWER_HV_ON;
		} else if (strncmp(line, "sethv", 5) == 0) {
			cmd.command = OW_POWER_SET_HV;
		} else if (sscanf(line, "rail %d", &on) == 1) {
			rail_on = on != 0;
			continue;
		} else {
			fprintf(stderr, "bad command: %s", line);
			return 2;
		}
		reply(hv_presets_process(&resp, &cmd), &resp);
	}
	return 0;
}
