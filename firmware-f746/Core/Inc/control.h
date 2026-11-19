/**
  ******************************************************************************
  * @file    control.h
  * @brief   Referans kontrolculer: PID (anti-windup) ve ON/OFF (histerezis)
  *          Cikis birimi Peltier komutu: -1000 (tam sogutma) .. +1000 (tam isitma)
  ******************************************************************************
  */
#ifndef __CONTROL_H
#define __CONTROL_H

#include <stdint.h>

typedef struct
{
  float kp;          /* komut / C                     */
  float ti_s;        /* integral zamani (s), 0 = yok  */
  float td_s;        /* turev zamani (s), 0 = yok     */
  float umin, umax;
  float integ;       /* integral terimi (komut birimi) */
  float prev_pv;
  uint8_t first;
} PID_Ctrl;

typedef struct
{
  float   hyst_c;    /* histerezis yarim genisligi (C) */
  int16_t u_heat;    /* isitma komutu (+)              */
  int16_t u_cool;    /* sogutma komutu (-)             */
  int16_t state;     /* son cikis                      */
} OnOff_Ctrl;

void    PID_Init(PID_Ctrl *c, float kp, float ti_s, float td_s, float umin, float umax);
void    PID_Reset(PID_Ctrl *c);
int16_t PID_Step(PID_Ctrl *c, float sp, float pv, float dt_s);

/* Calisirken cikis sinirlarini degistirir (Peltier yuzey sinirlayicisi icin).
 * Integral de yeni sinirlara kirpilir: sinir altinda integral sismez. */
void    PID_SetLimits(PID_Ctrl *c, float umin, float umax);

void    OnOff_Init(OnOff_Ctrl *c, float hyst_c, int16_t u_heat, int16_t u_cool);
void    OnOff_Reset(OnOff_Ctrl *c);
int16_t OnOff_Step(OnOff_Ctrl *c, float sp, float pv);

/* ---------------- Yuzey sicakligi sinirlayicisi (override PI) ----------------
 * Peltier yuzeyini bir sinirda tutmak icin izin verilen en yuksek komut
 * buyuklugunu (0..1000) hesaplar. Isitma ve sogutma icin ayri birer tane
 * kullanilir; deneyin istedigi komut bu degerle kirpilir.
 *
 *  margin = sinira kalan mesafe (C): isitmada  sinir - T, sogutmada  T - sinir
 *  margin >= band            : devre disi, izin = 1000
 *  margin <  band ilk kez    : devreye girer, izin = o anki uygulanan buyukluk
 *  devredeyken (hiz bicimli PI):
 *      izin += Kp*(margin - margin_onceki) + Kp/Ti * margin * dt
 *  margin > band + 2 C ve izin istenen buyuklugun ustunde: devre disi.
 *  Cikis zaten [0, 1000] ile sinirli oldugundan integral sismez. */
typedef struct
{
  float   kp;          /* komut / C                                */
  float   ti_s;        /* integral zamani (s)                      */
  float   band_c;      /* sinira bu kadar yaklasinca devreye girer */
  float   allow;       /* izin verilen en yuksek buyukluk 0..1000  */
  float   prev_margin;
  uint8_t active;
} Lim_Ctrl;

void    Lim_Init(Lim_Ctrl *c, float kp, float ti_s, float band_c);
void    Lim_Reset(Lim_Ctrl *c);
/* margin: sinira kalan mesafe (C), applied_mag: bir onceki uygulanan komutun bu
 * yondeki buyuklugu, req_mag: istenen buyukluk. Donus: izin (0..1000). */
int16_t Lim_Step(Lim_Ctrl *c, float margin, int16_t applied_mag, int16_t req_mag, float dt_s);

#endif /* __CONTROL_H */
