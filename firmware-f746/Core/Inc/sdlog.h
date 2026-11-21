/**
  ******************************************************************************
  * @file    sdlog.h
  * @brief   SD karta gunluk metin (TXT) log kaydi - FatFs
  *
  *  Iki kayit turu vardir:
 *
 *  1) Deney dosyasi (deney calisirken): exp.c SdLog_SetSession() ile ad verir,
 *     ornek P1_20261118_1705.txt (deney ve baslangic zamani). Tam protokolde
 *     her asama ayri dosyadir: T1P1_..., T2P2_..., T3P3_... Gece yarisinda
 *     dosya degismez; SDLOG_MAX_RECORDS satirda P1_..._2.txt ile devam edilir.
 *     Enerji kesintisinden sonra ayni dosyanin sonuna eklenir.
 *
 *  2) Gunluk dosya (deney yokken, KAYIT BASLAT ile):
 *  Dosya adi RTC tarihinden: LOG_YYYYMMDD.txt (ornek LOG_20261116.txt).
  *  - Kayit gun icinde ne zaman baslarsa baslasin o gunun dosyasina eklenir
  *    (ayni gun birden fazla baslat/durdur -> ayni dosya, "#" isaretleriyle).
  *  - Gece 00:00'da dosya kapatilir, yeni gunun dosyasi acilir.
  *  - Enerji kesilip baska bir gun geri gelirse: o gunun dosyasi yoksa
  *    olusturulur ve kayda oradan devam edilir.
  *  - Dosya basina en fazla SDLOG_MAX_RECORDS veri satiri (1 Hz'de 1 gun).
  *    Sinir asilirsa (or. saat geri alindi) LOG_YYYYMMDD_2.txt ile devam edilir.
  *
  *  Satirlar virgulle ayrilir; '#' ile baslayan satirlar aciklamadir.
  *  Enerji kesintisine karsi her SDLOG_SYNC_EVERY satirda f_sync yapilir.
  ******************************************************************************
  */
#ifndef __SDLOG_H
#define __SDLOG_H

#include <stdint.h>
#include "ds3231.h"

#define SDLOG_MAX_RECORDS    86400U   /* dosya basina veri satiri siniri (24 x 3600) */
#define SDLOG_SYNC_EVERY     5U       /* kac satirda bir diske kesin yazilsin        */
#define SDLOG_RETRY_MS       2000U    /* hata sonrasi yeniden deneme araligi         */
#define SDLOG_FREE_EVERY     600U     /* kac satirda bir bos alan guncellensin       */

/* Dosyanin basina yazilan sutun basligi (main.c'deki satir bicimiyle ayni) */
#define SDLOG_COLUMNS  "date,time,uptime_s,T_air_dht,RH_dht,T_heatsink,T_wall,T_ambient,u_cmd,peltier_ok,proc,phase,setpoint,event,safety,v_bus,u_req,door"

typedef enum
{
  SDLOG_NO_CARD = 0,     /* kart takili degil                          */
  SDLOG_IDLE,            /* kart var, kayit kapali                      */
  SDLOG_LOGGING,         /* dosya acik, yaziliyor                       */
  SDLOG_ERROR,           /* mount/acma/yazma hatasi (tekrar denenecek)  */
  SDLOG_NO_CLOCK         /* RTC okunamiyor: dosya adi (tarih) yok       */
} SdLog_State;

typedef struct
{
  SdLog_State state;
  uint8_t     present;
  char        file[32];      /* acik / acilacak dosya adi, ya da "--"  */
  uint32_t    records;       /* dosyadaki veri satiri                   */
  uint32_t    size_kb;
  uint32_t    free_mb;
  uint32_t    errors;        /* toplam yazma/acma hatasi                */
  char        last_write[12];/* "12:34:56" ya da "--"                   */
} SdLog_Info;

void              SdLog_Init(void);
/* Ana donguden her turda: kart tak/cikar, mount, dosya ac/kapat, gun degisimi */
void              SdLog_Task(uint8_t logging_active, const DS3231_TimeDate *rtc, uint8_t rtc_ok);
/* 1 Hz ornek satiri (sonunda \r\n olmadan). Dosya acik degilse yok sayilir. */
void              SdLog_WriteLine(const char *line, const DS3231_TimeDate *rtc);
const SdLog_Info *SdLog_GetInfo(void);
const char       *SdLog_FileName(void);
/* '#' ile baslayan aciklama satiri (deney olaylari). Dosya henuz acik degilse
 * kuyrukta bekler (en fazla SDLOG_CMT_N), dosya acilinca yazilir. */
void              SdLog_Comment(const char *line);
#define SDLOG_CMT_N     6U

/* Deney dosyasi: base = "P1_20261118_1705" (uzantisiz, en fazla 20 karakter),
 * NULL ya da "" = gunluk dosyaya don. Ad degisirse acik dosya, kuyruktaki
 * aciklamalar yazildiktan sonra kapatilir; yeni dosya bir sonraki
 * SdLog_Task'ta acilir. Ayni ad tekrar verilirse bir sey yapilmaz. */
void              SdLog_SetSession(const char *base);
#define SDLOG_SESSION_MAX  20U

/* FatFs get_fattime() icin (fatfs.c'den cagrilir) */
uint32_t          SdLog_FatTime(void);

#endif /* __SDLOG_H */
