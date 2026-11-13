/*
 * odometer.c
 *
 * Power-on / HV-on odometers persisted in the upper 1 KB of flash page 62
 * (see odometer.h for the layout and wear budget).
 *
 * Time is accumulated in RAM from HAL_GetTick() deltas in Odometer_Service(),
 * which the comms task calls while it waits for the next command. HV-on time
 * is the time HV_SHUTDOWN was low (getHVOnStatus()), sampled on every pass.
 * A save appends one record to the next blank log slot; a full log (or a slot
 * that is not blank, e.g. the zero padding left by firmware that predates the
 * odometer) is handled by rewriting the whole page through lifu_config.
 */

#include "odometer.h"
#include "lifu_config.h"
#include "flash_eeprom.h"
#include "hv_supply.h"
#include "usb_events.h"
#include "utils.h"
#include "dbg_print.h"

#include <stddef.h>

/* Live totals and their sub-second remainders. */
static uint32_t s_power_on_s;
static uint32_t s_hv_on_s;
static uint32_t s_power_ms;
static uint32_t s_hv_ms;
static uint32_t s_last_tick;

/* State seen on the previous Odometer_Service() pass, for edge detection. */
static bool s_hv_was_on;
static bool s_port_was_open;

/* Save bookkeeping. s_next_slot is the first slot after the newest used one
 * (ODO_LOG_SLOTS when the log is full); s_seq is the newest record's seq. */
static uint32_t s_last_save_tick;
static bool     s_save_requested;
static uint32_t s_saved_power_on_s;
static uint32_t s_saved_hv_on_s;
static uint32_t s_next_slot;
static uint16_t s_seq;
static bool     s_initialized;

static uint32_t odo_slot_addr(uint32_t slot)
{
    return ODO_LOG_ADDR + slot * ODO_RECORD_SIZE;
}

static void odo_read_slot(uint32_t slot, OdoRecord_t *rec)
{
    (void)Flash_Read(odo_slot_addr(slot), (uint32_t *)rec,
                     sizeof(OdoRecord_t) / sizeof(uint32_t));
}

static bool odo_slot_blank(const OdoRecord_t *rec)
{
    const uint32_t *w = (const uint32_t *)rec;
    for (uint32_t i = 0; i < sizeof(OdoRecord_t) / sizeof(uint32_t); ++i) {
        if (w[i] != 0xFFFFFFFFU) {
            return false;
        }
    }
    return true;
}

static bool odo_record_valid(const OdoRecord_t *rec)
{
    return rec->magic == ODO_RECORD_MAGIC &&
           rec->crc16 == util_crc16((const uint8_t *)rec, offsetof(OdoRecord_t, crc16));
}

/* Program the current totals into a blank slot. The slot counts as used
 * whatever the outcome, so a failed program is never retried in place. */
static HAL_StatusTypeDef odo_program_slot(uint32_t slot)
{
    OdoRecord_t rec;
    rec.magic = ODO_RECORD_MAGIC;
    rec.seq = (uint16_t)(s_seq + 1U);
    rec.power_on_s = s_power_on_s;
    rec.hv_on_s = s_hv_on_s;
    rec.reserved = 0;
    rec.crc16 = util_crc16((const uint8_t *)&rec, offsetof(OdoRecord_t, crc16));

    s_next_slot = slot + 1U;

    HAL_StatusTypeDef st = Flash_Write(odo_slot_addr(slot), (uint32_t *)&rec,
                                       sizeof(rec) / sizeof(uint32_t));
    if (st != HAL_OK) {
        DBG_PRINTF("Odometer: write to slot %lu failed (%d)\r\n", (unsigned long)slot, st);
        return st;
    }

    s_seq = rec.seq;
    s_saved_power_on_s = rec.power_on_s;
    s_saved_hv_on_s = rec.hv_on_s;
    return HAL_OK;
}

/* Fold the time since the last call into the totals. HV-on time is charged
 * by the HV state seen on the previous pass. */
static void odo_accumulate(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t elapsed = now - s_last_tick;   /* unsigned: survives tick wrap */
    if (elapsed == 0U) {
        return;
    }
    s_last_tick = now;

    s_power_ms += elapsed;
    if (s_hv_was_on) {
        s_hv_ms += elapsed;
    }
    s_power_on_s += s_power_ms / 1000U;
    s_power_ms %= 1000U;
    s_hv_on_s += s_hv_ms / 1000U;
    s_hv_ms %= 1000U;
}

static HAL_StatusTypeDef odo_save(void)
{
    s_last_save_tick = HAL_GetTick();
    s_save_requested = false;

    if (s_power_on_s == s_saved_power_on_s && s_hv_on_s == s_saved_hv_on_s) {
        return HAL_OK;
    }

    /* A long config written by older firmware owns the whole page; counts
     * stay RAM-only until the config is rewritten shorter. */
    if (!lifu_cfg_fits_shared_page()) {
        return HAL_ERROR;
    }

    if (s_next_slot < ODO_LOG_SLOTS) {
        OdoRecord_t rec;
        odo_read_slot(s_next_slot, &rec);
        if (odo_slot_blank(&rec)) {
            return odo_program_slot(s_next_slot);
        }
    }

    /* Log full, or the next slot is not blank: erase and start over. The
     * rewrite re-programs the config, then calls Odometer_RestoreAfterErase(). */
    return lifu_cfg_rewrite_page();
}

void Odometer_Init(void)
{
    OdoRecord_t newest = {0};
    bool found = false;

    s_next_slot = 0;
    for (uint32_t i = 0; i < ODO_LOG_SLOTS; ++i) {
        OdoRecord_t rec;
        odo_read_slot(i, &rec);
        if (!odo_slot_blank(&rec)) {
            s_next_slot = i + 1U;
        }
        if (odo_record_valid(&rec) &&
            (!found || (int16_t)(rec.seq - newest.seq) > 0)) {
            newest = rec;
            found = true;
        }
    }

    if (found) {
        s_power_on_s = newest.power_on_s;
        s_hv_on_s = newest.hv_on_s;
        s_seq = newest.seq;
    } else {
        s_power_on_s = 0;
        s_hv_on_s = 0;
        s_seq = 0;
    }
    s_saved_power_on_s = s_power_on_s;
    s_saved_hv_on_s = s_hv_on_s;
    s_power_ms = 0;
    s_hv_ms = 0;

    s_last_tick = HAL_GetTick();
    s_last_save_tick = s_last_tick;
    s_save_requested = false;
    s_hv_was_on = getHVOnStatus();
    s_port_was_open = usb_is_port_open() != 0U;
    s_initialized = true;

    DBG_PRINTF("Odometer: power-on=%lu s, HV-on=%lu s (seq %u, next slot %lu)\r\n",
               (unsigned long)s_power_on_s, (unsigned long)s_hv_on_s,
               (unsigned)s_seq, (unsigned long)s_next_slot);
}

void Odometer_Service(void)
{
    if (!s_initialized) {
        return;
    }

    odo_accumulate();

    bool hv_on = getHVOnStatus();
    bool port_open = usb_is_port_open() != 0U;
    if ((s_hv_was_on && !hv_on) || (s_port_was_open && !port_open)) {
        s_save_requested = true;
    }
    s_hv_was_on = hv_on;
    s_port_was_open = port_open;

    uint32_t since_save = HAL_GetTick() - s_last_save_tick;
    if (since_save >= ODO_SAVE_INTERVAL_MS ||
        (s_save_requested && since_save >= ODO_MIN_SAVE_SPACING_MS)) {
        (void)odo_save();
    }
}

HAL_StatusTypeDef Odometer_Flush(void)
{
    if (!s_initialized) {
        return HAL_ERROR;
    }
    odo_accumulate();
    return odo_save();
}

HAL_StatusTypeDef Odometer_Reset(OdoResetTarget target)
{
    if (!s_initialized) {
        return HAL_ERROR;
    }
    odo_accumulate();

    switch (target) {
        case ODO_RESET_POWER:
            s_power_on_s = 0;
            s_power_ms = 0;
            break;
        case ODO_RESET_HV:
            s_hv_on_s = 0;
            s_hv_ms = 0;
            break;
        case ODO_RESET_BOTH:
            s_power_on_s = 0;
            s_power_ms = 0;
            s_hv_on_s = 0;
            s_hv_ms = 0;
            break;
        default:
            return HAL_ERROR;
    }
    DBG_PRINTF("Odometer: reset target %d\r\n", (int)target);
    return odo_save();
}

uint32_t Odometer_Get_PowerOn_Seconds(void)
{
    if (s_initialized) {
        odo_accumulate();
    }
    return s_power_on_s;
}

uint32_t Odometer_Get_HvOn_Seconds(void)
{
    if (s_initialized) {
        odo_accumulate();
    }
    return s_hv_on_s;
}

HAL_StatusTypeDef Odometer_RestoreAfterErase(void)
{
    if (!s_initialized) {
        return HAL_OK;
    }
    return odo_program_slot(0);
}
