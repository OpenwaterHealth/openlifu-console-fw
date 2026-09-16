/*
 * utils.c
 *
 *  Created on: Jan 3, 2024
 *      Author: gvigelet
 */

#include "utils.h"
#include "dbg_print.h"
#include <stdio.h>
#include <string.h>

// testing crc calculations
#define BUFFER_SIZE  9
static const uint8_t CRC16_DATA8[BUFFER_SIZE] = {0x4D, 0x3C, 0x2B, 0x1A,
											   0xBE, 0x71, 0xC9, 0x8A,
											   0x5E};
uint32_t startTime = 0, endTime = 0, duration = 0;

static inline uint32_t bswap32(uint32_t x)
{
    return ((x & 0x000000FFU) << 24) |
           ((x & 0x0000FF00U) << 8)  |
           ((x & 0x00FF0000U) >> 8)  |
           ((x & 0xFF000000U) >> 24);
}

uint8_t crc_test(){
	  // Start TIM3
	  HAL_TIM_Base_Start(&htim3);

	  // Measure CPU CRC calculation time
	  startTime = __HAL_TIM_GET_COUNTER(&htim3);

	  DBG_PRINTF("CRC Test\r\n");
	  uint16_t cpu_CRC = util_crc16((uint8_t*)CRC16_DATA8, BUFFER_SIZE);

	  endTime = __HAL_TIM_GET_COUNTER(&htim3);
	  duration = endTime - startTime;
	  DBG_PRINTF("CPU CRC: 0x%04x Duration: %lu us\r\n\r\n", cpu_CRC, (unsigned long)duration);


	  // Reset Counter if needed
	  __HAL_TIM_SET_COUNTER(&htim3, 0);

	  // Measure HW CRC calculation time
	  startTime = __HAL_TIM_GET_COUNTER(&htim3);

	  uint16_t hw_CRC = util_hw_crc16((uint8_t*)CRC16_DATA8, BUFFER_SIZE);
	  endTime = __HAL_TIM_GET_COUNTER(&htim3);
	  duration = endTime - startTime;
	  DBG_PRINTF("HW CRC: 0x%04x Duration: %lu us\r\n\r\n", hw_CRC, (unsigned long)duration);

	  return cpu_CRC == hw_CRC?0:1;
}

// CRC16-ccitt lookup table
const uint16_t crc16_tab[256] = {
	0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
	0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
	0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
	0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
	0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
	0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
	0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
	0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
	0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
	0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
	0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
	0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
	0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
	0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
	0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
	0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
	0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
	0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
	0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
	0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
	0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
	0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
	0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
	0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
	0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
	0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
	0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
	0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
	0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
	0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
	0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
	0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0,
};


void printBuffer(const uint8_t* buffer, uint32_t size) {
	DBG_PRINTF("\r\nBuffer\r\n\r\n");
    for (uint32_t i = 0; i < size; i++) {
        DBG_PRINTF("%02X ", buffer[i]); // Print each byte in hexadecimal format
    }
    DBG_PRINTF("\r\n\r\n"); // Print a newline character to separate the output
}

uint16_t util_crc16(const uint8_t* buf, uint32_t size) {
	uint16_t crc = 0xFFFF;

	for (int i = 0; i < size; i++) {
		uint8_t byte = buf[i];
		crc = (crc<<8) ^ crc16_tab[(crc>>8)^byte];
	}

	return crc;
}

uint16_t util_hw_crc16(uint8_t* buf, uint32_t size)
{
	uint32_t uwCRCValue = HAL_CRC_Accumulate(&hcrc, (uint32_t *)buf, size);
	DBG_PRINTF("uwCRCValue 0x%08lx\r\n", (unsigned long)uwCRCValue);
	return (uint16_t)uwCRCValue;
}

void get_unique_identifier(uint32_t* uid)
{
    uid[0] = HAL_GetUIDw0();
    uid[1] = HAL_GetUIDw1();
    uid[2] = HAL_GetUIDw2();
}

// FNV-1a 32-bit hash function
uint32_t fnv1a_32(const uint8_t *data, size_t len) {
    const uint32_t FNV_prime = 16777619;
    uint32_t hash = 2166136261U;

    for (size_t i = 0; i < len; ++i) {
        hash ^= (uint32_t)data[i];
        hash *= FNV_prime;
    }

    return hash;
}

float be_bytes_to_float(const uint8_t *b, size_t len)
{
    if (len < 4) return 0.0f;

    /* Build the 32-bit bitpattern from MSB..LSB in the natural numeric order.
       Do NOT byte-swap this value afterwards. */
    uint32_t u = ((uint32_t)b[0] << 24) |
                 ((uint32_t)b[1] << 16) |
                 ((uint32_t)b[2] << 8)  |
                  (uint32_t)b[3];

    float f;
    memcpy(&f, &u, sizeof(f)); /* copy bit-pattern into float (safe, no UB) */
    return f;
}

void US_Delay_Init(void)
{
    HAL_TIM_Base_Start(&US_DELAY_TIMER);
}

void delay_us(uint32_t us)
{
    uint32_t start = __HAL_TIM_GET_COUNTER(&US_DELAY_TIMER);  // current timer value

    // Wait until 'us' ticks have elapsed (1 tick = 1 us)
    while ((uint32_t)(__HAL_TIM_GET_COUNTER(&US_DELAY_TIMER) - start) < us)
    {
        __NOP();
    }
}


void delay_ms(uint32_t ms)
{
    uint32_t delay_cycles = (SystemCoreClock / 1000) * ms;
    while (delay_cycles--) {
        __NOP();  // Ensures the loop doesn't get optimized away
    }
}
