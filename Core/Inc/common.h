/*
 * common.h
 *
 *  Created on: Apr 3, 2024
 *      Author: gvigelet
 */

#ifndef INC_COMMON_H_
#define INC_COMMON_H_

#include "version.h"

#include <stdint.h>
#include <stdbool.h>

#define COMMAND_MAX_SIZE 2080
#define DATA_MAX_SIZE 2048

#ifndef FW_VERSION
#define FW_VERSION "unknown"
#endif
#ifndef FW_SHA
#define FW_SHA "unknown"
#endif
#ifndef FW_BUILD_TIME
#define FW_BUILD_TIME "unknown"
#endif

#define FW_VERSION_STRING FW_VERSION
#define FW_SHA_STRING FW_SHA
#define FW_BUILD_TIME_STRING FW_BUILD_TIME


typedef enum {
	OW_START_BYTE = 0xAA,
	OW_END_BYTE = 0xDD,
} USTX_ProtocolTypes;

typedef enum {
	OW_ACK = 0xE0,
	OW_NAK = 0xE1,
	OW_CMD = 0xE2,
	OW_RESP = 0xE3,
	OW_DATA = 0xE4,
	OW_JSON = 0xE5,
	OW_I2C_PASSTHRU = 0xE9,
	OW_CONTROLLER = 0xEA,
	OW_POWER = 0xEB,
	OW_BAD_PARSE = 0xEC,
	OW_BAD_CRC = 0xED,
	OW_UNKNOWN = 0xEE,
	OW_ERROR = 0xEF,

} UartPacketTypes;

typedef enum {
	OW_CODE_SUCCESS = 0x00,
	OW_CODE_IDENT_ERROR = 0xFD,
	OW_CODE_DATA_ERROR = 0xFE,
	OW_CODE_ERROR = 0xFF,
} UstxErrorCodes;

typedef enum {
	OW_CMD_PING = 0x00,
	OW_CMD_PONG = 0x01,
	OW_CMD_VERSION = 0x02,
	OW_CMD_ECHO = 0x03,
	OW_CMD_TOGGLE_LED = 0x04,
	OW_CMD_HWID = 0x05,
	OW_CMD_USR_CFG = 0x0A,
	OW_CMD_DFU = 0x0D,
	OW_CMD_NOP = 0x0E,
	OW_CMD_RESET = 0x0F,
} UstxGlobalCommands;

typedef enum {
	OW_CTRL_SCAN_I2C = 0x10,
	OW_CTRL_WRITE_I2C = 0x11,
	OW_CTRL_READ_I2C = 0x12,
	OW_CTRL_SET_SWTRIG = 0x13,
	OW_CTRL_GET_SWTRIG = 0x14,
	OW_CTRL_START_SWTRIG = 0x15,
	OW_CTRL_STOP_SWTRIG = 0x16,
	OW_CTRL_STATUS_SWTRIG = 0x17,
} UstxControllerCommands;

typedef enum {
	OW_POWER_STATUS = 0x30,
	OW_POWER_SET_HV = 0x31,
	OW_POWER_GET_HV = 0x32,
	OW_POWER_HV_ON = 0x33,
	OW_POWER_HV_OFF = 0x34,
	OW_POWER_12V_ON = 0x35,
	OW_POWER_12V_OFF = 0x36,
	OW_POWER_GET_TEMP1 = 0x37,
	OW_POWER_GET_TEMP2 = 0x38,
	OW_POWER_SET_FAN = 0x39,
	OW_POWER_GET_FAN = 0x3A,
	OW_POWER_SET_RGB = 0x3B,
	OW_POWER_GET_RGB = 0x3C,
	OW_POWER_GET_HVON = 0x3D,
	OW_POWER_GET_12VON = 0x3E,
	OW_POWER_SET_DACS = 0x3F,
	OW_POWER_VMON = 0x40,
	OW_POWER_RAW_DAC = 0x41,
	OW_POWER_HV_ENABLE = 0x42,
	OW_POWER_SET_RGB_FX = 0x43,
} UstxPowerCommands;

/* OW_POWER_SET_RGB_FX payload (cmd.data, little-endian):
 *   byte 0    effect id (RgbFxId)
 *   byte 1-3  r, g, b        (primary color; ignored for STOP/RAINBOW)
 *   byte 4-5  period_ms u16  (FADE duration, BREATHE/RAINBOW/FLASH period,
 *                             CYCLE dwell per color; ignored for STOP/SOLID)
 *   byte 6+   CYCLE only: additional {r,g,b} triplets (first color comes
 *             from bytes 1-3; max 8 colors total)
 * Basic OW_POWER_SET_RGB / OW_POWER_GET_RGB are unchanged; effects do not
 * alter the basic enum state reported by OW_POWER_GET_RGB. */
typedef enum {
	OW_RGB_FX_STOP    = 0,  /* cancel effect, hold current color */
	OW_RGB_FX_SOLID   = 1,  /* static 24-bit color               */
	OW_RGB_FX_FADE    = 2,  /* fade from current color to r,g,b  */
	OW_RGB_FX_BREATHE = 3,  /* brightness 0->full->0, repeating  */
	OW_RGB_FX_RAINBOW = 4,  /* hue wheel sweep, repeating        */
	OW_RGB_FX_FLASH   = 5,  /* 50% on/off blink, repeating       */
	OW_RGB_FX_CYCLE   = 6,  /* step through a color list         */
} RgbFxId;

typedef struct  {
	uint16_t id;
	uint8_t packet_type;
	uint8_t command;
	uint8_t addr;
	uint8_t reserved;
	uint16_t data_len;
	uint8_t* data;
	uint16_t crc;
} UartPacket;

typedef struct  {
	uint32_t magic_num;
	float hv_settng;
	bool auto_on;
	uint8_t reserved;
	uint8_t reserved1;
	uint8_t reserved2;
	uint16_t data_len;
	uint8_t* data;
	uint16_t crc;
} LifuConfig;



#endif /* INC_COMMON_H_ */
