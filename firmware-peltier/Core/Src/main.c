/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim3;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
/* ---------------- Haberlesme protokolu (USART1, 115200 8N1) ----------------
 * Ana kart -> Surucu
 *   *<deger>#   Guc komutu, -1000 (max sogutma) ... +1000 (max isitma)
 *               Ornek: *-450#  *0#  *1000#
 *               Ana kart bu komutu ~500 ms'de bir tekrar gonderir.
 *   *S#         Durum sorgusu
 * Surucu -> Ana kart
 *   *A<deger>#  Onay: o anda uygulanan deger (her komuta ve *S#'ye cevap)
 *   *T#         Zaman asimi: 2 s komut gelmedi, cikis kapatildi
 *               (zaman asimi durumunda *S#'ye de *T# doner)
 * Paketlerin sonuna okunabilirlik icin '\n' eklenir; alici bunu yok sayar.
 */
#define LINK_TIMEOUT_MS   2000U     /* bu sure komut gelmezse cikis kapanir */

volatile int16_t PWM_Val = 0;       /* uygulanan komut (-1000..+1000)        */

#define RX_DIGITS_MAX   4
static uint8_t rx_byte;
static uint8_t rx_active = 0;       /* '*' alindi, paket toplaniyor */
static uint8_t rx_neg = 0;
static uint8_t rx_ndigits = 0;
static int16_t rx_value = 0;
static uint8_t rx_query = 0;        /* paket govdesi 'S' */

static volatile uint8_t  cmd_new = 0;        /* yeni guc komutu geldi, onay gonderilecek */
static volatile uint8_t  query_new = 0;      /* *S# geldi, durum gonderilecek           */
static volatile uint32_t last_cmd_tick = 0;  /* son gecerli guc komutunun zamani       */
static uint8_t           link_timeout = 1;   /* 1: zaman asimi, cikis kapali (acilista) */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM3_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* TIM3: 48 MHz, PSC = 0, ARR = 4800-1  ->  10 kHz PWM */
#define DRIVER_PWM_PERIOD   4800U   /* TIM3 ARR + 1 */
#define DRIVER_PWM_MAX      4560U   /* %95: bootstrap kondansatoru sarj suresi icin */
#define DRIVER_CMD_MAX      1000

/* IR2104 H koprusu surucu.
 * speed: -1000 (tam sol) ... 0 (dur, DRIVER_EN=0) ... +1000 (tam sag)
 * CH3 (PB0) = A kolu, CH4 (PB1) = B kolu. Pozitif: A kolu PWM, B kolu 0 (alt anahtar acik).
 * Negatif: B kolu PWM, A kolu 0. speed != 0 iken DRIVER_EN=1. */
void Driver_Set(int16_t speed)
{
  if (speed > DRIVER_CMD_MAX)  speed = DRIVER_CMD_MAX;
  if (speed < -DRIVER_CMD_MAX) speed = -DRIVER_CMD_MAX;

  uint32_t mag = (speed < 0) ? (uint32_t)(-speed) : (uint32_t)speed;
  uint32_t duty = (mag * DRIVER_PWM_MAX) / DRIVER_CMD_MAX;

  if (speed >= 0)
  {
    __HAL_TIM_SetCompare(&htim3, TIM_CHANNEL_4, 0);
    __HAL_TIM_SetCompare(&htim3, TIM_CHANNEL_3, duty);
  }
  else
  {
    __HAL_TIM_SetCompare(&htim3, TIM_CHANNEL_3, 0);
    __HAL_TIM_SetCompare(&htim3, TIM_CHANNEL_4, duty);
  }

  /* speed = 0 ise surucu kapali (DRIVER_EN=0), aksi halde acik */
  HAL_GPIO_WritePin(DRIVER_EN_GPIO_Port, DRIVER_EN_Pin, (speed != 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* "*A-450#\n" / "*T#\n" paketini gonderir (kisa, ~0.7 ms bloklar) */
static void Link_SendAck(int16_t value)
{
  char buf[16];
  uint8_t n = 0;
  int32_t v = value;

  buf[n++] = '*';
  buf[n++] = 'A';
  if (v < 0) { buf[n++] = '-'; v = -v; }
  if (v >= 1000) buf[n++] = (char)('0' + (v / 1000) % 10);
  if (v >= 100)  buf[n++] = (char)('0' + (v / 100) % 10);
  if (v >= 10)   buf[n++] = (char)('0' + (v / 10) % 10);
  buf[n++] = (char)('0' + v % 10);
  buf[n++] = '#';
  buf[n++] = '\n';
  HAL_UART_Transmit(&huart1, (uint8_t *)buf, n, 10);
}

static void Link_SendTimeout(void)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)"*T#\n", 4, 10);
}


/* Ana dongude surekli cagrilir: komut uygular, onay/durum gonderir,
 * zaman asimini denetler ve LED'i surer. Bloklamaz. */
static void Link_Task(void)
{
  static uint32_t led_tick = 0;
  uint32_t now = HAL_GetTick();

  /* 1) Yeni guc komutu: cikisa uygula ve onay gonder */
  if (cmd_new)
  {
    cmd_new = 0;
    link_timeout = 0;
    Driver_Set(PWM_Val);
    Link_SendAck(PWM_Val);
  }

  /* 2) Durum sorgusu */
  if (query_new)
  {
    query_new = 0;
    if (link_timeout) Link_SendTimeout();
    else              Link_SendAck(PWM_Val);
  }

  /* 3) Zaman asimi: 2 s gecerli komut yoksa cikisi tamamen kapat.
   *    last_cmd_tick kesmede guncellenir; once o okunur, sonra zaman alinir
   *    (aksi halde 'now < last' olup fark tasabilir). */
  uint32_t last = last_cmd_tick;
  now = HAL_GetTick();
  if (!link_timeout && !cmd_new && (now - last) >= LINK_TIMEOUT_MS)
  {
    link_timeout = 1;
    PWM_Val = 0;
    Driver_Set(0);          /* PWM = 0 ve DRIVER_EN = 0 */
    Link_SendTimeout();
  }

  /* 4) LED: normalde 100 ms, zaman asiminda 1 s yanip soner */
  if ((now - led_tick) >= (link_timeout ? 1000U : 100U))
  {
    led_tick = now;
    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
  }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM3_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  Driver_Set(0);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);
  HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    Link_Task();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL6;
  RCC_OscInitStruct.PLL.PREDIV = RCC_PREDIV_DIV1;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART1;
  PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK1;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 4800-1;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
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
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, DRIVER_EN_Pin|LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : DRIVER_EN_Pin LED_Pin */
  GPIO_InitStruct.Pin = DRIVER_EN_Pin|LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/* Gelen her byte icin cagrilir. '*' paketi baslatir, '#' bitirir. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    uint8_t c = rx_byte;

    if (c == '*')
    {
      rx_active = 1;
      rx_neg = 0;
      rx_ndigits = 0;
      rx_value = 0;
      rx_query = 0;
    }
    else if (rx_active)
    {
      if (c >= '0' && c <= '9' && rx_ndigits < RX_DIGITS_MAX && !rx_query)
      {
        rx_value = rx_value * 10 + (c - '0');
        rx_ndigits++;
      }
      else if (c == '-' && rx_ndigits == 0 && !rx_neg && !rx_query)
      {
        rx_neg = 1;
      }
      else if (c == 'S' && rx_ndigits == 0 && !rx_neg && !rx_query)
      {
        rx_query = 1;
      }
      else if (c == '#' && rx_query)
      {
        query_new = 1;
        rx_active = 0;
      }
      else if (c == '#' && rx_ndigits > 0)
      {
        int16_t v = rx_neg ? -rx_value : rx_value;
        if (v > DRIVER_CMD_MAX)  v = DRIVER_CMD_MAX;
        if (v < -DRIVER_CMD_MAX) v = -DRIVER_CMD_MAX;
        PWM_Val = v;
        last_cmd_tick = HAL_GetTick();
        cmd_new = 1;
        rx_active = 0;
      }
      else
      {
        rx_active = 0; /* gecersiz karakter: paketi at */
      }
    }

    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
  }
}

/* Overrun/framing hatasinda alimi yeniden baslat */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    rx_active = 0;
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
  }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
