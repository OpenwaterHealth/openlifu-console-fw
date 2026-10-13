/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * uart_comms.h
 *
 *  Created on: May 13, 2024
 *      Author: gvigelet
 */

#ifndef INC_UART_COMMS_H_
#define INC_UART_COMMS_H_

#include "main.h"  // This should contain your HAL includes and other basic includes.
#include "common.h"
#include <stdio.h>
#include <stdbool.h>

void comms_start_task(void);
void comms_handle_RxCpltCallback(const UART_HandleTypeDef *huart, uint16_t Size);
void comms_handle_TxCallback(const UART_HandleTypeDef *huart);
void CDC_handle_TxCpltCallback();

#endif /* INC_UART_COMMS_H_ */
