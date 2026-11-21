/**
  ******************************************************************************
  * @file    ui.h
  * @brief   480x272 LCD arayuzu: baslik/saat, 4 sensor karti, Peltier paneli,
  *          SD kart paneli. Cerceve bir kez cizilir; degerler yerinde guncellenir.
  ******************************************************************************
  */
#ifndef __UI_H
#define __UI_H

#include <stdint.h>
#include "ds3231.h"

/* Sensor kartlari (ekrandaki sira) */
#define UI_CARD_AIR        0U   /* KABIN HAVA    D5 (DHT22)   */
#define UI_CARD_PELTIER    1U   /* PELTIER YUZEY D3 (DS18B20) */
#define UI_CARD_WALL       2U   /* KARSI DUVAR   D4 (DS18B20) */
#define UI_CARD_AMBIENT    3U   /* DIS ORTAM     D2 (DS18B20) */

typedef enum
{
  UI_LEVEL_OK = 0,     /* yesil  */
  UI_LEVEL_WARN,       /* turuncu: son okuma hatali, eski deger gosteriliyor */
  UI_LEVEL_FAULT       /* kirmizi: ariza / deger yok */
} UI_Level;

typedef struct
{
  uint8_t     present;       /* kart takili mi                    */
  uint8_t     logging;       /* kayit aktif mi                    */
  const char *file;          /* dosya adi (NULL -> "--")          */
  uint32_t    records;       /* yazilan satir sayisi              */
  uint32_t    size_kb;       /* dosya boyutu, kB                  */
  const char *last_write;    /* "17:47:32" (NULL -> "--")         */
  uint32_t    free_mb;       /* bos alan, MB (0 -> "--")          */
  uint32_t    errors;        /* yazma hatasi sayisi               */
  uint8_t     eeprom_ok;     /* kayit durumu EEPROM'u bulundu mu  */
  uint8_t     resumed;       /* enerji kesintisi sonrasi devam    */
} UI_SdInfo;

/* ---------------- Dokunmatik butonlar ---------------- */
typedef struct
{
  uint16_t x, y, w, h;
} UI_Rect;

typedef enum
{
  /* Ana ekran */
  UI_BTN_REC = 0,        /* KAYIT BASLAT / DURDUR                 */
  UI_BTN_SETTINGS,       /* AYAR (saat)                           */
  UI_BTN_CLOCK,          /* baslikta saat alani (kisayol)         */
  /* Saat ayar sayfasi */
  UI_BTN_F_HOUR,
  UI_BTN_F_MIN,
  UI_BTN_F_DAY,
  UI_BTN_F_MON,
  UI_BTN_F_YEAR,
  UI_BTN_MINUS,
  UI_BTN_PLUS,
  UI_BTN_CANCEL,
  UI_BTN_SAVE,
  /* Onay penceresi */
  UI_BTN_NO,
  UI_BTN_YES,
  /* Ana ekran: deney */
  UI_BTN_EXP,
  /* Deney menusu (8 buton: 7 program + GERI) */
  UI_BTN_EM0, UI_BTN_EM1, UI_BTN_EM2, UI_BTN_EM3,
  UI_BTN_EM4, UI_BTN_EM5, UI_BTN_EM6, UI_BTN_EM7,
  /* Parametre sayfasi: satir basina - / + */
  UI_BTN_PM0, UI_BTN_PM1, UI_BTN_PM2, UI_BTN_PM3,
  UI_BTN_PP0, UI_BTN_PP1, UI_BTN_PP2, UI_BTN_PP3,
  UI_BTN_PBACK, UI_BTN_PSTART,
  /* Calisan deney sayfasi */
  UI_BTN_RBACK, UI_BTN_REVENT, UI_BTN_RPAUSE, UI_BTN_RSTOP,
  /* Olay sayfasi */
  UI_BTN_EV0, UI_BTN_EV1, UI_BTN_EV2, UI_BTN_EV3, UI_BTN_EVBACK,
  UI_BTN_COUNT
} UI_ButtonId;

/* Etiketli (genel) butonlarin gorunumu */
typedef enum
{
  UI_STYLE_NORMAL = 0,   /* koyu, gri kenar     */
  UI_STYLE_PRIMARY,      /* yesil (BASLAT)      */
  UI_STYLE_DANGER,       /* kirmizi (DURDUR)    */
  UI_STYLE_ACCENT,       /* mavi (DENEY)        */
  UI_STYLE_WARN,         /* turuncu             */
  UI_STYLE_DISABLED      /* soluk, basilamaz    */
} UI_Style;

/* Satir/rozet renkleri */
typedef enum
{
  UI_TONE_TEXT = 0, UI_TONE_MUTED, UI_TONE_OK, UI_TONE_WARN, UI_TONE_ERR, UI_TONE_INFO
} UI_Tone;

#define UI_FIELD_COUNT   5U    /* saat, dakika, gun, ay, yil */

const UI_Rect *UI_ButtonRect(UI_ButtonId id);
void UI_SetRecording(uint8_t active);               /* KAYIT butonunun gorunumu */
void UI_SetRecLocked(uint8_t locked);               /* deney calisirken KAYIT kilitli */
/* Genel butonlarin etiketi (l2 = alt satir, NULL olabilir) */
void UI_SetButtonLabel(UI_ButtonId id, const char *l1, const char *l2, UI_Style style);
void UI_DrawButton(UI_ButtonId id, uint8_t pressed);
void UI_DrawMainButtons(void);

/* Saat / tarih ayar sayfasi */
void UI_DrawClockSetupFrame(void);
void UI_DrawClockSetupFields(const uint16_t v[UI_FIELD_COUNT], uint8_t selected);
void UI_DrawClockSetupRtc(const DS3231_TimeDate *rtc, uint8_t ok);
void UI_DrawClockSetupMsg(const char *msg, uint8_t is_error);   /* msg=NULL: ipuclari */

/* Kayit durdurma onay penceresi */
void UI_DrawConfirmStop(const char *file);
/* Genel onay penceresi (HAYIR / EVET) */
void UI_DrawConfirm(const char *title, const char *line1, const char *line2);

/* ---------------- Deney ---------------- */
/* Ana ekrandaki deney paneli (2 satir) */
void UI_DrawExpPanel(const char *line1, const char *badge, UI_Tone badge_tone, const char *line2, UI_Tone line2_tone);
/* Tam ekran sayfa: baslik cubugu + bos govde */
void UI_DrawPage(const char *title, const char *right_text);
/* Govde satiri (Font16, row 0..6, y = 36 + 25*row) */
void UI_DrawRow(uint8_t row, const char *text, UI_Tone tone);
/* Parametre satiri (row 0..3): etiket + deger kutusu; enabled=0 ise satir bos */
void UI_DrawParamRow(uint8_t row, const char *label, const char *value, uint8_t enabled);
/* Sayfa altinda ortadaki bilgi yazisi (2 satir, Font12) */
void UI_DrawFooterNote(const char *l1, const char *l2, UI_Tone tone);

void UI_DrawFrame(void);
void UI_DrawDoor(uint8_t open);          /* Peltier panelinde kapak satiri */
/* Ana ekran basligi sol ust: ag (IP) ve sunucu durumu, iki satir */
void UI_DrawNetStatus(const char *line1, UI_Tone t1, const char *line2, UI_Tone t2);
void UI_DrawClock(const DS3231_TimeDate *rtc, uint8_t ok);

/* value: has_value=0 ise "--" gosterilir. decimals: 1 -> value x10, 2 -> value x100 */
void UI_DrawSensor(uint8_t card, uint8_t has_value, int32_t value, uint8_t decimals,
                   const char *status, UI_Level level, const char *subline, uint8_t sub_is_humidity);

/* cmd: -1000..+1000. link_ok=0 ise "HATA <kod>" gosterilir */
void UI_DrawPeltier(int16_t cmd, uint8_t link_ok, const char *link_code);

void UI_DrawSd(const UI_SdInfo *sd);

#endif /* __UI_H */
