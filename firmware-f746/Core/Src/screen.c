/**
  ******************************************************************************
  * @file    screen.c
  * @brief   Ekran durum makinesi ve dokunmatik buton islemleri
  *
  *  Buton tipleri:
  *   CLICK  : parmak buton uzerinde KALDIRILINCA calisir (yanlis dokunmaya karsi)
  *   INSTANT: basildigi anda calisir (alan secimi)
  *   REPEAT : basildigi anda + basili tutuldukca tekrar (+/-): once 400 ms
  *            bekler, sonra 150 ms, 2 s sonra 50 ms aralikla
  ******************************************************************************
  */
#include "screen.h"
#include "touch.h"
#include "ui.h"
#include "logctrl.h"
#include "sdlog.h"
#include "exp.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>

typedef enum { BT_CLICK = 0, BT_INSTANT, BT_REPEAT } Btn_Type;

typedef struct
{
  UI_ButtonId id;
  Btn_Type    type;
} Btn_Def;

static const Btn_Def main_btns[] =
{
  { UI_BTN_REC,      BT_CLICK },
  { UI_BTN_SETTINGS, BT_CLICK },
  { UI_BTN_CLOCK,    BT_CLICK },
  { UI_BTN_EXP,      BT_CLICK },
};

static const Btn_Def menu_btns[] =
{
  { UI_BTN_EM0, BT_CLICK }, { UI_BTN_EM1, BT_CLICK }, { UI_BTN_EM2, BT_CLICK }, { UI_BTN_EM3, BT_CLICK },
  { UI_BTN_EM4, BT_CLICK }, { UI_BTN_EM5, BT_CLICK }, { UI_BTN_EM6, BT_CLICK }, { UI_BTN_EM7, BT_CLICK },
};

static const Btn_Def param_btns[] =
{
  { UI_BTN_PM0, BT_REPEAT }, { UI_BTN_PM1, BT_REPEAT }, { UI_BTN_PM2, BT_REPEAT }, { UI_BTN_PM3, BT_REPEAT },
  { UI_BTN_PP0, BT_REPEAT }, { UI_BTN_PP1, BT_REPEAT }, { UI_BTN_PP2, BT_REPEAT }, { UI_BTN_PP3, BT_REPEAT },
  { UI_BTN_PBACK, BT_CLICK }, { UI_BTN_PSTART, BT_CLICK },
};

static const Btn_Def run_btns[] =
{
  { UI_BTN_RBACK, BT_CLICK }, { UI_BTN_REVENT, BT_CLICK }, { UI_BTN_RPAUSE, BT_CLICK }, { UI_BTN_RSTOP, BT_CLICK },
};

static const Btn_Def event_btns[] =
{
  { UI_BTN_EV0, BT_CLICK }, { UI_BTN_EV1, BT_CLICK }, { UI_BTN_EV2, BT_CLICK }, { UI_BTN_EV3, BT_CLICK },
  { UI_BTN_EVBACK, BT_CLICK },
};

static const Btn_Def setup_btns[] =
{
  { UI_BTN_F_HOUR, BT_INSTANT }, { UI_BTN_F_MIN, BT_INSTANT }, { UI_BTN_F_DAY, BT_INSTANT },
  { UI_BTN_F_MON,  BT_INSTANT }, { UI_BTN_F_YEAR, BT_INSTANT },
  { UI_BTN_MINUS,  BT_REPEAT  }, { UI_BTN_PLUS,  BT_REPEAT  },
  { UI_BTN_CANCEL, BT_CLICK   }, { UI_BTN_SAVE,  BT_CLICK   },
};

static const Btn_Def confirm_btns[] =
{
  { UI_BTN_NO,  BT_CLICK },
  { UI_BTN_YES, BT_CLICK },
};

/* Ayar sayfasi alan indeksleri */
enum { F_HOUR = 0, F_MIN, F_DAY, F_MON, F_YEAR };

static I2C_HandleTypeDef *hi2c;
static Screen_Id  screen = SCREEN_MAIN;
static const Btn_Def *active = NULL;     /* su an basili tutulan buton */
static uint32_t   next_repeat;
static uint32_t   last_activity;
static uint8_t    wait_release;          /* ekran degisti: parmak kalkana kadar yok say */

static uint16_t   fv[UI_FIELD_COUNT];    /* saat, dakika, gun, ay, yil (4 hane) */

/* ---------------- Deney sayfalari ---------------- */
typedef struct
{
  const char        *label;     /* NULL: satir kullanilmiyor */
  uint8_t            off;       /* Exp_Params icindeki konum */
  uint8_t            size;      /* 1 ya da 2 bayt            */
  uint16_t           min, max, step;
  const char *const *names;     /* secenek adlari (NULL: sayi) */
} ParDef;

#define PO(f)  (uint8_t)offsetof(Exp_Params, f), (uint8_t)sizeof(((Exp_Params *)0)->f)
static const char *const ctrl_names[] = { "PID", "ON/OFF", "KARMA" };

/* program: 1..6 = P1..P6, 7 = TAM PROTOKOL */
static const ParDef par_tab[8][4] =
{
  [1] = { { "Basamak (dk)",       PO(p1_hold_min),   10,  240,  5, NULL },
          { "Tekrar sayisi",      PO(p1_repeat),      1,    5,  1, NULL } },
  [2] = { { "Denge (dk)",         PO(p2_settle_min), 10,  180,  5, NULL },
          { "Basamak (dk)",       PO(p2_step_min),   10,  240,  5, NULL },
          { "Tekrar sayisi",      PO(p2_repeat),      1,   10,  1, NULL } },
  [3] = { { "Sure (gun)",         PO(p3_days),        1,   30,  1, NULL },
          { "En kisa tutma (dk)", PO(p3_hold_min),    1,   60,  1, NULL },
          { "En uzun tutma (dk)", PO(p3_hold_max),    5,  240,  5, NULL },
          { "Genlik (%)",         PO(p3_amp_pct),    10,  100, 10, NULL } },
  [4] = { { "Sure (gun)",         PO(p4_days),        1,   30,  1, NULL },
          { "Kontrol",            PO(p4_ctrl),        0,    2,  1, ctrl_names },
          { "PID Kp",             PO(kp),            10, 2000, 10, NULL },
          { "PID Ti (s)",         PO(ti_s),          30, 3600, 30, NULL } },
  [5] = { { "Sure (gun)",         PO(p5_days),        1,   30,  1, NULL },
          { "Ayar noktasi (C)",   PO(p5_sp_c),       10,   45,  1, NULL },
          { "PID Kp",             PO(kp),            10, 2000, 10, NULL },
          { "PID Ti (s)",         PO(ti_s),          30, 3600, 30, NULL } },
  [6] = { { "APRBS (saat)",       PO(p6_aprbs_h),     6,   72,  6, NULL },
          { "Kapali cevrim (sa)", PO(p6_closed_h),    6,   72,  6, NULL },
          { "PID Kp",             PO(kp),            10, 2000, 10, NULL },
          { "PID Ti (s)",         PO(ti_s),          30, 3600, 30, NULL } },
  [7] = { { "P1 basamak (dk)",    PO(p1_hold_min),   10,  240,  5, NULL },
          { "P3 APRBS (gun)",     PO(p3_days),        1,   30,  1, NULL },
          { "P4 kapali (gun)",    PO(p4_days),        1,   30,  1, NULL },
          { "PID Kp",             PO(kp),            10, 2000, 10, NULL } },
};

static uint8_t    exp_prog;              /* parametre sayfasindaki program */
static Exp_Params par_backup;            /* GERI / zaman asimi icin        */
static uint8_t    run_last_state = 0xFF;
static uint8_t    run_last_sec = 0xFF;
static char       run_note[24];

static void OpenMenu(void);
static void OpenParam(uint8_t prog, uint8_t take_backup, const char *note);
static void OpenRun(const char *note, const DS3231_TimeDate *rtc);
static void OpenEvent(void);
static uint8_t    fsel;
static uint8_t    rtc_last_sec = 0xFF;

/* ------------------------------------------------------------------------- */
static uint8_t IsLeap(uint16_t y)
{
  return (uint8_t)(((y % 4U) == 0U && (y % 100U) != 0U) || (y % 400U) == 0U);
}

static uint8_t DaysInMonth(uint16_t m, uint16_t y)
{
  static const uint8_t dim[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (m < 1U || m > 12U) return 31U;
  return (uint8_t)((m == 2U && IsLeap(y)) ? 29U : dim[m - 1U]);
}

/* Haftanin gunu (Sakamoto): 1 = Pazartesi ... 7 = Pazar */
static uint8_t WeekDay(uint16_t y, uint16_t m, uint16_t d)
{
  static const uint8_t t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
  if (m < 3U) y--;
  uint16_t w = (uint16_t)((y + y / 4U - y / 100U + y / 400U + t[m - 1U] + d) % 7U); /* 0 = Pazar */
  return (uint8_t)((w == 0U) ? 7U : w);
}

static void ClampDay(void)
{
  uint8_t dmax = DaysInMonth(fv[F_MON], fv[F_YEAR]);
  if (fv[F_DAY] > dmax) fv[F_DAY] = dmax;
  if (fv[F_DAY] < 1U)   fv[F_DAY] = 1U;
}

/* Secili alani +1 / -1 (dairesel) */
static void Step(int8_t dir)
{
  uint16_t lo, hi, *v = &fv[fsel];
  switch (fsel)
  {
    case F_HOUR: lo = 0U;    hi = 23U;   break;
    case F_MIN:  lo = 0U;    hi = 59U;   break;
    case F_DAY:  lo = 1U;    hi = DaysInMonth(fv[F_MON], fv[F_YEAR]); break;
    case F_MON:  lo = 1U;    hi = 12U;   break;
    default:     lo = 2000U; hi = 2099U; break;
  }
  if (dir > 0) *v = (*v >= hi) ? lo : (uint16_t)(*v + 1U);
  else         *v = (*v <= lo) ? hi : (uint16_t)(*v - 1U);
  if (fsel == F_MON || fsel == F_YEAR) ClampDay();     /* 31 Ocak -> Subat: 28/29 */
}

/* ------------------------------------------------------------------------- */
static void Go(Screen_Id s)
{
  screen        = s;
  active        = NULL;
  wait_release  = 1U;
  last_activity = HAL_GetTick();
}

void Screen_OpenClockSetup(const DS3231_TimeDate *rtc)
{
  fv[F_HOUR] = rtc->hours;
  fv[F_MIN]  = rtc->minutes;
  fv[F_DAY]  = rtc->date;
  fv[F_MON]  = (rtc->month >= 1U && rtc->month <= 12U) ? rtc->month : 1U;
  fv[F_YEAR] = (uint16_t)(2000U + ((rtc->year <= 99U) ? rtc->year : 26U));
  if (fv[F_HOUR] > 23U) fv[F_HOUR] = 0U;
  if (fv[F_MIN]  > 59U) fv[F_MIN]  = 0U;
  ClampDay();
  fsel = F_HOUR;
  rtc_last_sec = 0xFF;

  UI_DrawClockSetupFrame();
  UI_DrawClockSetupFields(fv, fsel);
  UI_DrawClockSetupRtc(rtc, 1U);
  Go(SCREEN_CLOCK_SETUP);
}

static void BackToMain(void)
{
  Go(SCREEN_MAIN);
  App_RedrawMain();
}

/* ======================= Deney sayfalari ======================= */
static uint16_t ParGet(const ParDef *d)
{
  const uint8_t *b = (const uint8_t *)Exp_GetParams() + d->off;
  return (d->size == 1U) ? b[0] : (uint16_t)(b[0] | (b[1] << 8));
}

static void ParSet(const ParDef *d, uint16_t v)
{
  uint8_t *b = (uint8_t *)Exp_GetParams() + d->off;
  if (d->size == 1U) b[0] = (uint8_t)v;
  else { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
}

static void ProgLabel(uint8_t prog, char *buf, uint32_t n)
{
  char d[20];
  Exp_FormatDuration(d, sizeof(d), Exp_ProgramDuration(prog));
  snprintf(buf, n, "~%s", d);
}

static void OpenMenu(void)
{
  static const char *const names[7] = { "P1 STATIK", "P2 BASAMAK", "P3 APRBS", "P4 KAPALI CEVRIM",
                                        "P5 BOZUCU", "P6 DOGRULAMA", "TAM PROTOKOL" };
  char sub[24];
  UI_DrawPage("DENEY SECIMI", "test protokolu");
  for (uint8_t i = 0; i < 7U; i++)
  {
    ProgLabel((uint8_t)(i + 1U), sub, sizeof(sub));
    UI_SetButtonLabel((UI_ButtonId)(UI_BTN_EM0 + i), names[i], sub, (i == 6U) ? UI_STYLE_ACCENT : UI_STYLE_NORMAL);
    UI_DrawButton((UI_ButtonId)(UI_BTN_EM0 + i), 0U);
  }
  UI_SetButtonLabel(UI_BTN_EM7, "GERI", NULL, UI_STYLE_NORMAL);
  UI_DrawButton(UI_BTN_EM7, 0U);
  Go(SCREEN_EXP_MENU);
}

static void DrawParamRows(void)
{
  char v[16];
  for (uint8_t i = 0; i < 4U; i++)
  {
    const ParDef *d = &par_tab[exp_prog][i];
    if (d->label == NULL)
    {
      UI_DrawParamRow(i, "", "", 0U);
      UI_SetButtonLabel((UI_ButtonId)(UI_BTN_PM0 + i), NULL, NULL, UI_STYLE_NORMAL);
      UI_SetButtonLabel((UI_ButtonId)(UI_BTN_PP0 + i), NULL, NULL, UI_STYLE_NORMAL);
      continue;
    }
    if (d->names != NULL) snprintf(v, sizeof(v), "%s", d->names[ParGet(d)]);
    else                  snprintf(v, sizeof(v), "%u", (unsigned)ParGet(d));
    UI_DrawParamRow(i, d->label, v, 1U);
    UI_SetButtonLabel((UI_ButtonId)(UI_BTN_PM0 + i), "-", NULL, UI_STYLE_NORMAL);
    UI_SetButtonLabel((UI_ButtonId)(UI_BTN_PP0 + i), "+", NULL, UI_STYLE_NORMAL);
    UI_DrawButton((UI_ButtonId)(UI_BTN_PM0 + i), 0U);
    UI_DrawButton((UI_ButtonId)(UI_BTN_PP0 + i), 0U);
  }
}

static void DrawParamFooter(const char *note, uint8_t is_error)
{
  char l1[32], d[20];
  if (note != NULL)
  {
    UI_DrawFooterNote(note, NULL, is_error ? UI_TONE_ERR : UI_TONE_OK);
    return;
  }
  Exp_FormatDuration(d, sizeof(d), Exp_ProgramDuration(exp_prog));
  snprintf(l1, sizeof(l1), "Sure ~%s", d);
  UI_DrawFooterNote(l1, "kayit otomatik acilir", UI_TONE_TEXT);
}

static void OpenParam(uint8_t prog, uint8_t take_backup, const char *note)
{
  char t[40];
  exp_prog = prog;
  if (take_backup) par_backup = *Exp_GetParams();
  snprintf(t, sizeof(t), "%s - AYARLAR", Exp_ProcName((Exp_Proc)prog));
  UI_DrawPage(t, NULL);
  DrawParamRows();
  UI_SetButtonLabel(UI_BTN_PBACK, "GERI", NULL, UI_STYLE_NORMAL);
  UI_SetButtonLabel(UI_BTN_PSTART, "BASLAT", NULL, UI_STYLE_PRIMARY);
  UI_DrawButton(UI_BTN_PBACK, 0U);
  UI_DrawButton(UI_BTN_PSTART, 0U);
  DrawParamFooter(note, 1U);
  Go(SCREEN_EXP_PARAM);
}

static void ParamStep(uint8_t row, int8_t dir)
{
  const ParDef *d = &par_tab[exp_prog][row];
  uint16_t v;
  if (d->label == NULL) return;
  v = ParGet(d);
  if (d->names != NULL)                                 /* secenek: dairesel */
  {
    v = (dir > 0) ? ((v >= d->max) ? d->min : (uint16_t)(v + 1U))
                  : ((v <= d->min) ? d->max : (uint16_t)(v - 1U));
  }
  else if (dir > 0) v = (uint16_t)((v + d->step > d->max) ? d->max : v + d->step);
  else              v = (uint16_t)((v < d->min + d->step) ? d->min : v - d->step);
  ParSet(d, v);

  /* APRBS: en kisa tutma <= en uzun tutma */
  {
    Exp_Params *q = Exp_GetParams();
    if (q->p3_hold_min > q->p3_hold_max)
    {
      if (d->off == offsetof(Exp_Params, p3_hold_min)) q->p3_hold_max = q->p3_hold_min;
      else                                             q->p3_hold_min = q->p3_hold_max;
    }
  }
  DrawParamRows();
  DrawParamFooter(NULL, 0U);
}

static void SetRunButtons(Exp_State stt)
{
  uint8_t running = (stt == EXP_RUNNING || stt == EXP_PAUSED) ? 1U : 0U;
  UI_SetButtonLabel(UI_BTN_RBACK,  "GERI", NULL, UI_STYLE_NORMAL);
  UI_SetButtonLabel(UI_BTN_REVENT, "OLAY", NULL, running ? UI_STYLE_WARN : UI_STYLE_DISABLED);
  if (stt == EXP_PAUSED)       UI_SetButtonLabel(UI_BTN_RPAUSE, "DEVAM", NULL, UI_STYLE_PRIMARY);
  else if (stt == EXP_RUNNING) UI_SetButtonLabel(UI_BTN_RPAUSE, "DURAKLAT", NULL, UI_STYLE_NORMAL);
  else                         UI_SetButtonLabel(UI_BTN_RPAUSE, "--", NULL, UI_STYLE_DISABLED);
  if (stt == EXP_DONE) UI_SetButtonLabel(UI_BTN_RSTOP, "TAMAM", NULL, UI_STYLE_PRIMARY);
  else                 UI_SetButtonLabel(UI_BTN_RSTOP, "DURDUR", NULL, running ? UI_STYLE_DANGER : UI_STYLE_DISABLED);
  UI_DrawButton(UI_BTN_RBACK, 0U);
  UI_DrawButton(UI_BTN_REVENT, 0U);
  UI_DrawButton(UI_BTN_RPAUSE, 0U);
  UI_DrawButton(UI_BTN_RSTOP, 0U);
}

static void RunRefresh(void)
{
  const Exp_Status *e = Exp_GetStatus();
  char s[64], a[20], b[20];
  UI_Tone tone;

  if (e->full) snprintf(s, sizeof(s), "Program : TAM PROTOKOL (asama %u/%u)", e->stage + 1U, e->stages);
  else         snprintf(s, sizeof(s), "Program : tekil prosedur");
  UI_DrawRow(0, s, UI_TONE_TEXT);

  if (e->safety != EXP_SAFE_OK) { snprintf(s, sizeof(s), "Durum   : GUVENLIK - %s", Exp_SafetyName(e->safety)); tone = UI_TONE_ERR; }
  else
  {
    snprintf(s, sizeof(s), "Durum   : %s%s", Exp_StateName(e->state), e->resumed ? " (kesinti sonrasi)" : "");
    tone = (e->state == EXP_RUNNING) ? UI_TONE_OK : ((e->state == EXP_PAUSED) ? UI_TONE_WARN : UI_TONE_INFO);
  }
  UI_DrawRow(1, s, tone);

  snprintf(s, sizeof(s), "Adim    : %u", e->phase);
  UI_DrawRow(2, s, UI_TONE_TEXT);

  Exp_FormatDuration(a, sizeof(a), e->elapsed_s);
  Exp_FormatDuration(b, sizeof(b), e->duration_s);
  snprintf(s, sizeof(s), "Gecen   : %s / %s", a, b);
  UI_DrawRow(3, s, UI_TONE_TEXT);

  Exp_FormatDuration(a, sizeof(a), (e->state == EXP_DONE) ? 0U : e->total_left_s);
  snprintf(s, sizeof(s), "Kalan   : %s%s", a, e->full ? " (programin tamami)" : "");
  UI_DrawRow(4, s, UI_TONE_TEXT);

  if (e->state != EXP_RUNNING)            snprintf(s, sizeof(s), "Sonraki : --");
  else if (e->closed && e->next_change_s <= 1U) snprintf(s, sizeof(s), "Sonraki : rampa (surekli degisim)");
  else
  {
    Exp_FormatDuration(a, sizeof(a), e->next_change_s);
    snprintf(s, sizeof(s), "Sonraki : %s sonra degisim", a);
  }
  UI_DrawRow(5, s, UI_TONE_MUTED);

  if (e->closed)
    snprintf(s, sizeof(s), "Komut   : %+d  SP %d.%d C (%s)", e->u, e->sp_x10 / 10, e->sp_x10 % 10,
             (e->ctrl == EXP_CTRL_ONOFF) ? "ON/OFF" : "PID");
  else
    snprintf(s, sizeof(s), "Komut   : %+d / 1000  (acik cevrim)", e->u);
  UI_DrawRow(6, s, UI_TONE_TEXT);

  if ((uint8_t)e->state != run_last_state)
  {
    run_last_state = (uint8_t)e->state;
    SetRunButtons(e->state);
  }
}

static void OpenRun(const char *note, const DS3231_TimeDate *rtc)
{
  char t[40];
  const Exp_Status *e = Exp_GetStatus();
  (void)rtc;
  snprintf(t, sizeof(t), "DENEY: %s", Exp_ProcName(e->proc));
  snprintf(run_note, sizeof(run_note), "%s", (note != NULL) ? note : "");
  UI_DrawPage(t, run_note);
  run_last_state = 0xFF;
  run_last_sec   = 0xFF;
  RunRefresh();
  Go(SCREEN_EXP_RUN);
}

static void OpenEvent(void)
{
  UI_DrawPage("OLAY ISARETLE", "log'a yazilir");
  UI_SetButtonLabel(UI_BTN_EV0, "KAPAK ACILDI", "kapi / kapak", UI_STYLE_WARN);
  UI_SetButtonLabel(UI_BTN_EV1, "ISIL YUK", "su sisesi vb.", UI_STYLE_WARN);
  UI_SetButtonLabel(UI_BTN_EV2, "DIS ORTAM", "degisti", UI_STYLE_WARN);
  UI_SetButtonLabel(UI_BTN_EV3, "DIGER", NULL, UI_STYLE_NORMAL);
  UI_SetButtonLabel(UI_BTN_EVBACK, "GERI", NULL, UI_STYLE_NORMAL);
  for (uint8_t i = 0; i < 5U; i++) UI_DrawButton((UI_ButtonId)(UI_BTN_EV0 + i), 0U);
  Go(SCREEN_EXP_EVENT);
}

static void SaveClock(void)
{
  DS3231_TimeDate t;
  ClampDay();
  t.seconds = 0U;
  t.minutes = (uint8_t)fv[F_MIN];
  t.hours   = (uint8_t)fv[F_HOUR];
  t.date    = (uint8_t)fv[F_DAY];
  t.month   = (uint8_t)fv[F_MON];
  t.year    = (uint8_t)(fv[F_YEAR] - 2000U);
  t.day     = WeekDay(fv[F_YEAR], fv[F_MON], fv[F_DAY]);

  if (DS3231_SetTimeDate(hi2c, &t))
  {
    printf("Saat ayarlandi: %02u/%02u/%04u %02u:%02u:00\r\n",
           (unsigned)fv[F_DAY], (unsigned)fv[F_MON], (unsigned)fv[F_YEAR],
           (unsigned)fv[F_HOUR], (unsigned)fv[F_MIN]);
    BackToMain();
  }
  else
  {
    UI_DrawClockSetupMsg("RTC YAZILAMADI!", 1U);   /* sayfada kal, tekrar denenebilir */
  }
}

/* Bir butonun islevi */
static void Fire(UI_ButtonId id, const DS3231_TimeDate *rtc, uint8_t rtc_ok)
{
  last_activity = HAL_GetTick();

  switch (screen)
  {
    case SCREEN_MAIN:
      if (id == UI_BTN_SETTINGS || id == UI_BTN_CLOCK)
      {
        if (rtc_ok) Screen_OpenClockSetup(rtc);
        else
        {
          DS3231_TimeDate z = { 0, 0, 12, 1, 1, 1, 26 };   /* RTC okunamiyorsa varsayilan */
          Screen_OpenClockSetup(&z);
        }
      }
      else if (id == UI_BTN_EXP)
      {
        if (Exp_GetStatus()->state == EXP_IDLE) OpenMenu();
        else                                    OpenRun(NULL, rtc);
      }
      else if (id == UI_BTN_REC)
      {
        Exp_State es = Exp_GetStatus()->state;
        if (es == EXP_RUNNING || es == EXP_PAUSED)
        {
          /* deney calisirken kayit durdurulamaz (buton kilitli) */
        }
        else if (LogCtrl_IsActive())
        {
          UI_DrawConfirmStop(SdLog_FileName());             /* durdurmak icin onay */
          Go(SCREEN_CONFIRM_STOP);
        }
        else
        {
          (void)LogCtrl_Start(rtc_ok ? rtc : NULL);
          App_RecordingChanged();
        }
      }
      break;

    case SCREEN_CLOCK_SETUP:
      if (id >= UI_BTN_F_HOUR && id <= UI_BTN_F_YEAR)
      {
        fsel = (uint8_t)(id - UI_BTN_F_HOUR);
        UI_DrawClockSetupFields(fv, fsel);
      }
      else if (id == UI_BTN_MINUS || id == UI_BTN_PLUS)
      {
        Step((id == UI_BTN_PLUS) ? 1 : -1);
        UI_DrawClockSetupFields(fv, fsel);
      }
      else if (id == UI_BTN_CANCEL) BackToMain();
      else if (id == UI_BTN_SAVE)   SaveClock();
      break;

    case SCREEN_CONFIRM_STOP:
      if (id == UI_BTN_YES)
      {
        (void)LogCtrl_Stop();
      }
      BackToMain();                       /* EVET ya da HAYIR: ana ekran yeniden cizilir */
      break;

    case SCREEN_EXP_MENU:
      if (id >= UI_BTN_EM0 && id <= UI_BTN_EM6) OpenParam((uint8_t)(id - UI_BTN_EM0 + 1U), 1U, NULL);
      else if (id == UI_BTN_EM7)                BackToMain();
      break;

    case SCREEN_EXP_PARAM:
      if (id >= UI_BTN_PM0 && id <= UI_BTN_PM3)      ParamStep((uint8_t)(id - UI_BTN_PM0), -1);
      else if (id >= UI_BTN_PP0 && id <= UI_BTN_PP3) ParamStep((uint8_t)(id - UI_BTN_PP0), +1);
      else if (id == UI_BTN_PBACK)
      {
        *Exp_GetParams() = par_backup;                /* degisiklikleri geri al */
        OpenMenu();
      }
      else if (id == UI_BTN_PSTART)
      {
        char l1[32], l2[40], d[20];
        if (!rtc_ok) { DrawParamFooter("RTC YOK: baslatilamaz", 1U); break; }
        Exp_FormatDuration(d, sizeof(d), Exp_ProgramDuration(exp_prog));
        snprintf(l1, sizeof(l1), "%s", Exp_ProcName((Exp_Proc)exp_prog));
        snprintf(l2, sizeof(l2), "Sure ~%s, kayit acilir", d);
        UI_DrawConfirm("TEST BASLATILSIN MI?", l1, l2);
        Go(SCREEN_CONFIRM_START);
      }
      break;

    case SCREEN_CONFIRM_START:
      if (id == UI_BTN_YES && rtc_ok)
      {
        Exp_SaveParams();
        if (Exp_Start(exp_prog, rtc))
        {
          App_RecordingChanged();
          OpenRun("test basladi", rtc);
        }
        else OpenParam(exp_prog, 0U, "Baslatilamadi");
      }
      else OpenParam(exp_prog, 0U, NULL);              /* HAYIR: ayarlar korunur */
      break;

    case SCREEN_EXP_RUN:
    {
      Exp_State es = Exp_GetStatus()->state;
      if (id == UI_BTN_RBACK) BackToMain();
      else if (id == UI_BTN_REVENT && (es == EXP_RUNNING || es == EXP_PAUSED)) OpenEvent();
      else if (id == UI_BTN_RPAUSE && (es == EXP_RUNNING || es == EXP_PAUSED))
      {
        if (rtc_ok) Exp_PauseToggle(rtc);
        RunRefresh();
      }
      else if (id == UI_BTN_RSTOP)
      {
        if (es == EXP_DONE) { Exp_Acknowledge(); BackToMain(); }
        else if (es == EXP_RUNNING || es == EXP_PAUSED)
        {
          UI_DrawConfirm("TEST DURDURULSUN MU?", Exp_ProcName(Exp_GetStatus()->proc), "Peltier kapatilir, kayit devam eder");
          Go(SCREEN_CONFIRM_EXPSTOP);
        }
      }
      break;
    }

    case SCREEN_CONFIRM_EXPSTOP:
      if (id == UI_BTN_YES) { Exp_Stop(); BackToMain(); }
      else                  OpenRun(NULL, rtc);
      break;

    case SCREEN_EXP_EVENT:
      if (id >= UI_BTN_EV0 && id <= UI_BTN_EV3)
      {
        static const Exp_Event ev[4] = { EXP_EVT_DOOR, EXP_EVT_LOAD, EXP_EVT_AMBIENT, EXP_EVT_OTHER };
        if (rtc_ok) Exp_MarkEvent(ev[id - UI_BTN_EV0], rtc);
        OpenRun("olay kaydedildi", rtc);
      }
      else OpenRun(NULL, rtc);
      break;

    default:
      break;
  }
}

static const Btn_Def *HitTest(uint16_t x, uint16_t y, const Btn_Def *list, uint8_t n)
{
  for (uint8_t i = 0; i < n; i++)
  {
    const UI_Rect *r = UI_ButtonRect(list[i].id);
    if (Touch_In(x, y, r->x, r->y, r->w, r->h)) return &list[i];
  }
  return NULL;
}

/* ------------------------------------------------------------------------- */
void Screen_Init(I2C_HandleTypeDef *hi2c_rtc)
{
  hi2c   = hi2c_rtc;
  screen = SCREEN_MAIN;
  active = NULL;
  wait_release = 0U;
  (void)Touch_Init();
}

Screen_Id Screen_Current(void)
{
  return screen;
}

void Screen_Task(const DS3231_TimeDate *rtc, uint8_t rtc_ok)
{
  const Touch_State *t = Touch_Task();
  const Btn_Def *list;
  uint8_t n;
  uint32_t now = HAL_GetTick();

  switch (screen)
  {
    case SCREEN_CLOCK_SETUP:  list = setup_btns;   n = (uint8_t)(sizeof(setup_btns) / sizeof(setup_btns[0]));     break;
    case SCREEN_CONFIRM_STOP:
    case SCREEN_CONFIRM_START:
    case SCREEN_CONFIRM_EXPSTOP: list = confirm_btns; n = (uint8_t)(sizeof(confirm_btns) / sizeof(confirm_btns[0])); break;
    case SCREEN_EXP_MENU:  list = menu_btns;  n = (uint8_t)(sizeof(menu_btns) / sizeof(menu_btns[0]));   break;
    case SCREEN_EXP_PARAM: list = param_btns; n = (uint8_t)(sizeof(param_btns) / sizeof(param_btns[0])); break;
    case SCREEN_EXP_RUN:   list = run_btns;   n = (uint8_t)(sizeof(run_btns) / sizeof(run_btns[0]));     break;
    case SCREEN_EXP_EVENT: list = event_btns; n = (uint8_t)(sizeof(event_btns) / sizeof(event_btns[0])); break;
    default:                  list = main_btns;    n = (uint8_t)(sizeof(main_btns) / sizeof(main_btns[0]));       break;
  }

  /* Ekran degistikten sonra parmak kalkana kadar dokunuslari yok say */
  if (wait_release)
  {
    if (!t->pressed) wait_release = 0U;
  }
  else
  {
    if (t->down_evt)
    {
      active = HitTest(t->x, t->y, list, n);
      if (active != NULL)
      {
        UI_DrawButton(active->id, 1U);
        if (active->type != BT_CLICK)
        {
          Fire(active->id, rtc, rtc_ok);
          next_repeat = now + 400U;
        }
      }
    }
    else if (t->pressed && active != NULL && active->type == BT_REPEAT)
    {
      if ((int32_t)(now - next_repeat) >= 0)
      {
        Fire(active->id, rtc, rtc_ok);
        next_repeat = now + (((now - t->down_tick) > 2000U) ? 50U : 150U);
      }
    }

    if (t->up_evt && active != NULL)
    {
      const Btn_Def *b = active;
      active = NULL;
      UI_DrawButton(b->id, 0U);
      if (b->type == BT_CLICK)
      {
        const UI_Rect *r = UI_ButtonRect(b->id);
        if (Touch_In(t->x, t->y, r->x, r->y, r->w, r->h))   /* parmak hala buton uzerinde */
        {
          Fire(b->id, rtc, rtc_ok);
        }
      }
    }
  }

  /* Sayfa ozel periyodik isler */
  if (screen == SCREEN_CLOCK_SETUP)
  {
    if (rtc_ok && rtc->seconds != rtc_last_sec)
    {
      rtc_last_sec = rtc->seconds;
      UI_DrawClockSetupRtc(rtc, 1U);
    }
    else if (!rtc_ok && rtc_last_sec != 0xFE)
    {
      rtc_last_sec = 0xFE;
      UI_DrawClockSetupRtc(rtc, 0U);
    }
    /* Not: ekran degisiminde last_activity HAL_GetTick() ile kuruluyor ve 'now'dan
     * buyuk olabilir; bu yuzden fark her zaman guncel tick ile hesaplanir. */
    if (t->pressed) last_activity = HAL_GetTick();
    if ((HAL_GetTick() - last_activity) > SCREEN_SETUP_TIMEOUT_MS) BackToMain();   /* kaydetmeden cik */
  }
  else if (screen != SCREEN_MAIN)
  {
    uint32_t limit = SCREEN_MENU_TIMEOUT_MS;
    if (t->pressed) last_activity = HAL_GetTick();
    if (screen == SCREEN_CONFIRM_STOP || screen == SCREEN_CONFIRM_START || screen == SCREEN_CONFIRM_EXPSTOP)
      limit = SCREEN_CONFIRM_TIMEOUT_MS;
    else if (screen == SCREEN_EXP_RUN)
      limit = SCREEN_RUN_TIMEOUT_MS;

    /* Deney izleme sayfasi: saniyede bir guncelle */
    if (screen == SCREEN_EXP_RUN && rtc_ok && rtc->seconds != run_last_sec)
    {
      run_last_sec = rtc->seconds;
      RunRefresh();
    }

    if ((HAL_GetTick() - last_activity) > limit)
    {
      if (screen == SCREEN_EXP_PARAM) *Exp_GetParams() = par_backup;     /* kaydetmeden cik */
      if (screen == SCREEN_CONFIRM_START) *Exp_GetParams() = par_backup;
      if (screen == SCREEN_CONFIRM_EXPSTOP) OpenRun(NULL, rtc);          /* HAYIR sayilir   */
      else BackToMain();
    }
  }
}
