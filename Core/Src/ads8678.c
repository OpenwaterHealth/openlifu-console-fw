/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * ads8678.c
 *
 *  Created on: Nov 12, 2025
 *      Author: gvigelet
 */

#include "ads8678.h"
#include "utils.h"
#include <string.h>

// Private helper functions
static inline void ADS8678_CS_Low(ADS8678__HandleTypeDef *dev) {
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_RESET);
}

static inline void ADS8678_CS_High(ADS8678__HandleTypeDef *dev) {
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);
}

static inline void ADS8678_RST_Low(ADS8678__HandleTypeDef *dev) {
    if (dev->rst_port != NULL) {
        HAL_GPIO_WritePin(dev->rst_port, dev->rst_pin, GPIO_PIN_RESET);
    }
}

static inline void ADS8678_RST_High(ADS8678__HandleTypeDef *dev) {
    if (dev->rst_port != NULL) {
        HAL_GPIO_WritePin(dev->rst_port, dev->rst_pin, GPIO_PIN_SET);
    }
}

/**
 * @brief Write a command to the ADS8678 and optionally read response
 * @param dev Pointer to device handle
 * @param command 16-bit command to send
 * @param response Pointer to store 16-bit response (can be NULL)
 * @retval HAL status
 */
HAL_StatusTypeDef ADS8678_WriteCommand(ADS8678__HandleTypeDef *dev, uint16_t command, uint16_t *response)
{
    if (dev == NULL || dev->spi == NULL) {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef status;
    uint8_t tx_data[4];
    uint8_t rx_data[4];

    // Convert command to bytes (MSB first) - 16 bits command + 16 bits to clock out data
    tx_data[0] = (command >> 8) & 0xFF;
    tx_data[1] = command & 0xFF;
    tx_data[2] = 0x00;
    tx_data[3] = 0x00;

    ADS8678_CS_Low(dev);
    delay_us(1); // Small delay after CS low
    status = HAL_SPI_TransmitReceive(dev->spi, tx_data, rx_data, 4, dev->spi_timeout_ms);
    ADS8678_CS_High(dev);

    if (status == HAL_OK && response != NULL) {
        // Data is in bytes 2 and 3, right-aligned 14-bit data needs shift by 2
        uint16_t raw_data = ((uint16_t)rx_data[2] << 8) | rx_data[3];
        *response = raw_data >> 2; // Right align 14-bit data
    }

    return status;
}

/**
 * @brief Write a value to an ADS8678 register
 * @param dev Pointer to device handle
 * @param reg_addr Register address (already shifted)
 * @param value Value to write (8-bit)
 * @retval HAL status
 */
HAL_StatusTypeDef ADS8678_WriteRegister(ADS8678__HandleTypeDef *dev, uint16_t reg_addr, uint8_t value)
{
    if (dev == NULL || dev->spi == NULL) {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef status;
    uint8_t tx_data[3];
    uint8_t rx_data[3];

    // Format: reg_addr (already includes bit 8 for W/R) | WRITE bit | value
    uint32_t command = (reg_addr | ADS8678_PROG_REG_WRITE | value);
    
    // Convert to bytes (MSB first), 24-bit total
    tx_data[0] = (command >> 16) & 0xFF;
    tx_data[1] = (command >> 8) & 0xFF;
    tx_data[2] = command & 0xFF;

    ADS8678_CS_Low(dev);
    delay_us(1);
    status = HAL_SPI_TransmitReceive(dev->spi, tx_data, rx_data, 3, dev->spi_timeout_ms);
    ADS8678_CS_High(dev);

    return status;
}

/**
 * @brief Read a value from an ADS8678 register
 * @param dev Pointer to device handle
 * @param reg_addr Register address (already shifted)
 * @param value Pointer to store read value
 * @retval HAL status
 */
HAL_StatusTypeDef ADS8678_ReadRegister(ADS8678__HandleTypeDef *dev, uint16_t reg_addr, uint8_t *value)
{
    if (dev == NULL || dev->spi == NULL || value == NULL) {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef status;
    uint8_t tx_data[3];
    uint8_t rx_data[3];

    // Format: reg_addr | READ bit (0x00)
    uint32_t command = (reg_addr | ADS8678_PROG_REG_READ);
    
    tx_data[0] = (command >> 16) & 0xFF;
    tx_data[1] = (command >> 8) & 0xFF;
    tx_data[2] = command & 0xFF;

    ADS8678_CS_Low(dev);
    delay_us(1);
    status = HAL_SPI_TransmitReceive(dev->spi, tx_data, rx_data, 3, dev->spi_timeout_ms);
    ADS8678_CS_High(dev);

    if (status == HAL_OK) {
        *value = rx_data[2]; // Data is in the last byte
    }

    return status;
}

/**
 * @brief Hardware reset of the ADS8678
 * @param dev Pointer to device handle
 * @retval HAL status
 */
HAL_StatusTypeDef ADS8678_Reset(ADS8678__HandleTypeDef *dev)
{
    if (dev == NULL) {
        return HAL_ERROR;
    }

    // Hardware reset if RST pin is available
    if (dev->rst_port != NULL) {
        ADS8678_RST_Low(dev);
        delay_us(1000); // Hold reset for at least 10ns (1ms is safe)
        ADS8678_RST_High(dev);
        delay_us(10000); // Wait for device to initialize
    }

    // Software reset
    HAL_StatusTypeDef status = ADS8678_WriteCommand(dev, ADS8678_CMD_RST, NULL);
    delay_us(10000); // Wait for reset to complete

    return status;
}

/**
 * @brief Read the device ID (not always reliable on ADS8678)
 * @param dev Pointer to device handle
 * @param device_id Pointer to store device ID
 * @retval HAL status
 */
HAL_StatusTypeDef ADS8678_ReadDeviceID(const ADS8678__HandleTypeDef *dev, uint16_t *device_id)
{
    if (dev == NULL || device_id == NULL) {
        return HAL_ERROR;
    }

    // Note: Device ID read may not be reliable, mainly for testing
    *device_id = 0x0000;
    return HAL_OK;
}

/**
 * @brief Set the input range for a specific channel
 * @param dev Pointer to device handle
 * @param channel Channel number (0-7)
 * @param range Range configuration
 * @retval HAL status
 */
HAL_StatusTypeDef ADS8678_SetChannelRange(ADS8678__HandleTypeDef *dev, uint8_t channel, ADS8678_Range_t range)
{
    if (dev == NULL || channel >= ADS8678_NUM_CHANNELS) {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef status;
    uint8_t reg_addr = ADS8678_REG_RANGE_CH0 + channel;

    status = ADS8678_WriteRegister(dev, reg_addr, (uint8_t)range);
    if (status == HAL_OK) {
        dev->channel_ranges[channel] = range;
    }

    return status;
}

/**
 * @brief Set the input range for all channels
 * @param dev Pointer to device handle
 * @param range Range configuration
 * @retval HAL status
 */
HAL_StatusTypeDef ADS8678_SetAllChannelRanges(ADS8678__HandleTypeDef *dev, ADS8678_Range_t range)
{
    if (dev == NULL) {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef status = HAL_OK;

    for (uint8_t i = 0; i < ADS8678_NUM_CHANNELS; i++) {
        status = ADS8678_SetChannelRange(dev, i, range);
        if (status != HAL_OK) {
            return status;
        }
    }

    return status;
}

/**
 * @brief Initialize the ADS8678
 * @param dev Pointer to device handle
 * @retval HAL status
 */
HAL_StatusTypeDef ADS8678_Init(ADS8678__HandleTypeDef *dev)
{
    if (dev == NULL || dev->spi == NULL) {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef status;

    // Set default values if not configured
    if (dev->spi_timeout_ms == 0) {
        dev->spi_timeout_ms = 100;
    }
    if (dev->api_retry_count == 0) {
        dev->api_retry_count = 3;
    }

    // Initialize CS pin high (inactive)
    ADS8678_CS_High(dev);

    // Initialize RST pin high (not in reset)
    ADS8678_RST_High(dev);

    delay_us(1000);

    // Hardware and software reset
    status = ADS8678_Reset(dev);
    if (status != HAL_OK) {
        return status;
    }

    // Configure auto-sequencing for channels 0-4 (like reference code)
    // Channels to enable: CH0, CH1, CH2, CH3, CH4
    //uint8_t auto_seq = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3);
    //status = ADS8678_WriteRegister(dev, ADS8678_REG_AUTO_SEQ_EN, auto_seq);
    //if (status != HAL_OK) {
    //    return status;
    //}

    // Power down unused channels (5, 6, 7)
    uint8_t pwr_down =  (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7);
    status = ADS8678_WriteRegister(dev, ADS8678_REG_CHNL_PWR_DWN, pwr_down);
    if (status != HAL_OK) {
        return status;
    }

    // Set range for active channels (use unipolar 1.25V * Vref = 5.12V with 4.096V ref)
    for (uint8_t ch = 0; ch < 8; ch++) {
        status = ADS8678_SetChannelRange(dev, ch, ADS8678_RANGE_UNIPOLAR_1_25V);
        if (status != HAL_OK) {
            return status;
        }
    }

    // Set AUTO_RST mode to start conversions
    status = ADS8678_WriteCommand(dev, ADS8678_CMD_AUTO_RST, NULL);
    if (status != HAL_OK) {
        return status;
    }

    delay_us(1000);

    dev->initialized = true;

    return HAL_OK;
}

/**
 * @brief Read next conversion in AUTO_RST mode (cycles through enabled channels)
 * @param dev Pointer to device handle
 * @param channel Not used in AUTO mode, kept for compatibility
 * @param result Pointer to store 14-bit ADC result (right-aligned)
 * @retval HAL status
 */
HAL_StatusTypeDef ADS8678_ReadChannel(ADS8678__HandleTypeDef *dev, uint8_t channel, uint16_t *result)
{
    if (dev == NULL || result == NULL) {
        return HAL_ERROR;
    }

    (void)channel; // Not used in AUTO_RST mode

    // In AUTO_RST mode, just send NO-OP to get the next conversion
    // The ADC automatically cycles through enabled channels
    return ADS8678_WriteCommand(dev, ADS8678_CMD_NO_OP, result);
}

/**
 * @brief Read a specific channel using manual mode
 * @param dev Pointer to device handle
 * @param channel Channel number (0-7)
 * @param result Pointer to store 14-bit ADC result
 * @retval HAL status
 */
HAL_StatusTypeDef ADS8678_ReadChannelManual(ADS8678__HandleTypeDef *dev, uint8_t channel, uint16_t *result)
{
    if (dev == NULL || result == NULL || channel >= ADS8678_NUM_CHANNELS) {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef status;
    uint16_t command;
    uint16_t response;

    // Select the channel using manual mode command
    switch (channel) {
        case 0: command = ADS8678_CMD_MAN_Ch_0; break;
        case 1: command = ADS8678_CMD_MAN_Ch_1; break;
        case 2: command = ADS8678_CMD_MAN_Ch_2; break;
        case 3: command = ADS8678_CMD_MAN_Ch_3; break;
        case 4: command = ADS8678_CMD_MAN_Ch_4; break;
        case 5: command = ADS8678_CMD_MAN_Ch_5; break;
        case 6: command = ADS8678_CMD_MAN_Ch_6; break;
        case 7: command = ADS8678_CMD_MAN_Ch_7; break;
        default: return HAL_ERROR;
    }

    // Send channel select command and get previous conversion result
    status = ADS8678_WriteCommand(dev, command, &response);
    if (status != HAL_OK) {
        return status;
    }

    // Send NO-OP to get the selected channel's conversion result
    status = ADS8678_WriteCommand(dev, ADS8678_CMD_NO_OP, result);

    return status;
}

/**
 * @brief Read all 8 channels sequentially
 * @param dev Pointer to device handle
 * @param results Pointer to array to store 8 results
 * @retval HAL status
 */
HAL_StatusTypeDef ADS8678_ReadAllChannels(ADS8678__HandleTypeDef *dev, uint16_t *results)
{
    if (dev == NULL || results == NULL) {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef status;

    for (uint8_t i = 0; i < ADS8678_NUM_CHANNELS; i++) {
        status = ADS8678_ReadChannel(dev, i, &results[i]);
        if (status != HAL_OK) {
            return status;
        }
    }

    return HAL_OK;
}

/**
 * @brief Put device into standby mode
 * @param dev Pointer to device handle
 * @retval HAL status
 */
HAL_StatusTypeDef ADS8678_Standby(ADS8678__HandleTypeDef *dev)
{
    if (dev == NULL) {
        return HAL_ERROR;
    }

    return ADS8678_WriteCommand(dev, ADS8678_CMD_STDBY, NULL);
}

/**
 * @brief Put device into power-down mode
 * @param dev Pointer to device handle
 * @retval HAL status
 */
HAL_StatusTypeDef ADS8678_PowerDown(ADS8678__HandleTypeDef *dev)
{
    if (dev == NULL) {
        return HAL_ERROR;
    }

    return ADS8678_WriteCommand(dev, ADS8678_CMD_PWR_DN, NULL);
}
