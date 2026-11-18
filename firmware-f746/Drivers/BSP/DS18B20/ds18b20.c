/******************************************************************************
*  @file    ds18b20.c
*  @author  Ferhat YOL
*  @version V3.0.0
*  @date    05-11-2026
*  @brief   DS18B20 Temperature Sensor Library (non-blocking, multi-instance)
******************************************************************************/
#include "ds18b20.h"

#define DS18B20_CMD_SKIP_ROM         0xCCU
#define DS18B20_CMD_CONVERT_TEMP     0x44U
#define DS18B20_CMD_READ_SCRATCHPAD  0xBEU
#define DS18B20_CMD_WRITE_SCRATCHPAD 0x4EU
#define DS18B20_SCRATCHPAD_SIZE      9U
#define DS18B20_POWERON_RAW          0x0550   /* 85.00 C */

static uint8_t Ds18B20_CRC8(const uint8_t *data, uint8_t length)
{
  uint8_t crc = 0U;

  while (length-- != 0U)
  {
    uint8_t value = *data++;
    for (uint8_t bit = 0U; bit < 8U; bit++)
    {
      uint8_t mix = (uint8_t)((crc ^ value) & 1U);
      crc >>= 1U;
      if (mix != 0U)
      {
        crc ^= 0x8CU;
      }
      value >>= 1U;
    }
  }
  return crc;
}

/* Hata kaydi: sayaclar + ARIZA bayragi, sonra bekleyip yeniden dene */
static void Ds18B20_SetError(DS18B20_HandleTypeDef *hds, DS18B20_StatusTypeDef err)
{
  hds->status = err;
  hds->error_count++;
  if (hds->consecutive_errors < 255U)
  {
    hds->consecutive_errors++;
  }
  if (hds->consecutive_errors >= DS18B20_FAULT_LIMIT)
  {
    hds->fault = 1U;
  }
  hds->tick  = HAL_GetTick();
  hds->state = DS_STATE_WAIT_RETRY;
}

DS18B20_StatusTypeDef Ds18B20_Init(DS18B20_HandleTypeDef *hds, GPIO_TypeDef *port, uint16_t pin, const char *name)
{
  hds->name               = name;
  hds->state              = DS_STATE_IDLE;
  hds->status             = DS18B20_BUSY;
  hds->tick               = 0U;
  hds->raw                = 0;
  hds->temperature_x100   = 0;
  hds->valid              = 0U;
  hds->fault              = 0U;
  hds->consecutive_errors = 0U;
  hds->ok_count           = 0U;
  hds->error_count        = 0U;
  hds->last_ok_tick       = 0U;

  if (OneWire_Init(&hds->ow, port, pin) != HAL_OK)
  {
    hds->status = DS18B20_ERROR;
    return hds->status;
  }

  /* 12 bit cozunurluk (fabrika ayari da 12 bit; garanti icin yazilir) */
  if (OneWire_Reset(&hds->ow) == HAL_OK)
  {
    OneWire_WriteByte(&hds->ow, DS18B20_CMD_SKIP_ROM);
    OneWire_WriteByte(&hds->ow, DS18B20_CMD_WRITE_SCRATCHPAD);
    OneWire_WriteByte(&hds->ow, 0x4BU);   /* TH (kullanilmiyor) */
    OneWire_WriteByte(&hds->ow, 0x46U);   /* TL (kullanilmiyor) */
    OneWire_WriteByte(&hds->ow, 0x7FU);   /* CONFIG: 12 bit     */
  }
  return DS18B20_OK;
}

/**
  * @brief  Bloklamayan gorev. Ana donguden olabildigince sik cagirin.
  *         IDLE -> (Convert T) -> CONVERTING -(750 ms)-> READING -> IDLE ...
  *         Hata olursa WAIT_RETRY'de 1 s bekler, sonra yeniden dener.
  */
void Ds18B20_Task(DS18B20_HandleTypeDef *hds)
{
  switch (hds->state)
  {
    case DS_STATE_WAIT_RETRY:
      if ((HAL_GetTick() - hds->tick) < DS18B20_RETRY_MS)
      {
        return;
      }
      hds->state = DS_STATE_IDLE;
      /* fall through */

    case DS_STATE_IDLE:
      if (OneWire_Reset(&hds->ow) != HAL_OK)
      {
        Ds18B20_SetError(hds, DS18B20_ERR_NO_SENSOR);
        return;
      }
      OneWire_WriteByte(&hds->ow, DS18B20_CMD_SKIP_ROM);
      OneWire_WriteByte(&hds->ow, DS18B20_CMD_CONVERT_TEMP);
      hds->tick  = HAL_GetTick();
      hds->state = DS_STATE_CONVERTING;
      return;

    case DS_STATE_CONVERTING:
      if ((HAL_GetTick() - hds->tick) < DS18B20_CONVERSION_TIME_MS)
      {
        return;                           /* bekleme bloklamaz */
      }
      hds->state = DS_STATE_READING;
      /* fall through */

    case DS_STATE_READING:
    {
      if (OneWire_Reset(&hds->ow) != HAL_OK)
      {
        Ds18B20_SetError(hds, DS18B20_ERR_NO_SENSOR);
        return;
      }
      OneWire_WriteByte(&hds->ow, DS18B20_CMD_SKIP_ROM);
      OneWire_WriteByte(&hds->ow, DS18B20_CMD_READ_SCRATCHPAD);
      for (uint8_t index = 0U; index < DS18B20_SCRATCHPAD_SIZE; index++)
      {
        hds->scratchpad[index] = OneWire_ReadByte(&hds->ow);
      }

      if (Ds18B20_CRC8(hds->scratchpad, 8U) != hds->scratchpad[8])
      {
        Ds18B20_SetError(hds, DS18B20_ERR_CRC);   /* kopuk hatta tum baytlar 0xFF */
        return;
      }

      int16_t raw = (int16_t)(((uint16_t)hds->scratchpad[1] << 8U) | hds->scratchpad[0]);
      if (raw == DS18B20_POWERON_RAW)
      {
        Ds18B20_SetError(hds, DS18B20_ERR_POWERON);
        return;
      }

      hds->raw                = raw;
      hds->temperature_x100   = ((int32_t)raw * 100 + ((raw >= 0) ? 8 : -8)) / 16;  /* yuvarlamali */
      hds->valid              = 1U;
      hds->fault              = 0U;
      hds->consecutive_errors = 0U;
      hds->status             = DS18B20_OK;
      hds->ok_count++;
      hds->last_ok_tick       = HAL_GetTick();
      hds->state              = DS_STATE_IDLE;   /* sonraki cagrida yeni donusum */
      return;
    }

    default:
      hds->state = DS_STATE_IDLE;
      return;
  }
}

uint8_t Ds18B20_IsReady(const DS18B20_HandleTypeDef *hds)
{
  return (hds->valid && hds->status == DS18B20_OK) ? 1U : 0U;
}

float Ds18B20_GetTemperature(const DS18B20_HandleTypeDef *hds)
{
  return (float)hds->raw * 0.0625f;
}

DS18B20_StatusTypeDef Ds18B20_GetStatus(const DS18B20_HandleTypeDef *hds)
{
  return hds->status;
}

const char *Ds18B20_StatusString(DS18B20_StatusTypeDef status)
{
  switch (status)
  {
    case DS18B20_OK:            return "OK";
    case DS18B20_BUSY:          return "BEKLIYOR";
    case DS18B20_ERR_NO_SENSOR: return "SENSOR YOK";
    case DS18B20_ERR_CRC:       return "CRC";
    case DS18B20_ERR_POWERON:   return "85C";
    case DS18B20_ERROR:         return "HATA";
    default:                    return "?";
  }
}
