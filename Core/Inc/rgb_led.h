/* SPDX-License-Identifier: AGPL-3.0-or-later */
/**
  ******************************************************************************
  * @file    rgb_led.h
  * @brief   GPIO + TIM + DMA RGB LED driver (no PWM peripheral, no interrupts).
  *          STM32F0 port.
  *
  *          A timer update event paces a circular DMA stream that writes a
  *          precomputed 256-slot pattern to the GPIO port BSRR register.
  *          Each channel's 8-bit intensity is the number of slots in which
  *          its pin is driven active, giving 256 brightness levels per
  *          channel (16.7M colors) with zero CPU load after setup: no ISRs
  *          fire and the stream reloads itself in hardware forever.
  *
  *          The driver is configuration-driven: pass the port/pin of each
  *          color, the pacing timer and the polarity in RGB_Config.
  *          Hardware constraints enforced by RGB_Init():
  *            - all three pins must live on ONE GPIO port (the DMA stream
  *              writes a single BSRR register);
  *            - on STM32F072 the DMA request routing is fixed per channel
  *              (plus SYSCFG remaps), so only timers with a known-free
  *              channel mapping are accepted: TIM16 (update request
  *              remapped to DMA1_Channel4) or TIM1 (update request on
  *              DMA1_Channel5). Unlike the F4, any DMA channel can reach
  *              GPIO on the F0 - the single AHB bus matrix has no
  *              peripheral-port restriction.
  ******************************************************************************
  */

#ifndef RGB_LED_H
#define RGB_LED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f0xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/* PWM resolution and default refresh rate. DMA transfer rate =
 * RGB_PWM_STEPS * refresh_hz (64 * 200 = 12.8 kHz, one 32-bit AHB write
 * every ~78 us - negligible bus load). Pattern buffer RAM cost is
 * RGB_PWM_STEPS * 4 bytes: 64 steps = 256 B, chosen because this 16 KB
 * part links at ~95% RAM. 64 gamma-corrected levels per channel is ample
 * for an indicator LED; raise to 256 for full 8-bit duty if RAM allows. */
#define RGB_PWM_STEPS           64U
#define RGB_DEFAULT_REFRESH_HZ  200U

/* Perceptual (gamma 2.8) brightness correction; set to 0 for linear duty */
#define RGB_USE_GAMMA           1

typedef struct
{
  GPIO_TypeDef *r_port;     /* port/pin driving the red channel            */
  uint16_t      r_pin;
  GPIO_TypeDef *g_port;     /* port/pin driving the green channel          */
  uint16_t      g_pin;
  GPIO_TypeDef *b_port;     /* port/pin driving the blue channel           */
  uint16_t      b_pin;
  TIM_TypeDef  *timer;      /* pacing timer: TIM16 or TIM1 on STM32F072    */
  uint32_t      refresh_hz; /* PWM refresh rate; 0 = RGB_DEFAULT_REFRESH_HZ */
  bool          active_low; /* true = pin LOW lights the LED (common anode) */
} RGB_Config;

/**
  * @brief  Validate the configuration, set up GPIO, timer and DMA, and
  *         start the (dark) PWM stream. Enables all needed clocks itself.
  * @param  cfg  Pin/timer configuration (copied; may live on the stack)
  * @retval HAL_OK on success, HAL_ERROR on invalid config (mixed GPIO
  *         ports, unsupported timer, bad pins) or HAL failure
  */
HAL_StatusTypeDef RGB_Init_Driver(const RGB_Config *cfg);

/**
  * @brief  Set the RGB color and turn the LED on or off. Polarity is
  *         handled internally - callers always work in logical color
  *         space (255 = fully lit).
  * @param  r,g,b  8-bit channel intensities (0..255)
  * @param  on     true = drive the color, false = LED dark (color is
  *                remembered; a later call with on=true restores it)
  */
void RGB_SetColor(uint8_t r, uint8_t g, uint8_t b, bool on);

/**
  * @brief  Read back the last color set and the on/off state.
  *         Any output pointer may be NULL.
  */
void RGB_GetColor(uint8_t *r, uint8_t *g, uint8_t *b, bool *on);

#ifdef __cplusplus
}
#endif

#endif /* RGB_LED_H */
