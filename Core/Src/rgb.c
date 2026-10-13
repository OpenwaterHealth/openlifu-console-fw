/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * rgb.c
 *
 * Compatibility shim over the GPIO+TIM+DMA RGB driver (rgb_led.c) and the
 * effects engine (rgb_effects.c). The original public API (RGB_Init /
 * RGB_Set / RGB_Get with the RGB_OFF..RGB_BLUE enum) is preserved exactly
 * so the host protocol (if_commands.c) and Error_Handler() are untouched.
 *
 * Hardware routing on this board (see docs/rgb_driver_port.md):
 *   - Color engine: TIM16 update -> DMA1_Channel4 (SYSCFG remap) ->
 *     GPIOC BSRR. Zero interrupts, zero CPU in steady state.
 *   - LED is common-anode (330R to +3.3V): active-low, handled inside the
 *     driver - this layer works in logical color space.
 *   - Effects step: TIM7 at 50 Hz (was initialized but unused). Runs only
 *     while an animated effect is active.
 *
 *  Created on: Dec 3, 2025
 *      Author: GitHub Copilot
 */

#include "rgb.h"
#include "main.h"
#include "rgb_led.h"
#include "rgb_effects.h"

extern TIM_HandleTypeDef htim7;   /* CubeMX handle, repurposed for effects */

/* Internal RGB state variable */
static volatile uint8_t rgb_state = RGB_GREEN;

/**
 * @brief Initialize the RGB LED module
 * @note Sets the initial RGB state to GREEN
 */
void RGB_Init(void)
{
    RGB_Config cfg = {
        .r_port = LD_R_GPIO_Port, .r_pin = LD_R_Pin,   /* PC4 */
        .g_port = LD_G_GPIO_Port, .g_pin = LD_G_Pin,   /* PC5 */
        .b_port = LD_B_GPIO_Port, .b_pin = LD_B_Pin,   /* PC6 */
        .timer      = TIM16,
        .refresh_hz = 0,           /* 0 = default 200 Hz */
        .active_low = true,        /* common anode via 330R to +3.3V */
    };

    /* On failure leave the LED dark; do NOT call Error_Handler() here -
     * it calls RGB_Set() back into this module. */
    if (RGB_Init_Driver(&cfg) != HAL_OK)
    {
        rgb_state = RGB_OFF;
        return;
    }

    /* Effects step timer: TIM7 (CubeMX-initialized, otherwise unused).
     * Lowest urgency - the ISR only rewrites the pattern buffer. */
    HAL_NVIC_SetPriority(TIM7_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(TIM7_IRQn);
    if (RGB_EffectsInit(&htim7) != HAL_OK)
    {
        rgb_state = RGB_OFF;
        return;
    }

    rgb_state = RGB_GREEN;
    RGB_Set(rgb_state);
}

/**
 * @brief Set the RGB LED state
 * @param state The desired RGB LED state (RGB_OFF, RGB_RED, RGB_GREEN, RGB_BLUE)
 * @retval 0 on success, -1 on invalid state
 */
int8_t RGB_Set(uint8_t state)
{
    /* Validate input */
    if (state > RGB_BLUE) {
        return -1;
    }

    /* Update state */
    rgb_state = state;

    /* A direct set cancels any running effect, then drives a solid color */
    RGB_EffectStop();
    switch (state)
    {
        case RGB_RED:
            RGB_SetColor(255, 0, 0, true);
            break;
        case RGB_GREEN:
            RGB_SetColor(0, 255, 0, true);
            break;
        case RGB_BLUE:
            RGB_SetColor(0, 0, 255, true);
            break;
        case RGB_OFF:
        default:
            RGB_SetColor(0, 0, 0, false);
            break;
    }

    return 0;
}

/**
 * @brief Get the current RGB LED state
 * @retval Current RGB LED state
 */
uint8_t RGB_Get(void)
{
    return rgb_state;
}
