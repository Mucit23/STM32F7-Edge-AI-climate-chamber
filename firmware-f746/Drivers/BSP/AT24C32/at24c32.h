/**
  ******************************************************************************
  * @file    at24c32.h
  * @brief   AT24C32 (4 kB) I2C EEPROM surucusu - DS3231 modulu uzerindeki
  *          EEPROM. 16 bit adres, 32 bayt sayfa, yazma cevrimi <= 10 ms.
  ******************************************************************************
  */
#ifndef __AT24C32_H
#define __AT24C32_H

#include "stm32f7xx_hal.h"

#define AT24C32_SIZE        4096U
#define AT24C32_PAGE_SIZE   32U

typedef struct
{
  I2C_HandleTypeDef *hi2c;
  uint8_t            addr7;   /* bulunan 7 bit adres (genelde 0x57), 0 = yok */
} AT24C32_HandleTypeDef;

/* 0x50..0x57 araligini tarar; bulursa 1 doner */
uint8_t           AT24C32_Init(AT24C32_HandleTypeDef *hee, I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef AT24C32_Read(AT24C32_HandleTypeDef *hee, uint16_t mem, uint8_t *buf, uint16_t len);
HAL_StatusTypeDef AT24C32_Write(AT24C32_HandleTypeDef *hee, uint16_t mem, const uint8_t *buf, uint16_t len);

#endif /* __AT24C32_H */
