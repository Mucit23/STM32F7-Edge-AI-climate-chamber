/**
  ******************************************************************************
  * @file    control.c
  * @brief   Referans kontrolculer: PID ve ON/OFF
  *
  *  PID: u = Kp*e + I + D,  I += Kp/Ti * e * dt
  *   - Turev olcumden alinir (ayar noktasi basamaginda "turev vurusu" olmaz).
  *   - Anti-windup: cikis doyumdaysa ve hata doyumu derinlestiriyorsa integral
  *     artirilmaz (kosullu integrasyon); integral ayrica [umin, umax] ile sinirli.
  *  ON/OFF: T < SP - h -> isit, T > SP + h -> sogut, aradayken son durum korunur.
  ******************************************************************************
  */
#include "control.h"

void PID_Init(PID_Ctrl *c, float kp, float ti_s, float td_s, float umin, float umax)
{
  c->kp   = kp;
  c->ti_s = ti_s;
  c->td_s = td_s;
  c->umin = umin;
  c->umax = umax;
  PID_Reset(c);
}

void PID_Reset(PID_Ctrl *c)
{
  c->integ   = 0.0f;
  c->prev_pv = 0.0f;
  c->first   = 1U;
}

int16_t PID_Step(PID_Ctrl *c, float sp, float pv, float dt_s)
{
  float e = sp - pv;
  float p = c->kp * e;
  float d = 0.0f;
  float u;

  if (!c->first && c->td_s > 0.0f && dt_s > 0.0f)
  {
    d = -c->kp * c->td_s * (pv - c->prev_pv) / dt_s;
  }
  c->first   = 0U;
  c->prev_pv = pv;

  u = p + c->integ + d;

  if (c->ti_s > 0.0f)
  {
    uint8_t sat_hi = (u >= c->umax) ? 1U : 0U;
    uint8_t sat_lo = (u <= c->umin) ? 1U : 0U;
    if (!((sat_hi && e > 0.0f) || (sat_lo && e < 0.0f)))
    {
      c->integ += (c->kp / c->ti_s) * e * dt_s;
      if (c->integ > c->umax) c->integ = c->umax;
      if (c->integ < c->umin) c->integ = c->umin;
    }
    u = p + c->integ + d;
  }

  if (u > c->umax) u = c->umax;
  if (u < c->umin) u = c->umin;
  return (int16_t)((u >= 0.0f) ? (u + 0.5f) : (u - 0.5f));
}

void PID_SetLimits(PID_Ctrl *c, float umin, float umax)
{
  c->umin = umin;
  c->umax = umax;
  if (c->integ > umax) c->integ = umax;
  if (c->integ < umin) c->integ = umin;
}

void OnOff_Init(OnOff_Ctrl *c, float hyst_c, int16_t u_heat, int16_t u_cool)
{
  c->hyst_c = hyst_c;
  c->u_heat = u_heat;
  c->u_cool = u_cool;
  OnOff_Reset(c);
}

void OnOff_Reset(OnOff_Ctrl *c)
{
  c->state = 0;
}

int16_t OnOff_Step(OnOff_Ctrl *c, float sp, float pv)
{
  if (pv < sp - c->hyst_c)      c->state = c->u_heat;
  else if (pv > sp + c->hyst_c) c->state = c->u_cool;
  else if (c->state == 0)       c->state = (pv < sp) ? c->u_heat : c->u_cool;  /* ilk cagri */
  return c->state;
}

/* ---------------- Yuzey sicakligi sinirlayicisi ---------------- */
void Lim_Init(Lim_Ctrl *c, float kp, float ti_s, float band_c)
{
  c->kp     = kp;
  c->ti_s   = ti_s;
  c->band_c = band_c;
  Lim_Reset(c);
}

void Lim_Reset(Lim_Ctrl *c)
{
  c->allow       = 1000.0f;
  c->prev_margin = 0.0f;
  c->active      = 0U;
}

int16_t Lim_Step(Lim_Ctrl *c, float margin, int16_t applied_mag, int16_t req_mag, float dt_s)
{
  if (applied_mag < 0) applied_mag = 0;
  if (req_mag < 0)     req_mag = 0;

  if (!c->active)
  {
    if (margin < c->band_c)
    {
      c->active      = 1U;                       /* carpmasiz giris */
      c->allow       = (float)applied_mag;
      c->prev_margin = margin;
    }
    else
    {
      c->allow = 1000.0f;
      return 1000;
    }
  }

  c->allow += c->kp * (margin - c->prev_margin);
  if (c->ti_s > 0.0f) c->allow += (c->kp / c->ti_s) * margin * dt_s;
  c->prev_margin = margin;
  if (c->allow > 1000.0f) c->allow = 1000.0f;
  if (c->allow < 0.0f)    c->allow = 0.0f;

  if (margin > c->band_c + 2.0f && c->allow >= (float)req_mag)
  {
    Lim_Reset(c);                                /* uzaklasti, kisitlamiyor */
    return 1000;
  }
  return (int16_t)(c->allow + 0.5f);
}
