/**
  ******************************************************************************
  * @file    ui.c
  * @brief   480x272 LCD arayuzu (BSP LCD, ARGB8888)
  ******************************************************************************
  * Font olculeri: Font24 17x24, Font16 11x16, Font12 7x12, Font8 5x8.
  * BSP fontlarinda Turkce karakter ve derece isareti yoktur; derece kucuk bir
  * daire olarak cizilir.
  ******************************************************************************
  */
#include "ui.h"
#include "stm32746g_discovery_lcd.h"
#include <stdio.h>
#include <string.h>

/* ---------------- Renkler (ARGB8888) ---------------- */
#define C_BG        0xFF0B1220u
#define C_HEADER    0xFF16213Au
#define C_PANEL     0xFF141D30u
#define C_TEXT      0xFFE6ECF5u
#define C_VALUE     0xFFFFFFFFu
#define C_MUTED     0xFF8C9BB5u
#define C_UNIT      0xFFC9D4E5u
#define C_CLOCK     0xFF9FB3D1u
#define C_OK        0xFF2ECC71u
#define C_WARN      0xFFE67E22u
#define C_ERR       0xFFE74C3Cu
#define C_COOL      0xFF3FA9F5u
#define C_HEAT      0xFFF5A35Cu
#define C_HUM       0xFF5DADE2u
#define C_BAR_BG    0xFF0B1220u
#define C_BAR_EDGE  0xFF2A3A57u
#define C_BADGE_OK  0xFF1E5631u
#define C_BADGE_OKT 0xFF7DFFB0u
#define C_BADGE_ERR 0xFF5A1E1Eu
#define C_BADGE_ERT 0xFFFFB0B0u
#define C_BTN       0xFF1F2B44u
#define C_BTN_P     0xFF2C3D5Eu   /* basili buton */
#define C_SEL       0xFF23324Fu   /* secili alan  */
#define C_REC_BG    0xFF3A1A1Au

/* ---------------- Yerlesim ---------------- */
#define CARD_W      153U
#define CARD_H      69U
static const uint16_t card_x[4] = { 6U, 165U, 6U, 165U };
static const uint16_t card_y[4] = { 32U, 32U, 107U, 107U };
static const char *const card_title[4] =
{
  "KABIN HAVA  D5", "PELTIER YUZ. D3", "KARSI DUVAR  D4", "DIS ORTAM  D2"
};

#define PEL_X       324U
#define PEL_Y       32U
#define PEL_W       150U
#define PEL_H       144U
#define BAR_X       332U
#define BAR_Y       108U
#define BAR_W       134U
#define BAR_H       12U
#define BAR_MID     (BAR_X + BAR_W / 2U)        /* 399 */

/* Alt bolum: solda deney paneli (ust) + SD paneli (alt), sagda butonlar */
#define EXPP_X      6U
#define EXPP_Y      182U
#define EXPP_W      324U
#define EXPP_H      40U
#define SD_X        6U
#define SD_Y        226U
#define SD_W        324U
#define SD_H        40U
#define LINE_N      45U                         /* panel satiri: 45 karakter (Font12) */

/* ---------------- Dokunmatik buton alanlari ---------------- */
static const UI_Rect btn_rect[UI_BTN_COUNT] =
{
  { 336U, 226U,  66U, 40U },   /* UI_BTN_REC      (sag alt)            */
  { 408U, 226U,  66U, 40U },   /* UI_BTN_SETTINGS                      */
  { 240U,   0U, 240U, 30U },   /* UI_BTN_CLOCK    (baslikta saat)      */
  {  10U,  50U,  76U, 56U },   /* UI_BTN_F_HOUR                        */
  { 104U,  50U,  76U, 56U },   /* UI_BTN_F_MIN                         */
  { 206U,  50U,  76U, 56U },   /* UI_BTN_F_DAY                         */
  { 300U,  50U,  76U, 56U },   /* UI_BTN_F_MON                         */
  { 394U,  50U,  80U, 56U },   /* UI_BTN_F_YEAR                        */
  {  10U, 120U, 222U, 62U },   /* UI_BTN_MINUS                         */
  { 248U, 120U, 226U, 62U },   /* UI_BTN_PLUS                          */
  {  10U, 196U, 140U, 66U },   /* UI_BTN_CANCEL                        */
  { 334U, 196U, 140U, 66U },   /* UI_BTN_SAVE                          */
  {  86U, 146U, 140U, 56U },   /* UI_BTN_NO                            */
  { 254U, 146U, 140U, 56U },   /* UI_BTN_YES                           */
  { 336U, 182U, 138U, 40U },   /* UI_BTN_EXP      (ana ekran DENEY)    */
  /* Deney menusu: 2 sutun x 4 satir */
  {   8U,  32U, 228U, 52U }, { 244U,  32U, 228U, 52U },
  {   8U,  90U, 228U, 52U }, { 244U,  90U, 228U, 52U },
  {   8U, 148U, 228U, 52U }, { 244U, 148U, 228U, 52U },
  {   8U, 206U, 228U, 52U }, { 244U, 206U, 228U, 52U },
  /* Parametre: eksi (PM0..3), arti (PP0..3) */
  { 338U,  34U,  62U, 40U }, { 338U,  78U,  62U, 40U }, { 338U, 122U,  62U, 40U }, { 338U, 166U,  62U, 40U },
  { 406U,  34U,  68U, 40U }, { 406U,  78U,  68U, 40U }, { 406U, 122U,  68U, 40U }, { 406U, 166U,  68U, 40U },
  {  10U, 214U, 140U, 52U },   /* UI_BTN_PBACK                         */
  { 334U, 214U, 140U, 52U },   /* UI_BTN_PSTART                        */
  /* Calisan deney */
  {   8U, 214U, 110U, 52U },   /* UI_BTN_RBACK                         */
  { 124U, 214U, 110U, 52U },   /* UI_BTN_REVENT                        */
  { 240U, 214U, 116U, 52U },   /* UI_BTN_RPAUSE                        */
  { 362U, 214U, 110U, 52U },   /* UI_BTN_RSTOP                         */
  /* Olay: 2 x 2 */
  {   8U,  40U, 228U, 58U }, { 244U,  40U, 228U, 58U },
  {   8U, 106U, 228U, 58U }, { 244U, 106U, 228U, 58U },
  {  10U, 214U, 140U, 52U },   /* UI_BTN_EVBACK                        */
};

static uint8_t rec_active = 0U;
static uint8_t rec_locked = 0U;

/* Genel buton etiketleri */
static struct
{
  char    l1[24];
  char    l2[24];
  uint8_t style;
} lab[UI_BTN_COUNT];

/* ------------------------------------------------------------------------- */
static void Text(uint16_t x, uint16_t y, sFONT *f, uint32_t fg, uint32_t bg, const char *s)
{
  BSP_LCD_SetFont(f);
  BSP_LCD_SetTextColor(fg);
  BSP_LCD_SetBackColor(bg);
  BSP_LCD_DisplayStringAt(x, y, (uint8_t *)s, LEFT_MODE);
}

static void Fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t c)
{
  BSP_LCD_SetTextColor(c);
  BSP_LCD_FillRect(x, y, w, h);
}

/* Metni n karaktere sagdan bosluk ile tamamlar (eski yazinin kalintisini siler) */
static void Pad(char *dst, size_t size, const char *src, uint8_t n)
{
  size_t len = strlen(src);
  if (n >= size) n = (uint8_t)(size - 1U);
  if (len > n) len = n;
  memcpy(dst, src, len);
  memset(dst + len, ' ', n - len);
  dst[n] = '\0';
}

static void Fmt_Fixed(char *buf, size_t n, int32_t v, uint8_t decimals)
{
  const char *sign = (v < 0) ? "-" : "";
  if (v < 0) v = -v;
  if (decimals == 2U)
    snprintf(buf, n, "%s%ld.%02ld", sign, (long)(v / 100), (long)(v % 100));
  else
    snprintf(buf, n, "%s%ld.%ld", sign, (long)(v / 10), (long)(v % 10));
}

static uint32_t LevelColor(UI_Level l)
{
  return (l == UI_LEVEL_OK) ? C_OK : ((l == UI_LEVEL_WARN) ? C_WARN : C_ERR);
}

/* ------------------------------------------------------------------------- */
void UI_DrawFrame(void)
{
  BSP_LCD_Clear(C_BG);

  /* Baslik */
  Fill(0, 0, 480, 26, C_HEADER);
  /* Sol ust: ag durumu (UI_DrawNetStatus ile doldurulur) */

  /* Sensor kartlari */
  for (uint8_t i = 0; i < 4U; i++)
  {
    Fill(card_x[i], card_y[i], CARD_W, CARD_H, C_PANEL);
    Text(card_x[i] + 10U, card_y[i] + 6U, &Font12, C_MUTED, C_PANEL, card_title[i]);
  }

  /* Peltier paneli */
  Fill(PEL_X, PEL_Y, PEL_W, PEL_H, C_PANEL);
  Text(PEL_X + 8U, PEL_Y + 6U, &Font12, C_MUTED, C_PANEL, "PELTIER CIKIS");
  Text(BAR_X, BAR_Y + 16U, &Font8, C_HUM, C_PANEL, "-100");
  Text(BAR_MID - 2U, BAR_Y + 16U, &Font8, C_MUTED, C_PANEL, "0");
  Text(BAR_X + BAR_W - 20U, BAR_Y + 16U, &Font8, C_HEAT, C_PANEL, "+100");
  Text(PEL_X + 8U, PEL_Y + 124U, &Font12, C_UNIT, C_PANEL, "Surucu:");

  /* Deney paneli ve SD paneli (icerik UI_DrawExpPanel / UI_DrawSd ile) */
  Fill(EXPP_X, EXPP_Y, EXPP_W, EXPP_H, C_PANEL);
  Fill(SD_X, SD_Y, SD_W, SD_H, C_PANEL);

  UI_DrawMainButtons();
}

void UI_DrawClock(const DS3231_TimeDate *rtc, uint8_t ok)
{
  char s[32];
  if (ok)
  {
    snprintf(s, sizeof(s), "%02u/%02u/20%02u  %02u:%02u:%02u",
             rtc->date, rtc->month, rtc->year, rtc->hours, rtc->minutes, rtc->seconds);
    Text(252, 5, &Font16, C_CLOCK, C_HEADER, s);
  }
  else
  {
    Text(252, 5, &Font16, C_WARN, C_HEADER, "--/--/----  --:--:--");
  }
}

void UI_DrawSensor(uint8_t card, uint8_t has_value, int32_t value, uint8_t decimals,
                   const char *status, UI_Level level, const char *subline, uint8_t sub_is_humidity)
{
  char v[16], line[24];
  uint16_t x, y, len;
  uint32_t col = LevelColor(level);

  if (card > 3U) return;
  x = card_x[card];
  y = card_y[card];

  /* Sol durum seridi ve durum kodu */
  Fill(x, y, 4, CARD_H, col);
  Pad(line, sizeof(line), status, 3);
  Text(x + CARD_W - 6U - 21U, y + 6U, &Font12, col, C_PANEL, line);

  /* Sicaklik: buyuk yazi + derece + C */
  if (has_value) Fmt_Fixed(v, sizeof(v), value, decimals);
  else           snprintf(v, sizeof(v), (decimals == 2U) ? "--.--" : "--.-");
  len = (uint16_t)strlen(v);
  Fill(x + 8U, y + 20U, CARD_W - 10U, 28U, C_PANEL);
  Text(x + 10U, y + 22U, &Font24, has_value ? C_VALUE : C_MUTED, C_PANEL, v);
  BSP_LCD_SetTextColor(C_UNIT);
  BSP_LCD_DrawCircle(x + 10U + len * 17U + 6U, y + 27U, 3);
  BSP_LCD_DrawCircle(x + 10U + len * 17U + 6U, y + 27U, 2);
  Text(x + 10U + len * 17U + 11U, y + 25U, &Font16, C_UNIT, C_PANEL, "C");

  /* Alt satir */
  Pad(line, sizeof(line), (subline != NULL) ? subline : "", 19);
  Text(x + 10U, y + 52U, &Font12,
       (level != UI_LEVEL_OK) ? col : (sub_is_humidity ? C_HUM : C_MUTED), C_PANEL, line);
}

/* Peltier panelinde "Kapak :" satiri (D7 anahtari) */
void UI_DrawDoor(uint8_t open)
{
  char p[16];
  Text(PEL_X + 8U, PEL_Y + 106U, &Font12, C_UNIT, C_PANEL, "Kapak :");
  Pad(p, sizeof(p), open ? "ACIK" : "KAPALI", 11);
  Text(PEL_X + 8U + 8U * 7U, PEL_Y + 106U, &Font12, open ? C_WARN : C_OK, C_PANEL, p);
}

void UI_DrawPeltier(int16_t cmd, uint8_t link_ok, const char *link_code)
{
  char s[24], p[24];
  int32_t a = (cmd < 0) ? -cmd : cmd;
  uint32_t col;
  const char *mode;
  uint16_t len, w;

  if (cmd > 0)      { col = C_HEAT;  mode = "ISITIYOR";  }
  else if (cmd < 0) { col = C_COOL;  mode = "SOGUTUYOR"; }
  else              { col = C_MUTED; mode = "KAPALI";    }

  /* Durum noktasi */
  BSP_LCD_SetTextColor(link_ok ? C_OK : C_ERR);
  BSP_LCD_FillCircle(PEL_X + PEL_W - 11U, PEL_Y + 11U, 4);

  /* Yuzde: -45.0% / +45.0% / 0.0% */
  snprintf(s, sizeof(s), "%s%ld.%ld%%", (cmd < 0) ? "-" : ((cmd > 0) ? "+" : ""),
           (long)(a / 10), (long)(a % 10));
  len = (uint16_t)strlen(s);
  Fill(PEL_X + 2U, PEL_Y + 24U, PEL_W - 4U, 26U, C_PANEL);
  Text(PEL_X + (PEL_W - len * 17U) / 2U, PEL_Y + 26U, &Font24, col, C_PANEL, s);

  /* Mod yazisi */
  len = (uint16_t)strlen(mode);
  Fill(PEL_X + 2U, PEL_Y + 54U, PEL_W - 4U, 18U, C_PANEL);
  Text(PEL_X + (PEL_W - len * 11U) / 2U, PEL_Y + 55U, &Font16, col, C_PANEL, mode);

  /* Cift yonlu bar: sol = sogutma, sag = isitma */
  Fill(BAR_X, BAR_Y, BAR_W, BAR_H, C_BAR_EDGE);
  Fill(BAR_X + 1U, BAR_Y + 1U, BAR_W - 2U, BAR_H - 2U, C_BAR_BG);
  w = (uint16_t)((a * (BAR_W / 2U - 1U)) / 1000);
  if (w > 0U)
  {
    if (cmd < 0) Fill(BAR_MID - w, BAR_Y + 1U, w, BAR_H - 2U, C_COOL);
    else         Fill(BAR_MID,     BAR_Y + 1U, w, BAR_H - 2U, C_HEAT);
  }
  Fill(BAR_MID - 1U, BAR_Y - 3U, 2U, BAR_H + 6U, C_TEXT);

  /* (106. satir: kapak durumu, UI_DrawDoor) */

  /* Surucu durumu */
  if (link_ok)
  {
    Pad(p, sizeof(p), "BAGLI", 11);
    Text(PEL_X + 8U + 8U * 7U, PEL_Y + 124U, &Font12, C_OK, C_PANEL, p);
  }
  else
  {
    snprintf(s, sizeof(s), "HATA %s", (link_code != NULL) ? link_code : "");
    Pad(p, sizeof(p), s, 11);
    Text(PEL_X + 8U + 8U * 7U, PEL_Y + 124U, &Font12, C_ERR, C_PANEL, p);
  }
}

void UI_DrawSd(const UI_SdInfo *sd)
{
  char s[64], p[64], sz[16], fr[16];
  const char *badge;
  uint32_t bg, fg;
  uint16_t len;

  /* Satir 1: rozet + dosya adi + REC */
  if (!sd->present)      { badge = "KART YOK";        bg = C_BADGE_ERR; fg = C_BADGE_ERT; }
  else if (sd->logging)  { badge = "SD KAYITTA";      bg = C_BADGE_OK;  fg = C_BADGE_OKT; }
  else                   { badge = "SD TAKILI";       bg = C_BADGE_OK;  fg = C_BADGE_OKT; }
  len = (uint16_t)strlen(badge);
  Fill(SD_X + 4U, SD_Y + 4U, 84U, 15U, C_PANEL);
  Fill(SD_X + 4U, SD_Y + 4U, (uint16_t)(len * 7U + 8U), 15U, bg);
  Text(SD_X + 8U, SD_Y + 6U, &Font12, fg, bg, badge);

  Pad(p, sizeof(p), (sd->file != NULL) ? sd->file : "--", 25);   /* T3P3_20261118_1705_12.txt */
  Text(SD_X + 94U, SD_Y + 6U, &Font12, C_TEXT, C_PANEL, p);

  Fill(SD_X + SD_W - 44U, SD_Y + 3U, 42U, 16U, C_PANEL);
  if (sd->present && sd->logging)
  {
    Text(SD_X + SD_W - 42U, SD_Y + 6U, &Font12, C_ERR, C_PANEL, "REC");
    BSP_LCD_SetTextColor(C_ERR);
    BSP_LCD_FillCircle(SD_X + SD_W - 11U, SD_Y + 11U, 4);
  }

  /* Satir 2: kayit sayisi, boyut, bos alan, hata (ya da EEPROM uyarisi) */
  if (!sd->eeprom_ok)
  {
    Pad(p, sizeof(p), "EEPROM HATA: kayit durumu kalici degil", LINE_N);
    Text(SD_X + 6U, SD_Y + 24U, &Font12, C_ERR, C_PANEL, p);
    return;
  }
  if (sd->present && sd->size_kb >= 1024U)
    snprintf(sz, sizeof(sz), "%lu.%lu MB", (unsigned long)(sd->size_kb / 1024U), (unsigned long)((sd->size_kb % 1024U) * 10U / 1024U));
  else if (sd->present && sd->size_kb > 0U)
    snprintf(sz, sizeof(sz), "%lu kB", (unsigned long)sd->size_kb);
  else
    snprintf(sz, sizeof(sz), "--");
  if (sd->present && sd->free_mb >= 1024U)
    snprintf(fr, sizeof(fr), "%lu.%lu GB", (unsigned long)(sd->free_mb / 1024U), (unsigned long)((sd->free_mb % 1024U) * 10U / 1024U));
  else if (sd->present && sd->free_mb > 0U)
    snprintf(fr, sizeof(fr), "%lu MB", (unsigned long)sd->free_mb);
  else
    snprintf(fr, sizeof(fr), "--");
  snprintf(s, sizeof(s), "%s%lu kayit  %s  bos %s  hata %lu", sd->resumed ? "DEVAM " : "",
           (unsigned long)sd->records, sz, fr, (unsigned long)sd->errors);
  Pad(p, sizeof(p), s, LINE_N);
  Text(SD_X + 6U, SD_Y + 24U, &Font12, (sd->errors > 0U) ? C_WARN : C_UNIT, C_PANEL, p);
}

/* =========================================================================
 *                         DOKUNMATIK BUTONLAR
 * ========================================================================= */
static void Border(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t c, uint8_t thick)
{
  BSP_LCD_SetTextColor(c);
  BSP_LCD_DrawRect(x, y, (uint16_t)(w - 1U), (uint16_t)(h - 1U));
  if (thick)
  {
    BSP_LCD_DrawRect((uint16_t)(x + 1U), (uint16_t)(y + 1U), (uint16_t)(w - 3U), (uint16_t)(h - 3U));
  }
}

/* Metni yatayda ortalayarak yazar (char_w: font karakter genisligi) */
static void TextC(uint16_t cx, uint16_t y, sFONT *f, uint32_t fg, uint32_t bg, const char *s)
{
  uint16_t w = (uint16_t)(strlen(s) * f->Width);
  Text((uint16_t)(cx - w / 2U), y, f, fg, bg, s);
}

const UI_Rect *UI_ButtonRect(UI_ButtonId id)
{
  return (id < UI_BTN_COUNT) ? &btn_rect[id] : &btn_rect[0];
}

void UI_SetRecording(uint8_t active)
{
  rec_active = active ? 1U : 0U;
}

void UI_DrawButton(UI_ButtonId id, uint8_t pressed)
{
  const UI_Rect *r;
  uint16_t cx, cy;
  uint32_t bg, edge, fg;

  if (id >= UI_BTN_COUNT) return;
  r  = &btn_rect[id];
  cx = (uint16_t)(r->x + r->w / 2U);
  cy = (uint16_t)(r->y + r->h / 2U);

  switch (id)
  {
    case UI_BTN_REC:
      if (rec_locked)      { bg = C_BTN;    edge = C_BAR_EDGE; fg = C_MUTED; }
      else if (rec_active) { bg = C_REC_BG; edge = C_ERR;      fg = C_BADGE_ERT; }
      else                 { bg = C_BTN;    edge = C_OK;       fg = C_OK; }
      if (pressed && !rec_locked) bg = C_BTN_P;
      Fill(r->x, r->y, r->w, r->h, bg);
      Border(r->x, r->y, r->w, r->h, edge, 1U);
      TextC(cx, (uint16_t)(cy - 14U), &Font12, fg, bg, "KAYIT");
      TextC(cx, (uint16_t)(cy + 2U), &Font12, fg, bg, rec_locked ? "DENEYDE" : (rec_active ? "DURDUR" : "BASLAT"));
      break;

    case UI_BTN_SETTINGS:
      bg = pressed ? C_BTN_P : C_BTN;
      Fill(r->x, r->y, r->w, r->h, bg);
      Border(r->x, r->y, r->w, r->h, C_HEAT, 1U);
      TextC(cx, (uint16_t)(cy - 14U), &Font12, C_HEAT, bg, "AYAR");
      TextC(cx, (uint16_t)(cy + 2U), &Font12, C_MUTED, bg, "saat");
      break;

    case UI_BTN_MINUS:
    case UI_BTN_PLUS:
      bg = pressed ? C_BTN_P : C_BTN;
      Fill(r->x, r->y, r->w, r->h, bg);
      Border(r->x, r->y, r->w, r->h, C_BAR_EDGE, 0U);
      fg = (id == UI_BTN_MINUS) ? C_COOL : C_HEAT;
      Fill((uint16_t)(cx - 20U), (uint16_t)(cy - 4U), 40U, 8U, fg);            /* yatay cizgi */
      if (id == UI_BTN_PLUS)
      {
        Fill((uint16_t)(cx - 4U), (uint16_t)(cy - 20U), 8U, 40U, fg);          /* dikey cizgi */
      }
      break;

    case UI_BTN_CANCEL:
    case UI_BTN_NO:
      bg = pressed ? C_BTN_P : C_BTN;
      Fill(r->x, r->y, r->w, r->h, bg);
      Border(r->x, r->y, r->w, r->h, C_BAR_EDGE, 0U);
      TextC(cx, (uint16_t)(cy - 8U), &Font16, C_UNIT, bg, (id == UI_BTN_CANCEL) ? "IPTAL" : "HAYIR");
      break;

    case UI_BTN_SAVE:
      bg = pressed ? C_BTN_P : C_BADGE_OK;
      Fill(r->x, r->y, r->w, r->h, bg);
      Border(r->x, r->y, r->w, r->h, C_OK, 1U);
      TextC(cx, (uint16_t)(cy - 8U), &Font16, C_BADGE_OKT, bg, "KAYDET");
      break;

    case UI_BTN_YES:
      bg = pressed ? C_BTN_P : C_BADGE_ERR;
      Fill(r->x, r->y, r->w, r->h, bg);
      Border(r->x, r->y, r->w, r->h, C_ERR, 1U);
      TextC(cx, (uint16_t)(cy - 8U), &Font16, C_BADGE_ERT, bg, "EVET");
      break;

    default:
      /* Etiketli genel butonlar (deney sayfalari); UI_BTN_CLOCK ve alan
       * kutularinin basili gorunumu yok */
      if (lab[id].l1[0] != '\0')
      {
        uint32_t tx, sub;
        switch (lab[id].style)
        {
          case UI_STYLE_PRIMARY:  bg = C_BADGE_OK;  edge = C_OK;       tx = C_BADGE_OKT; sub = C_BADGE_OKT; break;
          case UI_STYLE_DANGER:   bg = C_BADGE_ERR; edge = C_ERR;      tx = C_BADGE_ERT; sub = C_BADGE_ERT; break;
          case UI_STYLE_ACCENT:   bg = C_BTN;       edge = C_COOL;     tx = C_COOL;      sub = C_MUTED;     break;
          case UI_STYLE_WARN:     bg = C_BTN;       edge = C_HEAT;     tx = C_HEAT;      sub = C_MUTED;     break;
          case UI_STYLE_DISABLED: bg = C_PANEL;     edge = C_BAR_EDGE; tx = C_BAR_EDGE;  sub = C_BAR_EDGE;  break;
          default:                bg = C_BTN;       edge = C_BAR_EDGE; tx = C_UNIT;      sub = C_MUTED;     break;
        }
        if (pressed && lab[id].style != UI_STYLE_DISABLED) bg = C_BTN_P;
        Fill(r->x, r->y, r->w, r->h, bg);
        Border(r->x, r->y, r->w, r->h, edge, (lab[id].style == UI_STYLE_PRIMARY || lab[id].style == UI_STYLE_DANGER) ? 1U : 0U);
        if (lab[id].l2[0] != '\0')
        {
          TextC(cx, (uint16_t)(cy - 16U), &Font16, tx, bg, lab[id].l1);
          TextC(cx, (uint16_t)(cy + 4U), &Font12, sub, bg, lab[id].l2);
        }
        else
        {
          TextC(cx, (uint16_t)(cy - 8U), &Font16, tx, bg, lab[id].l1);
        }
      }
      break;
  }
}

void UI_SetButtonLabel(UI_ButtonId id, const char *l1, const char *l2, UI_Style style)
{
  if (id >= UI_BTN_COUNT) return;
  snprintf(lab[id].l1, sizeof(lab[id].l1), "%s", (l1 != NULL) ? l1 : "");
  snprintf(lab[id].l2, sizeof(lab[id].l2), "%s", (l2 != NULL) ? l2 : "");
  lab[id].style = (uint8_t)style;
}

void UI_SetRecLocked(uint8_t locked)
{
  rec_locked = locked ? 1U : 0U;
}

void UI_DrawMainButtons(void)
{
  UI_DrawButton(UI_BTN_EXP, 0U);
  UI_DrawButton(UI_BTN_REC, 0U);
  UI_DrawButton(UI_BTN_SETTINGS, 0U);
}

/* =========================================================================
 *                       SAAT / TARIH AYAR SAYFASI
 * ========================================================================= */
static const char *const field_label[UI_FIELD_COUNT] = { "SAAT", "DAKIKA", "GUN", "AY", "YIL" };

void UI_DrawClockSetupFrame(void)
{
  BSP_LCD_Clear(C_BG);
  Fill(0, 0, 480, 26, C_HEADER);
  Text(8, 5, &Font16, C_TEXT, C_HEADER, "SAAT / TARIH AYARI");

  /* Ayiricilar: HH : MM | GG / AA / YYYY */
  Text(90,  68, &Font16, C_MUTED, C_BG, ":");
  Text(286, 68, &Font16, C_MUTED, C_BG, "/");
  Text(380, 68, &Font16, C_MUTED, C_BG, "/");
  Fill(192, 40, 1, 66, C_BAR_EDGE);

  UI_DrawButton(UI_BTN_MINUS, 0U);
  UI_DrawButton(UI_BTN_PLUS, 0U);
  UI_DrawButton(UI_BTN_CANCEL, 0U);
  UI_DrawButton(UI_BTN_SAVE, 0U);
  UI_DrawClockSetupMsg(NULL, 0U);
}

void UI_DrawClockSetupFields(const uint16_t v[UI_FIELD_COUNT], uint8_t selected)
{
  char s[8];
  for (uint8_t i = 0; i < UI_FIELD_COUNT; i++)
  {
    const UI_Rect *r = &btn_rect[UI_BTN_F_HOUR + i];
    uint8_t  sel = (i == selected) ? 1U : 0U;
    uint32_t bg  = sel ? C_SEL : C_PANEL;
    uint16_t cx  = (uint16_t)(r->x + r->w / 2U);

    /* Etiket */
    Fill(r->x, 34, r->w, 14, C_BG);
    TextC(cx, 35, &Font12, sel ? C_HEAT : C_MUTED, C_BG, field_label[i]);

    /* Kutu + deger */
    Fill(r->x, r->y, r->w, r->h, bg);
    Border(r->x, r->y, r->w, r->h, sel ? C_HEAT : C_BAR_EDGE, sel);
    if (i == 4U) snprintf(s, sizeof(s), "%04u", (unsigned)v[i]);
    else         snprintf(s, sizeof(s), "%02u", (unsigned)v[i]);
    TextC(cx, (uint16_t)(r->y + 16U), &Font24, C_VALUE, bg, s);
  }
}

void UI_DrawClockSetupRtc(const DS3231_TimeDate *rtc, uint8_t ok)
{
  char s[24];
  if (ok) snprintf(s, sizeof(s), "RTC: %02u:%02u:%02u", rtc->hours, rtc->minutes, rtc->seconds);
  else    snprintf(s, sizeof(s), "RTC: HATA    ");
  Text(381, 8, &Font12, ok ? C_MUTED : C_ERR, C_HEADER, s);
}

void UI_DrawClockSetupMsg(const char *msg, uint8_t is_error)
{
  Fill(156, 198, 172, 64, C_BG);
  if (msg == NULL)
  {
    TextC(242, 202, &Font12, C_MUTED, C_BG, "Kutuya dokun: sec");
    TextC(242, 218, &Font12, C_MUTED, C_BG, "+/- basili: hizli");
    TextC(242, 234, &Font12, C_MUTED, C_BG, "Kaydet: saniye=00");
    TextC(242, 250, &Font12, C_MUTED, C_BG, "60 s islem yok: cik");
  }
  else
  {
    TextC(242, 224, &Font12, is_error ? C_ERR : C_OK, C_BG, msg);
  }
}

/* =========================================================================
 *                       KAYIT DURDURMA ONAYI
 * ========================================================================= */
void UI_DrawConfirm(const char *title, const char *line1, const char *line2)
{
  Fill(70, 56, 340, 160, C_PANEL);
  Border(70, 56, 340, 160, C_ERR, 1U);
  TextC(240, 72, &Font16, C_TEXT, C_PANEL, title);
  if (line1 != NULL) TextC(240, 100, &Font12, C_UNIT, C_PANEL, line1);
  if (line2 != NULL) TextC(240, 118, &Font12, C_MUTED, C_PANEL, line2);
  UI_DrawButton(UI_BTN_NO, 0U);
  UI_DrawButton(UI_BTN_YES, 0U);
}

void UI_DrawConfirmStop(const char *file)
{
  char s[40];
  snprintf(s, sizeof(s), "Dosya: %s", (file != NULL) ? file : "--");
  UI_DrawConfirm("KAYIT DURDURULSUN MU?", s, "Veri kaydi duracak");
}

/* =========================================================================
 *                              DENEY
 * ========================================================================= */
static uint32_t ToneColor(UI_Tone t)
{
  switch (t)
  {
    case UI_TONE_MUTED: return C_MUTED;
    case UI_TONE_OK:    return C_OK;
    case UI_TONE_WARN:  return C_WARN;
    case UI_TONE_ERR:   return C_ERR;
    case UI_TONE_INFO:  return C_COOL;
    default:            return C_TEXT;
  }
}

void UI_DrawExpPanel(const char *line1, const char *badge, UI_Tone badge_tone, const char *line2, UI_Tone line2_tone)
{
  char p[64];
  uint16_t bw = (uint16_t)(strlen(badge) * 7U + 8U);
  uint32_t c = ToneColor(badge_tone);

  Pad(p, sizeof(p), line1, 30);
  Text(EXPP_X + 6U, EXPP_Y + 6U, &Font12, C_TEXT, C_PANEL, p);

  Fill(EXPP_X + EXPP_W - 108U, EXPP_Y + 3U, 106U, 16U, C_PANEL);
  BSP_LCD_SetTextColor(c);
  BSP_LCD_DrawRect((uint16_t)(EXPP_X + EXPP_W - 4U - bw), EXPP_Y + 3U, (uint16_t)(bw - 1U), 15U);
  Text((uint16_t)(EXPP_X + EXPP_W - bw), EXPP_Y + 6U, &Font12, c, C_PANEL, badge);

  Pad(p, sizeof(p), line2, LINE_N);
  Text(EXPP_X + 6U, EXPP_Y + 24U, &Font12, ToneColor(line2_tone), C_PANEL, p);
}

/* Baslik sol ust: iki satir ag durumu. Her satirin basinda durum rengi bir
 * nokta vardir; normal durumda metin beyaz, uyari/hata durumunda renklidir. */
#define NET_X        18U     /* metin baslangici (nokta x=10)       */
#define NET_CHARS    32U     /* 32 x 7 px = 224 px (saat x=252'den once biter) */

static void NetLine(uint16_t y, const char *s, UI_Tone t)
{
  char p[NET_CHARS + 1U];
  uint32_t dot = ToneColor(t);
  uint32_t txt = (t == UI_TONE_OK || t == UI_TONE_TEXT) ? C_TEXT : dot;

  BSP_LCD_SetTextColor(dot);
  BSP_LCD_FillCircle(10U, (uint16_t)(y + 6U), 3U);
  Pad(p, sizeof(p), s, (uint8_t)NET_CHARS);
  Text(NET_X, y, &Font12, txt, C_HEADER, p);
}

void UI_DrawNetStatus(const char *line1, UI_Tone t1, const char *line2, UI_Tone t2)
{
  Fill(0, 0, 248, 26, C_HEADER);
  NetLine(1U,  line1, t1);
  NetLine(13U, line2, t2);
}

void UI_DrawPage(const char *title, const char *right_text)
{
  BSP_LCD_Clear(C_BG);
  Fill(0, 0, 480, 26, C_HEADER);
  Text(8, 5, &Font16, C_TEXT, C_HEADER, title);
  if (right_text != NULL)
  {
    uint16_t w = (uint16_t)(strlen(right_text) * 7U);
    Text((uint16_t)(472U - w), 8, &Font12, C_MUTED, C_HEADER, right_text);
  }
}

void UI_DrawRow(uint8_t row, const char *text, UI_Tone tone)
{
  char p[48];
  Pad(p, sizeof(p), text, 42);
  Text(12, (uint16_t)(36U + 25U * row), &Font16, ToneColor(tone), C_BG, p);
}

void UI_DrawParamRow(uint8_t row, const char *label, const char *value, uint8_t enabled)
{
  char p[32];
  uint16_t y = (uint16_t)(34U + 44U * row);
  Fill(6, y, 324, 40, C_BG);
  if (!enabled)
  {
    Fill(338, y, 136, 40, C_BG);
    return;
  }
  Pad(p, sizeof(p), label, 19);
  Text(10, (uint16_t)(y + 12U), &Font16, C_UNIT, C_BG, p);
  Fill(222, y, 108, 40, C_PANEL);
  Border(222, y, 108, 40, C_BAR_EDGE, 0U);
  TextC(276, (uint16_t)(y + 12U), &Font16, C_VALUE, C_PANEL, value);
}

void UI_DrawFooterNote(const char *l1, const char *l2, UI_Tone tone)
{
  Fill(156, 216, 172, 48, C_BG);
  if (l1 != NULL) TextC(242, 222, &Font12, ToneColor(tone), C_BG, l1);
  if (l2 != NULL) TextC(242, 240, &Font12, C_MUTED, C_BG, l2);
}
