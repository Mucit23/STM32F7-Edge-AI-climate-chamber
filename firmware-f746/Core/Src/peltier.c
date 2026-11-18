/**
  ******************************************************************************
  * @file    peltier.c
  * @brief   Peltier surucu karti haberlesmesi (USART6, kesme tabanli)
  ******************************************************************************
  * Alim : her byte kesmede halka tampona yazilir, Peltier_Task() cozumler.
  * Gonderim: HAL_UART_Transmit_IT, bloklamaz.
  * USART6_IRQHandler bu dosyada tanimlidir. CubeMX'te USART6 kesmesi
  * etkinlestirilirse stm32f7xx_it.c'ye de eklenir; o durumda buradakini silin.
  ******************************************************************************
  */
#include "peltier.h"
#include <string.h>

#define RX_RING_SIZE   64U            /* 2'nin kuvveti olmali */
#define RX_BODY_MAX    8U

static UART_HandleTypeDef *pl_uart = NULL;
static Peltier_Status      pl;

/* Alim halka tamponu (kesme yazar, Task okur) */
static volatile uint8_t  rx_ring[RX_RING_SIZE];
static volatile uint16_t rx_head = 0;
static uint16_t          rx_tail = 0;
static uint8_t           rx_byte;

/* Paket cozucu */
static uint8_t  rx_active = 0;
static char     rx_body[RX_BODY_MAX + 1];
static uint8_t  rx_len = 0;

/* Gonderim */
static uint8_t  tx_buf[12];
static int16_t  sent_value = 0;       /* son gonderilen komut */
static int16_t  prev_value = 0;       /* bir onceki komut (gecikmeli onay icin) */
static uint32_t change_tick = 0;      /* komutun son degistigi an                */
#define LATE_ACK_MS   300U            /* bu sure icinde eski degerin onayi normal */
static uint32_t last_tx_tick = 0;
static uint32_t init_tick = 0;

/* ------------------------------------------------------------------------- */
static void Rx_Start(void)
{
  if (pl_uart != NULL)
  {
    (void)HAL_UART_Receive_IT(pl_uart, &rx_byte, 1);
  }
}

static void Link_Set(Peltier_LinkState st)
{
  if (st != PELTIER_LINK_OK && st != PELTIER_LINK_WAIT && pl.link == PELTIER_LINK_OK)
  {
    pl.error_count++;
  }
  pl.link = st;
}

/* "A-450" / "T" govdesini yorumlar */
static void Frame_Handle(const char *b, uint8_t n, uint32_t now)
{
  if (n == 1 && b[0] == 'T')
  {
    Link_Set(PELTIER_LINK_TIMEOUT);
    pl.applied = 0;
    return;
  }
  if (n >= 2 && b[0] == 'A')
  {
    uint8_t i = 1, neg = 0, nd = 0;
    int32_t v = 0;
    if (b[i] == '-') { neg = 1; i++; }
    for (; i < n; i++)
    {
      if (b[i] < '0' || b[i] > '9' || nd >= 4) return;   /* bozuk paket: yok say */
      v = v * 10 + (b[i] - '0');
      nd++;
    }
    if (nd == 0) return;
    if (neg) v = -v;

    pl.applied = (int16_t)v;
    pl.last_ack_tick = now;
    pl.ack_count++;
    if ((int16_t)v == sent_value)
    {
      Link_Set(PELTIER_LINK_OK);
    }
    else if (!((int16_t)v == prev_value && (now - change_tick) < LATE_ACK_MS))
    {
      Link_Set(PELTIER_LINK_MISMATCH);
    }
    /* komut yeni degistiyse eski degerin onayi beklenen bir durum: degistirme */
  }
}

static void Rx_Process(uint32_t now)
{
  while (rx_tail != rx_head)
  {
    char c = (char)rx_ring[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1U) & (RX_RING_SIZE - 1U));

    if (c == '*')
    {
      rx_active = 1;
      rx_len = 0;
    }
    else if (rx_active)
    {
      if (c == '#')
      {
        rx_body[rx_len] = '\0';
        Frame_Handle(rx_body, rx_len, now);
        rx_active = 0;
      }
      else if (rx_len < RX_BODY_MAX)
      {
        rx_body[rx_len++] = c;
      }
      else
      {
        rx_active = 0;            /* cok uzun: paketi at */
      }
    }
    /* paket disindaki karakterler ('\n' vb.) yok sayilir */
  }
}

static void Tx_Command(int16_t v, uint32_t now)
{
  uint8_t n = 0;
  int32_t a = v;

  if (pl_uart == NULL || pl_uart->gState != HAL_UART_STATE_READY)
  {
    return;                       /* onceki gonderim surmekte; sonraki turda */
  }
  tx_buf[n++] = '*';
  if (a < 0) { tx_buf[n++] = '-'; a = -a; }
  if (a >= 1000) tx_buf[n++] = (uint8_t)('0' + (a / 1000) % 10);
  if (a >= 100)  tx_buf[n++] = (uint8_t)('0' + (a / 100) % 10);
  if (a >= 10)   tx_buf[n++] = (uint8_t)('0' + (a / 10) % 10);
  tx_buf[n++] = (uint8_t)('0' + a % 10);
  tx_buf[n++] = '#';

  if (HAL_UART_Transmit_IT(pl_uart, tx_buf, n) == HAL_OK)
  {
    if (v != sent_value)
    {
      prev_value  = sent_value;
      change_tick = now;
    }
    sent_value = v;
    last_tx_tick = now;
    pl.tx_count++;
  }
}

/* ------------------------------------------------------------------------- */
void Peltier_Init(UART_HandleTypeDef *huart)
{
  memset(&pl, 0, sizeof(pl));
  pl.link = PELTIER_LINK_WAIT;
  pl_uart = huart;
  rx_head = rx_tail = 0;
  rx_active = 0;
  init_tick = HAL_GetTick();
  last_tx_tick = init_tick - PELTIER_TX_PERIOD_MS;   /* ilk komut hemen gitsin */

  HAL_NVIC_SetPriority(USART6_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(USART6_IRQn);
  Rx_Start();
}

void Peltier_Task(void)
{
  uint32_t now = HAL_GetTick();

  Rx_Process(now);

  /* Komut degistiyse hemen, degismediyse periyodik olarak gonder */
  if (pl.command != sent_value || (now - last_tx_tick) >= PELTIER_TX_PERIOD_MS)
  {
    Tx_Command(pl.command, now);
  }

  /* Onay gelmiyorsa: HATA (cevap yok). Surucu *T# bildirdiyse o daha
   * aciklayici oldugu icin korunur; yeni bir onay gelince OK'a doner. */
  if (pl.link != PELTIER_LINK_TIMEOUT)
  {
    if (pl.ack_count == 0U)
    {
      if ((now - init_tick) >= PELTIER_LINK_LOST_MS) Link_Set(PELTIER_LINK_NO_REPLY);
    }
    else if ((now - pl.last_ack_tick) >= PELTIER_LINK_LOST_MS)
    {
      Link_Set(PELTIER_LINK_NO_REPLY);
    }
  }
}

void Peltier_SetCommand(int16_t value)
{
  if (value >  PELTIER_CMD_MAX) value =  PELTIER_CMD_MAX;
  if (value < -PELTIER_CMD_MAX) value = -PELTIER_CMD_MAX;
  pl.command = value;
}

int16_t Peltier_GetCommand(void)               { return pl.command; }
uint8_t Peltier_IsLinkOk(void)                 { return (pl.link == PELTIER_LINK_OK) ? 1U : 0U; }
const Peltier_Status *Peltier_GetStatus(void)  { return &pl; }

const char *Peltier_LinkShort(Peltier_LinkState st)
{
  switch (st)
  {
    case PELTIER_LINK_OK:       return "OK";
    case PELTIER_LINK_NO_REPLY: return "YOK";   /* cevap yok          */
    case PELTIER_LINK_TIMEOUT:  return "ZAM";   /* surucu zaman asimi */
    case PELTIER_LINK_MISMATCH: return "UYM";   /* deger uyusmuyor    */
    default:                    return "BEK";   /* bekleniyor         */
  }
}

/* ------------------------------------------------------------------------- */
void Peltier_UartRxCplt(UART_HandleTypeDef *huart)
{
  if (huart != pl_uart) return;
  uint16_t next = (uint16_t)((rx_head + 1U) & (RX_RING_SIZE - 1U));
  if (next != rx_tail)            /* dolu ise byte atilir */
  {
    rx_ring[rx_head] = rx_byte;
    rx_head = next;
  }
  Rx_Start();
}

void Peltier_UartError(UART_HandleTypeDef *huart)
{
  if (huart != pl_uart) return;
  rx_active = 0;
  Rx_Start();                     /* overrun/framing sonrasi alimi yeniden baslat */
}

void USART6_IRQHandler(void)
{
  if (pl_uart != NULL)
  {
    HAL_UART_IRQHandler(pl_uart);
  }
}
