/**
  ******************************************************************************
  * @file    screen.h
  * @brief   Ekran durum makinesi ve dokunmatik buton islemleri
  *          Ana ekran / Saat-tarih ayar sayfasi / Kayit durdurma onayi.
  *          Hicbir ekran ana donguyu bloklamaz: sensor, Peltier ve kayit
  *          gorevleri her ekranda calismaya devam eder.
  ******************************************************************************
  */
#ifndef __SCREEN_H
#define __SCREEN_H

#include "stm32f7xx_hal.h"
#include "ds3231.h"

typedef enum
{
  SCREEN_MAIN = 0,
  SCREEN_CLOCK_SETUP,
  SCREEN_CONFIRM_STOP,
  SCREEN_EXP_MENU,          /* deney secimi                    */
  SCREEN_EXP_PARAM,         /* parametreler + BASLAT           */
  SCREEN_EXP_RUN,           /* calisan deney izleme            */
  SCREEN_EXP_EVENT,         /* OLAY isaretleme                 */
  SCREEN_CONFIRM_START,     /* test baslatma onayi             */
  SCREEN_CONFIRM_EXPSTOP    /* test durdurma onayi             */
} Screen_Id;

#define SCREEN_SETUP_TIMEOUT_MS    60000U   /* ayar sayfasinda islem yoksa cik */
#define SCREEN_CONFIRM_TIMEOUT_MS  10000U   /* onay penceresi: HAYIR sayilir   */
#define SCREEN_MENU_TIMEOUT_MS     60000U   /* deney menu/parametre/olay       */
#define SCREEN_RUN_TIMEOUT_MS      120000U  /* deney izleme -> ana ekran       */
#define SCREEN_BL_TIMEOUT_MS       300000U  /* 5 dk dokunulmazsa ekran isigi kapanir (0 = kapanmaz) */

void      Screen_Init(I2C_HandleTypeDef *hi2c_rtc);
/* Ana donguden her turda: rtc = son okunan saat, rtc_ok = okuma basarili mi */
void      Screen_Task(const DS3231_TimeDate *rtc, uint8_t rtc_ok);
Screen_Id Screen_Current(void);
void      Screen_OpenClockSetup(const DS3231_TimeDate *rtc);   /* fiziksel buton icin */
void      Screen_Wake(void);           /* ekran isigini ac, sayaci sifirla (or. guvenlik olayi) */
uint8_t   Screen_BacklightOn(void);

/* main.c'de tanimlanir: ana ekrani bastan cizer */
void      App_RedrawMain(void);
/* main.c'de tanimlanir: kayit durumu degisti (SD paneli/buton guncellensin) */
void      App_RecordingChanged(void);

#endif /* __SCREEN_H */
