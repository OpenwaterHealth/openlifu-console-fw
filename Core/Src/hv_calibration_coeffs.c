/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Auto-generated High Voltage Calibration Functions */
#include "hv_calibration_coeffs.h"
#include <stdint.h>

float pos_adc_ch0_to_voltage(uint16_t adc_raw) {
    float x = (float)adc_raw;
    return POS_CH0_A * x * x + POS_CH0_B * x + POS_CH0_C;
}

float pos_adc_ch1_to_voltage(uint16_t adc_raw) {
    float x = (float)adc_raw;
    return POS_CH1_A * x * x + POS_CH1_B * x + POS_CH1_C;
}

uint16_t pos_voltage_to_dac(float voltage) {
    float dac_float = POS_VOLT_A * voltage * voltage + POS_VOLT_B * voltage + POS_VOLT_C;
    int32_t dac = (int32_t)(dac_float + 0.5f);
    if (dac < 0) return 0;
    if (dac > 2100) return 2100;
    return (uint16_t)dac;
}

float neg_adc_ch2_to_voltage(uint16_t adc_raw) {
    float x = (float)adc_raw;
    return NEG_CH2_A * x * x + NEG_CH2_B * x + NEG_CH2_C;
}

float neg_adc_ch3_to_voltage(uint16_t adc_raw) {
    float x = (float)adc_raw;
    return NEG_CH3_A * x * x + NEG_CH3_B * x + NEG_CH3_C;
}

uint16_t neg_voltage_to_dac(float voltage) {
    float dac_float = NEG_VOLT_A * voltage * voltage + NEG_VOLT_B * voltage + NEG_VOLT_C;
    int32_t dac = (int32_t)(dac_float + 0.5f);
    if (dac < 0) return 0;
    if (dac > 2200) return 2200;
    return (uint16_t)dac;
}