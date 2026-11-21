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
#include "fatfs.h"
#include "lwip.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stm32746g_discovery_lcd.h"
#include "stm32746g_discovery_sdram.h"
#include "stm32746g_discovery.h"
#include "ds3231.h"
#include "dht22.h"
#include "ds18b20.h"
#include "peltier.h"
#include "ui.h"
#include "screen.h"
#include "logctrl.h"
#include "sdlog.h"
#include "exp.h"
#include "lwip/sio.h"
#include "net.h"
#include "aio.h"
#include "fatfs_platform.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* ---------------- Sensor haberlesme pinleri ----------------
 *  Arduino   MCU    Sensor     Konum
 *  D2        PG6    DS18B20    dis ortam
 *  D3        PB4    DS18B20    ic sogutucu tabani (Peltier tarafi)
 *  D4        PG7    DS18B20    karsi duvar yuzeyi
 *  D5        PI0    DHT22      kabin ortasi: hava sicakligi + nem (kontrol edilen)
 *  Her DS18B20 hattina 3.3V'a pull-up (4.7k ideal, kisa kabloda 10k).
 */

#define DS18B20_AMBIENT_GPIO_Port  GPIOG
#define DS18B20_AMBIENT_Pin        GPIO_PIN_6     /* D2 */

#define DS18B20_HEATSINK_GPIO_Port GPIOB
#define DS18B20_HEATSINK_Pin       GPIO_PIN_4     /* D3 */

#define DS18B20_WALL_GPIO_Port     GPIOG
#define DS18B20_WALL_Pin           GPIO_PIN_7     /* D4 */

#define DHT22_AIR_GPIO_Port        GPIOI
#define DHT22_AIR_Pin              GPIO_PIN_0     /* D5 */

#define SAMPLE_PERIOD_MS           1000U          /* ornekleme/log periyodu: 1 Hz */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

DMA2D_HandleTypeDef hdma2d;

I2C_HandleTypeDef hi2c1;

LTDC_HandleTypeDef hltdc;

SD_HandleTypeDef hsd1;

UART_HandleTypeDef huart6;

SDRAM_HandleTypeDef hsdram1;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA2D_Init(void);
static void MX_FMC_Init(void);
static void MX_LTDC_Init(void);
static void MX_I2C1_Init(void);
static void MX_SDMMC1_SD_Init(void);
static void MX_USART6_UART_Init(void);
/* USER CODE BEGIN PFP */
static uint8_t Button_IsPressed(void);
static void Draw_StartupScreen(void);
static void UI_UpdateSensors(void);
static void UI_UpdatePeltier(uint8_t force);
static void UI_UpdateSd(void);
static void UI_UpdateExp(uint8_t force);
static void UI_UpdateNet(uint8_t force);
static void Aio_Fill(Aio_Data *a);
static void Fmt_Fixed(char *buf, size_t n, int32_t v, int32_t scale);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* ---------------- Kapak anahtari (D7 = PI3) ----------------
 * Anahtar kapak kapaliyken pini GND'ye ceker: D7 = 0 kapali, D7 = 1 acik.
 * Ic pull-up acik; anahtar takili degilse kapak "acik" gorunur.
 * CubeMX D7'yi cikis olarak tanimliyor; MX_GPIO_Init'ten sonra burada
 * girise cevrilir (CubeMX'te de PI3 = GPIO_Input, Pull-up yapilabilir). */
#define DOOR_GPIO_Port      GPIOI
#define DOOR_Pin            GPIO_PIN_3
#define DOOR_DEBOUNCE_MS    50U

static uint8_t  door_open;          /* kararli durum: 1 = acik */
static uint8_t  door_raw;
static uint32_t door_t;

static void Door_Init(void)
{
  GPIO_InitTypeDef g = {0};
  g.Pin   = DOOR_Pin;
  g.Mode  = GPIO_MODE_INPUT;
  g.Pull  = GPIO_PULLUP;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(DOOR_GPIO_Port, &g);
  HAL_Delay(2);                     /* pull-up otursun */
  door_raw  = (HAL_GPIO_ReadPin(DOOR_GPIO_Port, DOOR_Pin) == GPIO_PIN_SET) ? 1U : 0U;
  door_open = door_raw;
  door_t    = HAL_GetTick();
}

/* Her dongude cagrilir; kararli durum degisince 1 doner (50 ms sabit kalmali). */
static uint8_t Door_Task(void)
{
  uint8_t  raw = (HAL_GPIO_ReadPin(DOOR_GPIO_Port, DOOR_Pin) == GPIO_PIN_SET) ? 1U : 0U;
  uint32_t now = HAL_GetTick();
  if (raw != door_raw) { door_raw = raw; door_t = now; return 0U; }
  if (raw != door_open && (now - door_t) >= DOOR_DEBOUNCE_MS)
  {
    door_open = raw;
    return 1U;
  }
  return 0U;
}


/* Sensor nesneleri (pinler: USER CODE PD bolumu) */
static DHT22_HandleTypeDef   hdht22;
static DS18B20_HandleTypeDef hds_heatsink;
static DS18B20_HandleTypeDef hds_wall;
static DS18B20_HandleTypeDef hds_ambient;
static DS18B20_HandleTypeDef *const ds_list[3] = { &hds_heatsink, &hds_wall, &hds_ambient };

static uint32_t sample_count = 0;
static uint8_t  rtc_last_sec = 0xFF;   /* 0xFF: saat bir sonraki dongude yeniden cizilsin */

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
  MX_DMA2D_Init();
  MX_FMC_Init();
  MX_LTDC_Init();
  MX_I2C1_Init();
  MX_SDMMC1_SD_Init();
  MX_FATFS_Init();
  MX_USART6_UART_Init();
  MX_LWIP_Init();
  /* USER CODE BEGIN 2 */
  /* SDMMC saati: 48 MHz / (ClockDiv + 2). CubeMX'teki 0 (24 MHz) polling modunda
   * FIFO tasmasina/eksik veriye yol acabiliyor; 1 Hz kayit icin 12 MHz fazlasiyla
   * yeterli. HAL_SD_Init f_mount sirasinda cagrildigi icin burada ayarlamak yeterli. */
  hsd1.Init.ClockDiv = 2U;
  Debug_UART_Init();          /* printf -> USART1 (ST-LINK sanal COM) */
  Door_Init();                /* D7 kapak anahtari: giris + pull-up  */
  Peltier_Init(&huart6);      /* USART6 -> Peltier surucu karti       */

  if (BSP_LCD_Init() != LCD_OK)
  {
    Error_Handler();
  }
  BSP_LCD_LayerDefaultInit(0, LCD_FB_START_ADDRESS);
  BSP_LCD_SelectLayer(0);

  BSP_LCD_SetBackColor(LCD_COLOR_BLACK);
  BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
  BSP_LCD_Clear(LCD_COLOR_BLACK);

  BSP_LCD_SetFont(&Font24);
  BSP_LCD_DisplayStringAt(0, 20, (uint8_t *)"Akilli Iklim Sistemi", CENTER_MODE);
  BSP_LCD_SetFont(&Font16);
  BSP_LCD_DrawRect(20, 60, 440, 150);
  BSP_LCD_DisplayStringAt(20, 80, (uint8_t *)"RTC DS3231", LEFT_MODE);

  DS3231_TimeDate rtc = {0};
  uint8_t rtc_ok = 0;

  if (DS3231_Init(&hi2c1))
  {
    BSP_LCD_DisplayStringAt(20, 110, (uint8_t *)"RTC Basarili", LEFT_MODE);
  }
  else
  {
    BSP_LCD_DisplayStringAt(20, 110, (uint8_t *)"RTC Bulunamadi", LEFT_MODE);
  }

  BSP_PB_Init(BUTTON_WAKEUP, BUTTON_MODE_GPIO);
  DHT22_Init(&hdht22, DHT22_AIR_GPIO_Port, DHT22_AIR_Pin);
  Ds18B20_Init(&hds_heatsink, DS18B20_HEATSINK_GPIO_Port, DS18B20_HEATSINK_Pin, "Sogutucu");
  Ds18B20_Init(&hds_wall,     DS18B20_WALL_GPIO_Port,     DS18B20_WALL_Pin,     "Duvar");
  Ds18B20_Init(&hds_ambient,  DS18B20_AMBIENT_GPIO_Port,  DS18B20_AMBIENT_Pin,  "Dis ortam");
  LogCtrl_Init(&hi2c1);       /* 24C32: kayit durumu; aciksa kayda devam   */
  SdLog_Init();               /* SD kart TXT kaydi                         */
  rtc_ok = DS3231_GetTimeDate(&hi2c1, &rtc);
  Exp_Init(&hi2c1, &rtc, rtc_ok);   /* deney: calisan test varsa kaldigi yerden */
  Aio_Init();                       /* Adafruit IO MQTT istemcisi (netcfg.h)    */
  Screen_Init(&hi2c1);        /* dokunmatik panel + ekran durum makinesi   */
  printf("\r\n%s\r\n", SDLOG_COLUMNS);
  Draw_StartupScreen();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint8_t button_down = 0;
  uint8_t long_press_done = 0;
  uint32_t press_started_at = 0;
  uint32_t last_print_tick = 0;
  uint32_t last_sample_tick = HAL_GetTick();
  uint8_t  sample_last_sec  = 0xFFU;      /* son orneklenen RTC saniyesi */

  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* ---------------- Ethernet / LwIP (bloklamaz) ----------------
     * Gelen paketleri isler, LwIP zamanlayicilarini (DHCP, ARP, TCP) calistirir,
     * 100 ms'de bir kablo durumunu kontrol eder. Her dongude cagrilmali. */
    MX_LWIP_Process();

    /* ---------------- Haberlesme gorevleri (bloklamaz) ---------------- */
    Peltier_Task();             /* komut tekrari, onay takibi            */
    Debug_Task();               /* konsol: *<deger># , *S#               */

    /* ---------------- Saat oku ---------------- */
    rtc_ok = DS3231_GetTimeDate(&hi2c1, &rtc);

    /* ---------------- Kapak anahtari: acilis/kapanis olay olarak kaydedilir ---------------- */
    if (Door_Task())
    {
      if (rtc_ok) Exp_MarkEvent(door_open ? EXP_EVT_DOOR : EXP_EVT_DOOR_CLOSE, &rtc);
      printf("KAPAK %s\r\n", door_open ? "ACILDI" : "KAPANDI");
      Screen_Wake();
      if (Screen_Current() == SCREEN_MAIN) UI_DrawDoor(door_open);
    }

    /* ---------------- Dokunmatik + ekran durum makinesi (bloklamaz) ---------------- */
    Screen_Task(&rtc, rtc_ok);

    /* ---------------- SD kart: tak/cikar, mount, dosya ac/kapat ---------------- */
    SdLog_Task(LogCtrl_IsActive(), &rtc, rtc_ok);

    /* Ana ekran parcalari yalnizca ana ekrandayken cizilir */
    if (Screen_Current() == SCREEN_MAIN)
    {
      UI_UpdatePeltier(0);      /* komut/baglanti degistiyse paneli ciz  */
      if (!rtc_ok || rtc.seconds != rtc_last_sec)
      {
        rtc_last_sec = rtc_ok ? rtc.seconds : 0xFF;
        UI_DrawClock(&rtc, rtc_ok);
      }
    }

    /* ---------------- Sensor gorevleri (bloklamaz) ----------------
     * Her cagrida yalnizca sirasi gelen kisa islem yapilir; DS18B20'nin
     * 750 ms donusum suresi ve DHT22'nin 2 s araligi beklenmez. */
    DHT22_Task(&hdht22);
    for (uint8_t k = 0; k < 3; k++)
    {
      Ds18B20_Task(ds_list[k]);
    }

    /* ---------------- 1 Hz ornekleme ----------------
     * RTC saniyesi degisince bir ornek alinir: her gercek saniyeye tam bir satir.
     * (P1 on testinde HAL_GetTick ile 1000 ms beklemek, dongu gecikmesi ve DHT22
     * okumasindaki kesme kapatma yuzunden 22 saatte 563 saniyeyi atlatti.)
     * RTC okunamazsa HAL_GetTick ile 1000 ms'ye geri donulur. */
    if (rtc_ok ? (rtc.seconds != sample_last_sec)
               : (HAL_GetTick() - last_sample_tick >= SAMPLE_PERIOD_MS))
    {
      char tAir[12], rhAir[12], tHs[12], tWall[12], tAmb[12];
      last_sample_tick = HAL_GetTick();
      sample_last_sec  = rtc_ok ? rtc.seconds : 0xFFU;

      /* Sensorlerin en son gecerli degerleri alinir (okuma Task'larda yapildi) */
      sample_count++;

      /* ---------------- Deney: komutu hesapla ve Peltier'e gonder ---------------- */
      {
        Exp_Inputs ein;
        ein.air_valid     = (hdht22.valid && hdht22.last_status == DHT22_OK) ? 1U : 0U;
        ein.air_x10       = hdht22.temperature_x10;
        ein.peltier_valid = (hds_heatsink.status == DS18B20_OK) ? 1U : 0U;   /* D3: Peltier yuzeyi */
        ein.peltier_x100  = hds_heatsink.temperature_x100;
        Exp_Task1Hz(&ein, &rtc, rtc_ok);
      }

      /* Ekran isigi kapaliysa deney durumu ya da guvenlik degisince ac */
      {
        static Exp_State  w_state  = EXP_IDLE;
        static Exp_Safety w_safety = EXP_SAFE_OK;
        const Exp_Status *es = Exp_GetStatus();
        if (es->state != w_state || es->safety != w_safety) Screen_Wake();
        w_state  = es->state;
        w_safety = es->safety;
      }

      Net_Task1Hz();            /* kablo / IP / sunucu durumu, degisince log */
      {
        Aio_Data ad;
        Aio_Fill(&ad);
        Aio_Task1Hz(&ad);       /* Adafruit IO: baglanti + 20 s'de bir gonderim */
      }

      if (Screen_Current() == SCREEN_MAIN)
      {
        UI_UpdateSensors();
        UI_UpdatePeltier(1);
        UI_UpdateSd();
        UI_UpdateExp(0);
        UI_UpdateNet(0);
      }

      /* Ornek satiri (son okuma hataliysa alan bos birakilir):
       * seri porta her zaman, SD karta kayit aciksa yazilir */
      tAir[0] = rhAir[0] = tHs[0] = tWall[0] = tAmb[0] = '\0';
      if (hdht22.valid && hdht22.last_status == DHT22_OK)
      {
        Fmt_Fixed(tAir,  sizeof(tAir),  hdht22.temperature_x10, 10);
        Fmt_Fixed(rhAir, sizeof(rhAir), (int32_t)hdht22.humidity_x10, 10);
      }
      if (hds_heatsink.status == DS18B20_OK) Fmt_Fixed(tHs,   sizeof(tHs),   hds_heatsink.temperature_x100, 100);
      if (hds_wall.status     == DS18B20_OK) Fmt_Fixed(tWall, sizeof(tWall), hds_wall.temperature_x100,     100);
      if (hds_ambient.status  == DS18B20_OK) Fmt_Fixed(tAmb,  sizeof(tAmb),  hds_ambient.temperature_x100,  100);
      {
        char line[160], sp[12], ureq[8];
        const Exp_Status *es = Exp_GetStatus();
        uint8_t exp_on = (es->state == EXP_RUNNING || es->state == EXP_PAUSED) ? 1U : 0U;
        sp[0] = '\0';
        ureq[0] = '\0';
        if (exp_on && es->closed && es->state == EXP_RUNNING) Fmt_Fixed(sp, sizeof(sp), es->sp_x10, 10);
        if (exp_on) snprintf(ureq, sizeof(ureq), "%d", (int)es->u_req);   /* deneyin istedigi komut */
        snprintf(line, sizeof(line), "%02u/%02u/20%02u,%02u:%02u:%02u,%lu,%s,%s,%s,%s,%s,%d,%u,%u,%u,%s,%u,%u,,%s,%u",
                 rtc.date, rtc.month, rtc.year, rtc.hours, rtc.minutes, rtc.seconds,
                 (unsigned long)(HAL_GetTick() / 1000U), tAir, rhAir, tHs, tWall, tAmb,
                 Peltier_GetCommand(), (unsigned)Peltier_IsLinkOk(),
                 exp_on ? (unsigned)es->proc : 0U, exp_on ? (unsigned)es->phase : 0U, sp,
                 (unsigned)Exp_TakeEvent(), exp_on ? (unsigned)es->safety : 0U, ureq, (unsigned)door_open);
        printf("%s\r\n", line);
        if (LogCtrl_IsActive())
        {
          SdLog_WriteLine(line, &rtc);
        }
      }
    }

    if (Button_IsPressed())
    {
      if (!button_down)
      {
        button_down = 1;
        press_started_at = HAL_GetTick();
        long_press_done = 0;
      }
      else if (!long_press_done && (HAL_GetTick() - press_started_at) >= 2000)
      {
        /* Yedek yol: user butonuna 2 s basinca dokunmatik saat ayar sayfasi acilir */
        Screen_Wake();
        if (Screen_Current() == SCREEN_MAIN)
        {
          Screen_OpenClockSetup(&rtc);
        }
        long_press_done = 1;
      }
    }
    else if (button_down)
    {
      button_down = 0;
      long_press_done = 0;
    }

    if (HAL_GetTick() - last_print_tick >= 1000)
    {
      last_print_tick = HAL_GetTick();
    }

    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    HAL_Delay(20);
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 432;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_7) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief DMA2D Initialization Function
  * @param None
  * @retval None
  */
static void MX_DMA2D_Init(void)
{

  /* USER CODE BEGIN DMA2D_Init 0 */

  /* USER CODE END DMA2D_Init 0 */

  /* USER CODE BEGIN DMA2D_Init 1 */

  /* USER CODE END DMA2D_Init 1 */
  hdma2d.Instance = DMA2D;
  hdma2d.Init.Mode = DMA2D_M2M;
  hdma2d.Init.ColorMode = DMA2D_OUTPUT_ARGB8888;
  hdma2d.Init.OutputOffset = 0;
  hdma2d.LayerCfg[1].InputOffset = 0;
  hdma2d.LayerCfg[1].InputColorMode = DMA2D_INPUT_ARGB8888;
  hdma2d.LayerCfg[1].AlphaMode = DMA2D_NO_MODIF_ALPHA;
  hdma2d.LayerCfg[1].InputAlpha = 0;
  if (HAL_DMA2D_Init(&hdma2d) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_DMA2D_ConfigLayer(&hdma2d, 1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DMA2D_Init 2 */

  /* USER CODE END DMA2D_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x20404768;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief LTDC Initialization Function
  * @param None
  * @retval None
  */
static void MX_LTDC_Init(void)
{

  /* USER CODE BEGIN LTDC_Init 0 */

  /* USER CODE END LTDC_Init 0 */

  LTDC_LayerCfgTypeDef pLayerCfg = {0};

  /* USER CODE BEGIN LTDC_Init 1 */

  /* USER CODE END LTDC_Init 1 */
  hltdc.Instance = LTDC;
  hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
  hltdc.Init.HorizontalSync = 40;
  hltdc.Init.VerticalSync = 9;
  hltdc.Init.AccumulatedHBP = 53;
  hltdc.Init.AccumulatedVBP = 11;
  hltdc.Init.AccumulatedActiveW = 533;
  hltdc.Init.AccumulatedActiveH = 283;
  hltdc.Init.TotalWidth = 565;
  hltdc.Init.TotalHeigh = 285;
  hltdc.Init.Backcolor.Blue = 0;
  hltdc.Init.Backcolor.Green = 0;
  hltdc.Init.Backcolor.Red = 0;
  if (HAL_LTDC_Init(&hltdc) != HAL_OK)
  {
    Error_Handler();
  }
  pLayerCfg.WindowX0 = 0;
  pLayerCfg.WindowX1 = 480;
  pLayerCfg.WindowY0 = 0;
  pLayerCfg.WindowY1 = 272;
  pLayerCfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
  pLayerCfg.Alpha = 255;
  pLayerCfg.Alpha0 = 0;
  pLayerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
  pLayerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
  pLayerCfg.FBStartAdress = 0xC0000000;
  pLayerCfg.ImageWidth = 480;
  pLayerCfg.ImageHeight = 272;
  pLayerCfg.Backcolor.Blue = 0;
  pLayerCfg.Backcolor.Green = 0;
  pLayerCfg.Backcolor.Red = 0;
  if (HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LTDC_Init 2 */

  /* USER CODE END LTDC_Init 2 */

}

/**
  * @brief SDMMC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SDMMC1_SD_Init(void)
{

  /* USER CODE BEGIN SDMMC1_Init 0 */

  /* USER CODE END SDMMC1_Init 0 */

  /* USER CODE BEGIN SDMMC1_Init 1 */

  /* USER CODE END SDMMC1_Init 1 */
  hsd1.Instance = SDMMC1;
  hsd1.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
  hsd1.Init.ClockBypass = SDMMC_CLOCK_BYPASS_DISABLE;
  hsd1.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
  hsd1.Init.BusWide = SDMMC_BUS_WIDE_1B;
  hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
  hsd1.Init.ClockDiv = 0;
  /* USER CODE BEGIN SDMMC1_Init 2 */

  /* USER CODE END SDMMC1_Init 2 */

}

/**
  * @brief USART6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART6_UART_Init(void)
{

  /* USER CODE BEGIN USART6_Init 0 */

  /* USER CODE END USART6_Init 0 */

  /* USER CODE BEGIN USART6_Init 1 */

  /* USER CODE END USART6_Init 1 */
  huart6.Instance = USART6;
  huart6.Init.BaudRate = 115200;
  huart6.Init.WordLength = UART_WORDLENGTH_8B;
  huart6.Init.StopBits = UART_STOPBITS_1;
  huart6.Init.Parity = UART_PARITY_NONE;
  huart6.Init.Mode = UART_MODE_TX_RX;
  huart6.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart6.Init.OverSampling = UART_OVERSAMPLING_16;
  huart6.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart6.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart6) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART6_Init 2 */

  /* USER CODE END USART6_Init 2 */

}

/* FMC initialization function */
static void MX_FMC_Init(void)
{

  /* USER CODE BEGIN FMC_Init 0 */

  /* USER CODE END FMC_Init 0 */

  FMC_SDRAM_TimingTypeDef SdramTiming = {0};

  /* USER CODE BEGIN FMC_Init 1 */

  /* USER CODE END FMC_Init 1 */

  /** Perform the SDRAM1 memory initialization sequence
  */
  hsdram1.Instance = FMC_SDRAM_DEVICE;
  /* hsdram1.Init */
  hsdram1.Init.SDBank = FMC_SDRAM_BANK1;
  hsdram1.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_8;
  hsdram1.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_12;
  hsdram1.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
  hsdram1.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
  hsdram1.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_3;
  hsdram1.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
  hsdram1.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;
  hsdram1.Init.ReadBurst = FMC_SDRAM_RBURST_DISABLE;
  hsdram1.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_0;
  /* SdramTiming */
  SdramTiming.LoadToActiveDelay = 2;
  SdramTiming.ExitSelfRefreshDelay = 7;
  SdramTiming.SelfRefreshTime = 4;
  SdramTiming.RowCycleDelay = 7;
  SdramTiming.WriteRecoveryTime = 3;
  SdramTiming.RPDelay = 2;
  SdramTiming.RCDDelay = 2;

  if (HAL_SDRAM_Init(&hsdram1, &SdramTiming) != HAL_OK)
  {
    Error_Handler( );
  }

  /* USER CODE BEGIN FMC_Init 2 */

  /* USER CODE END FMC_Init 2 */
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
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOJ_CLK_ENABLE();
  __HAL_RCC_GPIOI_CLK_ENABLE();
  __HAL_RCC_GPIOK_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(OTG_FS_PowerSwitchOn_GPIO_Port, OTG_FS_PowerSwitchOn_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOI, ARDUINO_D7_Pin|ARDUINO_D8_Pin|LED_Pin|LCD_DISP_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(DCMI_PWR_EN_GPIO_Port, DCMI_PWR_EN_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOG, ARDUINO_D4_Pin|ARDUINO_D2_Pin|EXT_RST_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : OTG_HS_OverCurrent_Pin */
  GPIO_InitStruct.Pin = OTG_HS_OverCurrent_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(OTG_HS_OverCurrent_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : QSPI_D2_Pin */
  GPIO_InitStruct.Pin = QSPI_D2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF9_QUADSPI;
  HAL_GPIO_Init(QSPI_D2_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : ULPI_D7_Pin ULPI_D6_Pin ULPI_D5_Pin ULPI_D3_Pin
                           ULPI_D2_Pin ULPI_D1_Pin ULPI_D4_Pin */
  GPIO_InitStruct.Pin = ULPI_D7_Pin|ULPI_D6_Pin|ULPI_D5_Pin|ULPI_D3_Pin
                          |ULPI_D2_Pin|ULPI_D1_Pin|ULPI_D4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF10_OTG_HS;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : ARDUINO_PWM_D3_Pin */
  GPIO_InitStruct.Pin = ARDUINO_PWM_D3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF2_TIM3;
  HAL_GPIO_Init(ARDUINO_PWM_D3_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : SPDIF_RX0_Pin */
  GPIO_InitStruct.Pin = SPDIF_RX0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF8_SPDIFRX;
  HAL_GPIO_Init(SPDIF_RX0_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : ARDUINO_PWM_D9_Pin */
  GPIO_InitStruct.Pin = ARDUINO_PWM_D9_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF1_TIM2;
  HAL_GPIO_Init(ARDUINO_PWM_D9_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : DCMI_D6_Pin DCMI_D7_Pin */
  GPIO_InitStruct.Pin = DCMI_D6_Pin|DCMI_D7_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF13_DCMI;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : VCP_RX_Pin */
  GPIO_InitStruct.Pin = VCP_RX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
  HAL_GPIO_Init(VCP_RX_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : QSPI_NCS_Pin */
  GPIO_InitStruct.Pin = QSPI_NCS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF10_QUADSPI;
  HAL_GPIO_Init(QSPI_NCS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : OTG_FS_VBUS_Pin */
  GPIO_InitStruct.Pin = OTG_FS_VBUS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(OTG_FS_VBUS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : Audio_INT_Pin */
  GPIO_InitStruct.Pin = Audio_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(Audio_INT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : OTG_FS_P_Pin OTG_FS_N_Pin OTG_FS_ID_Pin */
  GPIO_InitStruct.Pin = OTG_FS_P_Pin|OTG_FS_N_Pin|OTG_FS_ID_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF10_OTG_FS;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : SAI2_MCLKA_Pin SAI2_SCKA_Pin SAI2_FSA_Pin SAI2_SDA_Pin */
  GPIO_InitStruct.Pin = SAI2_MCLKA_Pin|SAI2_SCKA_Pin|SAI2_FSA_Pin|SAI2_SDA_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF10_SAI2;
  HAL_GPIO_Init(GPIOI, &GPIO_InitStruct);

  /*Configure GPIO pin : SAI2_SDB_Pin */
  GPIO_InitStruct.Pin = SAI2_SDB_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF10_SAI2;
  HAL_GPIO_Init(SAI2_SDB_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : OTG_FS_PowerSwitchOn_Pin */
  GPIO_InitStruct.Pin = OTG_FS_PowerSwitchOn_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(OTG_FS_PowerSwitchOn_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : DCMI_D5_Pin */
  GPIO_InitStruct.Pin = DCMI_D5_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF13_DCMI;
  HAL_GPIO_Init(DCMI_D5_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : ARDUINO_D7_Pin ARDUINO_D8_Pin LED_Pin LCD_DISP_Pin */
  GPIO_InitStruct.Pin = ARDUINO_D7_Pin|ARDUINO_D8_Pin|LED_Pin|LCD_DISP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOI, &GPIO_InitStruct);

  /*Configure GPIO pin : uSD_Detect_Pin */
  GPIO_InitStruct.Pin = uSD_Detect_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(uSD_Detect_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LCD_BL_CTRL_Pin */
  GPIO_InitStruct.Pin = LCD_BL_CTRL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LCD_BL_CTRL_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : DCMI_VSYNC_Pin */
  GPIO_InitStruct.Pin = DCMI_VSYNC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF13_DCMI;
  HAL_GPIO_Init(DCMI_VSYNC_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : OTG_FS_OverCurrent_Pin */
  GPIO_InitStruct.Pin = OTG_FS_OverCurrent_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(OTG_FS_OverCurrent_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : TP3_Pin NC2_Pin */
  GPIO_InitStruct.Pin = TP3_Pin|NC2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

  /*Configure GPIO pin : DCMI_PWR_EN_Pin */
  GPIO_InitStruct.Pin = DCMI_PWR_EN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(DCMI_PWR_EN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : DCMI_D4_Pin DCMI_D3_Pin DCMI_D0_Pin DCMI_D2_Pin
                           DCMI_D1_Pin */
  GPIO_InitStruct.Pin = DCMI_D4_Pin|DCMI_D3_Pin|DCMI_D0_Pin|DCMI_D2_Pin
                          |DCMI_D1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF13_DCMI;
  HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

  /*Configure GPIO pin : ARDUINO_PWM_CS_D5_Pin */
  GPIO_InitStruct.Pin = ARDUINO_PWM_CS_D5_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF2_TIM5;
  HAL_GPIO_Init(ARDUINO_PWM_CS_D5_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : VCP_TX_Pin */
  GPIO_InitStruct.Pin = VCP_TX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
  HAL_GPIO_Init(VCP_TX_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : ARDUINO_PWM_D10_Pin */
  GPIO_InitStruct.Pin = ARDUINO_PWM_D10_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF1_TIM1;
  HAL_GPIO_Init(ARDUINO_PWM_D10_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LCD_INT_Pin */
  GPIO_InitStruct.Pin = LCD_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(LCD_INT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : ULPI_NXT_Pin */
  GPIO_InitStruct.Pin = ULPI_NXT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF10_OTG_HS;
  HAL_GPIO_Init(ULPI_NXT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : ARDUINO_D4_Pin ARDUINO_D2_Pin EXT_RST_Pin */
  GPIO_InitStruct.Pin = ARDUINO_D4_Pin|ARDUINO_D2_Pin|EXT_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /*Configure GPIO pins : ARDUINO_A4_Pin ARDUINO_A5_Pin ARDUINO_A1_Pin ARDUINO_A2_Pin
                           ARDUINO_A3_Pin */
  GPIO_InitStruct.Pin = ARDUINO_A4_Pin|ARDUINO_A5_Pin|ARDUINO_A1_Pin|ARDUINO_A2_Pin
                          |ARDUINO_A3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

  /*Configure GPIO pins : ULPI_STP_Pin ULPI_DIR_Pin */
  GPIO_InitStruct.Pin = ULPI_STP_Pin|ULPI_DIR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF10_OTG_HS;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PB2 */
  GPIO_InitStruct.Pin = GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF9_QUADSPI;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : QSPI_D1_Pin QSPI_D3_Pin QSPI_D0_Pin */
  GPIO_InitStruct.Pin = QSPI_D1_Pin|QSPI_D3_Pin|QSPI_D0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF9_QUADSPI;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pin : RMII_RXER_Pin */
  GPIO_InitStruct.Pin = RMII_RXER_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(RMII_RXER_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : ARDUINO_A0_Pin */
  GPIO_InitStruct.Pin = ARDUINO_A0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(ARDUINO_A0_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : DCMI_HSYNC_Pin PA6 */
  GPIO_InitStruct.Pin = DCMI_HSYNC_Pin|GPIO_PIN_6;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF13_DCMI;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : LCD_SCL_Pin LCD_SDA_Pin */
  GPIO_InitStruct.Pin = LCD_SCL_Pin|LCD_SDA_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF4_I2C3;
  HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

  /*Configure GPIO pins : ULPI_CLK_Pin ULPI_D0_Pin */
  GPIO_InitStruct.Pin = ULPI_CLK_Pin|ULPI_D0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF10_OTG_HS;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : ARDUINO_PWM_D6_Pin */
  GPIO_InitStruct.Pin = ARDUINO_PWM_D6_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF9_TIM12;
  HAL_GPIO_Init(ARDUINO_PWM_D6_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : ARDUINO_MISO_D12_Pin ARDUINO_MOSI_PWM_D11_Pin */
  GPIO_InitStruct.Pin = ARDUINO_MISO_D12_Pin|ARDUINO_MOSI_PWM_D11_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/* LwIP seri port (SLIP) baglantisi - projede kullanilmiyor.
 * CubeMX slipif.c'yi her zaman projeye ekler; bos fonksiyonlarini yalnizca
 * eski Keil derleyicisi (__CC_ARM) icin lwip.c'de uretir. ArmClang (V6) icin
 * baglayici hatasi (L6218E sio_open/sio_send/sio_tryread) olmamasi icin burada. */
sio_fd_t sio_open(u8_t devnum)                         { (void)devnum; return NULL; }
void     sio_send(u8_t c, sio_fd_t fd)                  { (void)c; (void)fd; }
u32_t    sio_read(sio_fd_t fd, u8_t *data, u32_t len)   { (void)fd; (void)data; (void)len; return 0; }
u32_t    sio_tryread(sio_fd_t fd, u8_t *data, u32_t len){ (void)fd; (void)data; (void)len; return 0; }

static uint8_t Button_IsPressed(void)
{
  return (BSP_PB_GetState(BUTTON_WAKEUP) == GPIO_PIN_SET);
}

static void Draw_StartupScreen(void)
{
  Exp_State es = Exp_GetStatus()->state;
  UI_SetRecording(LogCtrl_IsActive());
  UI_SetRecLocked((es == EXP_RUNNING || es == EXP_PAUSED) ? 1U : 0U);
  UI_SetButtonLabel(UI_BTN_EXP, "DENEY", (es == EXP_IDLE) ? "menu" : "izle", UI_STYLE_ACCENT);
  UI_DrawFrame();
  rtc_last_sec = 0xFF;        /* saat bir sonraki dongude yeniden cizilsin */
  UI_UpdateSensors();
  UI_UpdatePeltier(1);
  UI_UpdateSd();
  UI_UpdateExp(1);
  UI_UpdateNet(1);
  UI_DrawDoor(door_open);
}

/* Sabit noktali sayiyi metne cevirir (float printf gerektirmez).
 * scale = 10  -> 1 ondalik  (ornek 234  -> "23.4")
 * scale = 100 -> 2 ondalik  (ornek 2344 -> "23.44") */
static void Fmt_Fixed(char *buf, size_t n, int32_t v, int32_t scale)
{
  const char *sign = (v < 0) ? "-" : "";
  if (v < 0) v = -v;
  if (scale == 100)
  {
    snprintf(buf, n, "%s%ld.%02ld", sign, (long)(v / 100), (long)(v % 100));
  }
  else
  {
    snprintf(buf, n, "%s%ld.%ld", sign, (long)(v / 10), (long)(v % 10));
  }
}

static const char *Dht_Short(DHT22_Status st)
{
  switch (st)
  {
    case DHT22_OK:              return "OK ";
    case DHT22_ERR_NO_RESPONSE: return "YOK";
    case DHT22_ERR_TIMEOUT:     return "ZAM";
    case DHT22_ERR_CHECKSUM:    return "CRC";
    default:                    return "-- ";
  }
}

static const char *Ds_Short(DS18B20_StatusTypeDef st)
{
  switch (st)
  {
    case DS18B20_OK:            return "OK ";
    case DS18B20_ERR_NO_SENSOR: return "YOK";
    case DS18B20_ERR_CRC:       return "CRC";
    case DS18B20_ERR_POWERON:   return "85C";
    case DS18B20_ERROR:         return "HAT";
    default:                    return "-- ";
  }
}

/* DS18B20 kartinin durum seviyesi ve alt satiri */
static UI_Level Ds_Level(const DS18B20_HandleTypeDef *d, const char **sub, const char *ok_text)
{
  if (d->fault)
  {
    *sub = d->valid ? "ARIZA - eski deger" : "ARIZA - sensor yok";
    return UI_LEVEL_FAULT;
  }
  if (d->status == DS18B20_OK)
  {
    *sub = ok_text;
    return UI_LEVEL_OK;
  }
  if (!d->valid)
  {
    if (d->status == DS18B20_BUSY) { *sub = "olcum bekleniyor"; return UI_LEVEL_WARN; }
    *sub = "veri yok";
    return UI_LEVEL_FAULT;
  }
  *sub = "son gecerli deger";
  return UI_LEVEL_WARN;
}

static void UI_UpdateSensors(void)
{
  static const char *const ds_text[3] = { "ic sogutucu", "duvar yuzeyi", "dolap disi" };
  static const uint8_t     ds_card[3] = { UI_CARD_PELTIER, UI_CARD_WALL, UI_CARD_AMBIENT };
  char rh[24], st[8], h[12];
  UI_Level lv;
  const char *sub;

  /* DHT22: kabin havasi + nem */
  if (hdht22.fault)                                   lv = UI_LEVEL_FAULT;
  else if (hdht22.last_status == DHT22_OK ||
           hdht22.last_status == DHT22_ERR_TOO_SOON)  lv = UI_LEVEL_OK;
  else                                                lv = hdht22.valid ? UI_LEVEL_WARN : UI_LEVEL_FAULT;

  if (hdht22.valid)
  {
    Fmt_Fixed(h, sizeof(h), (int32_t)hdht22.humidity_x10, 10);
    snprintf(rh, sizeof(rh), "Nem %%%s", h);
  }
  else
  {
    snprintf(rh, sizeof(rh), "veri yok");
  }
  snprintf(st, sizeof(st), "%s", hdht22.fault ? "ARZ"
                                 : ((lv == UI_LEVEL_OK) ? "OK" : Dht_Short(hdht22.last_status)));
  UI_DrawSensor(UI_CARD_AIR, hdht22.valid, hdht22.temperature_x10, 1, st, lv, rh, 1);

  /* DS18B20'ler */
  for (uint8_t k = 0; k < 3U; k++)
  {
    DS18B20_HandleTypeDef *d = ds_list[k];
    lv = Ds_Level(d, &sub, ds_text[k]);
    snprintf(st, sizeof(st), "%s", d->fault ? "ARZ" : Ds_Short(d->status));
    UI_DrawSensor(ds_card[k], d->valid, d->temperature_x100, 2, st, lv, sub, 0);
  }
}

/* Peltier paneli: force=0 iken yalnizca komut veya baglanti degistiyse cizer */
static void UI_UpdatePeltier(uint8_t force)
{
  static int16_t           last_cmd  = 0x7FFF;
  static Peltier_LinkState last_link = (Peltier_LinkState)0xFF;
  const Peltier_Status    *ps = Peltier_GetStatus();

  if (force || ps->command != last_cmd || ps->link != last_link)
  {
    last_cmd  = ps->command;
    last_link = ps->link;
    UI_DrawPeltier(ps->command, Peltier_IsLinkOk(), Peltier_LinkShort(ps->link));
  }
}

/* SD paneli: kart, dosya, kayit sayisi, boyut, bos alan, hata */
static void UI_UpdateSd(void)
{
  UI_SdInfo sd;
  const SdLog_Info *li = SdLog_GetInfo();
  memset(&sd, 0, sizeof(sd));
  sd.present   = (BSP_PlatformIsDetected() == SD_PRESENT) ? 1U : 0U;
  sd.logging   = LogCtrl_IsActive();
  sd.file      = SdLog_FileName();
  sd.records   = li->records;
  sd.size_kb   = li->size_kb;
  sd.last_write= (li->last_write[0] != '-') ? li->last_write : NULL;
  sd.free_mb   = li->free_mb;
  sd.errors    = li->errors;
  sd.eeprom_ok = LogCtrl_EepromOk();
  sd.resumed   = LogCtrl_WasResumed();
  UI_DrawSd(&sd);
}

/* screen.c geri cagirmalari */
void App_RedrawMain(void)
{
  Draw_StartupScreen();
}

void App_RecordingChanged(void)
{
  Exp_State es = Exp_GetStatus()->state;
  UI_SetRecording(LogCtrl_IsActive());
  UI_SetRecLocked((es == EXP_RUNNING || es == EXP_PAUSED) ? 1U : 0U);
  if (Screen_Current() == SCREEN_MAIN)
  {
    UI_DrawButton(UI_BTN_REC, 0U);
    UI_UpdateSd();
  }
}

/* Adafruit IO'ya gidecek anlik degerler (gecersiz sensorler gonderilmez) */
static void Aio_Fill(Aio_Data *a)
{
  const Exp_Status *e = Exp_GetStatus();
  char k[20];

  memset(a, 0, sizeof(*a));
  a->air_ok   = (hdht22.valid && hdht22.last_status == DHT22_OK) ? 1U : 0U;
  a->air_x10  = hdht22.temperature_x10;
  a->rh_ok    = a->air_ok;
  a->rh_x10   = hdht22.humidity_x10;
  a->pel_ok   = (hds_heatsink.status == DS18B20_OK) ? 1U : 0U;
  a->pel_x100 = hds_heatsink.temperature_x100;
  a->wall_ok  = (hds_wall.status == DS18B20_OK) ? 1U : 0U;
  a->wall_x100 = hds_wall.temperature_x100;
  a->amb_ok   = (hds_ambient.status == DS18B20_OK) ? 1U : 0U;
  a->amb_x100 = hds_ambient.temperature_x100;
  a->u_cmd    = Peltier_GetCommand();
  a->safety   = (uint8_t)e->safety;

  /* SD kart: durum|dosya|kayit|hata|bos MB|dosya KB (web sayfasi ayristirir) */
  {
    static const char *const sdn[] = { "KART YOK", "HAZIR", "KAYITTA", "HATA", "SAAT YOK" };
    const SdLog_Info *s = SdLog_GetInfo();
    snprintf(a->sd, sizeof(a->sd), "%s|%s|%lu|%lu|%lu|%lu",
             ((unsigned)s->state < 5U) ? sdn[s->state] : "?", s->file,
             (unsigned long)s->records, (unsigned long)s->errors,
             (unsigned long)s->free_mb, (unsigned long)s->size_kb);
  }

  if (e->state == EXP_IDLE)
  {
    snprintf(a->status, sizeof(a->status), door_open ? "BOSTA | KAPAK ACIK" : "BOSTA");
    return;
  }
  a->sp_ok  = (e->closed && e->state != EXP_DONE) ? 1U : 0U;
  a->sp_x10 = e->sp_x10;

  Exp_FormatDuration(k, sizeof(k), (e->state == EXP_DONE) ? 0U : e->total_left_s);
  if (e->safety != EXP_SAFE_OK)
    snprintf(a->status, sizeof(a->status), "%s | GUVENLIK: %s", Exp_ProcName(e->proc), Exp_SafetyName(e->safety));
  else if (e->limit != 0U && e->state == EXP_RUNNING)
    snprintf(a->status, sizeof(a->status), "%s | %s SINIRLI | kalan %s", Exp_ProcName(e->proc),
             (e->limit == 1U) ? "ISITMA" : "SOGUTMA", k);
  else if (e->full)
    snprintf(a->status, sizeof(a->status), "%s [%u/%u] | %s | kalan %s", Exp_ProcName(e->proc),
             e->stage + 1U, e->stages, Exp_StateName(e->state), k);
  else
    snprintf(a->status, sizeof(a->status), "%s | %s | kalan %s", Exp_ProcName(e->proc), Exp_StateName(e->state), k);
  if (door_open)
  {
    size_t n = strlen(a->status);
    snprintf(a->status + n, sizeof(a->status) - n, " | KAPAK ACIK");
  }
}

/* Ana ekran basligi: ag ve sunucu durumu. Metin degismedikce yeniden cizilmez. */
static void UI_UpdateNet(uint8_t force)
{
  static char last1[40], last2[40];
  static UI_Tone lt1, lt2;
  const Net_Status *n = Net_GetStatus();
  char l1[40], l2[40];
  UI_Tone t1, t2;

  switch (n->link)
  {
    case NET_LINK_DOWN:
      snprintf(l1, sizeof(l1), "AG: ETHERNET KABLOSU YOK");
      t1 = UI_TONE_ERR;
      break;
    case NET_LINK_NOIP:
      snprintf(l1, sizeof(l1), "AG: IP BEKLENIYOR (DHCP)");
      t1 = UI_TONE_WARN;
      break;
    default:
      snprintf(l1, sizeof(l1), "IP: %u.%u.%u.%u", n->ip[0], n->ip[1], n->ip[2], n->ip[3]);
      t1 = UI_TONE_OK;
      break;
  }

  if (n->srv == NET_SRV_OFF)
  {
    snprintf(l2, sizeof(l2), "%s: KAPALI", NET_SERVER_NAME);
    t2 = UI_TONE_MUTED;
  }
  else if (n->link != NET_LINK_UP)
  {
    snprintf(l2, sizeof(l2), "%s: AG BEKLENIYOR", NET_SERVER_NAME);
    t2 = UI_TONE_MUTED;
  }
  else if (n->srv == NET_SRV_CONNECTING)
  {
    snprintf(l2, sizeof(l2), "%s: BAGLANIYOR", NET_SERVER_NAME);
    t2 = UI_TONE_WARN;
  }
  else if (n->srv == NET_SRV_CONNECTED)
  {
    if (n->sent_valid && n->sent_age_s < 3600U)
      snprintf(l2, sizeof(l2), "%s: BAGLI (%lu sn once)", NET_SERVER_NAME, (unsigned long)n->sent_age_s);
    else
      snprintf(l2, sizeof(l2), "%s: BAGLI", NET_SERVER_NAME);
    t2 = UI_TONE_OK;
  }
  else
  {
    snprintf(l2, sizeof(l2), "%s: BAGLANTI HATASI", NET_SERVER_NAME);
    t2 = UI_TONE_ERR;
  }

  if (force || t1 != lt1 || t2 != lt2 || strcmp(l1, last1) != 0 || strcmp(l2, last2) != 0)
  {
    UI_DrawNetStatus(l1, t1, l2, t2);
    strcpy(last1, l1); strcpy(last2, l2);
    lt1 = t1; lt2 = t2;
  }
}

/* Ana ekrandaki deney paneli + DENEY / KAYIT butonlarinin durumu */
static void UI_UpdateExp(uint8_t force)
{
  static uint8_t last_state = 0xFF;
  const Exp_Status *e = Exp_GetStatus();
  char l1[48], l2[64], a[20];
  const char *badge;
  UI_Tone tone;

  if (force || (uint8_t)e->state != last_state)
  {
    last_state = (uint8_t)e->state;
    UI_SetRecLocked((e->state == EXP_RUNNING || e->state == EXP_PAUSED) ? 1U : 0U);
    UI_SetRecording(LogCtrl_IsActive());
    UI_SetButtonLabel(UI_BTN_EXP, "DENEY", (e->state == EXP_IDLE) ? "menu" : "izle", UI_STYLE_ACCENT);
    UI_DrawButton(UI_BTN_EXP, 0U);
    UI_DrawButton(UI_BTN_REC, 0U);
  }

  if (e->state == EXP_IDLE)
  {
    UI_DrawExpPanel("DENEY: --", "BOSTA", UI_TONE_MUTED, "Test secmek icin DENEY butonuna dokunun", UI_TONE_MUTED);
    return;
  }

  if (e->full) snprintf(l1, sizeof(l1), "DENEY: %s [%u/%u]", Exp_ProcName(e->proc), e->stage + 1U, e->stages);
  else         snprintf(l1, sizeof(l1), "DENEY: %s", Exp_ProcName(e->proc));

  if (e->safety != EXP_SAFE_OK)       { badge = "GUVENLIK";     tone = UI_TONE_ERR;  }
  else if (e->state == EXP_RUNNING && e->limit) { badge = "SINIRLI"; tone = UI_TONE_WARN; }
  else if (e->state == EXP_RUNNING)   { badge = "CALISIYOR";    tone = UI_TONE_OK;   }
  else if (e->state == EXP_PAUSED)    { badge = "DURAKLATILDI"; tone = UI_TONE_WARN; }
  else                                { badge = "TAMAMLANDI";   tone = UI_TONE_INFO; }

  Exp_FormatDuration(a, sizeof(a), (e->state == EXP_DONE) ? 0U : e->total_left_s);
  if (e->state == EXP_DONE)
    snprintf(l2, sizeof(l2), "Tamamlandi - izle > TAMAM ile kapatin");
  else if (e->closed)
    snprintf(l2, sizeof(l2), "SP %d.%d  Adim %u  Kalan %s", e->sp_x10 / 10, e->sp_x10 % 10, e->phase, a);
  else
  {
    char g[20];
    Exp_FormatDuration(g, sizeof(g), e->elapsed_s);
    snprintf(l2, sizeof(l2), "Adim %u  Gecen %s  Kalan %s", e->phase, g, a);
  }
  UI_DrawExpPanel(l1, badge, tone, l2, (e->safety != EXP_SAFE_OK) ? UI_TONE_ERR : UI_TONE_TEXT);
}

/* HAL UART geri cagirmalari: ilgili module yonlendirilir */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  Peltier_UartRxCplt(huart);
  Debug_UartRxCplt(huart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  Peltier_UartError(huart);
  Debug_UartError(huart);
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
