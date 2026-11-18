/**
  ******************************************************************************
  * @file    sdlog.c
  * @brief   SD karta gunluk metin (TXT) log kaydi - FatFs
  *
  *  Dosya acma nedenleri ve dosyaya yazilan isaret satirlari:
  *   START   : KAYIT BASLAT                 -> "# KAYIT BASLADI"
  *   RESUME  : enerji kesintisi sonrasi      -> "# DEVAM: enerji kesintisi sonrasi"
  *   REINSERT: kart cikip takildi / hata     -> "# DEVAM: kart yeniden baglandi"
  *   NEWDAY  : gece yarisi gecisi            -> "# GUN DEVAMI: onceki dosya ..."
  *   OVERFLOW: dosya 86400 satira ulasti     -> "# DEVAM: onceki dosya sinira ulasti"
  *  Yeni olusturulan dosyaya once baslik ve sutun satiri yazilir.
  ******************************************************************************
  */
#include "sdlog.h"
#include "logctrl.h"
#include "fatfs.h"
#include "fatfs_platform.h"
#include <stdio.h>
#include <string.h>

#if !defined(_USE_LFN) || (_USE_LFN == 0)
#error "LOG_YYYYMMDD.txt adlari icin FatFs uzun dosya adi gerekli: CubeMX > FATFS > USE_LFN = Enabled (static buffer on BSS)"
#endif

extern SD_HandleTypeDef hsd1;
extern Disk_drvTypeDef  disk;      /* ff_gen_drv.c: surucu tablosu */

typedef enum { OPEN_START = 0, OPEN_RESUME, OPEN_REINSERT, OPEN_NEWDAY, OPEN_OVERFLOW } OpenReason;

static SdLog_Info info;
static FIL        logf;
static uint8_t    mounted;
static uint8_t    file_open;
static uint8_t    session_opened;   /* bu oturumda (bu acilista) dosya en az bir kez acildi */
static uint32_t   unsynced;
static uint32_t   since_free;
static uint32_t   retry_tick;
static uint8_t    have_retry;

static uint8_t    f_year, f_mon, f_day;   /* acik dosyanin tarihi (yy, mm, dd) */
static uint8_t    f_part;                  /* 1 = LOG_YYYYMMDD.txt, 2.. = _2, _3   */

static char      cmt_q[SDLOG_CMT_N][104];   /* aciklama satiri kuyrugu */
static uint8_t   cmt_head, cmt_cnt;

static DS3231_TimeDate last_rtc;     /* get_fattime icin */
static uint8_t         last_rtc_ok;

/* ------------------------------------------------------------------------- */
static void Stamp(char *buf, size_t n, const DS3231_TimeDate *r)
{
  snprintf(buf, n, "%02u/%02u/20%02u %02u:%02u:%02u",
           r->date, r->month, r->year, r->hours, r->minutes, r->seconds);
}

static void MakeName(char *buf, size_t n, uint8_t yy, uint8_t mm, uint8_t dd, uint8_t part)
{
  if (part <= 1U) snprintf(buf, n, "LOG_20%02u%02u%02u.txt", yy, mm, dd);
  else            snprintf(buf, n, "LOG_20%02u%02u%02u_%u.txt", yy, mm, dd, part);
}

static uint8_t SameDay(const DS3231_TimeDate *r)
{
  return (r->year == f_year && r->month == f_mon && r->date == f_day) ? 1U : 0U;
}

static FRESULT PutLine(const char *s)
{
  UINT bw;
  size_t len = strlen(s);
  FRESULT fr = f_write(&logf, s, (UINT)len, &bw);
  if (fr == FR_OK && bw != len) fr = FR_DISK_ERR;   /* disk dolu */
  if (fr == FR_OK) fr = f_write(&logf, "\r\n", 2, &bw);
  return fr;
}

/* Kuyruktaki aciklama satirlarini yazar */
static FRESULT FlushComments(void)
{
  FRESULT fr = FR_OK;
  while (cmt_cnt > 0U && file_open && fr == FR_OK)
  {
    fr = PutLine(cmt_q[cmt_head]);
    cmt_head = (uint8_t)((cmt_head + 1U) % SDLOG_CMT_N);
    cmt_cnt--;
  }
  return fr;
}

void SdLog_Comment(const char *line)
{
  uint8_t slot;
  if (cmt_cnt >= SDLOG_CMT_N)                 /* dolu: en eskisini at */
  {
    cmt_head = (uint8_t)((cmt_head + 1U) % SDLOG_CMT_N);
    cmt_cnt--;
  }
  slot = (uint8_t)((cmt_head + cmt_cnt) % SDLOG_CMT_N);
  snprintf(cmt_q[slot], sizeof(cmt_q[slot]), "%s", line);
  cmt_cnt++;
}

static void UpdateFree(void)
{
  FATFS *fs;
  DWORD fre;
  if (f_getfree(SDPath, &fre, &fs) == FR_OK)
  {
    info.free_mb = (uint32_t)(((uint64_t)fre * fs->csize * 512U) / (1024U * 1024U));
  }
  since_free = 0;
}

/* Acik dosyadaki veri satirlarini sayar (veri satiri rakamla baslar).
 * Tam gunluk dosya ~5 MB; okuma acilista bir kez yapilir (birkac saniye). */
static uint32_t CountRecords(void)
{
  static uint8_t buf[4096];
  UINT     br;
  uint32_t n = 0;
  uint8_t  at_line_start = 1U;

  if (f_lseek(&logf, 0) != FR_OK) return 0;
  do
  {
    if (f_read(&logf, buf, sizeof(buf), &br) != FR_OK) break;
    for (UINT i = 0; i < br; i++)
    {
      if (at_line_start && buf[i] >= '0' && buf[i] <= '9') n++;
      at_line_start = (buf[i] == '\n') ? 1U : 0U;
    }
  } while (br == sizeof(buf));
  return n;
}

static void Fail(const char *what, FRESULT fr)
{
  printf("SD: %s hatasi (FRESULT %d)\r\n", what, (int)fr);
  info.errors++;
  if (file_open) { (void)f_close(&logf); file_open = 0; }
  info.state = SDLOG_ERROR;
  retry_tick = HAL_GetTick();
  have_retry = 1U;
}

static void Unmount(void)
{
  if (file_open) { (void)f_close(&logf); file_open = 0; }
  if (mounted)
  {
    (void)f_mount(NULL, SDPath, 0);
    mounted = 0;
  }
}

static uint8_t Mount(void)
{
  FRESULT fr;
  /* Kart degismis olabilir: SDMMC'yi temiz baslat. diskio.c disk_initialize()'i
   * yalnizca bir kez cagirir; bayrak sifirlanmazsa yeniden takilan kart
   * baslatilmaz. */
  (void)HAL_SD_DeInit(&hsd1);
  disk.is_initialized[(uint8_t)(SDPath[0] - '0')] = 0U;
  fr = f_mount(&SDFatFS, SDPath, 1);
  if (fr != FR_OK)
  {
    Fail("mount", fr);
    return 0;
  }
  mounted = 1U;
  UpdateFree();
  printf("SD: kart baglandi, bos alan %lu MB\r\n", (unsigned long)info.free_mb);
  return 1U;
}

/* rtc tarihine ait dosyayi acar (yoksa olusturur), isaret satirini yazar.
 * prev: NEWDAY/OVERFLOW icin onceki dosyanin adi (isaret satirinda). */
static uint8_t OpenLog(const DS3231_TimeDate *rtc, OpenReason why, const char *prev)
{
  char line[112], ts[24], name[24];
  FILINFO fno;
  FRESULT fr;
  uint8_t is_new;
  uint8_t part = (why == OPEN_OVERFLOW) ? (uint8_t)(f_part + 1U) : 1U;

  Stamp(ts, sizeof(ts), rtc);

  /* Uygun dosyayi bul: var ve dolu ise sonraki parcaya gec */
  for (;;)
  {
    MakeName(name, sizeof(name), rtc->year, rtc->month, rtc->date, part);
    is_new = (f_stat(name, &fno) != FR_OK) ? 1U : 0U;

    fr = f_open(&logf, name, FA_OPEN_ALWAYS | FA_READ | FA_WRITE);
    if (fr != FR_OK) { Fail("dosya acma", fr); return 0; }
    file_open = 1U;

    info.records = is_new ? 0U : CountRecords();
    if (info.records < SDLOG_MAX_RECORDS || part >= 9U) break;

    (void)f_close(&logf);                    /* dolu: LOG_..._2.txt, _3 ... */
    file_open = 0;
    part++;
  }

  fr = f_lseek(&logf, f_size(&logf));        /* sona git (ekleme) */
  if (fr != FR_OK) { Fail("lseek", fr); return 0; }

  if (is_new)
  {
    snprintf(line, sizeof(line), "# Edge AI test duzenegi - %s (gunluk kayit, ornekleme 1 s, en fazla %lu satir)",
             name, (unsigned long)SDLOG_MAX_RECORDS);
    fr = PutLine(line);
    if (fr == FR_OK) fr = PutLine(SDLOG_COLUMNS);
    if (fr != FR_OK) { Fail("baslik yazma", fr); return 0; }
  }

  switch (why)
  {
    case OPEN_START:    snprintf(line, sizeof(line), "# KAYIT BASLADI: %s", ts); break;
    case OPEN_RESUME:   snprintf(line, sizeof(line), "# DEVAM: enerji kesintisi sonrasi, %s", ts); break;
    case OPEN_REINSERT: snprintf(line, sizeof(line), "# DEVAM: kart yeniden baglandi, %s", ts); break;
    case OPEN_NEWDAY:   snprintf(line, sizeof(line), "# GUN DEVAMI: onceki dosya %s, %s", prev ? prev : "--", ts); break;
    default:            snprintf(line, sizeof(line), "# DEVAM: %s %lu satir sinirina ulasti, %s",
                                 prev ? prev : "--", (unsigned long)SDLOG_MAX_RECORDS, ts); break;
  }
  fr = PutLine(line);
  if (fr == FR_OK && !is_new) fr = PutLine("# SUTUNLAR: " SDLOG_COLUMNS);   /* ayni gun eski bicimli dosya olabilir */
  if (fr == FR_OK) { file_open = 1U; fr = FlushComments(); }
  if (fr == FR_OK) fr = f_sync(&logf);
  if (fr != FR_OK) { Fail("isaret yazma", fr); return 0; }

  f_year = rtc->year; f_mon = rtc->month; f_day = rtc->date; f_part = part;
  snprintf(info.file, sizeof(info.file), "%s", name);
  session_opened = 1U;
  unsynced       = 0;
  info.size_kb   = (uint32_t)(f_size(&logf) / 1024U);
  info.state     = SDLOG_LOGGING;
  have_retry     = 0U;
  printf("SD: %s %s (%lu kayit)\r\n", name, is_new ? "olusturuldu" : "acildi, sonuna ekleniyor",
         (unsigned long)info.records);
  return 1U;
}

/* Acik dosyayi bir aciklama satiriyla kapatir */
static void CloseLog(const char *why, const DS3231_TimeDate *rtc, uint8_t rtc_ok)
{
  char line[80], ts[24];
  if (!file_open) return;
  if (rtc_ok) Stamp(ts, sizeof(ts), rtc); else snprintf(ts, sizeof(ts), "saat yok");
  snprintf(line, sizeof(line), "# %s: %s, dosyada %lu kayit", why, ts, (unsigned long)info.records);
  (void)PutLine(line);
  (void)f_close(&logf);
  file_open = 0;
  printf("SD: %s kapatildi (%s)\r\n", info.file, why);
}

/* Gun degistiyse eski dosyayi kapatip yeni gunun dosyasini acar */
static void CheckNewDay(const DS3231_TimeDate *rtc)
{
  char prev[24];
  if (!file_open || SameDay(rtc)) return;
  snprintf(prev, sizeof(prev), "%s", info.file);
  CloseLog("GUN SONU", rtc, 1U);
  if (!OpenLog(rtc, OPEN_NEWDAY, prev)) Unmount();
}

/* ------------------------------------------------------------------------- */
void SdLog_Init(void)
{
  memset(&info, 0, sizeof(info));
  snprintf(info.last_write, sizeof(info.last_write), "--");
  snprintf(info.file, sizeof(info.file), "--");
  mounted = file_open = session_opened = 0;
  have_retry = 0;
  cmt_head = cmt_cnt = 0;
  f_part = 1U;
  info.state = SDLOG_NO_CARD;
}

void SdLog_Task(uint8_t active, const DS3231_TimeDate *rtc, uint8_t rtc_ok)
{
  last_rtc_ok = rtc_ok;
  if (rtc_ok) last_rtc = *rtc;

  info.present = (BSP_PlatformIsDetected() == SD_PRESENT) ? 1U : 0U;

  /* Kart cikarildi */
  if (!info.present)
  {
    if (mounted || file_open)
    {
      printf("SD: kart cikarildi\r\n");
      file_open = 0;                         /* FIL artik gecersiz; kapatmaya calisma */
      mounted   = 0;
      (void)f_mount(NULL, SDPath, 0);
    }
    info.state = SDLOG_NO_CARD;
    have_retry = 0U;
    return;
  }

  /* Kayit kapandi */
  if (!active)
  {
    if (file_open) CloseLog("DURDU", rtc, rtc_ok);
    session_opened = 0;
    have_retry = 0U;
    info.state = SDLOG_IDLE;
    return;
  }

  /* Kayit acik, dosya acik: gun degisimi kontrolu + bekleyen aciklamalar */
  if (file_open)
  {
    if (rtc_ok) CheckNewDay(rtc);
    if (file_open && cmt_cnt > 0U && FlushComments() != FR_OK)
    {
      Fail("aciklama yazma", FR_DISK_ERR);
      Unmount();
    }
    return;
  }

  /* Dosya adi tarihten geldigi icin saat gerekli */
  if (!rtc_ok)
  {
    info.state = SDLOG_NO_CLOCK;
    return;
  }
  MakeName(info.file, sizeof(info.file), rtc->year, rtc->month, rtc->date, 1U);

  /* Hata sonrasi bekleme */
  if (have_retry && (HAL_GetTick() - retry_tick) < SDLOG_RETRY_MS) return;

  if (!mounted && !Mount()) return;

  {
    OpenReason why = !session_opened ? (LogCtrl_WasResumed() ? OPEN_RESUME : OPEN_START)
                                     : OPEN_REINSERT;
    if (!OpenLog(rtc, why, NULL))
    {
      Unmount();                             /* bir sonraki denemede bastan baglan */
    }
  }
}

void SdLog_WriteLine(const char *line, const DS3231_TimeDate *rtc)
{
  FRESULT fr;
  if (!file_open) return;

  /* Satirin tarihi acik dosyadan farkliysa once gun gecisi (00:00:00 ornegi yeni dosyaya).
   * RTC bu turda okunamadiysa tarih guvenilmez: gecis yapma, acik dosyaya yaz. */
  if (last_rtc_ok)
  {
    CheckNewDay(rtc);
    if (!file_open) return;
  }

  /* Dosya siniri: LOG_..._2.txt ile devam */
  if (info.records >= SDLOG_MAX_RECORDS)
  {
    char prev[24];
    snprintf(prev, sizeof(prev), "%s", info.file);
    CloseLog("DOSYA DOLU", rtc, 1U);
    if (!OpenLog(rtc, OPEN_OVERFLOW, prev)) { Unmount(); return; }
  }

  fr = PutLine(line);
  if (fr == FR_OK && ++unsynced >= SDLOG_SYNC_EVERY)
  {
    fr = f_sync(&logf);
    unsynced = 0;
  }
  if (fr != FR_OK)
  {
    Fail("yazma", fr);
    Unmount();
    return;
  }

  info.records++;
  info.size_kb = (uint32_t)(f_size(&logf) / 1024U);
  snprintf(info.last_write, sizeof(info.last_write), "%02u:%02u:%02u", rtc->hours, rtc->minutes, rtc->seconds);
  if (++since_free >= SDLOG_FREE_EVERY) UpdateFree();
}

const SdLog_Info *SdLog_GetInfo(void)
{
  return &info;
}

const char *SdLog_FileName(void)
{
  return info.file;
}

/* FatFs zaman damgasi: bit31:25 yil-1980, 24:21 ay, 20:16 gun, 15:11 saat, 10:5 dk, 4:0 sn/2 */
uint32_t SdLog_FatTime(void)
{
  if (!last_rtc_ok)
  {
    return ((uint32_t)(2026U - 1980U) << 25) | (1UL << 21) | (1UL << 16);
  }
  return ((uint32_t)(last_rtc.year + 20U) << 25) |     /* 2000+yy-1980 */
         ((uint32_t)last_rtc.month   << 21) |
         ((uint32_t)last_rtc.date    << 16) |
         ((uint32_t)last_rtc.hours   << 11) |
         ((uint32_t)last_rtc.minutes << 5)  |
         ((uint32_t)last_rtc.seconds >> 1);
}
