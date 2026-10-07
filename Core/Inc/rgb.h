/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * rgb.h
 *
 *  Created on: Dec 3, 2025
 *      Author: GitHub Copilot
 */

#ifndef INC_RGB_H_
#define INC_RGB_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RGB LED state definitions */
typedef enum {
    RGB_OFF   = 0,  // All LEDs off
    RGB_RED   = 1,  // Red LED on
    RGB_GREEN = 2,  // Green LED on
    RGB_BLUE  = 3   // Blue LED on
} RGB_State_t;

/**
 * @brief Initialize the RGB LED module
 * @note Sets the initial RGB state to GREEN
 */
void RGB_Init(void);

/**
 * @brief Set the RGB LED state
 * @param state The desired RGB LED state (RGB_OFF, RGB_RED, RGB_GREEN, RGB_BLUE)
 * @retval 0 on success, -1 on invalid state
 */
int8_t RGB_Set(uint8_t state);

/**
 * @brief Get the current RGB LED state
 * @retval Current RGB LED state
 */
uint8_t RGB_Get(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_RGB_H_ */
