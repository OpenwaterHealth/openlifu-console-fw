/* SPDX-License-Identifier: Apache-2.0 */
/* Host stand-in for Core/Inc/hv_supply.h: the two calls hv_presets.c makes. */
#ifndef HV_SUPPLY_H
#define HV_SUPPLY_H
#include <stdbool.h>
bool getHVOnStatus(void);
bool hv_set_voltage(float value);
#endif
