/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * ads8678.h
 *
 *  Created on: Nov 12, 2025
 *      Author: gvigelet
 */

#ifndef INC_ADS8678_H_
#define INC_ADS8678_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f0xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

// ADS8678 Command Definitions
#define ADS8678_CMD_NO_OP              0x0000
#define ADS8678_CMD_STDBY              0x8200
#define ADS8678_CMD_PWR_DN             0x8300
#define ADS8678_CMD_RST                0x8500
#define ADS8678_CMD_AUTO_RST           0xA000
#define ADS8678_CMD_MAN_Ch_0           0xC000
#define ADS8678_CMD_MAN_Ch_1           0xC400
#define ADS8678_CMD_MAN_Ch_2           0xC800
#define ADS8678_CMD_MAN_Ch_3           0xCC00
#define ADS8678_CMD_MAN_Ch_4           0xD000
#define ADS8678_CMD_MAN_Ch_5           0xD400
#define ADS8678_CMD_MAN_Ch_6           0xD800
#define ADS8678_CMD_MAN_Ch_7           0xDC00

// Program Register Commands (address in bits 15-9, R/W in bit 8, data in bits 7-0)
#define ADS8678_PROG_REG_WRITE         0x0100
#define ADS8678_PROG_REG_READ          0x0000

// Register Addresses (need to be shifted left by 1 when used)
#define ADS8678_REG_AUTO_SEQ_EN        (0x01 << 1)
#define ADS8678_REG_CHNL_PWR_DWN       (0x02 << 1)
#define ADS8678_REG_FEATURE            (0x03 << 1)
#define ADS8678_REG_RANGE_CH0          (0x05 << 1)
#define ADS8678_REG_RANGE_CH1          (0x06 << 1)
#define ADS8678_REG_RANGE_CH2          (0x07 << 1)
#define ADS8678_REG_RANGE_CH3          (0x08 << 1)
#define ADS8678_REG_RANGE_CH4          (0x09 << 1)
#define ADS8678_REG_RANGE_CH5          (0x0A << 1)
#define ADS8678_REG_RANGE_CH6          (0x0B << 1)
#define ADS8678_REG_RANGE_CH7          (0x0C << 1)
#define ADS8678_REG_ALARM_OVERVIEW     (0x10 << 1)
#define ADS8678_REG_CMD_READ_BAK       (0x3F << 1)

// Range Configuration Values (PGA Gain settings)
typedef enum {
    ADS8678_RANGE_BIPOLAR_2_5V = 0x00,   // ±2.5V * Vref
    ADS8678_RANGE_BIPOLAR_1_25V = 0x01,  // ±1.25V * Vref  
    ADS8678_RANGE_BIPOLAR_0_625V = 0x02, // ±0.625V * Vref
    ADS8678_RANGE_UNIPOLAR_2_5V = 0x05,  // 0 to 2.5V * Vref
    ADS8678_RANGE_UNIPOLAR_1_25V = 0x06, // 0 to 1.25V * Vref (GAIN_1_25: 1.25 * 4.096V = 5.12V)
    ADS8678_RANGE_UNIPOLAR_0_625V = 0x07 // 0 to 0.625V * Vref
} ADS8678_Range_t;

// Number of channels
#define ADS8678_NUM_CHANNELS 8

typedef struct
{
	// Parameters
	SPI_HandleTypeDef* spi;
	uint8_t api_retry_count;
	uint16_t spi_timeout_ms;
	GPIO_TypeDef *cs_port;   uint16_t cs_pin;
	GPIO_TypeDef *rst_port;  uint16_t rst_pin;
	
	// Internal state
	bool initialized;
	ADS8678_Range_t channel_ranges[ADS8678_NUM_CHANNELS];

} ADS8678__HandleTypeDef;


// Initialization and configuration
HAL_StatusTypeDef ADS8678_Init(ADS8678__HandleTypeDef *dev);
HAL_StatusTypeDef ADS8678_Reset(ADS8678__HandleTypeDef *dev);
HAL_StatusTypeDef ADS8678_SetChannelRange(ADS8678__HandleTypeDef *dev, uint8_t channel, ADS8678_Range_t range);
HAL_StatusTypeDef ADS8678_SetAllChannelRanges(ADS8678__HandleTypeDef *dev, ADS8678_Range_t range);

// Data acquisition
HAL_StatusTypeDef ADS8678_ReadChannel(ADS8678__HandleTypeDef *dev, uint8_t channel, uint16_t *result);
HAL_StatusTypeDef ADS8678_ReadChannelManual(ADS8678__HandleTypeDef *dev, uint8_t channel, uint16_t *result);
HAL_StatusTypeDef ADS8678_ReadAllChannels(ADS8678__HandleTypeDef *dev, uint16_t *results);

// Low-level functions
HAL_StatusTypeDef ADS8678_WriteCommand(ADS8678__HandleTypeDef *dev, uint16_t command, uint16_t *response);
HAL_StatusTypeDef ADS8678_WriteRegister(ADS8678__HandleTypeDef *dev, uint16_t reg_addr, uint8_t value);
HAL_StatusTypeDef ADS8678_ReadRegister(ADS8678__HandleTypeDef *dev, uint16_t reg_addr, uint8_t *value);
HAL_StatusTypeDef ADS8678_ReadDeviceID(const ADS8678__HandleTypeDef *dev, uint16_t *device_id);

// Power management
HAL_StatusTypeDef ADS8678_PowerDown(ADS8678__HandleTypeDef *dev);
HAL_StatusTypeDef ADS8678_Standby(ADS8678__HandleTypeDef *dev);

#ifdef __cplusplus
}
#endif

#endif /* INC_ADS8678_H_ */
