/**
  ******************************************************************************
  * @file    dht22.c
  * @brief   DHT22 / AM2302 sicaklik-nem sensoru surucusu (STM32F7, tek hat)
  *
  *  Baglanti : DATA -> GPIO (open-drain, dahili pull-up acik)
  *  Zamanlama: DWT->CYCCNT (216 MHz'de 1 us = 216 dongu)
  *
  *  Protokol (AM2302 veri sayfasi):
  *   MCU  : hatti >= 1 ms LOW tutar, sonra birakir
  *   DHT  : 20-40 us sonra 80 us LOW + 80 us HIGH cevap verir
  *   Veri : 40 bit; her bit 50 us LOW + HIGH (26-28 us = '0', 70 us = '1')
 *          Karar HIGH suresinin LOW suresiyle karsilastirilmasiyla verilir.
  *   Paket: RH_H RH_L T_H T_L CHECKSUM   (T_H bit7 = negatif isaret)
  ******************************************************************************
  */
#include "dht22.h"

#define DHT22_START_LOW_US     2000U   /* MCU baslatma darbesi            */
#define DHT22_EDGE_TIMEOUT_US  120U    /* her kenar icin azami bekleme    */

static uint32_t cyc_per_us = 216U;

/* ---------------------------------------------------------------------------*/
static void DWT_Init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
#if defined(__CORTEX_M) && (__CORTEX_M == 7U)
  DWT->LAR = 0xC5ACCE55U;               /* Cortex-M7: DWT kilidini ac */
#endif
  DWT->CYCCNT = 0U;
  DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
  cyc_per_us  = SystemCoreClock / 1000000U;
  if (cyc_per_us == 0U) cyc_per_us = 1U;
}

static void delay_us(uint32_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = us * cyc_per_us;
  while ((DWT->CYCCNT - start) < ticks) { }
}

static inline void line_low(DHT22_HandleTypeDef *h)     { h->port->BSRR = (uint32_t)h->pin << 16U; }
static inline void line_release(DHT22_HandleTypeDef *h) { h->port->BSRR = h->pin; }
static inline uint8_t line_read(DHT22_HandleTypeDef *h) { return (h->port->IDR & h->pin) ? 1U : 0U; }

/* Hat 'level' seviyesinde kaldigi surece bekler.
 * Donus: gecen sure (us) veya zaman asiminda -1 */
static int32_t wait_while(DHT22_HandleTypeDef *h, uint8_t level, uint32_t timeout_us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t limit = timeout_us * cyc_per_us;
  while (line_read(h) == level)
  {
    if ((DWT->CYCCNT - start) > limit)
    {
      return -1;
    }
  }
  return (int32_t)((DWT->CYCCNT - start) / cyc_per_us);
}

/* ---------------------------------------------------------------------------*/
void DHT22_Init(DHT22_HandleTypeDef *hdht, GPIO_TypeDef *port, uint16_t pin)
{
  GPIO_InitTypeDef gpio = {0};

  hdht->port            = port;
  hdht->pin             = pin;
  hdht->temperature_x10 = 0;
  hdht->humidity_x10    = 0;
  hdht->temperature     = 0.0f;
  hdht->humidity        = 0.0f;
  hdht->valid           = 0U;
  hdht->last_status     = DHT22_ERR_TOO_SOON;
  hdht->ok_count        = 0U;
  hdht->error_count     = 0U;
  hdht->fault           = 0U;
  hdht->consecutive_errors = 0U;
  hdht->last_ok_tick    = 0U;
  hdht->raw[0] = hdht->raw[1] = hdht->raw[2] = hdht->raw[3] = hdht->raw[4] = 0U;

  DWT_Init();

  /* Pini open-drain yap (CubeMX push-pull uretse bile burada duzeltilir).
   * GPIO port saati MX_GPIO_Init icinde acilmis olmali. */
  line_release(hdht);
  gpio.Pin   = pin;
  gpio.Mode  = GPIO_MODE_OUTPUT_OD;
  gpio.Pull  = GPIO_PULLUP;            /* dahili pull-up (~40k); kablo uzunsa harici 4.7-10k ekleyin */
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(port, &gpio);
  line_release(hdht);

  /* Sensor enerjilendikten sonra ~1 s kararlilik ister; ilk okuma
   * DHT22_MIN_INTERVAL_MS sonra yapilabilir. */
  hdht->last_read_tick = HAL_GetTick();
}

DHT22_Status DHT22_Read(DHT22_HandleTypeDef *hdht)
{
  uint8_t  data[5] = {0};
  uint32_t primask;
  int32_t  t, t_low;
  uint8_t  i;
  DHT22_Status st = DHT22_OK;

  if ((HAL_GetTick() - hdht->last_read_tick) < DHT22_MIN_INTERVAL_MS)
  {
    return DHT22_ERR_TOO_SOON;
  }
  hdht->last_read_tick = HAL_GetTick();

  /* 1) Baslatma darbesi: >= 1 ms LOW */
  line_low(hdht);
  delay_us(DHT22_START_LOW_US);

  /* Bit zamanlamasi bozulmasin diye okuma suresince (~5 ms) kesmeler kapali */
  primask = __get_PRIMASK();
  __disable_irq();

  line_release(hdht);

  /* Hat birakildiktan sonra pull-up ile yukselmesi birkac us surer.
   * Once hattin gercekten HIGH oldugunu bekle; aksi halde yukselme suresi
   * sensor cevabi sanilir ve tum bitler 1 bit kayar (checksum hatasi). */
  if (wait_while(hdht, 0U, 20U) < 0) { st = DHT22_ERR_NO_RESPONSE; goto done; }

  /* 2) Sensor cevabi: (20-40 us HIGH) -> 80 us LOW -> 80 us HIGH */
  if (wait_while(hdht, 1U, DHT22_EDGE_TIMEOUT_US) < 0) { st = DHT22_ERR_NO_RESPONSE; goto done; }
  if (wait_while(hdht, 0U, DHT22_EDGE_TIMEOUT_US) < 0) { st = DHT22_ERR_NO_RESPONSE; goto done; }
  if (wait_while(hdht, 1U, DHT22_EDGE_TIMEOUT_US) < 0) { st = DHT22_ERR_NO_RESPONSE; goto done; }

  /* 3) 40 veri biti */
  for (i = 0U; i < 40U; i++)
  {
    t_low = wait_while(hdht, 0U, DHT22_EDGE_TIMEOUT_US);     /* ~50 us        */
    if (t_low < 0) { st = DHT22_ERR_TIMEOUT; goto done; }
    t = wait_while(hdht, 1U, DHT22_EDGE_TIMEOUT_US);         /* 26-28 / 70 us */
    if (t < 0) { st = DHT22_ERR_TIMEOUT; goto done; }

    /* Karar: HIGH suresi, ayni bitin LOW suresinden uzunsa '1'.
     * Sabit esige gore saat/olcum sapmalarina karsi daha toleransli. */
    data[i / 8U] <<= 1U;
    if (t > t_low)
    {
      data[i / 8U] |= 1U;
    }
  }

done:
  __set_PRIMASK(primask);
  line_release(hdht);

  /* Hata ayiklama icin ham baytlar (Keil Watch: hdht22.raw) */
  for (i = 0U; i < 5U; i++)
  {
    hdht->raw[i] = data[i];
  }

  if (st == DHT22_OK)
  {
    uint8_t sum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (sum != data[4])
    {
      st = DHT22_ERR_CHECKSUM;
    }
  }

  if (st == DHT22_OK)
  {
    uint16_t rh = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
    int16_t  tc = (int16_t)((((uint16_t)data[2] & 0x7FU) << 8) | data[3]);
    if (data[2] & 0x80U)
    {
      tc = (int16_t)(-tc);
    }
    hdht->humidity_x10    = rh;
    hdht->temperature_x10 = tc;
    hdht->humidity        = (float)rh / 10.0f;
    hdht->temperature     = (float)tc / 10.0f;
    hdht->valid           = 1U;
    hdht->ok_count++;
    hdht->fault              = 0U;
    hdht->consecutive_errors = 0U;
    hdht->last_ok_tick       = HAL_GetTick();
  }
  else
  {
    hdht->error_count++;
    if (hdht->consecutive_errors < 255U) hdht->consecutive_errors++;
    if (hdht->consecutive_errors >= DHT22_FAULT_LIMIT) hdht->fault = 1U;
  }

  hdht->last_status = st;
  return st;
}

/**
  * @brief  Bloklamayan gorev: 2 s doldugunda bir okuma yapar, aksi halde
  *         hemen doner. Ana donguden surekli cagrilir.
  *         (Okumanin kendisi ~5 ms surer; bit zamanlamasi geregi kisaltilamaz.)
  */
void DHT22_Task(DHT22_HandleTypeDef *hdht)
{
  if ((HAL_GetTick() - hdht->last_read_tick) >= DHT22_MIN_INTERVAL_MS)
  {
    (void)DHT22_Read(hdht);
  }
}

const char *DHT22_StatusString(DHT22_Status status)
{
  switch (status)
  {
    case DHT22_OK:              return "OK";
    case DHT22_ERR_NO_RESPONSE: return "CEVAP YOK";
    case DHT22_ERR_TIMEOUT:     return "ZAMAN ASIMI";
    case DHT22_ERR_CHECKSUM:    return "CHECKSUM";
    case DHT22_ERR_TOO_SOON:    return "BEKLIYOR";
    default:                    return "?";
  }
}
