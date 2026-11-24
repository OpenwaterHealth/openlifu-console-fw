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

enum {
    OW_USR_CFG_READ  = 0,
    OW_USR_CFG_WRITE = 1,
};

#endif /* INC_IF_COMMANDS_H_ */
