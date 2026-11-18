/**
  ******************************************************************************
  * @file    exp_proc.c
  * @brief   Deney prosedurlerinin uretecleri
  *
  *  P1: 11 seviye x tekrar, her biri p1_hold_min
  *      seviye: 0, +25, +50, +75, +100, 0, -25, -50, -75, -100, 0 (%)
  *  P2: her tekrarda 3 genlik (25/50/100 %) x 2 yon:
  *      [0: denge] [+A: basamak] [0: denge] [-A: basamak]
  *  P3: APRBS - her segmentte seviye ~ U[-A, +A] (10'luk adim),
  *      sure ~ U[min, max] dk. Xorshift32 PRNG, tohum deney basinda.
  *  P4: gun gun donen profiller (0: basamak, 1: rampa, 2: gun/gece);
  *      KARMA kontrolde 3 gunde bir PID <-> ON/OFF degisir.
  *  P5: sabit ayar noktasi, PID.
  *  P6: once sabit tohumlu APRBS, sonra egitimde olmayan ayar noktasi dizisi.
  ******************************************************************************
  */
#include "exp_proc.h"

static const int16_t p1_levels[11] = { 0, 250, 500, 750, 1000, 0, -250, -500, -750, -1000, 0 };
static const int16_t p2_amps[3]    = { 250, 500, 1000 };

/* P4 basamak profili: 8 x 3 saat (0.1 C) */
static const int16_t p4_steps[8]   = { 250, 350, 200, 300, 150, 280, 400, 220 };
/* P4 rampa profili: (baslangic, bitis, sure dk) - toplam 1440 dk, rampalar 0.05 C/dk */
static const struct { int16_t a, b; uint16_t min; } p4_ramp[6] =
{
  { 200, 400, 400 }, { 400, 400, 80 }, { 400, 150, 500 },
  { 150, 150, 100 }, { 150, 200, 100 }, { 200, 200, 260 }
};
/* P6 kapali cevrim dizisi: 6 x 2.5 saat, egitimdekilerden farkli degerler */
static const int16_t p6_steps[6]   = { 220, 320, 170, 270, 370, 240 };

/* ------------------------------------------------------------------------- */
uint32_t Prng_Next(uint32_t *s)                 /* xorshift32 */
{
  uint32_t x = *s;
  if (x == 0U) x = 0x9E3779B9UL;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *s = x;
  return x;
}

int16_t Proc_Dither(uint32_t seed, uint32_t t)
{
  uint32_t s = seed ^ (0x85EBCA6BUL * ((t / 60U) + 1U));   /* dakikada bir degisir */
  (void)Prng_Next(&s);
  return (Prng_Next(&s) & 1U) ? EXP_DITHER : -EXP_DITHER;
}

/* APRBS: t anindaki segmenti bastan uretir (10 gunde ~700 segment, us mertebesi) */
static void Aprbs(uint32_t seed, uint16_t amp, uint16_t hmin, uint16_t hmax, uint32_t t,
                  int16_t *level, uint16_t *seg, uint32_t *next)
{
  uint32_t s = seed;
  uint32_t start = 0;
  uint16_t idx = 0;
  uint16_t nlev = (uint16_t)(2U * (amp / 10U) + 1U);    /* -A..+A, 10'luk adim */
  if (hmax < hmin) hmax = hmin;
  for (;;)
  {
    uint32_t hold = (uint32_t)(hmin + (Prng_Next(&s) % (uint32_t)(hmax - hmin + 1U))) * EXP_MIN_S;
    int16_t  lv   = (int16_t)((int32_t)(Prng_Next(&s) % nlev) * 10 - (int32_t)amp);
    if (hold == 0U) hold = 1U;
    if (t < start + hold)
    {
      *level = lv;
      *seg   = idx;
      *next  = start + hold - t;
      return;
    }
    start += hold;
    idx++;
  }
}

static int16_t Lerp(int16_t a, int16_t b, uint32_t x, uint32_t len)
{
  if (len == 0U) return b;
  return (int16_t)(a + ((int32_t)(b - a) * (int32_t)x) / (int32_t)len);
}

/* ------------------------------------------------------------------------- */
uint32_t Proc_Duration(Exp_Proc p, const Exp_Params *pr)
{
  switch (p)
  {
    case EXP_P1_STATIC: return 11U * pr->p1_repeat * pr->p1_hold_min * EXP_MIN_S;
    case EXP_P2_STEP:   return 6U * pr->p2_repeat * (uint32_t)(pr->p2_settle_min + pr->p2_step_min) * EXP_MIN_S;
    case EXP_P3_APRBS:  return (uint32_t)pr->p3_days * EXP_DAY_S;
    case EXP_P4_CLOSED: return (uint32_t)pr->p4_days * EXP_DAY_S;
    case EXP_P5_DIST:   return (uint32_t)pr->p5_days * EXP_DAY_S;
    case EXP_P6_VALID:  return (uint32_t)(pr->p6_aprbs_h + pr->p6_closed_h) * EXP_HOUR_S;
    default:            return 0U;
  }
}

uint8_t Proc_IsStepBased(Exp_Proc p)
{
  return (p == EXP_P1_STATIC || p == EXP_P2_STEP) ? 1U : 0U;
}

uint32_t Proc_StepOffset(Exp_Proc p, const Exp_Params *pr, uint16_t step)
{
  if (p == EXP_P1_STATIC)
  {
    return (uint32_t)step * pr->p1_hold_min * EXP_MIN_S;
  }
  if (p == EXP_P2_STEP)
  {
    uint32_t pair = step / 2U;
    return (pair * (uint32_t)(pr->p2_settle_min + pr->p2_step_min) +
            ((step & 1U) ? pr->p2_settle_min : 0U)) * EXP_MIN_S;
  }
  return 0U;
}

void Proc_Eval(Exp_Proc p, const Exp_Params *pr, uint32_t seed,
               uint32_t t, uint32_t tod_s, Proc_Out *o)
{
  o->u = 0; o->closed = 0; o->sp_x10 = 0; o->ctrl = EXP_CTRL_PID; o->dither = 0;
  o->phase = 0; o->next_change_s = 0; o->done = 0;

  if (t >= Proc_Duration(p, pr))
  {
    o->done = 1U;
    return;
  }

  switch (p)
  {
    case EXP_P1_STATIC:
    {
      uint32_t hold = (uint32_t)pr->p1_hold_min * EXP_MIN_S;
      uint32_t step = t / hold;
      o->u = p1_levels[step % 11U];
      o->phase = (uint16_t)step;
      o->next_change_s = hold - (t % hold);
      break;
    }

    case EXP_P2_STEP:
    {
      uint32_t settle = (uint32_t)pr->p2_settle_min * EXP_MIN_S;
      uint32_t plen   = settle + (uint32_t)pr->p2_step_min * EXP_MIN_S;
      uint32_t pair   = t / plen;
      uint32_t r      = t % plen;
      uint8_t  in_step = (r >= settle) ? 1U : 0U;
      uint32_t k      = pair % 6U;                    /* 3 genlik x 2 yon */
      int16_t  amp    = p2_amps[k / 2U];
      o->u = in_step ? ((k & 1U) ? (int16_t)-amp : amp) : 0;
      o->phase = (uint16_t)(pair * 2U + in_step);
      o->next_change_s = in_step ? (plen - r) : (settle - r);
      break;
    }

    case EXP_P3_APRBS:
    {
      uint16_t seg;
      Aprbs(seed, (uint16_t)(pr->p3_amp_pct * 10U), pr->p3_hold_min, pr->p3_hold_max, t,
            &o->u, &seg, &o->next_change_s);
      o->phase = seg;
      break;
    }

    case EXP_P4_CLOSED:
    {
      uint32_t day  = t / EXP_DAY_S;
      uint32_t td   = t % EXP_DAY_S;
      uint8_t  prof = (uint8_t)(day % 3U);
      o->closed = 1U;
      if (pr->p4_ctrl == EXP_CTRL_MIXED) o->ctrl = ((day / 3U) % 2U) ? EXP_CTRL_ONOFF : EXP_CTRL_PID;
      else                               o->ctrl = pr->p4_ctrl;
      o->dither = (o->ctrl == EXP_CTRL_PID) ? 1U : 0U;

      if (prof == 0U)                                 /* basamak: 8 x 3 saat */
      {
        uint32_t seg = td / (3U * EXP_HOUR_S);
        o->sp_x10 = p4_steps[seg % 8U];
        o->next_change_s = 3U * EXP_HOUR_S - (td % (3U * EXP_HOUR_S));
        o->phase = (uint16_t)(day * 100U + seg);
      }
      else if (prof == 1U)                            /* rampa */
      {
        uint32_t start = 0;
        for (uint8_t i = 0; i < 6U; i++)
        {
          uint32_t len = (uint32_t)p4_ramp[i].min * EXP_MIN_S;
          if (td < start + len || i == 5U)
          {
            o->sp_x10 = Lerp(p4_ramp[i].a, p4_ramp[i].b, td - start, len);
            o->next_change_s = (p4_ramp[i].a != p4_ramp[i].b) ? 1U : (start + len - td);
            o->phase = (uint16_t)(day * 100U + i);
            break;
          }
          start += len;
        }
      }
      else                                            /* gun/gece: duvar saati */
      {
        uint32_t h6 = 6U * 3600U, h7 = 7U * 3600U, h18 = 18U * 3600U, h19 = 19U * 3600U;
        uint32_t x = tod_s % 86400U;
        if (x < h6)       { o->sp_x10 = 180; o->phase = 0; o->next_change_s = h6 - x; }
        else if (x < h7)  { o->sp_x10 = Lerp(180, 280, x - h6, 3600U);  o->phase = 1; o->next_change_s = 1U; }
        else if (x < h18) { o->sp_x10 = 280; o->phase = 2; o->next_change_s = h18 - x; }
        else if (x < h19) { o->sp_x10 = Lerp(280, 180, x - h18, 3600U); o->phase = 3; o->next_change_s = 1U; }
        else              { o->sp_x10 = 180; o->phase = 4; o->next_change_s = 86400U - x + h6; }
        o->phase = (uint16_t)(day * 100U + o->phase);
      }
      break;
    }

    case EXP_P5_DIST:
      o->closed = 1U;
      o->ctrl   = EXP_CTRL_PID;
      o->sp_x10 = (int16_t)(pr->p5_sp_c * 10U);
      o->next_change_s = Proc_Duration(p, pr) - t;
      break;

    case EXP_P6_VALID:
    {
      uint32_t ta = (uint32_t)pr->p6_aprbs_h * EXP_HOUR_S;
      if (t < ta)
      {
        uint16_t seg;
        Aprbs(EXP_P6_SEED, 1000U, 3U, 40U, t, &o->u, &seg, &o->next_change_s);
        if (o->next_change_s > ta - t) o->next_change_s = ta - t;
        o->phase = seg;
      }
      else
      {
        uint32_t tc  = t - ta;
        uint32_t len = 150U * EXP_MIN_S;               /* 2.5 saat */
        uint32_t seg = tc / len;
        o->closed = 1U;
        o->ctrl   = EXP_CTRL_PID;
        o->sp_x10 = p6_steps[seg % 6U];
        o->next_change_s = len - (tc % len);
        o->phase  = (uint16_t)(1000U + seg);
      }
      break;
    }

    default:
      o->done = 1U;
      break;
  }
}
