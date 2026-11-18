/**
  ******************************************************************************
  * @file    exp.h
  * @brief   Deney (test protokolu) yoneticisi - durum makinesi
  *
  *  Prosedurler:
  *   P1 Statik karakterizasyon : 0,+25..+100,0,-25..-100,0 basamaklari
  *   P2 Basamak cevaplari      : denge -> +/-25/50/100 basamak, tekrarli
  *   P3 APRBS                  : rastgele seviye + rastgele sure (acik cevrim)
  *   P4 Kapali cevrim          : PID / ON-OFF; basamak, rampa, gun/gece profilleri
  *   P5 Bozucu senaryolari     : sabit ayar noktasi + OLAY isaretleri
  *   P6 Dogrulama              : sabit tohumlu APRBS + yeni ayar noktasi profili
  *   TAM PROTOKOL              : P1 -> P2 -> P3 -> P4 -> P6
  *
  *  Zamanlama RTC'den (2000'den bu yana saniye). Durum 24C32 EEPROM'da
  *  saklanir; enerji gelince deney kendiliginden devam eder:
  *   - P3/P4/P5/P6: saat cizelgesine gore (kesinti suresi atlanir)
  *   - P1/P2      : yarim kalan basamak bastan alinir
  ******************************************************************************
  */
#ifndef __EXP_H
#define __EXP_H

#include "stm32f7xx_hal.h"
#include "ds3231.h"

/* ---------------- Guvenlik (son koruma, cok yuksek sinirlar) ---------------- */
#define EXP_SAFETY_ENABLE      1        /* 0: tamamen kapali                    */
#define EXP_LIMIT_AIR_C        65       /* kabin havasi ust siniri (C)          */
#define EXP_LIMIT_PELTIER_C    80       /* Peltier yuzeyi ust siniri (C)        */
#define EXP_SAFETY_HYST_C      5        /* sinirin bu kadar altina inince...    */
#define EXP_SAFETY_HOLD_S      600U     /* ...ve bu kadar sure sonra devam      */
#define EXP_NO_AIR_S           30U      /* kapali cevrimde hava olcumu yoksa    */

/* ---------------- Hizli test modu ----------------
 * 1 yapilirsa tum sureler EXP_FAST_DIV'e bolunur (karta ilk denemede tum
 * protokolu birkac saatte gormek icin). Gercek test icin 0 olmali. */
#define EXP_FAST_TEST          0
#define EXP_FAST_DIV           60U

typedef enum
{
  EXP_P_NONE = 0,
  EXP_P1_STATIC,
  EXP_P2_STEP,
  EXP_P3_APRBS,
  EXP_P4_CLOSED,
  EXP_P5_DIST,
  EXP_P6_VALID,
  EXP_P_COUNT
} Exp_Proc;

typedef enum
{
  EXP_IDLE = 0,
  EXP_RUNNING,
  EXP_PAUSED,
  EXP_DONE
} Exp_State;

typedef enum
{
  EXP_SAFE_OK = 0,
  EXP_SAFE_AIR_HIGH,      /* 1: kabin havasi siniri asti           */
  EXP_SAFE_PELTIER_HIGH,  /* 2: Peltier yuzeyi siniri asti         */
  EXP_SAFE_NO_AIR         /* 3: kapali cevrimde hava olcumu yok     */
} Exp_Safety;

typedef enum
{
  EXP_CTRL_PID = 0,
  EXP_CTRL_ONOFF,
  EXP_CTRL_MIXED          /* P4: gunluk PID / ON-OFF donusumlu      */
} Exp_CtrlType;

typedef enum
{
  EXP_EVT_NONE = 0,
  EXP_EVT_DOOR = 1,       /* kapak acildi               */
  EXP_EVT_LOAD = 2,       /* isil yuk eklendi/cikarildi */
  EXP_EVT_AMBIENT = 3,    /* dis ortam degisti          */
  EXP_EVT_OTHER = 9
} Exp_Event;

/* Kullanicinin ayarlayabildigi parametreler (EEPROM'da saklanir) */
typedef struct
{
  uint16_t p1_hold_min;     /* P1 basamak suresi (dk)              */
  uint8_t  p1_repeat;
  uint16_t p2_settle_min;   /* P2 denge suresi (dk)                */
  uint16_t p2_step_min;     /* P2 basamak suresi (dk)              */
  uint8_t  p2_repeat;
  uint8_t  p3_days;
  uint16_t p3_hold_min;     /* APRBS en kisa tutma (dk)            */
  uint16_t p3_hold_max;     /* APRBS en uzun tutma (dk)            */
  uint8_t  p3_amp_pct;      /* APRBS genlik (%)                    */
  uint8_t  p4_days;
  uint8_t  p4_ctrl;         /* Exp_CtrlType                        */
  uint8_t  p5_days;
  uint8_t  p5_sp_c;         /* P5 ayar noktasi (C)                 */
  uint8_t  p6_aprbs_h;
  uint8_t  p6_closed_h;
  uint16_t kp;              /* PID Kp (komut / C)                  */
  uint16_t ti_s;            /* PID Ti (s)                          */
} Exp_Params;

typedef struct
{
  Exp_State  state;
  uint8_t    full;          /* 1: TAM PROTOKOL calisiyor           */
  uint8_t    stage;         /* tam protokolde asama 0..4           */
  uint8_t    stages;        /* tam protokol asama sayisi (5)       */
  Exp_Proc   proc;
  uint16_t   phase;         /* adim / segment numarasi             */
  int16_t    u;             /* uygulanan Peltier komutu            */
  uint8_t    closed;        /* 1: kapali cevrim (ayar noktasi var) */
  int16_t    sp_x10;        /* ayar noktasi (0.1 C)                */
  uint8_t    ctrl;          /* Exp_CtrlType (kapali cevrimde)      */
  Exp_Safety safety;
  uint32_t   elapsed_s;     /* aktif prosedurde gecen sure         */
  uint32_t   duration_s;    /* aktif prosedurun toplam suresi      */
  uint32_t   next_change_s; /* sonraki komut/ayar noktasi degisimi */
  uint32_t   total_left_s;  /* programin tamaminin kalan suresi    */
  uint8_t    resumed;       /* bu acilista enerji kesintisi sonrasi devam edildi */
  uint8_t    pending_event; /* bir sonraki log satirina yazilacak olay */
} Exp_Status;

typedef struct
{
  uint8_t air_valid;        /* DHT22 son okuma gecerli */
  int16_t air_x10;          /* kabin havasi (0.1 C)    */
  uint8_t peltier_valid;    /* Peltier yuzeyi DS18B20  */
  int32_t peltier_x100;     /* (0.01 C)                */
} Exp_Inputs;

/* Acilista bir kez: EEPROM'dan durum okunur, calisan deney varsa devam edilir */
void              Exp_Init(I2C_HandleTypeDef *hi2c, const DS3231_TimeDate *rtc, uint8_t rtc_ok);
/* 1 Hz: komutu hesaplar ve Peltier'e gonderir (deney bostayken komuta dokunmaz) */
void              Exp_Task1Hz(const Exp_Inputs *in, const DS3231_TimeDate *rtc, uint8_t rtc_ok);

/* program: EXP_P1_STATIC..EXP_P6_VALID tek prosedur, ya da EXP_P_COUNT = TAM PROTOKOL */
uint8_t           Exp_Start(uint8_t program, const DS3231_TimeDate *rtc);
void              Exp_Stop(void);
void              Exp_PauseToggle(const DS3231_TimeDate *rtc);
void              Exp_Acknowledge(void);            /* BITTI -> BOSTA */
void              Exp_MarkEvent(Exp_Event ev, const DS3231_TimeDate *rtc);
uint8_t           Exp_TakeEvent(void);              /* log satiri icin (okununca sifirlanir) */

const Exp_Status *Exp_GetStatus(void);
Exp_Params       *Exp_GetParams(void);              /* parametre sayfasi duzenler          */
void              Exp_SaveParams(void);             /* EEPROM'a yaz                        */
uint32_t          Exp_ProgramDuration(uint8_t program);   /* s (tahmini sure)              */

const char       *Exp_ProcName(Exp_Proc p);         /* "P3 APRBS"          */
const char       *Exp_StateName(Exp_State s);       /* "CALISIYOR"         */
const char       *Exp_SafetyName(Exp_Safety s);
void              Exp_FormatDuration(char *buf, uint32_t n, uint32_t s);   /* "2g 04:12" */

#endif /* __EXP_H */
