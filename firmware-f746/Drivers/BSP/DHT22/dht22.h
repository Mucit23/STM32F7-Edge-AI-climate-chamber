/**
  ******************************************************************************
  * @file    dht22.h
  * @brief   DHT22 / AM2302 sicaklik-nem sensoru surucusu (STM32F7, tek hat)
  *          Zamanlama DWT dongu sayaci ile yapilir (us cozunurluk).
  ******************************************************************************
  */
#ifndef __DHT22_H
#define __DHT22_H

#include "stm32f7xx_hal.h"

/* Iki okuma arasi en kisa sure (sensor siniri 2 s) */
#define DHT22_MIN_INTERVAL_MS   2000U   /* veri sayfasi: iki okuma arasi en az 2 s */
#define DHT22_FAULT_LIMIT       3U      /* ust uste bu kadar hata = ARIZA          */

typedef enum
{
  DHT22_OK = 0,
  DHT22_ERR_NO_RESPONSE,   /* sensor baslatma sinyaline cevap vermedi  */
  DHT22_ERR_TIMEOUT,       /* veri bitleri okunurken zaman asimi       */
  DHT22_ERR_CHECKSUM,      /* 5. bayt (checksum) tutmadi               */
  DHT22_ERR_TOO_SOON       /* 2 s dolmadan okuma istendi, eski deger   */
} DHT22_Status;

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t      pin;

  int16_t  temperature_x10;   /* 0.1 C birimi, ornek: 234 = 23.4 C  */
  uint16_t humidity_x10;      /* 0.1 %RH birimi, ornek: 452 = 45.2 % */
  float    temperature;       /* C    */
  float    humidity;          /* %RH  */

  uint8_t      valid;         /* en az bir basarili okuma yapildi mi */
  DHT22_Status last_status;
  uint32_t     last_read_tick;
  uint32_t     ok_count;
  uint32_t     error_count;
  uint8_t      fault;              /* 1: ust uste DHT22_FAULT_LIMIT hata     */
  uint8_t      consecutive_errors;
  uint32_t     last_ok_tick;       /* son basarili okuma zamani              */
  uint8_t      raw[5];        /* son okumanin ham baytlari (hata ayiklama) */
} DHT22_HandleTypeDef;

void         DHT22_Init(DHT22_HandleTypeDef *hdht, GPIO_TypeDef *port, uint16_t pin);
DHT22_Status DHT22_Read(DHT22_HandleTypeDef *hdht);
void         DHT22_Task(DHT22_HandleTypeDef *hdht);   /* ana donguden surekli cagrilir */
const char  *DHT22_StatusString(DHT22_Status status);

#endif /* __DHT22_H */
