/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "max31875.h"

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */


/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
/** The main loop is alive: keeps the TIM1 watchdog refresh going (main.c). */
void app_watchdog_checkin(void);

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LDAC_Pin GPIO_PIN_14
#define LDAC_GPIO_Port GPIOC
#define V12_ENABLE_Pin GPIO_PIN_13
#define V12_ENABLE_GPIO_Port GPIOC
#define ADC_OVERFLOW_Pin GPIO_PIN_9
#define ADC_OVERFLOW_GPIO_Port GPIOB
#define BTN_PUSH_Pin GPIO_PIN_4
#define BTN_PUSH_GPIO_Port GPIOB
#define V12_PWR_G_Pin GPIO_PIN_3
#define V12_PWR_G_GPIO_Port GPIOB
#define USB_RESET_Pin GPIO_PIN_15
#define USB_RESET_GPIO_Port GPIOA
#define SYS_RDY_Pin GPIO_PIN_2
#define SYS_RDY_GPIO_Port GPIOD
#define V5B_PWR_G_Pin GPIO_PIN_5
#define V5B_PWR_G_GPIO_Port GPIOB
#define SYS_EN_Pin GPIO_PIN_12
#define SYS_EN_GPIO_Port GPIOC
#define LED_ON_Pin GPIO_PIN_10
#define LED_ON_GPIO_Port GPIOA
#define ADC_PD_Pin GPIO_PIN_9
#define ADC_PD_GPIO_Port GPIOA
#define ADC_CS_Pin GPIO_PIN_8
#define ADC_CS_GPIO_Port GPIOA
#define HV_SHUTDOWN_Pin GPIO_PIN_9
#define HV_SHUTDOWN_GPIO_Port GPIOC
#define HV_ON_Pin GPIO_PIN_0
#define HV_ON_GPIO_Port GPIOC
#define SCL_CFG_Pin GPIO_PIN_7
#define SCL_CFG_GPIO_Port GPIOC
#define SDA_REM_Pin GPIO_PIN_8
#define SDA_REM_GPIO_Port GPIOC
#define TEMP1_Pin GPIO_PIN_2
#define TEMP1_GPIO_Port GPIOA
#define CLR_Pin GPIO_PIN_0
#define CLR_GPIO_Port GPIOB
#define LD_B_Pin GPIO_PIN_6
#define LD_B_GPIO_Port GPIOC
#define GPIO4_Pin GPIO_PIN_15
#define GPIO4_GPIO_Port GPIOB
#define GPIO3_Pin GPIO_PIN_14
#define GPIO3_GPIO_Port GPIOB
#define HV_P_Pin GPIO_PIN_0
#define HV_P_GPIO_Port GPIOA
#define TEMP2_Pin GPIO_PIN_3
#define TEMP2_GPIO_Port GPIOA
#define V5A_PWR_G_Pin GPIO_PIN_1
#define V5A_PWR_G_GPIO_Port GPIOB
#define PWR_G_Pin GPIO_PIN_2
#define PWR_G_GPIO_Port GPIOB
#define GPIO2_Pin GPIO_PIN_13
#define GPIO2_GPIO_Port GPIOB
#define HB_LED_Pin GPIO_PIN_1
#define HB_LED_GPIO_Port GPIOA
#define SYNC_Pin GPIO_PIN_4
#define SYNC_GPIO_Port GPIOA
#define LD_R_Pin GPIO_PIN_4
#define LD_R_GPIO_Port GPIOC
#define LD_G_Pin GPIO_PIN_5
#define LD_G_GPIO_Port GPIOC
#define GPIO1_Pin GPIO_PIN_12
#define GPIO1_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
// #define SEMI_HOSTING_ENABLED

extern CRC_HandleTypeDef hcrc;
extern UART_HandleTypeDef huart3;
#define DEBUG_UART huart3

extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim6;
extern TIM_HandleTypeDef htim14;
extern TIM_HandleTypeDef htim17;

#define CDC_TIMER htim14
#define US_DELAY_TIMER htim2
extern I2C_HandleTypeDef hi2c1;
extern I2C_HandleTypeDef hi2c2;

#define LOCAL_I2C_HANDLE hi2c1
#define SYNC_I2C_HANDLE hi2c2

extern ADC_HandleTypeDef hadc;

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
