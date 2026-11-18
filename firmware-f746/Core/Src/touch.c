/**
  ******************************************************************************
  * @file    touch.c
  * @brief   Dokunmatik panel (FT5336, BSP TS) - yoklamali okuma ve debounce
  *
  *  - BSP_TS_Init() I2C3'u (PH7/PH8) kendisi baslatir; CubeMX ayari gerekmez.
  *  - Kesme kullanilmaz; Touch_Task() ana donguden (~20 ms) cagrilir.
  *  - Basma: 2 ardisik okuma (~40 ms), birakma: 3 ardisik bos okuma (~60 ms).
  ******************************************************************************
  */
#include "touch.h"
#include "stm32746g_discovery_ts.h"
#include "stm32746g_discovery_lcd.h"
#include <stdio.h>

#define TOUCH_DOWN_COUNT   2U
#define TOUCH_UP_COUNT     3U

static Touch_State ts;
static uint8_t     cnt_on, cnt_off;

uint8_t Touch_Init(void)
{
  ts.ok = (BSP_TS_Init((uint16_t)BSP_LCD_GetXSize(), (uint16_t)BSP_LCD_GetYSize()) == TS_OK) ? 1U : 0U;
  ts.pressed = ts.down_evt = ts.up_evt = 0U;
  cnt_on = cnt_off = 0U;
  printf("Touch: %s\r\n", ts.ok ? "FT5336 bulundu" : "BULUNAMADI");
  return ts.ok;
}

const Touch_State *Touch_Task(void)
{
  TS_StateTypeDef st;

  ts.down_evt = 0U;
  ts.up_evt   = 0U;
  if (!ts.ok)
  {
    return &ts;
  }

  if (BSP_TS_GetState(&st) != TS_OK)
  {
    st.touchDetected = 0U;
  }

  if (st.touchDetected)
  {
    cnt_off = 0U;
    ts.x = st.touchX[0];
    ts.y = st.touchY[0];
    if (!ts.pressed && ++cnt_on >= TOUCH_DOWN_COUNT)
    {
      ts.pressed   = 1U;
      ts.down_evt  = 1U;
      ts.down_tick = HAL_GetTick();
#if TOUCH_DEBUG_PRINT
      printf("TOUCH x=%u y=%u\r\n", ts.x, ts.y);
#endif
    }
  }
  else
  {
    cnt_on = 0U;
    if (ts.pressed && ++cnt_off >= TOUCH_UP_COUNT)
    {
      ts.pressed = 0U;
      ts.up_evt  = 1U;
    }
  }
  return &ts;
}

uint8_t Touch_In(uint16_t x, uint16_t y, uint16_t rx, uint16_t ry, uint16_t rw, uint16_t rh)
{
  return (x >= rx && x < (uint16_t)(rx + rw) && y >= ry && y < (uint16_t)(ry + rh)) ? 1U : 0U;
}
