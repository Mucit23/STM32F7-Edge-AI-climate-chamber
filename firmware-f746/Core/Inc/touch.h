/**
  ******************************************************************************
  * @file    touch.h
  * @brief   Dokunmatik panel (FT5336, BSP TS) - yoklamali okuma, debounce,
  *          bas/birak olaylari. Ana donguden her turda Touch_Task() cagrilir.
  ******************************************************************************
  */
#ifndef __TOUCH_H
#define __TOUCH_H

#include <stdint.h>

/* 1: her dokunusta koordinati printf ile yazar (ilk kurulum/test icin) */
#define TOUCH_DEBUG_PRINT   1

typedef struct
{
  uint8_t  ok;          /* panel bulundu mu (BSP_TS_Init basarili)       */
  uint8_t  pressed;     /* debounce sonrasi: parmak ekranda               */
  uint8_t  down_evt;    /* bu turda basildi (tek seferlik)                */
  uint8_t  up_evt;      /* bu turda birakildi (tek seferlik)              */
  uint16_t x;           /* son gecerli koordinat (birakmada da korunur)   */
  uint16_t y;
  uint32_t down_tick;   /* basilma ani (HAL_GetTick)                      */
} Touch_State;

uint8_t            Touch_Init(void);
const Touch_State *Touch_Task(void);
uint8_t            Touch_In(uint16_t x, uint16_t y, uint16_t rx, uint16_t ry, uint16_t rw, uint16_t rh);

#endif /* __TOUCH_H */
