/**
  * printf yonlendirmesi ve hata ayiklama konsolu
  *
  * printf -> USART1 (ST-LINK sanal COM portu, PA9/PB7), 115200 8N1.
  * USART6 artik Peltier surucu kartina ayrildi (peltier.c).
  *
  * Konsol komutlari (terminalden yazilir, Enter gerekmez):
  *   *<deger>#   Peltier komutu, -1000..+1000   (ornek: *-300#)
  *   *S#         Peltier baglanti durumunu yazdir
  * Bu komutlar acik cevrim denemeleri icin gecicidir.
  */
#include <stdio.h>
#include <stdint.h>
#include "main.h"
#include "peltier.h"

#ifndef S_IFCHR
#define S_IFCHR 0x2000
#endif

struct stat
{
    unsigned int st_mode;
};

UART_HandleTypeDef huart1;               /* ST-LINK VCP */

#ifndef STDOUT_USART
#define STDOUT_USART huart1
#endif

/* ---------------- Konsol alimi (kesme + halka tampon) ---------------- */
#define DBG_RING_SIZE  64U
static volatile uint8_t  dbg_ring[DBG_RING_SIZE];
static volatile uint16_t dbg_head = 0;
static uint16_t          dbg_tail = 0;
static uint8_t           dbg_byte;

void Debug_UART_Init(void)
{
    /* PA9 (TX) / PB7 (RX) MX_GPIO_Init'te zaten AF7_USART1 olarak ayarli */
    __HAL_RCC_USART1_CLK_ENABLE();

    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
        Error_Handler();
    }
    HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    (void)HAL_UART_Receive_IT(&huart1, &dbg_byte, 1);
}

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

void Debug_UartRxCplt(UART_HandleTypeDef *huart)
{
    if (huart != &huart1) return;
    uint16_t next = (uint16_t)((dbg_head + 1U) & (DBG_RING_SIZE - 1U));
    if (next != dbg_tail)
    {
        dbg_ring[dbg_head] = dbg_byte;
        dbg_head = next;
    }
    (void)HAL_UART_Receive_IT(&huart1, &dbg_byte, 1);
}

void Debug_UartError(UART_HandleTypeDef *huart)
{
    if (huart != &huart1) return;
    (void)HAL_UART_Receive_IT(&huart1, &dbg_byte, 1);
}

/* Ana donguden cagrilir: "*<deger>#" ve "*S#" komutlarini cozer */
void Debug_Task(void)
{
    static uint8_t active = 0, neg = 0, nd = 0, query = 0;
    static int32_t val = 0;

    while (dbg_tail != dbg_head)
    {
        char c = (char)dbg_ring[dbg_tail];
        dbg_tail = (uint16_t)((dbg_tail + 1U) & (DBG_RING_SIZE - 1U));

        if (c == '*') { active = 1; neg = 0; nd = 0; query = 0; val = 0; continue; }
        if (!active) continue;

        if (c >= '0' && c <= '9' && nd < 4 && !query) { val = val * 10 + (c - '0'); nd++; }
        else if (c == '-' && nd == 0 && !neg && !query) { neg = 1; }
        else if ((c == 'S' || c == 's') && nd == 0 && !neg) { query = 1; }
        else if (c == '#' && query)
        {
            const Peltier_Status *st = Peltier_GetStatus();
            printf("# Peltier: komut=%d onay=%d baglanti=%s tx=%lu ack=%lu hata=%lu\r\n",
                   st->command, st->applied, Peltier_LinkShort(st->link),
                   (unsigned long)st->tx_count, (unsigned long)st->ack_count,
                   (unsigned long)st->error_count);
            active = 0;
        }
        else if (c == '#' && nd > 0)
        {
            Peltier_SetCommand((int16_t)(neg ? -val : val));
            printf("# Peltier komutu: %d\r\n", Peltier_GetCommand());
            active = 0;
        }
        else { active = 0; }
    }
}

FILE __stdout;
FILE __stdin;

int fputc(int ch, FILE *f)
{
    (void)f;
    uint8_t c = (uint8_t)ch;
    HAL_UART_Transmit(&STDOUT_USART, &c, 1, 10);
    return ch;
}

int fgetc(FILE *f)
{
    (void)f;
    return EOF;               /* alim kesme ile yapiliyor (Debug_Task) */
}

int ferror(FILE *f)
{
    (void)f;
    return 0;
}

int _write(int file, char *ptr, int len)
{
    (void)file;
    HAL_UART_Transmit(&STDOUT_USART, (uint8_t *)ptr, (uint16_t)len, 10);
    return len;
}

int _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    return 0;
}

int _close(int file)
{
    (void)file;
    return 0;
}

int _fstat(int file, struct stat *st)
{
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file)
{
    return (file == 1 || file == 2);
}

int _lseek(int file, int ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

void _ttywrch(int ch)
{
    uint8_t c = (uint8_t)ch;
    HAL_UART_Transmit(&STDOUT_USART, &c, 1, 10);
}

void _sys_exit(int return_code)
{
    (void)return_code;
    while (1)
    {
    }
}
