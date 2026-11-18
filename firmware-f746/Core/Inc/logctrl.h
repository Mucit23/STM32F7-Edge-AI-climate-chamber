/**
  ******************************************************************************
  * @file    logctrl.h
  * @brief   Kayit (log) durumu kontrolu - durum DS3231 modulundeki 24C32
  *          EEPROM'da saklanir. Enerji kesilip geri geldiginde kayit aciksa
  *          otomatik olarak devam edilir.
  *
  *  EEPROM yerlesimi: iki kopya (A: 0x0000, B: 0x0020), her yazmada sira
  *  numarasi artar ve diger kopyaya yazilir. Yazma sirasinda enerji kesilse
  *  bile bir onceki gecerli kopya kalir (CRC ile dogrulanir).
  ******************************************************************************
  */
#ifndef __LOGCTRL_H
#define __LOGCTRL_H

#include "stm32f7xx_hal.h"
#include "ds3231.h"

#define LOGCTRL_FILE_EXT   "TXT"      /* kayit dosyasi uzantisi (LOG_0001.TXT) */

typedef struct
{
  uint8_t  active;         /* 1: kayit acik                                  */
  uint16_t file_index;     /* LOG_<file_index>.CSV                            */
  uint16_t seq;            /* EEPROM yazma sira numarasi                      */
  uint8_t  start[6];       /* oturum baslangici: yy, mm, dd, hh, mi, ss       */
  uint16_t resume_count;   /* enerji kesintisi sonrasi devam sayisi (oturum)  */
} LogCtrl_State;

/* Acilista bir kez: EEPROM'u bulur, durumu okur, kayit aciksa devam eder */
void                 LogCtrl_Init(I2C_HandleTypeDef *hi2c);

uint8_t              LogCtrl_IsActive(void);
uint8_t              LogCtrl_EepromOk(void);     /* EEPROM bulundu ve son yazma basarili */
uint8_t              LogCtrl_WasResumed(void);   /* bu acilista kayda devam edildi */
const char          *LogCtrl_FileName(void);     /* "LOG_0003.TXT"                */
const LogCtrl_State *LogCtrl_Get(void);

/* Yeni oturum: dosya numarasi artar, durum EEPROM'a yazilir. 1 = EEPROM'a yazildi */
uint8_t              LogCtrl_Start(const DS3231_TimeDate *now);
/* Ayni oturumda bir sonraki dosya numarasina gecer (SD'de ayni adli dosya varsa) */
uint8_t              LogCtrl_NextFile(void);
/* Kaydi durdurur, durum EEPROM'a yazilir. 1 = EEPROM'a yazildi */
uint8_t              LogCtrl_Stop(void);

#endif /* __LOGCTRL_H */
