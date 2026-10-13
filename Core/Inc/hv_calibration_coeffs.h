/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Auto-generated High Voltage Calibration Coefficients */
#ifndef HV_CALIBRATION_COEFFS_H
#define HV_CALIBRATION_COEFFS_H
#include <stdint.h>

// Positive Supply - ADC Ch0 -> Voltage
#define POS_CH0_A  (-7.223141111598e-08f)
#define POS_CH0_B  (3.595002398663e-02f)
#define POS_CH0_C  (-2.921906190881e+02f)

// Positive Supply - ADC Ch1 -> Voltage
#define POS_CH1_A  (-8.517456284879e-09f)
#define POS_CH1_B  (1.525595103286e-02f)
#define POS_CH1_C  (-1.269209207784e+02f)

// Positive Supply - Voltage -> DAC
#define POS_VOLT_A (-9.300426997513e-04f)
#define POS_VOLT_B (2.643026901020e+01f)
#define POS_VOLT_C (5.095107506289e+00f)

// Negative Supply - ADC Ch2 -> Voltage
#define NEG_CH2_A  (4.269568473782e-08f)
#define NEG_CH2_B  (3.353516086271e-02f)
#define NEG_CH2_C  (-2.797693926172e+02f)

// Negative Supply - ADC Ch3 -> Voltage
#define NEG_CH3_A  (4.458164209519e-09f)
#define NEG_CH3_B  (1.490026646735e-02f)
#define NEG_CH3_C  (-1.245665053535e+02f)

// Negative Supply - Voltage -> DAC
#define NEG_VOLT_A (2.971601947589e-05f)
#define NEG_VOLT_B (-2.645957350138e+01f)
#define NEG_VOLT_C (6.695799616395e+00f)

float pos_adc_ch0_to_voltage(uint16_t adc_raw);
float pos_adc_ch1_to_voltage(uint16_t adc_raw);
uint16_t pos_voltage_to_dac(float voltage);
float neg_adc_ch2_to_voltage(uint16_t adc_raw);
float neg_adc_ch3_to_voltage(uint16_t adc_raw);
uint16_t neg_voltage_to_dac(float voltage);

#endif
