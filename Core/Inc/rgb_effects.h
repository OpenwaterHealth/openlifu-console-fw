/* SPDX-License-Identifier: AGPL-3.0-or-later */
/**
  ******************************************************************************
  * @file    rgb_effects.h
  * @brief   Timer-interrupt-driven animation layer on top of the rgb_led
  *          driver. STM32F0 port.
  *
  *          A dedicated timer (TIM7 in this project) fires at
  *          RGB_EFFECT_RATE_HZ and steps the active effect from its ISR.
  *          The timer runs ONLY while an effect is active: starting an
  *          effect arms it, and a completed fade or RGB_EffectStop()
  *          disarms it again - so a static color or dark LED costs zero
  *          interrupts and zero CPU, exactly like the bare driver.
  *
  *          Each timer tick costs ~10 us worst case (one 64-slot pattern
  *          rewrite at 48 MHz), i.e. ~0.05% CPU at 50 Hz while animating.
  ******************************************************************************
  */

#ifndef RGB_EFFECTS_H
#define RGB_EFFECTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f0xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/* Effect step rate. 50 Hz is smooth to the eye and one quarter of the
 * 200 Hz PWM refresh. */
#define RGB_EFFECT_RATE_HZ   50U

/**
  * @brief  Bind the effects engine to a basic timer and reconfigure it to
  *         RGB_EFFECT_RATE_HZ. The timer is left stopped; effects start it
  *         on demand. Call once after MX_TIMx_Init(), e.g.
  *         RGB_EffectsInit(&htim7).
  * @retval HAL status
  */
HAL_StatusTypeDef RGB_EffectsInit(TIM_HandleTypeDef *htim);

/**
  * @brief  Advance the active effect by one frame. Call from the bound
  *         timer's update interrupt handler.
  */
void RGB_EffectTimerStep(void);

/**
  * @brief  Smoothly fade from the current color to (r,g,b) over ms
  *         milliseconds, then hold (the timer stops itself on arrival).
  *         Fading to (0,0,0) is a smooth off.
  */
void RGB_FadeTo(uint8_t r, uint8_t g, uint8_t b, uint32_t ms);

/**
  * @brief  Breathe the given color: brightness ramps 0 -> full -> 0 over
  *         period_ms, repeating until another effect or stop is requested.
  */
void RGB_Breathe(uint8_t r, uint8_t g, uint8_t b, uint32_t period_ms);

/**
  * @brief  Sweep the full hue wheel at maximum saturation, one revolution
  *         every period_ms, repeating.
  */
void RGB_Rainbow(uint32_t period_ms);

/**
  * @brief  Flash the given color: 50% duty on/off blink with a full cycle
  *         of period_ms (e.g. 2000 = 1 s on, 1 s off), repeating.
  */
void RGB_Flash(uint8_t r, uint8_t g, uint8_t b, uint32_t period_ms);

/* Maximum colors RGB_ColorCycle() can hold (the list is copied) */
#define RGB_CYCLE_MAX_COLORS  8U

/**
  * @brief  Step through a list of colors, showing each for dwell_ms, then
  *         wrapping. colors is an array of {r,g,b} triplets; count above
  *         RGB_CYCLE_MAX_COLORS is clamped, count 0 stops the effect.
  */
void RGB_ColorCycle(const uint8_t colors[][3], uint8_t count,
                    uint32_t dwell_ms);

/**
  * @brief  Cancel any running effect, stop the effect timer and hold the
  *         last displayed color. Follow with RGB_SetColor() for direct
  *         static control.
  */
void RGB_EffectStop(void);

#ifdef __cplusplus
}
#endif

#endif /* RGB_EFFECTS_H */
