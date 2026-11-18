/**
  ******************************************************************************
  * @file    peltier.h
  * @brief   Peltier surucu karti (STM32F072) ile USART6 haberlesmesi
  ******************************************************************************
  * Protokol (115200 8N1, ASCII):
  *   Ana kart -> Surucu   *<deger>#   guc komutu, -1000 (max sogutma) ... +1000 (max isitma)
  *                        *S#         durum sorgusu
  *   Surucu -> Ana kart   *A<deger>#  onay: surucunun o anda uyguladigi deger
  *                        *T#         surucu 2 s komut alamadi, cikisi kapatti
  *
  * Bu modul komutu PELTIER_TX_PERIOD_MS'de bir tekrar gonderir (surucu 2 s
  * komut alamazsa cikisi kendisi kapatir) ve gelen onaylardan baglanti
  * durumunu cikarir. Hicbir fonksiyon bloklamaz.
  *
  * Baglanti: USART6 TX = D1 (PC6) -> F072 PA10 (RX)
  *           USART6 RX = D0 (PC7) <- F072 PA9  (TX)
  *           GND ortak.
  ******************************************************************************
  */
#ifndef __PELTIER_H
#define __PELTIER_H

#include "stm32f7xx_hal.h"
#include <stdint.h>

#define PELTIER_CMD_MAX           1000     /* +1000 = max isitma, -1000 = max sogutma */
#define PELTIER_TX_PERIOD_MS      500U     /* komut tekrar periyodu                   */
#define PELTIER_LINK_LOST_MS      1500U    /* bu sure onay gelmezse: HATA (cevap yok) */

typedef enum
{
  PELTIER_LINK_WAIT = 0,    /* acilis: henuz onay gelmedi                       */
  PELTIER_LINK_OK,          /* onay geliyor ve gonderilen degerle ayni          */
  PELTIER_LINK_NO_REPLY,    /* PELTIER_LINK_LOST_MS boyunca onay yok            */
  PELTIER_LINK_TIMEOUT,     /* surucu *T# bildirdi: komut alamiyor, cikis kapali */
  PELTIER_LINK_MISMATCH     /* onaydaki deger gonderilen degerden farkli        */
} Peltier_LinkState;

typedef struct
{
  Peltier_LinkState link;
  int16_t  command;          /* istenen deger (-1000..+1000)                  */
  int16_t  applied;          /* surucunun son onayladigi deger                */
  uint32_t last_ack_tick;    /* son onayin zamani                             */
  uint32_t tx_count;         /* gonderilen komut sayisi                       */
  uint32_t ack_count;        /* alinan onay sayisi                            */
  uint32_t error_count;      /* baglanti hatasina dusme sayisi                */
} Peltier_Status;

void              Peltier_Init(UART_HandleTypeDef *huart);
void              Peltier_Task(void);                 /* ana donguden surekli cagrilir */
void              Peltier_SetCommand(int16_t value);  /* -1000..+1000, sinirlanir      */
int16_t           Peltier_GetCommand(void);
uint8_t           Peltier_IsLinkOk(void);
const Peltier_Status *Peltier_GetStatus(void);
const char       *Peltier_LinkShort(Peltier_LinkState st);  /* "OK","YOK","ZAM","UYM","BEK" */

/* HAL UART geri cagirmalarindan cagrilir (main.c USER CODE 4) */
void              Peltier_UartRxCplt(UART_HandleTypeDef *huart);
void              Peltier_UartError(UART_HandleTypeDef *huart);

#endif /* __PELTIER_H */
