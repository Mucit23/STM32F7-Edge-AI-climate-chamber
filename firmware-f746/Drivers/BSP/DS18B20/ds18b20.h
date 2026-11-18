/******************************************************************************
*  @file    ds18b20.h
*  @author  Ferhat YOL
*  @version V3.0.0
*  @date    05-11-2026
*  @brief   DS18B20 Temperature Sensor Library (non-blocking, multi-instance)
*
*  Bloklamayan durum makinesi: DS18B20_Task() ana donguden surekli cagrilir.
*  750 ms donusum suresi beklenmez; Task her cagrida yalnizca sirasi gelen
*  kisa islemi yapar ve doner (baslat -> bekle -> oku -> tekrar baslat).
*  Hat basina tek sensor: Skip ROM, adres taramaya gerek yok.
******************************************************************************/
#ifndef __DS18B20_H
#define __DS18B20_H

#include "stm32f7xx_hal.h"
#include "onewire.h"

#define DS18B20_CONVERSION_TIME_MS  750U   /**< 12 bit donusum suresi            */
#define DS18B20_RETRY_MS            1000U  /**< Hata sonrasi yeniden deneme      */
#define DS18B20_FAULT_LIMIT         3U     /**< Ust uste bu kadar hata = ARIZA   */

typedef enum
{
  DS18B20_OK = 0,          /**< Son okuma basarili                          */
  DS18B20_BUSY,            /**< Ilk olcum henuz tamamlanmadi                */
  DS18B20_ERR_NO_SENSOR,   /**< Presence yok: sensor takili degil/kablo kopuk */
  DS18B20_ERR_CRC,         /**< Scratchpad CRC hatasi: veri bozuk           */
  DS18B20_ERR_POWERON,     /**< 85.00 C guc-acilis degeri: donusum yapilmadi */
  DS18B20_ERROR            /**< Baslatma hatasi                             */
} DS18B20_StatusTypeDef;

typedef enum
{
  DS_STATE_IDLE = 0,
  DS_STATE_CONVERTING,
  DS_STATE_READING,
  DS_STATE_WAIT_RETRY
} DS18B20_StateTypeDef;

typedef struct
{
  OneWire_HandleTypeDef ow;
  const char           *name;             /**< ekranda/logda gorunen ad      */

  DS18B20_StateTypeDef  state;
  DS18B20_StatusTypeDef status;           /**< son islemin sonucu            */
  uint32_t              tick;             /**< donusum/yeniden deneme zamani */

  uint8_t               scratchpad[9];
  int16_t               raw;              /**< 1/16 C                        */
  int32_t               temperature_x100; /**< 0.01 C, ornek 2344 = 23.44 C  */

  uint8_t               valid;            /**< en az bir basarili okuma var  */
  uint8_t               fault;            /**< 1: ust uste FAULT_LIMIT hata  */
  uint8_t               consecutive_errors;
  uint32_t              ok_count;
  uint32_t              error_count;
  uint32_t              last_ok_tick;     /**< son basarili okuma zamani     */
} DS18B20_HandleTypeDef;

DS18B20_StatusTypeDef Ds18B20_Init(DS18B20_HandleTypeDef *hds, GPIO_TypeDef *port, uint16_t pin, const char *name);
void                  Ds18B20_Task(DS18B20_HandleTypeDef *hds);
uint8_t               Ds18B20_IsReady(const DS18B20_HandleTypeDef *hds);
float                 Ds18B20_GetTemperature(const DS18B20_HandleTypeDef *hds);
DS18B20_StatusTypeDef Ds18B20_GetStatus(const DS18B20_HandleTypeDef *hds);
const char           *Ds18B20_StatusString(DS18B20_StatusTypeDef status);

#endif /* __DS18B20_H */
