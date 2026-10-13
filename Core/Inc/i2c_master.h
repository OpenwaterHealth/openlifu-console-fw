/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * i2c_master.h
 *
 *  Created on: Jan 17, 2024
 *      Author: gvigelet
 */

#ifndef INC_I2C_MASTER_H_
#define INC_I2C_MASTER_H_

#include "main.h"
#include <stdio.h>
#include <stdbool.h>

void I2C_scan(I2C_HandleTypeDef *hi2c);
uint16_t I2C_read_CDCE6214_reg(I2C_HandleTypeDef *hi2c, uint8_t i2c_addr, uint16_t reg_addr);
bool I2C_write_CDCE6214_reg(I2C_HandleTypeDef *hi2c, uint8_t i2c_addr, uint16_t reg_addr, uint16_t reg_val);

#endif /* INC_I2C_MASTER_H_ */
