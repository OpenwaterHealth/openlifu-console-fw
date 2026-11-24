/*
 * preset_store.h
 *
 * The preset page: page 61, right above the application slot, outside it
 * (open-lifu-console-bl >= 1.1.0-rc.2). It holds one signed preset package:
 *
 *   0x000  PRS1 header, the SBSFU header layout (320 bytes): magic "PRS1",
 *          the body size (FwSize, offset 8), the body's SHA-256 (FwTag), the
 *          diathermy commit and context, and the signature of the first 128
 *          bytes with the same key as the firmware
 *   0x140  body: PRESET_HV_RECORD v2 records back to back (hv_presets.h)
 *
 * The bootloader writes it over DFU only after the header verifies. Research
 * and bare-metal builds can also write it (OW_POWER_PRESET_WRITE/COMMIT,
 * hv_presets.c); the clinical (FDA_MODE) build only reads it. This module
 * finds the page and does the flash I/O; hv_presets.c owns the protocol.
 */

#ifndef INC_PRESET_STORE_H_
#define INC_PRESET_STORE_H_

#include <stdbool.h>
#include <stdint.h>

#define PRESET_PKG_MAGIC        "PRS1"
#define PRESET_PKG_HEADER_LEN   320u   /* the body starts here */
#define PRESET_PKG_SIZE_OFFSET  8u     /* FwSize: u32 LE, the body size */

/* The preset page, or NULL when this unit has none: a bootloader before
 * 1.1.0-rc.2 (no info block), whose slot still covers page 61. Bare-metal
 * builds have no bootloader and always have the page. */
const uint8_t *preset_store_page(void);

/* The stored package's body and its size, or NULL when the page holds no
 * package (no page, no PRS1 magic, or a body size that does not fit). Does
 * not check the records: hv_presets.c parses those. */
const uint8_t *preset_store_body(uint32_t *size);

#ifndef FDA_MODE
/* Research and bare-metal builds only: the flash side of a preset write. */
bool preset_store_erase(void);
/* Program len bytes at offset into the (erased) page; a partial last word is
 * padded with 0xFF. Offset must be word-aligned and the bytes inside the page. */
bool preset_store_program(uint32_t offset, const uint8_t *data, uint32_t len);
#endif

#endif /* INC_PRESET_STORE_H_ */
