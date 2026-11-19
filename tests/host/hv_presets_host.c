/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host build of Core/Src/hv_presets.c for test_hv_presets.py, compiled with
 * and without FDA_MODE. The preset page (page 61, preset_store.h) is a 2 KB
 * array that behaves like flash: erase sets every byte to 0xFF, programming
 * can only clear bits. argv[1], if given, is a package file loaded into it.
 * Reads one command per line on stdin and prints the reply:
 *
 *   get INDEX                      OW_POWER_PRESET_GET
 *   select INDEX CRC SENSITIVITY   OW_POWER_PRESET_SELECT (CRC hex)
 *   clear                          OW_POWER_PRESET_SELECT, no payload
 *   info                           OW_POWER_PRESET_INFO
 *   write OFFSET HEX               OW_POWER_PRESET_WRITE (offset decimal, bytes hex)
 *   commit LEN CRC                 OW_POWER_PRESET_COMMIT (len decimal, CRC hex)
 *   hvon                           OW_POWER_HV_ON
 *   sethv                          OW_POWER_SET_HV
 *   rail 0|1                       what getHVOnStatus() reports
 *   nopage                         no preset page (a bootloader before 1.1.0-rc.2)
 *   page                           print the page, hex
 *
 * Each reply is "<handled> <packet_type> <reserved> <data hex>"; select also
 * prints the voltage hv_set_voltage() was last given.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hv_supply.h"
#include "preset_store.h"

#define PAGE_SIZE 2048u

static bool rail_on;
static float last_set = -1.0f;
static uint8_t page[PAGE_SIZE];
static bool have_page = true;

bool getHVOnStatus(void) { return rail_on; }
bool hv_set_voltage(float value) { last_set = value; return true; }
// hv_monitor.c is the device's; here the setpoint calls only need to link.
void hv_monitor_set_point(float v_set, float range_min, float range_max)
{
	(void)v_set; (void)range_min; (void)range_max;
}
void hv_monitor_clear_fault(void) {}

// preset_store.c is the device's flash side; this is the same contract on RAM.
const uint8_t *preset_store_page(void) { return have_page ? page : NULL; }

const uint8_t *preset_store_body(uint32_t *size)
{
	*size = 0;
	if (!have_page || memcmp(page, PRESET_PKG_MAGIC, 4) != 0) return NULL;
	uint32_t n = (uint32_t)page[8] | ((uint32_t)page[9] << 8) | ((uint32_t)page[10] << 16) | ((uint32_t)page[11] << 24);
	if (n == 0 || n > PAGE_SIZE - PRESET_PKG_HEADER_LEN) return NULL;
	*size = n;
	return &page[PRESET_PKG_HEADER_LEN];
}

#ifndef FDA_MODE
bool preset_store_erase(void)
{
	if (!have_page) return false;
	memset(page, 0xFF, sizeof page);
	return true;
}

bool preset_store_program(uint32_t offset, const uint8_t *data, uint32_t len)
{
	if (!have_page || (offset & 3u) || offset > PAGE_SIZE || len > PAGE_SIZE - offset) return false;
	for (uint32_t i = 0; i < len; i++) {
		if (page[offset + i] != 0xFF) return false;   // flash: written bytes stay written
		page[offset + i] = data[i];
	}
	return true;
}
#endif

#include "../../Core/Src/hv_presets.c"

static void reply(bool handled, const UartPacket *r)
{
	printf("%d %02x %02x ", handled ? 1 : 0, r->packet_type, r->reserved);
	for (uint16_t i = 0; i < r->data_len; i++) printf("%02x", r->data[i]);
	printf(" %.6f\n", (double)last_set);
	fflush(stdout);
}

static int hex_bytes(const char *hex, uint8_t *out, int max)
{
	int n = 0;
	while (hex[0] && hex[1] && hex[0] != '\n' && n < max) {
		unsigned b;
		if (sscanf(hex, "%2x", &b) != 1) break;
		out[n++] = (uint8_t)b;
		hex += 2;
	}
	return n;
}

int main(int argc, char **argv)
{
	memset(page, 0xFF, sizeof page);
	if (argc > 1) {
		FILE *f = fopen(argv[1], "rb");
		if (!f) return 3;
		(void)fread(page, 1, sizeof page, f);
		fclose(f);
	}
	static char line[8192];
	while (fgets(line, sizeof line, stdin)) {
		UartPacket cmd = { .packet_type = OW_CMD, .command = 0 };
		UartPacket resp = { .packet_type = OW_RESP };
		static uint8_t payload[2 + PAGE_SIZE];
		unsigned index = 0, crc = 0, off = 0, len = 0;
		float sens = 0.0f;
		int on = 0;
		char hex[sizeof line];

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
		} else if (strncmp(line, "info", 4) == 0) {
			cmd.command = OW_POWER_PRESET_INFO;
		} else if (sscanf(line, "write %u %s", &off, hex) == 2) {
			cmd.command = OW_POWER_PRESET_WRITE;
			payload[0] = (uint8_t)off;
			payload[1] = (uint8_t)(off >> 8);
			cmd.data = payload;
			cmd.data_len = (uint16_t)(2 + hex_bytes(hex, &payload[2], PAGE_SIZE));
		} else if (sscanf(line, "commit %u %x", &len, &crc) == 2) {
			cmd.command = OW_POWER_PRESET_COMMIT;
			put_u32(payload, len);
			put_u32(&payload[4], crc);
			cmd.data = payload;
			cmd.data_len = 8;
		} else if (strncmp(line, "hvon", 4) == 0) {
			cmd.command = OW_POWER_HV_ON;
		} else if (strncmp(line, "sethv", 5) == 0) {
			cmd.command = OW_POWER_SET_HV;
		} else if (sscanf(line, "rail %d", &on) == 1) {
			rail_on = on != 0;
			continue;
		} else if (strncmp(line, "nopage", 6) == 0) {
			have_page = false;
			continue;
		} else if (strncmp(line, "page", 4) == 0) {
			for (unsigned i = 0; i < sizeof page; i++) printf("%02x", page[i]);
			printf("\n");
			fflush(stdout);
			continue;
		} else {
			fprintf(stderr, "bad command: %s", line);
			return 2;
		}
		reply(hv_presets_process(&resp, &cmd), &resp);
	}
	return 0;
}
