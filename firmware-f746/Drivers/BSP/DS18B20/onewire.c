/******************************************************************************
*  @file    onewire.c
*  @author  Ferhat YOL
*  @version V3.0.0
*  @date    05-11-2026
*  @brief   OneWire Protocol Communication Library - STM32F7 HAL Implementation
*           (V2.0.0 STM32G0 surumunden coklu hat + DWT gecikme ile uyarlandi)
******************************************************************************/
#include "onewire.h"

static uint32_t ow_cyc_per_us = 0U;

/* ---------------------------------------------------------------------------
 * Hat seviyesi (open-drain): LOW = surukle, RELEASE = pull-up yukari ceker
 * ---------------------------------------------------------------------------*/
static inline void OneWire_BusLow(OneWire_HandleTypeDef *how)
{
  how->port->BSRR = (uint32_t)how->pin << 16U;
}

static inline void OneWire_BusRelease(OneWire_HandleTypeDef *how)
{
  how->port->BSRR = how->pin;
}

static inline GPIO_PinState OneWire_BusRead(OneWire_HandleTypeDef *how)
{
  return ((how->port->IDR & how->pin) != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

/**
  * @brief  DWT dongu sayacini baslatir (tum hatlar icin bir kez yeterli)
  */
static void OneWire_DelayInit(void)
{
  if (ow_cyc_per_us != 0U)
  {
    return;
  }
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
#if defined(__CORTEX_M) && (__CORTEX_M == 7U)
  DWT->LAR = 0xC5ACCE55U;                 /* Cortex-M7: DWT kilidini ac */
#endif
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  ow_cyc_per_us = SystemCoreClock / 1000000U;
  if (ow_cyc_per_us == 0U)
  {
    ow_cyc_per_us = 1U;
  }
}

/**
  * @brief  Microsecond delay using DWT cycle counter
  * @param  us: Microseconds to delay
  */
void DelayUs(uint32_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = us * ow_cyc_per_us;
  while ((DWT->CYCCNT - start) < ticks) { }
}

/**
  * @brief  Initialize OneWire line (open-drain output, internal pull-up)
  * @param  how : OneWire handle
  * @param  port: GPIO port (main.c'deki #define)
  * @param  pin : GPIO pin  (main.c'deki #define)
  * @return HAL_OK
  */
HAL_StatusTypeDef OneWire_Init(OneWire_HandleTypeDef *how, GPIO_TypeDef *port, uint16_t pin)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  how->port = port;
  how->pin  = pin;

  OneWire_DelayInit();

  /* GPIO port saati MX_GPIO_Init icinde acilmis olmali */
  OneWire_BusRelease(how);
  GPIO_InitStruct.Pin   = pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull  = GPIO_PULLUP;      /* harici pull-up'a ek destek */
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(port, &GPIO_InitStruct);
  OneWire_BusRelease(how);

  return HAL_OK;
}

/**
  * @brief  Reset OneWire bus and wait for slave presence pulse
  * @return HAL_OK if slave present, HAL_ERROR if timeout/no slave
  */
HAL_StatusTypeDef OneWire_Reset(OneWire_HandleTypeDef *how)
{
  uint32_t primask;
  uint32_t start, limit;
  GPIO_PinState released;

  /* Reset/presence el sikismasi zamanlama kritik: kesmeler kapali */
  primask = __get_PRIMASK();
  __disable_irq();

  OneWire_BusLow(how);
  DelayUs(ONEWIRE_RESET_LOW);
  OneWire_BusRelease(how);

  /* Sensorun hatti LOW cekmesini (presence) bekle */
  DelayUs(ONEWIRE_PRESENCE_WAIT_MIN);
  start = DWT->CYCCNT;
  limit = ONEWIRE_PRESENCE_TIMEOUT * ow_cyc_per_us;
  while (OneWire_BusRead(how) == GPIO_PIN_SET)
  {
    if ((DWT->CYCCNT - start) > limit)
    {
      __set_PRIMASK(primask);
      return HAL_ERROR;                 /* presence yok: sensor yok/kablo kopuk */
    }
  }

  /* Sensorun hatti birakmasini bekle */
  DelayUs(ONEWIRE_RESET_HIGH);
  released = OneWire_BusRead(how);

  __set_PRIMASK(primask);

  return (released == GPIO_PIN_SET) ? HAL_OK : HAL_ERROR;  /* LOW kaldiysa hat kisa devre */
}

/**
  * @brief  Write one bit (her slot kesmesiz/atomik)
  */
static void OneWire_WriteBit(OneWire_HandleTypeDef *how, uint8_t bit)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();

  OneWire_BusLow(how);
  if (bit)
  {
    DelayUs(ONEWIRE_WRITE_1_LOW);
    OneWire_BusRelease(how);
    DelayUs(64U);
  }
  else
  {
    DelayUs(ONEWIRE_WRITE_0_LOW);
    OneWire_BusRelease(how);
    DelayUs(10U);
  }

  __set_PRIMASK(primask);
}

/**
  * @brief  Read one bit (slot basindan ~12 us icinde ornekleme)
  */
GPIO_PinState OneWire_ReadBit(OneWire_HandleTypeDef *how)
{
  GPIO_PinState readbit;
  uint32_t primask = __get_PRIMASK();
  __disable_irq();

  OneWire_BusLow(how);
  DelayUs(ONEWIRE_READ_LOW);
  OneWire_BusRelease(how);
  DelayUs(ONEWIRE_READ_SAMPLE);
  readbit = OneWire_BusRead(how);

  __set_PRIMASK(primask);

  DelayUs(50U);                         /* slotun geri kalani (kesmeler acik) */
  return readbit;
}

/**
  * @brief  Write one byte (LSB first)
  */
void OneWire_WriteByte(OneWire_HandleTypeDef *how, uint8_t data)
{
  uint8_t i = 8U;
  do
  {
    OneWire_WriteBit(how, data & 1U);
    data >>= 1U;
  } while (--i);
}

/**
  * @brief  Read one byte (LSB first)
  */
uint8_t OneWire_ReadByte(OneWire_HandleTypeDef *how)
{
  uint8_t i = 8U, byte = 0U;
  do
  {
    byte >>= 1U;
    if (OneWire_ReadBit(how) == GPIO_PIN_SET)
    {
      byte |= 0x80U;
    }
  } while (--i);
  return byte;
}
