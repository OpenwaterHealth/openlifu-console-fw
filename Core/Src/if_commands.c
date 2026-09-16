/*
 * if_commands.c
 *
 *  Created on: Nov 15, 2024
 *      Author: GeorgeVigelette
 */

#include "main.h"
#include "if_commands.h"
#include "common.h"
#include "dbg_print.h"
#include "i2c_master.h"
#include "hv_supply.h"
#include "fan_driver.h"
#include "lifu_config.h"
#include "rgb.h"
#include "rgb_led.h"
#include "rgb_effects.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>  // For rand() and srand()
#include <time.h>    // For seeding random number generator

extern bool _enter_dfu;
extern bool _force_stm32_dfu;
extern ADS8678__HandleTypeDef vmon_adc;
extern MAX31875_Init_t temp_sensor_1;
extern MAX31875_Init_t temp_sensor_2;
extern FAN_Driver fan[2];

static uint32_t id_words[3] = {0};
static float ret_voltage = 0;
static ADC_ChannelData_t vmon_adc_data;
volatile float last_temperature1 = 0;
volatile float last_temperature2 = 0;
volatile uint8_t last_btFan_speed = 0;
volatile uint8_t last_tpFan_speed = 0;

static void print_uart_packet(const UartPacket* packet) {
    DBG_PRINTF("ID: 0x%04X\r\n", packet->id);
    DBG_PRINTF("Packet Type: 0x%02X\r\n", packet->packet_type);
    DBG_PRINTF("Command: 0x%02X\r\n", packet->command);
    DBG_PRINTF("Data Length: %d\r\n", packet->data_len);
    DBG_PRINTF("CRC: 0x%04X\r\n", packet->crc);
    DBG_PRINTF("Data: ");
    for (int i = 0; i < packet->data_len; i++) {
        DBG_PRINTF("0x%02X ", packet->data[i]);
    }
    DBG_PRINTF("\r\n");
}

/* Parse and apply an OW_POWER_SET_RGB_FX payload (layout in common.h).
 * Returns 0 on success, -1 on malformed payload / unknown effect. */
static int32_t apply_rgb_fx(const UartPacket *cmd)
{
	if ((cmd->data == NULL) || (cmd->data_len < 6)) {
		return -1;
	}

	uint8_t  fx     = cmd->data[0];
	uint8_t  r      = cmd->data[1];
	uint8_t  g      = cmd->data[2];
	uint8_t  b      = cmd->data[3];
	uint16_t period = (uint16_t)(cmd->data[4] | ((uint16_t)cmd->data[5] << 8));

	switch (fx) {
		case OW_RGB_FX_STOP:
			RGB_EffectStop();
			return 0;
		case OW_RGB_FX_SOLID:
			RGB_EffectStop();
			RGB_SetColor(r, g, b, true);
			return 0;
		case OW_RGB_FX_FADE:
			RGB_FadeTo(r, g, b, period);
			return 0;
		case OW_RGB_FX_BREATHE:
			RGB_Breathe(r, g, b, period);
			return 0;
		case OW_RGB_FX_RAINBOW:
			RGB_Rainbow(period);
			return 0;
		case OW_RGB_FX_FLASH:
			RGB_Flash(r, g, b, period);
			return 0;
		case OW_RGB_FX_CYCLE: {
			uint8_t  colors[RGB_CYCLE_MAX_COLORS][3];
			uint16_t extra = (uint16_t)(cmd->data_len - 6U);
			uint8_t  count = (uint8_t)(1U + (extra / 3U));

			if (((extra % 3U) != 0U) || (count > RGB_CYCLE_MAX_COLORS)) {
				return -1;
			}
			colors[0][0] = r;
			colors[0][1] = g;
			colors[0][2] = b;
			for (uint8_t i = 1; i < count; i++) {
				colors[i][0] = cmd->data[6U + (uint16_t)(i - 1U) * 3U];
				colors[i][1] = cmd->data[7U + (uint16_t)(i - 1U) * 3U];
				colors[i][2] = cmd->data[8U + (uint16_t)(i - 1U) * 3U];
			}
			RGB_ColorCycle(colors, count, period);
			return 0;
		}
		default:
			return -1;
	}
}

static void POWER_ProcessCommand(UartPacket *uartResp, UartPacket cmd)
{
	switch (cmd.command)
	{
		case OW_CMD_PING:
			uartResp->command = OW_CMD_PING;
			uartResp->addr = cmd.addr;
			uartResp->reserved = cmd.reserved;
			break;
		case OW_CMD_PONG:
			uartResp->command = OW_CMD_PONG;
			uartResp->addr = cmd.addr;
			uartResp->reserved = cmd.reserved;
			break;
		case OW_CMD_VERSION:
			uartResp->command = OW_CMD_VERSION;
			uartResp->addr = cmd.addr;
			uartResp->reserved = cmd.reserved;
            uartResp->data_len = sizeof(FW_VERSION_STRING);
            uartResp->data = (uint8_t*)FW_VERSION_STRING;
			break;
		case OW_CMD_ECHO:
			// exact copy
			uartResp->command = OW_CMD_ECHO;
			uartResp->addr = cmd.addr;
			uartResp->reserved = cmd.reserved;
			uartResp->data_len = cmd.data_len;
			uartResp->data = cmd.data;
			break;
		case OW_CMD_TOGGLE_LED:
			uartResp->command = OW_CMD_TOGGLE_LED;
			HAL_GPIO_TogglePin(HB_LED_GPIO_Port, HB_LED_Pin);
			break;
		case OW_CMD_HWID:
			uartResp->command = OW_CMD_HWID;
			uartResp->addr = cmd.addr;
			uartResp->reserved = cmd.reserved;
			id_words[0] = HAL_GetUIDw0();
			id_words[1] = HAL_GetUIDw1();
			id_words[2] = HAL_GetUIDw2();
			uartResp->data_len = 16;
			uartResp->data = (uint8_t *)&id_words;
			break;
		case OW_POWER_12V_ON:
			uartResp->command = OW_POWER_12V_ON;
			HAL_GPIO_WritePin(V12_ENABLE_GPIO_Port, V12_ENABLE_Pin, GPIO_PIN_SET);
			break;
		case OW_POWER_12V_OFF:
			uartResp->command = OW_POWER_12V_OFF;
			HAL_GPIO_WritePin(V12_ENABLE_GPIO_Port, V12_ENABLE_Pin, GPIO_PIN_RESET);
			break;
		case OW_POWER_HV_ON:
			uartResp->command = OW_POWER_HV_ON;
			HV_Enable();
			RGB_Set(RGB_BLUE);
		    // start timer
		    // HAL_TIM_Base_Start_IT(&htim6);
			break;
		case OW_POWER_HV_OFF:
			uartResp->command = OW_POWER_HV_OFF;
		    // stop timer
		    // HAL_TIM_Base_Stop_IT(&htim6);
			HV_Disable();
			RGB_Set(RGB_GREEN);
			break;
		case OW_POWER_SET_HV:
			uartResp->command = OW_POWER_SET_HV;
			uartResp->addr = 0;
			uartResp->reserved = cmd.reserved;
			uartResp->data_len = 0;
			if(cmd.data_len == 4)
			{
				float set_value = be_bytes_to_float((const uint8_t*)cmd.data, cmd.data_len);

				hv_set_voltage(set_value);
				// set_use_exact(false);
			}else{
				uartResp->packet_type = OW_ERROR;
				uartResp->data_len = 0;
				uartResp->data = NULL;
			}
			break;
		case OW_POWER_GET_HV:
			uartResp->command = OW_POWER_GET_HV;

			// Read ADC channels into structure
			read_all_adc_channels(&vmon_adc, &vmon_adc_data);

			ret_voltage = vmon_adc_data.converted[1];
			uartResp->data_len = 4;
			uartResp->data = (uint8_t *)&ret_voltage;
			break;
		case OW_POWER_STATUS:
			uartResp->command = OW_POWER_STATUS;
			break;
		case OW_POWER_GET_TEMP1:
			// last_temperature1 = 30.0f + (rand() % 41);  // Random float between 30.0 and 70.0
			last_temperature1 = MAX31875_ReadTemperature(MAX31875_TEMP1_DEV_ADDR);
			//DBG_PRINTF("TEMP1: %d.%02d\r\n", (int)last_temperature1, (((int)last_temperature1 - (int)last_temperature1) * 100));
			uartResp->command = OW_POWER_GET_TEMP1;
			uartResp->data_len = 4;
			uartResp->data = (uint8_t *)&last_temperature1;
			break;
		case OW_POWER_GET_TEMP2:
			// last_temperature2 = 30.0f + (rand() % 41);  // Random float between 30.0 and 70.0
			last_temperature2 = MAX31875_ReadTemperature(MAX31875_TEMP2_DEV_ADDR);
			//DBG_PRINTF("TEMP2: %d.%02d\r\n", (int)last_temperature2, (((int)last_temperature2 - (int)last_temperature2) * 100));
			uartResp->command = OW_POWER_GET_TEMP2;
			uartResp->data_len = 4;
			uartResp->data = (uint8_t *)&last_temperature2;		
			break;
		case OW_POWER_SET_FAN:
			uartResp->command = OW_POWER_SET_FAN;
			if(cmd.addr == 0){
				last_btFan_speed = cmd.data[0];
				FAN_SetManualPWM(&fan[0], cmd.data[0]);
			}
			else if(cmd.addr == 1){
				last_tpFan_speed = cmd.data[0];
				FAN_SetManualPWM(&fan[1], cmd.data[0]);
			}else{
				uartResp->packet_type = OW_ERROR;
				uartResp->data_len = 0;
				uartResp->data = NULL;
			}

			break;
		case OW_POWER_GET_FAN:
			uartResp->command = OW_POWER_GET_FAN;
			if(cmd.addr == 0){
				last_btFan_speed = FAN_GetPWMDuty(&fan[0]);
				uartResp->data_len = 1;
				uartResp->data = (uint8_t *)&last_btFan_speed;
			}
			else if(cmd.addr == 1){
				last_tpFan_speed = FAN_GetPWMDuty(&fan[1]);
				uartResp->data_len = 1;
				uartResp->data = (uint8_t *)&last_tpFan_speed;
			}else{
				uartResp->packet_type = OW_ERROR;
				uartResp->data_len = 0;
				uartResp->data = NULL;
			}

			break;
		case OW_POWER_SET_RGB:
			uartResp->command = OW_POWER_SET_RGB;
			if (RGB_Set(cmd.reserved) != 0) {
				uartResp->packet_type = OW_ERROR;
				uartResp->data_len = 0;
				uartResp->data = NULL;
			}
			break;
		case OW_POWER_GET_RGB:
			uartResp->command = OW_POWER_GET_RGB;
			uartResp->reserved = RGB_Get();
			break;
		case OW_POWER_SET_RGB_FX:
			uartResp->command = OW_POWER_SET_RGB_FX;
			if (apply_rgb_fx(&cmd) != 0) {
				uartResp->packet_type = OW_ERROR;
				uartResp->data_len = 0;
				uartResp->data = NULL;
			}
			break;
		case OW_POWER_GET_HVON:
			uartResp->command = OW_POWER_GET_HVON;
			if(getHVOnStatus())
			{
				uartResp->reserved = 1;
			}
			else
			{
				uartResp->reserved = 0;
			}

			break;
		case OW_POWER_GET_12VON:
			uartResp->command = OW_POWER_GET_12VON;
			if(get12VOnStatus()){
				uartResp->reserved = 1;
			}else{
				uartResp->reserved = 0;
			}

			break;
		case OW_CMD_USR_CFG:
			// reserved == 0: READ  -> [16-byte wire header][json]
			// reserved == 1: WRITE -> cmd.data is [header][json] or raw JSON,
			//                         ACK carries just the updated header back
			uartResp->command = OW_CMD_USR_CFG;
			uartResp->addr = cmd.addr;
			uartResp->reserved = cmd.reserved;
			{
				const uint8_t *wire_buf = NULL;
				uint16_t wire_len = 0;

				if (cmd.reserved > 1 ||
				    (cmd.reserved == 1 && (cmd.data == NULL || cmd.data_len == 0)))
				{
					uartResp->packet_type = OW_ERROR;
					uartResp->data_len = 0;
					uartResp->data = NULL;
					break;
				}

				if (cmd.reserved == 1 &&
				    lifu_cfg_wire_write(cmd.data, cmd.data_len) != HAL_OK)
				{
					uartResp->packet_type = OW_ERROR;
					uartResp->data_len = 0;
					uartResp->data = NULL;
					break;
				}

				if (lifu_cfg_wire_read(&wire_buf, &wire_len, (uint16_t)DATA_MAX_SIZE) != HAL_OK
				    || wire_buf == NULL)
				{
					uartResp->packet_type = OW_ERROR;
					uartResp->data_len = 0;
					uartResp->data = NULL;
					break;
				}

				uartResp->data_len = (cmd.reserved == 1)
				                   ? (uint16_t)sizeof(lifu_cfg_wire_hdr_t)
				                   : wire_len;
				uartResp->data = (uint8_t *)wire_buf;
			}
			break;
		case OW_CMD_NOP:
			uartResp->command = OW_CMD_NOP;
			break;
		case OW_CMD_RESET:
			uartResp->command = OW_CMD_RESET;
			uartResp->addr = 0;
			uartResp->reserved = 0;
			uartResp->data_len = 0;

			_enter_dfu = false;
			__HAL_TIM_CLEAR_FLAG(&htim17, TIM_FLAG_UPDATE);
			__HAL_TIM_SET_COUNTER(&htim17, 0);

			if(HAL_TIM_Base_Start_IT(&htim17) != HAL_OK){
				uartResp->packet_type = OW_ERROR;
			}
			break;
		case OW_CMD_DFU:
			uartResp->command = OW_CMD_DFU;
			uartResp->addr = cmd.addr;
			uartResp->reserved = cmd.reserved;
			uartResp->data_len = 0;

			_enter_dfu = true;
			if(cmd.reserved == 0x77){
				_force_stm32_dfu = true;
			}else{
				_force_stm32_dfu = false;
			}
			__HAL_TIM_CLEAR_FLAG(&htim17, TIM_FLAG_UPDATE);
			__HAL_TIM_SET_COUNTER(&htim17, 0);

			if(HAL_TIM_Base_Start_IT(&htim17) != HAL_OK){
				uartResp->packet_type = OW_ERROR;
			}
			break;
		case OW_POWER_SET_DACS:
			uartResp->command = OW_POWER_SET_DACS;
			uartResp->addr = 0;
			uartResp->reserved = cmd.reserved;
			uartResp->data_len = 0;
			if(cmd.data_len == 8)
			{
				uint16_t hvp_dac_value = ((uint16_t)cmd.data[0] << 8) | (uint16_t)cmd.data[1];
				uint16_t hvm_dac_value = ((uint16_t)cmd.data[4] << 8) | (uint16_t)cmd.data[5];

		        // Debug print
		        DBG_PRINTF("Received HVP DAC Value: %u (0x%04X)\r\n", hvp_dac_value, hvp_dac_value);
		        set_hvp(hvp_dac_value);
		        DBG_PRINTF("Received HVM DAC Value: %u (0x%04X)\r\n", hvm_dac_value, hvm_dac_value);
		        set_hvm(hvm_dac_value);
		        set_use_exact(true);
			}else{
				uartResp->packet_type = OW_ERROR;
				uartResp->data_len = 0;
				uartResp->data = NULL;
			}
			break;
		case OW_POWER_VMON:
			uartResp->command = OW_POWER_VMON;
			uartResp->addr = cmd.addr;
			uartResp->reserved = cmd.reserved;
			
			// Read ADC channels into structure
			read_all_adc_channels(&vmon_adc, &vmon_adc_data);
			
			// Send the structure directly as bytes (96 bytes: 8×uint16 + 8×float + 8×float)
			uartResp->data_len = sizeof(ADC_ChannelData_t);
			uartResp->data = (uint8_t *)&vmon_adc_data;
			break;
		case OW_POWER_HV_ENABLE:
			uartResp->command = OW_POWER_HV_ENABLE;
			uartResp->addr = cmd.addr;
			uartResp->reserved = cmd.reserved;

			if(cmd.addr == 1){
				HV_Enable();
			}else{
				HV_Disable();
			}
			break;
		case OW_POWER_RAW_DAC:
			uartResp->command = OW_POWER_RAW_DAC;
			uartResp->addr = cmd.addr;
			uartResp->reserved = cmd.reserved;

			if(cmd.addr > 3 || cmd.data_len != 2)
			{
				uartResp->packet_type = OW_ERROR;
				uartResp->data_len = 0;
				uartResp->data = NULL;
			}else{
				uint16_t dac_raw = ((uint16_t)cmd.data[0] << 8) | (uint16_t)cmd.data[1];
				HV_SetDACValue(cmd.addr, DAC_BIT_12, dac_raw);
			}
			break;
		default:
			uartResp->packet_type = OW_UNKNOWN;
			break;
	}

}

UartPacket process_if_command(UartPacket cmd)
{
	UartPacket uartResp;
	// I2C_TX_Packet i2c_packet;
	(void)print_uart_packet;

	uartResp.id = cmd.id;
	uartResp.packet_type = OW_RESP;
	uartResp.addr = 0;
	uartResp.reserved = 0;
	uartResp.data_len = 0;
	uartResp.data = 0;
	switch (cmd.packet_type)
	{
	case OW_JSON:
		//JSON_ProcessCommand(&uartResp, cmd);
		break;
	case OW_CMD:
	case OW_POWER:
		//process by the USTX Controller
		POWER_ProcessCommand(&uartResp, cmd);
		break;
	default:
		uartResp.data_len = 0;
		uartResp.packet_type = OW_UNKNOWN;
		// uartResp.data = (uint8_t*)&cmd.tag;
		break;
	}

	return uartResp;

}

