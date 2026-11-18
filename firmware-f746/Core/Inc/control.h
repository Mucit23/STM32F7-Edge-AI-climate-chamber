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

void    OnOff_Init(OnOff_Ctrl *c, float hyst_c, int16_t u_heat, int16_t u_cool);
void    OnOff_Reset(OnOff_Ctrl *c);
int16_t OnOff_Step(OnOff_Ctrl *c, float sp, float pv);

#endif /* __CONTROL_H */
