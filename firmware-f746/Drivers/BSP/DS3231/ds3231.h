#ifndef __DS3231_H
#define __DS3231_H

#include "stm32f7xx_hal.h"

typedef struct
{
  uint8_t seconds;
  uint8_t minutes;
  uint8_t hours;
  uint8_t day;
  uint8_t date;
  uint8_t month;
  uint8_t year;
} DS3231_TimeDate;

#define DS3231_I2C_ADDRESS      0x68U

uint8_t DS3231_Init(I2C_HandleTypeDef *hi2c);
uint8_t DS3231_GetTimeDate(I2C_HandleTypeDef *hi2c, DS3231_TimeDate *rtc);
uint8_t DS3231_SetTimeDate(I2C_HandleTypeDef *hi2c, const DS3231_TimeDate *rtc);

#endif
