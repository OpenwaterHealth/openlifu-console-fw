/*
 * preset_store.c — the preset page (see preset_store.h).
 */
#include "preset_store.h"

#include "memory_map.h"

#include <string.h>

#ifndef FDA_MODE
#include "flash_eeprom.h"
#endif

static uint32_t rd_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

const uint8_t *preset_store_page(void)
{
#ifdef BARE_METAL
	return (const uint8_t *)PRESET_STORE_ADDRESS;
#else
	/* bl_info_t: magic, layout u16, reserved u16, slot_base, slot_end,
	 * preset_base, preset_size, caps, reserved (open-lifu-console-bl). */
	const uint8_t *info = (const uint8_t *)BL_INFO_ADDRESS;
	if (rd_u32(&info[0]) != BL_INFO_MAGIC || (info[4] | (info[5] << 8)) < 1U) return NULL;
	if (rd_u32(&info[16]) != PRESET_STORE_ADDRESS || rd_u32(&info[20]) != PRESET_STORE_SIZE) return NULL;
	if (rd_u32(&info[12]) > PRESET_STORE_ADDRESS) return NULL;   /* the slot must end below it */
	return (const uint8_t *)PRESET_STORE_ADDRESS;
#endif
}

const uint8_t *preset_store_body(uint32_t *size)
{
	const uint8_t *page = preset_store_page();
	*size = 0U;
	if (page == NULL || memcmp(page, PRESET_PKG_MAGIC, 4U) != 0) return NULL;
	uint32_t n = rd_u32(&page[PRESET_PKG_SIZE_OFFSET]);
	if (n == 0U || n > PRESET_STORE_SIZE - PRESET_PKG_HEADER_LEN) return NULL;
	*size = n;
	return &page[PRESET_PKG_HEADER_LEN];
}

#ifndef FDA_MODE
bool preset_store_erase(void)
{
	if (preset_store_page() == NULL) return false;
	return Flash_Erase(PRESET_STORE_ADDRESS, PRESET_STORE_ADDRESS + PRESET_STORE_SIZE) == HAL_OK;
}

bool preset_store_program(uint32_t offset, const uint8_t *data, uint32_t len)
{
	if (preset_store_page() == NULL || (offset & 3U) != 0U
	    || offset > PRESET_STORE_SIZE || len > PRESET_STORE_SIZE - offset) return false;
	for (uint32_t i = 0U; i < len; i += 4U) {
		uint32_t word = 0xFFFFFFFFu;
		memcpy(&word, &data[i], (len - i) < 4U ? (len - i) : 4U);
		if (Flash_Write(PRESET_STORE_ADDRESS + offset + i, &word, 1U) != HAL_OK) return false;
	}
	return memcmp((const void *)(PRESET_STORE_ADDRESS + offset), data, len) == 0;
}
#endif /* FDA_MODE */
