/**
  ******************************************************************************
  * @file    aio.h
  * @brief   Adafruit IO MQTT istemcisi (LwIP MQTT, RTOS'suz).
  *
  *          Aio_Task1Hz() ana dongudeki 1 Hz blogunda, Net_Task1Hz()'den sonra
  *          cagrilir. Baglanti (DNS -> TCP -> MQTT), yeniden deneme ve
  *          AIO_PUB_PERIOD_S aralikla grup gonderimi burada yapilir.
  *          Durum net.c uzerinden LCD basligina ve log'a yansir.
  *
  *          Gonderilen grup: <kullanici>/groups/<AIO_GROUP>/json
  *          Feed'ler: t-air, rh, t-peltier, t-wall, t-ambient, u-cmd,
  *                    setpoint, safety, status
  ******************************************************************************
  */
#ifndef __AIO_H
#define __AIO_H

#include <stdint.h>

#define AIO_STATUS_LEN   64U

typedef struct
{
  uint8_t  air_ok;      int16_t air_x10;      /* kabin hava (DHT22), 0.1 C   */
  uint8_t  rh_ok;       uint16_t rh_x10;      /* bagil nem, 0.1 %            */
  uint8_t  pel_ok;      int32_t pel_x100;     /* Peltier yuzeyi, 0.01 C      */
  uint8_t  wall_ok;     int32_t wall_x100;    /* karsi duvar, 0.01 C         */
  uint8_t  amb_ok;      int32_t amb_x100;     /* dis ortam, 0.01 C           */
  int16_t  u_cmd;                             /* Peltier komutu -1000..+1000 */
  uint8_t  sp_ok;       int16_t sp_x10;       /* set degeri (kapali cevrim)  */
  uint8_t  safety;                            /* 0 = normal                  */
  char     status[AIO_STATUS_LEN];            /* deney durumu metni          */
} Aio_Data;

void Aio_Init(void);
void Aio_Task1Hz(const Aio_Data *d);

#endif /* __AIO_H */
