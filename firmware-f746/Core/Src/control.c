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
