/**
  ******************************************************************************
  * @file    logctrl.c
  * @brief   Kayit durumu kontrolu (24C32 EEPROM'da kalici)
  ******************************************************************************
  */
#include "logctrl.h"
#include "at24c32.h"
#include <stdio.h>
#include <string.h>

#define LOGCTRL_MAGIC       0x31494145UL      /* "EAI1" (Edge AI) */
#define LOGCTRL_VERSION     1U
#define LOGCTRL_ADDR_A      0x0000U           /* sayfa basi */
#define LOGCTRL_ADDR_B      0x0020U           /* sonraki sayfa */
#define LOGCTRL_REC_SIZE    20U

static AT24C32_HandleTypeDef hee;
static LogCtrl_State         st;
static uint8_t               ee_ok;           /* EEPROM bulundu            */
static uint8_t               ee_fail;         /* son yazma basarisiz oldu  */
static uint8_t               resumed;
static char                  fname[16] = "--";

/* ------------------------------------------------------------------------- */
static uint16_t Crc16(const uint8_t *d, uint16_t n)   /* CRC-16/CCITT-FALSE */
{
  uint16_t crc = 0xFFFFU;
  while (n--)
  {
    crc ^= (uint16_t)(*d++) << 8;
    for (uint8_t i = 0; i < 8U; i++)
    {
      crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

/* Kayit bicimi (20 bayt, little-endian):
 *  0..3 magic | 4 version | 5 active | 6..7 file_index | 8..9 seq |
 *  10..15 start[6] | 16..17 resume_count | 18..19 crc16(0..17)      */
static void Pack(const LogCtrl_State *s, uint8_t *b)
{
  b[0] = (uint8_t)(LOGCTRL_MAGIC);       b[1] = (uint8_t)(LOGCTRL_MAGIC >> 8);
  b[2] = (uint8_t)(LOGCTRL_MAGIC >> 16); b[3] = (uint8_t)(LOGCTRL_MAGIC >> 24);
  b[4] = LOGCTRL_VERSION;
  b[5] = s->active;
  b[6] = (uint8_t)s->file_index;  b[7] = (uint8_t)(s->file_index >> 8);
  b[8] = (uint8_t)s->seq;         b[9] = (uint8_t)(s->seq >> 8);
  memcpy(&b[10], s->start, 6);
  b[16] = (uint8_t)s->resume_count; b[17] = (uint8_t)(s->resume_count >> 8);
  uint16_t c = Crc16(b, 18);
  b[18] = (uint8_t)c; b[19] = (uint8_t)(c >> 8);
}

static uint8_t Unpack(const uint8_t *b, LogCtrl_State *s)
{
  uint32_t magic = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
  uint16_t crc   = (uint16_t)(b[18] | (b[19] << 8));
  if (magic != LOGCTRL_MAGIC || b[4] != LOGCTRL_VERSION || Crc16(b, 18) != crc)
  {
    return 0U;
  }
  s->active       = (b[5] != 0U) ? 1U : 0U;
  s->file_index   = (uint16_t)(b[6] | (b[7] << 8));
  s->seq          = (uint16_t)(b[8] | (b[9] << 8));
  memcpy(s->start, &b[10], 6);
  s->resume_count = (uint16_t)(b[16] | (b[17] << 8));
  return 1U;
}

static void UpdateName(void)
{
  if (st.file_index == 0U) snprintf(fname, sizeof(fname), "--");
  else                     snprintf(fname, sizeof(fname), "LOG_%04u." LOGCTRL_FILE_EXT, (unsigned)st.file_index);
}

/* Durumu siradaki kopyaya yazar ve geri okuyarak dogrular */
static uint8_t Save(void)
{
  uint8_t b[LOGCTRL_REC_SIZE], r[LOGCTRL_REC_SIZE];
  uint16_t addr;

  if (!ee_ok) return 0U;
  st.seq++;
  addr = (st.seq & 1U) ? LOGCTRL_ADDR_B : LOGCTRL_ADDR_A;
  Pack(&st, b);
  ee_fail = 1U;
  if (AT24C32_Write(&hee, addr, b, LOGCTRL_REC_SIZE) != HAL_OK) return 0U;
  if (AT24C32_Read(&hee, addr, r, LOGCTRL_REC_SIZE) != HAL_OK)  return 0U;
  if (memcmp(b, r, LOGCTRL_REC_SIZE) != 0)                      return 0U;
  ee_fail = 0U;
  return 1U;
}

/* ------------------------------------------------------------------------- */
void LogCtrl_Init(I2C_HandleTypeDef *hi2c)
{
  uint8_t ba[LOGCTRL_REC_SIZE], bb[LOGCTRL_REC_SIZE];
  LogCtrl_State sa, sb;
  uint8_t va = 0U, vb = 0U;

  memset(&st, 0, sizeof(st));
  resumed = 0U;
  ee_fail = 0U;
  ee_ok   = AT24C32_Init(&hee, hi2c);

  if (ee_ok)
  {
    if (AT24C32_Read(&hee, LOGCTRL_ADDR_A, ba, LOGCTRL_REC_SIZE) == HAL_OK) va = Unpack(ba, &sa);
    if (AT24C32_Read(&hee, LOGCTRL_ADDR_B, bb, LOGCTRL_REC_SIZE) == HAL_OK) vb = Unpack(bb, &sb);

    if (va && vb)      st = ((int16_t)(sb.seq - sa.seq) > 0) ? sb : sa;   /* yeni olan */
    else if (va)       st = sa;
    else if (vb)       st = sb;
    /* ikisi de gecersizse: ilk kullanim, st sifir (kayit kapali) */

    printf("EEPROM: 24C32 @0x%02X, kayit %s, seq %u\r\n",
           hee.addr7, st.active ? "ACIK" : "KAPALI", (unsigned)st.seq);

    if (st.active)
    {
      /* Enerji kesintisi oncesi kayit acikti: ayni dosyaya devam */
      resumed = 1U;
      st.resume_count++;
      (void)Save();
      printf("Kayda devam ediliyor (enerji kesintisi sonrasi, devam sayisi %u)\r\n",
             (unsigned)st.resume_count);
    }
  }
  else
  {
    printf("EEPROM: BULUNAMADI - kayit durumu kalici olmayacak\r\n");
  }
  UpdateName();
}

uint8_t LogCtrl_Start(const DS3231_TimeDate *now)
{
  st.active       = 1U;
  st.file_index   = (uint16_t)((st.file_index >= 9999U) ? 1U : (st.file_index + 1U));
  st.resume_count = 0U;
  if (now != NULL)
  {
    st.start[0] = now->year;  st.start[1] = now->month;   st.start[2] = now->date;
    st.start[3] = now->hours; st.start[4] = now->minutes; st.start[5] = now->seconds;
  }
  resumed = 0U;
  UpdateName();
  uint8_t ok = Save();
  printf("Kayit BASLADI (EEPROM %s)\r\n", ok ? "OK" : "HATA");
  return ok;
}

uint8_t LogCtrl_NextFile(void)
{
  st.file_index = (uint16_t)((st.file_index >= 9999U) ? 1U : (st.file_index + 1U));
  UpdateName();
  printf("Dosya adi kullanimda, yeni ad: %s\r\n", fname);
  return Save();
}

uint8_t LogCtrl_Stop(void)
{
  st.active = 0U;
  resumed   = 0U;
  uint8_t ok = Save();
  printf("Kayit DURDU (EEPROM %s)\r\n", ok ? "OK" : "HATA");
  return ok;
}

uint8_t              LogCtrl_IsActive(void)   { return st.active; }
uint8_t              LogCtrl_EepromOk(void)   { return (ee_ok && !ee_fail) ? 1U : 0U; }
uint8_t              LogCtrl_WasResumed(void) { return resumed; }
const char          *LogCtrl_FileName(void)   { return fname; }
const LogCtrl_State *LogCtrl_Get(void)        { return &st; }
