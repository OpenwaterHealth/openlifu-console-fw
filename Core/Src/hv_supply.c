/*
 * hv_supply.c
 *
 *  Created on: Dec 3, 2024
 *      Author: GeorgeVigelette
 */


#include "hv_supply.h"
#include "utils.h"
#include "hv_calibration_coeffs.h"
#include "usb_events.h"
#include "dbg_print.h"
#include <stdio.h>
#include <stdbool.h>

#define VOLTAGE_DIVIDER_RATIO 0.03543
#define ADC_MAX 4095.0   // 12-bit ADC resolution

#define DAC_MAX_VALUE 4095
#define STEP_SIZE 100  // Number of steps to ramp up/down
#define PAUSE_DURATION_MS 15  // 500ms pause every STEP_SIZE

static uint16_t current_hvp_val = 0;
static uint16_t current_hvm_val = 0;
static bool _use_exact = false;

extern SPI_HandleTypeDef hspi1;

static HAL_StatusTypeDef HV_DAC_ReadReg(uint32_t *data) {
    uint8_t rxBuffer[4];
    HAL_StatusTypeDef status = HAL_ERROR;

    HAL_GPIO_WritePin(SYNC_GPIO_Port, SYNC_Pin, GPIO_PIN_RESET);
    status = HAL_SPI_Receive(&hspi1, rxBuffer, 4, HAL_MAX_DELAY);
    delay_us(1000);
    HAL_GPIO_WritePin(SYNC_GPIO_Port, SYNC_Pin, GPIO_PIN_SET);

    if (status == HAL_OK) {
        *data = (rxBuffer[0] << 24) | (rxBuffer[1] << 16) | (rxBuffer[2] << 8) | rxBuffer[3];
    }

    return status;
}

static HAL_StatusTypeDef HV_DAC_WriteReg(uint32_t data) {
    uint8_t txBuffer[4];

    txBuffer[0] = (data >> 24) & 0xFF;
    txBuffer[1] = (data >> 16) & 0xFF;
    txBuffer[2] = (data >> 8) & 0xFF;
    txBuffer[3] = data & 0xFF;

    HAL_GPIO_WritePin(SYNC_GPIO_Port, SYNC_Pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef status = HAL_SPI_Transmit(&hspi1, txBuffer, 4, HAL_MAX_DELAY);
    delay_us(1000);
    HAL_GPIO_WritePin(SYNC_GPIO_Port, SYNC_Pin, GPIO_PIN_SET);

    return status;
}

void set_use_exact(bool val)
{
	_use_exact = val;
}

void HV_ReadStatusRegister(uint32_t *status_data) {
    uint32_t command = 0xF4000000;
    HV_DAC_WriteReg(command);
    HV_DAC_ReadReg(status_data);
}

uint32_t HV_SetDACValue(DAC_Channel_t channel, DAC_BitDepth_t bitDepth, uint16_t value) {
    uint32_t command = 0x0;
    if(value >4095)	// prevent the power supply from blowing a cap
    	value = 4095;
    switch (bitDepth) {
        case DAC_BIT_12:
            value &= 0x0FFF;
            value <<= 4;
            break;
        case DAC_BIT_14:
            value &= 0x3FFF;
            value <<= 2;
            break;
        case DAC_BIT_16:
            value &= 0xFFFF;
            break;
        default:
            return 0;
    }

    // cppcheck-suppress badBitmaskCheck -- 0x0<<28 documents the DAC command field layout
    command = (0x0 << 28) | (0x3 << 24) | (channel << 20) | (value << 4);
    HV_DAC_WriteReg(command);

    return command;
}

bool hv_set_voltage(float value) {
	current_hvp_val = pos_voltage_to_dac(value);
	current_hvm_val = neg_voltage_to_dac(-1*value);

	//DBG_PRINTF("Set voltage %f POS: 0x%04X  NEG: 0x%04X\r\n", value, current_hvp_val, current_hvm_val);

	// turn HV OFF
    // HAL_GPIO_WritePin(HV_ON_GPIO_Port, HV_ON_Pin, GPIO_PIN_SET);
	// HV_SetDACValue(DAC_CHANNEL_HVP, DAC_BIT_12, 0);
	// HV_SetDACValue(DAC_CHANNEL_HVM, DAC_BIT_12, 0);
	// delay_ms(100);

	//HV_SetDACValue(DAC_CHANNEL_HVP, DAC_BIT_12, current_hvp_val);
    //delay_ms(250);
	//HV_SetDACValue(DAC_CHANNEL_HVM, DAC_BIT_12, current_hvm_val);
    return true;
}

uint16_t set_hvm(uint16_t value) {
	current_hvm_val = value;
    return value;
}

uint16_t set_hvp(uint16_t value) {
	current_hvp_val = value;
    return value;
}

uint16_t HV_GetVoltage() {
    return current_hvp_val;
}

uint16_t HV_GetOnVoltage() {
	uint32_t value = 0;
	HV_ReadStatusRegister(&value);
    return (uint16_t)value;
}

void set_current_dac(void)
{
	HV_SetDACValue(DAC_CHANNEL_HVP, DAC_BIT_12, current_hvp_val);
    HV_SetDACValue(DAC_CHANNEL_HVM, DAC_BIT_12, current_hvm_val);
}

void HV_Enable(void) {
    if (!usb_is_port_open()) {
        DBG_PRINTF("HV Enable BLOCKED: USB port not open\r\n");
        return;
    }
    DBG_PRINTF("HV Enable: Ramping to HVP: 0x%04X HVM: 0x%04X\r\n", current_hvp_val, current_hvm_val);
    uint16_t step_hvp = current_hvp_val / STEP_SIZE;
    uint16_t step_hvm = current_hvm_val / STEP_SIZE;
    uint16_t hvp_value = 0;
    uint16_t hvm_value = 0;
    
    DBG_PRINTF("Steps HVP: %d Steps HVM: %d\r\n", step_hvp, step_hvm);

	HV_SetDACValue(DAC_CHANNEL_HVP, DAC_BIT_12, hvp_value);
    HV_SetDACValue(DAC_CHANNEL_HVM, DAC_BIT_12, hvm_value);

	HAL_GPIO_WritePin(HV_SHUTDOWN_GPIO_Port, HV_SHUTDOWN_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(HV_ON_GPIO_Port, HV_ON_Pin, GPIO_PIN_RESET);
    
    for (uint16_t i = 1; i <= step_hvp; i++) {
        hvp_value += STEP_SIZE;
        if(hvp_value > current_hvp_val)
        	hvp_value = current_hvp_val;
        HV_SetDACValue(DAC_CHANNEL_HVP, DAC_BIT_12, hvp_value);
        delay_ms(PAUSE_DURATION_MS);
    }
    
    HV_SetDACValue(DAC_CHANNEL_HVP, DAC_BIT_12, current_hvp_val);
    delay_ms(100);

    for (uint16_t i = 1; i <= step_hvm; i++) {
        hvm_value += STEP_SIZE;
        if(hvm_value > current_hvm_val)
        	hvm_value = current_hvm_val;
        HV_SetDACValue(DAC_CHANNEL_HVM, DAC_BIT_12, hvm_value);
        delay_ms(PAUSE_DURATION_MS);
    }

    HV_SetDACValue(DAC_CHANNEL_HVM, DAC_BIT_12, current_hvm_val);
    delay_ms(100);
    
}

void HV_Disable(void) {

	HAL_GPIO_WritePin(HV_SHUTDOWN_GPIO_Port, HV_SHUTDOWN_Pin, GPIO_PIN_SET);
    HV_SetDACValue(DAC_CHANNEL_HVM_REG, DAC_BIT_12, 0);
    HV_SetDACValue(DAC_CHANNEL_HVP_REG, DAC_BIT_12, 0);
    HV_SetDACValue(DAC_CHANNEL_HVP, DAC_BIT_12, 0);
    HV_SetDACValue(DAC_CHANNEL_HVM, DAC_BIT_12, 0);

    HAL_GPIO_WritePin(HV_ON_GPIO_Port, HV_ON_Pin, GPIO_PIN_SET);
}

/**
 * @brief  USB event callback — disables HV when the application disconnects.
 */
static void hv_usb_event_cb(usb_event_t event)
{
    (void)event;
    if (getHVOnStatus()) {
        DBG_PRINTF("HV interlock: USB lost — disabling HV\r\n");
        HV_Disable();
    }
}

void HV_RegisterInterlock(void)
{
    usb_register_callback(USB_EVENT_PORT_CLOSE, hv_usb_event_cb);
    usb_register_callback(USB_EVENT_DISCONNECT, hv_usb_event_cb);
}

void HV_ClearDAC(void) {
	HV_Disable();
    HAL_GPIO_WritePin(CLR_GPIO_Port, CLR_Pin, GPIO_PIN_SET);
    delay_us(1000);
    HAL_GPIO_WritePin(CLR_GPIO_Port, CLR_Pin, GPIO_PIN_RESET);
    delay_us(1000);
    HAL_GPIO_WritePin(CLR_GPIO_Port, CLR_Pin, GPIO_PIN_SET);
    delay_us(1000);
}

void V12_Enable(void)
{
    HAL_GPIO_WritePin(V12_ENABLE_GPIO_Port, V12_ENABLE_Pin, GPIO_PIN_RESET);
}

void V12_Disable(void)
{
    HAL_GPIO_WritePin(V12_ENABLE_GPIO_Port, V12_ENABLE_Pin, GPIO_PIN_SET);
}

void System_Enable(void)
{
    HAL_GPIO_WritePin(SYS_EN_GPIO_Port, SYS_EN_Pin, GPIO_PIN_SET);
}

void System_Disable(void)
{
    HAL_GPIO_WritePin(SYS_EN_GPIO_Port, SYS_EN_Pin, GPIO_PIN_RESET);
}

bool getHVOnStatus()
{
    GPIO_PinState pinState = HAL_GPIO_ReadPin(HV_SHUTDOWN_GPIO_Port, HV_SHUTDOWN_Pin);

    if (pinState == GPIO_PIN_SET) {
        // Pin is HIGH
        return false;
    } else {
        // Pin is LOW
        return true;
    }
}

bool get12VOnStatus()
{
    GPIO_PinState pinState = HAL_GPIO_ReadPin(V12_ENABLE_GPIO_Port, V12_ENABLE_Pin);

    if (pinState == GPIO_PIN_SET) {
        // Pin is HIGH
        return true;
    } else {
        // Pin is LOW
        return false;
    }
}

/**
 * @brief Read all ADC channels and optionally display or store voltages
 * @param adc Pointer to ADS8678 device handle
 * @param output Pointer to output buffer (NULL to print, non-NULL to store data)
 */
void read_all_adc_channels(ADS8678__HandleTypeDef *adc, ADC_ChannelData_t *output) {
    uint16_t adc_values[8];
    float voltages[8];
    float converted[8];

    // Conversion factors for each channel
    const float factors[8] = {
        0.038462f,  // Ch 0: HVP_1
        0.090909f,  // Ch 1: HVP_2
        0.038462f,  // Ch 2: HVM_2
        0.090909f,  // Ch 3: HVM_1
        0.375f,     // Ch 4: 12V line
        1.0f,       // Ch 5: VCON-A1
        1.0f,       // Ch 6: VCON-B1
        1.0f        // Ch 7: VCON-C1
    };

    // Read all channels using manual mode
    for (int i = 0; i < 8; i++) {
        if (ADS8678_ReadChannelManual(adc, i, &adc_values[i]) == HAL_OK) {
            // Convert raw ADC value to voltage using the factor
        	// Convert to voltage: Range is 1.25 * Vref = 4.096V
            voltages[i] = (float)(adc_values[i]-8192) * 10.240f / 8192;


            switch(i){
				case 0:
					converted[i] = pos_adc_ch0_to_voltage(adc_values[i]);
					break;
				case 1:
					converted[i] = pos_adc_ch1_to_voltage(adc_values[i]);
					break;
				case 2:
					converted[i] = neg_adc_ch2_to_voltage(adc_values[i]);
					break;
				case 3:
					converted[i] = neg_adc_ch3_to_voltage(adc_values[i]);
					break;
				case 4:
					converted[i] = voltages[i] / factors[i];
					break;
				case 5:
					converted[i] = voltages[i] / factors[i];
					break;
				case 6:
					converted[i] = voltages[i] / factors[i];
					break;
				default:

            }
            // converted[i] = voltages[i] / factors[i];

        } else {
            // If read fails, set values to 0
            adc_values[i] = 0;
            voltages[i] = 0.0f;
            converted[i] = 0.0f;
            if (output == NULL) {
                DBG_PRINTF("Error reading channel %d\r\n", i);
            }
        }
    }

    // If output pointer is NULL, print to console
    if (output == NULL) {
        // Channel names (only needed for printing; unused when DBG_PRINTF
        // is compiled out)
        const char* channel_names[8] __attribute__((unused)) = {
            "HVP_1",
            "HVP_2",
            "HVM_1",
            "HVM_2",
            "12V Line",
            "VCON-A1",
            "VCON-B1",
            "VCON-C1"
        };
        DBG_PRINTF("\r\n=== ADC Channel Readings ===\r\n");
        for (int i = 0; i < 8; i++) {
            DBG_PRINTF("Channel %d (%s): %u (0x%04X) = %.3fV REAL: %.3fV \r\n",
                   i, channel_names[i], adc_values[i], adc_values[i], voltages[i], converted[i]);
        }
        DBG_PRINTF("=============================\r\n\r\n");
    } else {
        // If output pointer is provided, copy data to it
        for (int i = 0; i < 8; i++) {
            output->raw_values[i] = adc_values[i];
            output->voltages[i] = voltages[i];
            output->converted[i] = converted[i];
        }
    }
}
