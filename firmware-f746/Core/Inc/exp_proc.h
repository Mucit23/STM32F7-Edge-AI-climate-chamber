/**
  ******************************************************************************
  * @file    exp_proc.h
  * @brief   Deney prosedurlerinin zaman -> komut / ayar noktasi uretecleri
  *          (durumsuz fonksiyonlar: ayni t icin her zaman ayni sonuc; bu sayede
  *          enerji kesintisinden sonra kaldigi saniyeden yeniden hesaplanir)
  ******************************************************************************
  */
#ifndef __EXP_PROC_H
#define __EXP_PROC_H

#include "exp.h"

/* Zaman birimleri (hizli test modunda kisaltilir) */
#if EXP_FAST_TEST
#define EXP_MIN_S    (60U / EXP_FAST_DIV)
#else
#define EXP_MIN_S    60U
#endif
#define EXP_HOUR_S   (60U * EXP_MIN_S)
#define EXP_DAY_S    (24U * EXP_HOUR_S)

#define EXP_P6_SEED  0x5EED6006UL      /* dogrulama APRBS'i icin sabit tohum */
#define EXP_DITHER   50                /* PID komutuna eklenen +/- dither (%5) */

typedef struct
{
  int16_t  u;              /* acik cevrim komutu                     */
  uint8_t  closed;         /* 1: kapali cevrim, sp_x10 gecerli       */
  int16_t  sp_x10;         /* ayar noktasi (0.1 C)                   */
  uint8_t  ctrl;           /* Exp_CtrlType                           */
  uint8_t  dither;         /* 1: PID komutuna dither eklenir         */
  uint16_t phase;          /* adim / segment numarasi                */
  uint32_t next_change_s;  /* sonraki degisime kalan sure            */
  uint8_t  done;           /* t >= sure                              */
} Proc_Out;

uint32_t Proc_Duration(Exp_Proc p, const Exp_Params *pr);
uint8_t  Proc_IsStepBased(Exp_Proc p);                        /* P1, P2 */
uint32_t Proc_StepOffset(Exp_Proc p, const Exp_Params *pr, uint16_t step);
void     Proc_Eval(Exp_Proc p, const Exp_Params *pr, uint32_t seed,
                   uint32_t t, uint32_t tod_s, Proc_Out *o);
int16_t  Proc_Dither(uint32_t seed, uint32_t t);
uint32_t Prng_Next(uint32_t *s);

#endif /* __EXP_PROC_H */
