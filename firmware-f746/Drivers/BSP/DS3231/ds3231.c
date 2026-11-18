#include "ds3231.h"

static uint8_t bcd2dec(uint8_t val) { return (val >> 4) * 10 + (val & 0x0F); }
static uint8_t dec2bcd(uint8_t val) { return ((val / 10) << 4) | (val % 10); }

uint8_t DS3231_Init(I2C_HandleTypeDef *hi2c)
{
  uint8_t data[2] = {0x0E, 0x00};
  return HAL_I2C_Master_Transmit(hi2c, DS3231_I2C_ADDRESS << 1, data, 2, 1000) == HAL_OK ? 1 : 0;
}

uint8_t DS3231_GetTimeDate(I2C_HandleTypeDef *hi2c, DS3231_TimeDate *rtc)
{
  uint8_t reg = 0x00;
  uint8_t data[7];
  if (HAL_I2C_Master_Transmit(hi2c, DS3231_I2C_ADDRESS << 1, &reg, 1, 1000) != HAL_OK)
  {
    return 0;
  }

  if (HAL_I2C_Master_Receive(hi2c, DS3231_I2C_ADDRESS << 1, data, 7, 1000) != HAL_OK)
  {
    return 0;
  }

  rtc->seconds = bcd2dec(data[0]);
  rtc->minutes = bcd2dec(data[1]);
  rtc->hours   = bcd2dec(data[2]);
  rtc->day     = bcd2dec(data[3]);
  rtc->date    = bcd2dec(data[4]);
  rtc->month   = bcd2dec(data[5]);
  rtc->year    = bcd2dec(data[6]);
  return 1;
}

uint8_t DS3231_SetTimeDate(I2C_HandleTypeDef *hi2c, const DS3231_TimeDate *rtc)
{
  uint8_t data[8];
  data[0] = 0x00;
  data[1] = dec2bcd(rtc->seconds);
  data[2] = dec2bcd(rtc->minutes);
  data[3] = dec2bcd(rtc->hours);
  data[4] = dec2bcd(rtc->day);
  data[5] = dec2bcd(rtc->date);
  data[6] = dec2bcd(rtc->month);
  data[7] = dec2bcd(rtc->year);
  return HAL_I2C_Master_Transmit(hi2c, DS3231_I2C_ADDRESS << 1, data, 8, 1000) == HAL_OK ? 1 : 0;
}
