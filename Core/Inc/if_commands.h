/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * if_commands.h
 *
 *  Created on: Nov 15, 2024
 *      Author: GeorgeVigelette
 */

#ifndef INC_IF_COMMANDS_H_
#define INC_IF_COMMANDS_H_


#include "common.h"
#include "utils.h"

UartPacket process_if_command(UartPacket cmd);

#endif /* INC_IF_COMMANDS_H_ */
