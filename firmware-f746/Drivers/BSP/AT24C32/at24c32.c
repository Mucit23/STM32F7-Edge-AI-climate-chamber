/**
  ******************************************************************************
  * @file    at24c32.c
  * @brief   AT24C32 I2C EEPROM surucusu
  *  - Yazma sayfa sinirinda bolunur (32 bayt).
  *  - Her sayfa yazmasindan sonra EEPROM ACK verene kadar beklenir
  *    (acknowledge polling, en fazla ~10 ms).
  *  - ZS-042 tipi DS3231 modullerinde A0..A2 pull-up'li: adres 0x57.
  ******************************************************************************
  */
#include "at24c32.h"

#define AT24C32_TIMEOUT_MS     50U
#define AT24C32_WRITE_WAIT_MS  15U

uint8_t AT24C32_Init(AT24C32_HandleTypeDef *hee, I2C_HandleTypeDef *hi2c)
{
  hee->hi2c  = hi2c;
  hee->addr7 = 0U;

  /* Once en yaygin adres, sonra tum aralik */
  if (HAL_I2C_IsDeviceReady(hi2c, (uint16_t)(0x57U << 1), 2, 5) == HAL_OK)
  {
    hee->addr7 = 0x57U;
    return 1U;
  }
  for (uint8_t a = 0x50U; a <= 0x56U; a++)
  {
    if (HAL_I2C_IsDeviceReady(hi2c, (uint16_t)(a << 1), 2, 5) == HAL_OK)
    {
      hee->addr7 = a;
      return 1U;
    }
  }
  return 0U;
}

HAL_StatusTypeDef AT24C32_Read(AT24C32_HandleTypeDef *hee, uint16_t mem, uint8_t *buf, uint16_t len)
{
  if (hee->addr7 == 0U || (uint32_t)mem + len > AT24C32_SIZE)
  {
    return HAL_ERROR;
  }
  return HAL_I2C_Mem_Read(hee->hi2c, (uint16_t)(hee->addr7 << 1), mem, I2C_MEMADD_SIZE_16BIT,
                          buf, len, AT24C32_TIMEOUT_MS);
}

static HAL_StatusTypeDef AT24C32_WaitReady(AT24C32_HandleTypeDef *hee)
{
  uint32_t t0 = HAL_GetTick();
  while ((HAL_GetTick() - t0) < AT24C32_WRITE_WAIT_MS)
  {
    if (HAL_I2C_IsDeviceReady(hee->hi2c, (uint16_t)(hee->addr7 << 1), 1, 2) == HAL_OK)
    {
      return HAL_OK;
    }
  }
  return HAL_TIMEOUT;
}

HAL_StatusTypeDef AT24C32_Write(AT24C32_HandleTypeDef *hee, uint16_t mem, const uint8_t *buf, uint16_t len)
{
  if (hee->addr7 == 0U || (uint32_t)mem + len > AT24C32_SIZE)
  {
    return HAL_ERROR;
  }

  while (len > 0U)
  {
    uint16_t room  = (uint16_t)(AT24C32_PAGE_SIZE - (mem % AT24C32_PAGE_SIZE));
    uint16_t chunk = (len < room) ? len : room;

    if (HAL_I2C_Mem_Write(hee->hi2c, (uint16_t)(hee->addr7 << 1), mem, I2C_MEMADD_SIZE_16BIT,
                          (uint8_t *)buf, chunk, AT24C32_TIMEOUT_MS) != HAL_OK)
    {
      return HAL_ERROR;
    }
    if (AT24C32_WaitReady(hee) != HAL_OK)
    {
      return HAL_TIMEOUT;
    }
    mem += chunk;
    buf += chunk;
    len  = (uint16_t)(len - chunk);
  }
  return HAL_OK;
}
