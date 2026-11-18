/**
  ******************************************************************************
  * @file    net.h
  * @brief   Ag durumu izleme: Ethernet kablosu, DHCP IP adresi ve sunucu
  *          (Adafruit IO) baglanti durumu. LCD basligi ve log icin tek kaynak.
  *
  *          Net_Task1Hz() ana dongudeki 1 Hz blogunda cagrilir. Sunucu
  *          istemcisi (MQTT modulu) durumunu Net_SetServer() / Net_ServerSent()
  *          ile bildirir.
  ******************************************************************************
  */
#ifndef __NET_H
#define __NET_H

#include <stdint.h>

#define NET_SERVER_NAME   "ADAFRUIT IO"

typedef enum
{
  NET_LINK_DOWN = 0,     /* Ethernet kablosu takili degil            */
  NET_LINK_NOIP,         /* kablo takili, DHCP'den IP bekleniyor     */
  NET_LINK_UP            /* IP alindi                                */
} Net_Link;

typedef enum
{
  NET_SRV_OFF = 0,       /* sunucu istemcisi devrede degil           */
  NET_SRV_CONNECTING,    /* DNS / TCP / MQTT baglantisi kuruluyor     */
  NET_SRV_CONNECTED,     /* bagli, veri gonderilebilir               */
  NET_SRV_ERROR          /* baglanti kurulamadi / koptu (tekrar denenecek) */
} Net_Srv;

typedef struct
{
  Net_Link link;
  uint8_t  ip[4];
  Net_Srv  srv;          /* istemcinin bildirdigi durum                         */
  uint8_t  sent_valid;   /* en az bir basarili gonderim yapildi mi              */
  uint32_t sent_age_s;   /* son basarili gonderimden bu yana gecen sure (s)     */
} Net_Status;

void              Net_Task1Hz(void);
const Net_Status *Net_GetStatus(void);
uint8_t           Net_IsUp(void);           /* IP alinmis ve kablo bagli */

void Net_SetServer(Net_Srv s);              /* MQTT istemcisi cagirir */
void Net_ServerSent(void);                  /* basarili gonderimde cagirir */

#endif /* __NET_H */
