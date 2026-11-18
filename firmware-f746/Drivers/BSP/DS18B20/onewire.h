/******************************************************************************
*  @file    onewire.h
*  @author  Ferhat YOL
*  @version V3.0.0
*  @date    05-11-2026
*  @brief   OneWire Protocol Communication Library for STM32 HAL
*
*  V3.0.0 (STM32F7 portu):
*   - Coklu hat: her sensor kendi OneWire_HandleTypeDef nesnesini kullanir
*     (port/pin main.c'de #define ile verilir).
*   - Mikrosaniye gecikmesi DWT dongu sayaci ile yapilir (F7'de TIM16 yok,
*     ayrica bir timer ayirmaya gerek kalmaz).
*   - Hat open-drain cikista tutulur; '1' yazmak hatti birakir, ayni pinden
*     okuma yapilir (her bitte HAL_GPIO_Init cagirilmaz, slot zamanlamasi
*     daha kararli).
*
*  @note    Hat basina bir pull-up gerekir (4.7k ideal, kisa kabloda 10k).
******************************************************************************/
#ifndef __ONEWIRE_H
#define __ONEWIRE_H

#include "stm32f7xx_hal.h"

/* ============================================================================
 * Zamanlama (us) - DS18B20 veri sayfasi
 * ============================================================================ */
#define ONEWIRE_RESET_LOW         480U   /**< Reset darbesi LOW suresi        */
#define ONEWIRE_RESET_HIGH        480U   /**< Reset sonrasi bekleme           */
#define ONEWIRE_WRITE_0_LOW       60U    /**< '0' yazma LOW suresi            */
#define ONEWIRE_WRITE_1_LOW       6U     /**< '1' yazma LOW suresi            */
#define ONEWIRE_READ_LOW          2U     /**< Okuma slotu LOW suresi          */
#define ONEWIRE_READ_SAMPLE       10U    /**< Hat birakildiktan sonra ornekleme */
#define ONEWIRE_PRESENCE_WAIT_MIN 15U    /**< Presence oncesi en kisa bekleme  */
#define ONEWIRE_PRESENCE_TIMEOUT  500U   /**< Presence darbesi zaman asimi     */

/* ============================================================================
 * Tipler
 * ============================================================================ */
typedef struct
{
  GPIO_TypeDef *port;   /**< OneWire GPIO portu  */
  uint16_t      pin;    /**< OneWire GPIO pini   */
} OneWire_HandleTypeDef;

/* ============================================================================
 * Fonksiyonlar
 * ============================================================================ */
HAL_StatusTypeDef OneWire_Init(OneWire_HandleTypeDef *how, GPIO_TypeDef *port, uint16_t pin);
HAL_StatusTypeDef OneWire_Reset(OneWire_HandleTypeDef *how);
void              OneWire_WriteByte(OneWire_HandleTypeDef *how, uint8_t data);
uint8_t           OneWire_ReadByte(OneWire_HandleTypeDef *how);
GPIO_PinState     OneWire_ReadBit(OneWire_HandleTypeDef *how);
void              DelayUs(uint32_t us);

#endif /* __ONEWIRE_H */
