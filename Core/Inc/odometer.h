/*
 * odometer.h
 *
 * Power-on and HV-on odometers for the console.
 *
 * The STM32F072 has no spare flash page and the board has no EEPROM, so the
 * odometer shares flash page 62 with the runtime config (lifu_config.c):
 *
 *   0x0801F000  lower 1 KB  lifu_config block (header + JSON, see lifu_config.h)
 *   0x0801F400  upper 1 KB  odometer log: ODO_LOG_SLOTS x 16-byte records
 *
 * The log is append-only: each save programs the next blank slot, with no
 * erase. Only when the log is full (or the config is rewritten) is the page
 * erased; lifu_cfg_rewrite_page() then re-programs the config and calls
 * Odometer_RestoreAfterErase() to put the newest record back in slot 0.
 *
 * Wear: at the 10-minute save cadence the page is erased about once per
 * ~10.7 h of power-on, so its 10k-cycle endurance lasts ~12 years of 24/7 use.
 */

#ifndef INC_ODOMETER_H_
#define INC_ODOMETER_H_

#include "stm32f0xx_hal.h"
#include "memory_map.h"
#include <stdint.h>
#include <stdbool.h>

#define ODO_LOG_ADDR      (ADDR_FLASH_PAGE_62 + 0x400U)
#define ODO_RECORD_SIZE   16U
#define ODO_LOG_SLOTS     (0x400U / ODO_RECORD_SIZE)   /* 64 */

#define ODO_RECORD_MAGIC  0x4F44U  /* "OD" */

/* On-flash record layout. Exactly ODO_RECORD_SIZE bytes, word aligned so it
 * can be handed straight to Flash_Write(). */
typedef struct __attribute__((packed, aligned(4))) {
    uint16_t magic;          /* ODO_RECORD_MAGIC */
    uint16_t seq;            /* wrapping save sequence; newest wins */
    uint32_t power_on_s;     /* seconds the console firmware has been running */
    uint32_t hv_on_s;        /* seconds the HV supply has been enabled */
    uint16_t reserved;       /* 0 for now */
    uint16_t crc16;          /* CRC16-CCITT over the preceding 14 bytes */
} OdoRecord_t;

_Static_assert(sizeof(OdoRecord_t) == ODO_RECORD_SIZE,
               "OdoRecord_t must be exactly one log slot");

/* Periodic save cadence while powered. */
#define ODO_SAVE_INTERVAL_MS     (10UL * 60UL * 1000UL)  /* 10 minutes */

/* HV-off and USB-port-close edges request an early save, but saves are never
 * closer together than this, so a script toggling HV cannot wear the page. */
#define ODO_MIN_SAVE_SPACING_MS  (2UL * 60UL * 1000UL)   /* 2 minutes */

/* Reset targets for Odometer_Reset() / OW_POWER_RESET_ODO. */
typedef enum {
    ODO_RESET_POWER = 0,
    ODO_RESET_HV    = 1,
    ODO_RESET_BOTH  = 2,
} OdoResetTarget;

/* Load the newest valid record from the log. Must run before the first
 * lifu_cfg_get(): a config load that rewrites page 62 restores the odometer
 * from RAM, so RAM has to hold the persisted totals by then. */
void Odometer_Init(void);

/* Accumulate elapsed power-on / HV-on time and save when due. Call from the
 * main-context idle loop; never from an ISR (it may program flash). */
void Odometer_Service(void);

/* Save now, ignoring the spacing limit. For rare events that end the run,
 * such as a commanded reset or DFU entry. */
HAL_StatusTypeDef Odometer_Flush(void);

/* Reset one or both odometers to zero and save. */
HAL_StatusTypeDef Odometer_Reset(OdoResetTarget target);

/* Live totals: persisted value plus time accumulated since. */
uint32_t Odometer_Get_PowerOn_Seconds(void);
uint32_t Odometer_Get_HvOn_Seconds(void);

/* Called by lifu_cfg_rewrite_page() right after it erased page 62 and
 * re-programmed the config: writes the current totals into slot 0. */
HAL_StatusTypeDef Odometer_RestoreAfterErase(void);

#endif /* INC_ODOMETER_H_ */
