/**
  ******************************************************************************
  * @file    exp.c
  * @brief   Deney (test protokolu) yoneticisi
  *
  *  Durumlar: BOSTA -> CALISIYOR <-> DURAKLATILDI -> BITTI -> BOSTA
  *  - Zaman: RTC'den "2000-01-01'den bu yana saniye" (epoch).
  *    gecen = simdi - baslangic - duraklatma_toplami
  *  - EEPROM (24C32) 0x0100 / 0x0140: cift kopya + sira no + CRC.
  *    Yalnizca durum degisiminde yazilir (baslat/durdur/duraklat, asama
  *    gecisi, P1/P2 basamak degisimi, parametre kaydi).
  *  - Enerji kesintisi sonrasi:
  *      P3..P6: baslangic zamani korunur -> saat cizelgesine gore devam
  *      P1, P2: yarim kalan basamak bastan alinir (baslangic kaydirilir)
  ******************************************************************************
  */
#include "exp.h"
#include "exp_proc.h"
#include "control.h"
#include "at24c32.h"
#include "peltier.h"
#include "logctrl.h"
#include "sdlog.h"
#include <stdio.h>
#include <string.h>

#define EXP_MAGIC        0x31505845UL     /* "EXP1" */
#define EXP_VERSION      1U
#define EXP_ADDR_A       0x0100U
#define EXP_ADDR_B       0x0140U
#define EXP_REC_SIZE     60U

static const Exp_Proc full_stages[] = { EXP_P1_STATIC, EXP_P2_STEP, EXP_P3_APRBS, EXP_P4_CLOSED, EXP_P6_VALID };
#define FULL_STAGES  ((uint8_t)(sizeof(full_stages) / sizeof(full_stages[0])))

/* Kalici kayit */
static struct
{
  uint8_t    state;
  uint8_t    full;
  uint8_t    stage;
  uint8_t    proc;
  uint32_t   seed;
  uint32_t   proc_start;
  uint16_t   step_idx;
  uint32_t   paused_at;
  uint32_t   paused_total;
  uint16_t   seq;
  Exp_Params pr;
} R;

static AT24C32_HandleTypeDef hee;
static uint8_t     ee_ok;
static Exp_Status  st;
static PID_Ctrl    pid;
static OnOff_Ctrl  onoff;
static uint8_t     last_ctrl = 0xFF;
static uint8_t     last_closed = 0xFF;
static uint8_t     need_resume;
static uint32_t    no_air_s;
static uint32_t    trip_at;
static uint32_t    tod_at_start;     /* hizli test modunda sanal gun saati icin */

/* ========================================================================= */
static const Exp_Params defaults =
{
  .p1_hold_min = 60, .p1_repeat = 2,
  .p2_settle_min = 30, .p2_step_min = 60, .p2_repeat = 3,
  .p3_days = 10, .p3_hold_min = 3, .p3_hold_max = 40, .p3_amp_pct = 100,
  .p4_days = 7, .p4_ctrl = EXP_CTRL_MIXED,
  .p5_days = 5, .p5_sp_c = 25,
  .p6_aprbs_h = 36, .p6_closed_h = 36,
  .kp = 150, .ti_s = 600
};

/* ---------------- Zaman ---------------- */
static uint32_t Epoch(const DS3231_TimeDate *r)
{
  static const uint16_t cum[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
  uint32_t y = r->year, m = (r->month >= 1U && r->month <= 12U) ? r->month : 1U;
  uint32_t days = y * 365U + (y + 3U) / 4U;             /* 2000..y-1 (2000 artik) */
  days += cum[m - 1U] + (uint32_t)((r->date >= 1U) ? r->date - 1U : 0U);
  if (m > 2U && (y % 4U) == 0U) days++;
  return days * 86400U + r->hours * 3600U + r->minutes * 60U + r->seconds;
}

static uint32_t Tod(const DS3231_TimeDate *r)
{
  return r->hours * 3600U + r->minutes * 60U + r->seconds;
}

/* ---------------- EEPROM ---------------- */
static uint16_t Crc16(const uint8_t *d, uint16_t n)
{
  uint16_t crc = 0xFFFFU;
  while (n--)
  {
    crc ^= (uint16_t)(*d++) << 8;
    for (uint8_t i = 0; i < 8U; i++) crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
  }
  return crc;
}

static uint8_t *P8(uint8_t *b, uint8_t v)   { *b++ = v; return b; }
static uint8_t *P16(uint8_t *b, uint16_t v) { *b++ = (uint8_t)v; *b++ = (uint8_t)(v >> 8); return b; }
static uint8_t *P32(uint8_t *b, uint32_t v) { b = P16(b, (uint16_t)v); return P16(b, (uint16_t)(v >> 16)); }
static const uint8_t *G8(const uint8_t *b, uint8_t *v)   { *v = *b++; return b; }
static const uint8_t *G16(const uint8_t *b, uint16_t *v) { *v = (uint16_t)(b[0] | (b[1] << 8)); return b + 2; }
static const uint8_t *G32(const uint8_t *b, uint32_t *v) { uint16_t lo, hi; b = G16(b, &lo); b = G16(b, &hi); *v = lo | ((uint32_t)hi << 16); return b; }

static void Pack(uint8_t *b0)
{
  uint8_t *b = b0;
  memset(b0, 0, EXP_REC_SIZE);
  b = P32(b, EXP_MAGIC); b = P8(b, EXP_VERSION);
  b = P8(b, R.state); b = P8(b, R.full); b = P8(b, R.stage); b = P8(b, R.proc);
  b = P32(b, R.seed); b = P32(b, R.proc_start); b = P16(b, R.step_idx);
  b = P32(b, R.paused_at); b = P32(b, R.paused_total); b = P16(b, R.seq);
  b = P16(b, R.pr.p1_hold_min); b = P8(b, R.pr.p1_repeat);
  b = P16(b, R.pr.p2_settle_min); b = P16(b, R.pr.p2_step_min); b = P8(b, R.pr.p2_repeat);
  b = P8(b, R.pr.p3_days); b = P16(b, R.pr.p3_hold_min); b = P16(b, R.pr.p3_hold_max); b = P8(b, R.pr.p3_amp_pct);
  b = P8(b, R.pr.p4_days); b = P8(b, R.pr.p4_ctrl);
  b = P8(b, R.pr.p5_days); b = P8(b, R.pr.p5_sp_c);
  b = P8(b, R.pr.p6_aprbs_h); b = P8(b, R.pr.p6_closed_h);
  b = P16(b, R.pr.kp); b = P16(b, R.pr.ti_s);
  (void)b;
  uint16_t c = Crc16(b0, EXP_REC_SIZE - 2U);
  b0[EXP_REC_SIZE - 2U] = (uint8_t)c;
  b0[EXP_REC_SIZE - 1U] = (uint8_t)(c >> 8);
}

static uint8_t Unpack(const uint8_t *b0, uint16_t *seq_out)
{
  const uint8_t *b = b0;
  uint32_t magic; uint8_t ver;
  uint16_t c = (uint16_t)(b0[EXP_REC_SIZE - 2U] | (b0[EXP_REC_SIZE - 1U] << 8));
  if (Crc16(b0, EXP_REC_SIZE - 2U) != c) return 0U;
  b = G32(b, &magic); b = G8(b, &ver);
  if (magic != EXP_MAGIC || ver != EXP_VERSION) return 0U;
  b = G8(b, &R.state); b = G8(b, &R.full); b = G8(b, &R.stage); b = G8(b, &R.proc);
  b = G32(b, &R.seed); b = G32(b, &R.proc_start); b = G16(b, &R.step_idx);
  b = G32(b, &R.paused_at); b = G32(b, &R.paused_total); b = G16(b, &R.seq);
  b = G16(b, &R.pr.p1_hold_min); b = G8(b, &R.pr.p1_repeat);
  b = G16(b, &R.pr.p2_settle_min); b = G16(b, &R.pr.p2_step_min); b = G8(b, &R.pr.p2_repeat);
  b = G8(b, &R.pr.p3_days); b = G16(b, &R.pr.p3_hold_min); b = G16(b, &R.pr.p3_hold_max); b = G8(b, &R.pr.p3_amp_pct);
  b = G8(b, &R.pr.p4_days); b = G8(b, &R.pr.p4_ctrl);
  b = G8(b, &R.pr.p5_days); b = G8(b, &R.pr.p5_sp_c);
  b = G8(b, &R.pr.p6_aprbs_h); b = G8(b, &R.pr.p6_closed_h);
  b = G16(b, &R.pr.kp); b = G16(b, &R.pr.ti_s);
  (void)b;
  *seq_out = R.seq;
  return 1U;
}

static void Save(void)
{
  uint8_t b[EXP_REC_SIZE];
  if (!ee_ok) return;
  R.seq++;
  Pack(b);
  if (AT24C32_Write(&hee, (R.seq & 1U) ? EXP_ADDR_B : EXP_ADDR_A, b, EXP_REC_SIZE) != HAL_OK)
  {
    printf("EXP: EEPROM yazma hatasi\r\n");
  }
}

static void Load(void)
{
  uint8_t ba[EXP_REC_SIZE], bb[EXP_REC_SIZE];
  uint16_t sa = 0, sb = 0;
  uint8_t va = 0, vb = 0;

  if (ee_ok && AT24C32_Read(&hee, EXP_ADDR_B, bb, EXP_REC_SIZE) == HAL_OK) vb = Unpack(bb, &sb);
  if (ee_ok && AT24C32_Read(&hee, EXP_ADDR_A, ba, EXP_REC_SIZE) == HAL_OK) va = Unpack(ba, &sa);

  if (va && vb)
  {
    if ((int16_t)(sb - sa) > 0) (void)Unpack(bb, &sb);   /* B daha yeni: tekrar yukle */
  }
  else if (vb)
  {
    (void)Unpack(bb, &sb);
  }
  else if (!va)
  {
    memset(&R, 0, sizeof(R));
    R.pr = defaults;
  }
  if (R.state > EXP_DONE || R.proc >= EXP_P_COUNT) { R.state = EXP_IDLE; R.proc = EXP_P_NONE; }
}

/* ---------------- Yardimcilar ---------------- */
static void Comment(const char *fmt_line)
{
  SdLog_Comment(fmt_line);
  printf("%s\r\n", fmt_line);
}

static void ParamText(char *s, uint32_t n, Exp_Proc p)
{
  const Exp_Params *q = &R.pr;
  switch (p)
  {
    case EXP_P1_STATIC: snprintf(s, n, "basamak=%u dk tekrar=%u", q->p1_hold_min, q->p1_repeat); break;
    case EXP_P2_STEP:   snprintf(s, n, "denge=%u dk basamak=%u dk tekrar=%u", q->p2_settle_min, q->p2_step_min, q->p2_repeat); break;
    case EXP_P3_APRBS:  snprintf(s, n, "gun=%u tutma=%u-%u dk genlik=%u%% seed=%08lX", q->p3_days, q->p3_hold_min, q->p3_hold_max, q->p3_amp_pct, (unsigned long)R.seed); break;
    case EXP_P4_CLOSED: snprintf(s, n, "gun=%u kontrol=%u Kp=%u Ti=%u s", q->p4_days, q->p4_ctrl, q->kp, q->ti_s); break;
    case EXP_P5_DIST:   snprintf(s, n, "gun=%u SP=%u C Kp=%u Ti=%u s", q->p5_days, q->p5_sp_c, q->kp, q->ti_s); break;
    case EXP_P6_VALID:  snprintf(s, n, "aprbs=%u sa kapali=%u sa Kp=%u Ti=%u s", q->p6_aprbs_h, q->p6_closed_h, q->kp, q->ti_s); break;
    default:            snprintf(s, n, "-"); break;
  }
}

static void ResetControllers(void)
{
  PID_Init(&pid, (float)R.pr.kp, (float)R.pr.ti_s, 0.0f, -1000.0f, 1000.0f);
  OnOff_Init(&onoff, 0.5f, 1000, -1000);
  last_ctrl = 0xFF;
  last_closed = 0xFF;
  no_air_s = 0;
}

static void BeginProc(Exp_Proc p, uint32_t now, const DS3231_TimeDate *rtc)
{
  char s[96], prm[72];
  R.proc = p;
  R.proc_start = now;
  R.paused_total = 0;
  R.step_idx = 0;
  tod_at_start = Tod(rtc);
  ResetControllers();
  ParamText(prm, sizeof(prm), p);
  if (R.full) snprintf(s, sizeof(s), "# DENEY ASAMA %u/%u: %s, %s", R.stage + 1U, FULL_STAGES, Exp_ProcName(p), prm);
  else        snprintf(s, sizeof(s), "# DENEY BASLADI: %s, %s", Exp_ProcName(p), prm);
  Comment(s);
}

static uint32_t Elapsed(uint32_t now)
{
  uint32_t ref = (R.state == EXP_PAUSED) ? R.paused_at : now;
  uint32_t used = R.proc_start + R.paused_total;
  return (ref > used) ? (ref - used) : 0U;     /* saat geri alinirsa 0 */
}

static void DoResume(uint32_t now)
{
  char s[96];
  need_resume = 0;
  if (R.state != EXP_RUNNING && R.state != EXP_PAUSED) return;
  st.resumed = 1U;
  ResetControllers();
  if (R.state == EXP_RUNNING && Proc_IsStepBased((Exp_Proc)R.proc))
  {
    /* Yarim kalan basamak bastan: baslangici, o basamak 'simdi' baslayacak sekilde kaydir */
    R.proc_start = now - R.paused_total - Proc_StepOffset((Exp_Proc)R.proc, &R.pr, R.step_idx);
    snprintf(s, sizeof(s), "# DENEY DEVAM: %s, enerji kesintisi sonrasi basamak %u bastan",
             Exp_ProcName((Exp_Proc)R.proc), R.step_idx);
    Save();
  }
  else
  {
    snprintf(s, sizeof(s), "# DENEY DEVAM: %s, enerji kesintisi sonrasi saat cizelgesine gore (gecen %lu s)",
             Exp_ProcName((Exp_Proc)R.proc), (unsigned long)Elapsed(now));
  }
  Comment(s);
}

/* ========================================================================= */
void Exp_Init(I2C_HandleTypeDef *hi2c, const DS3231_TimeDate *rtc, uint8_t rtc_ok)
{
  memset(&st, 0, sizeof(st));
  ee_ok = AT24C32_Init(&hee, hi2c);
  Load();
  st.state  = (Exp_State)R.state;
  st.proc   = (Exp_Proc)R.proc;
  st.full   = R.full;
  st.stage  = R.stage;
  st.stages = FULL_STAGES;
  printf("EXP: %s, %s\r\n", Exp_StateName(st.state), Exp_ProcName(st.proc));
  ResetControllers();
  need_resume = (R.state == EXP_RUNNING || R.state == EXP_PAUSED) ? 1U : 0U;
  if (need_resume && rtc_ok)
  {
    tod_at_start = Tod(rtc);
    DoResume(Epoch(rtc));
  }
}

uint8_t Exp_Start(uint8_t program, const DS3231_TimeDate *rtc)
{
  uint32_t now = Epoch(rtc);
  if (program < EXP_P1_STATIC || program > EXP_P_COUNT) return 0U;

  R.full  = (program == EXP_P_COUNT) ? 1U : 0U;
  R.stage = 0;
  R.seed  = (now * 2654435761UL) ^ 0xA5A5F00DUL;
  if (R.seed == 0U) R.seed = 1U;
  R.state = EXP_RUNNING;
  if (R.full)
  {
    char s[64];
    snprintf(s, sizeof(s), "# TAM PROTOKOL BASLADI: %u asama", FULL_STAGES);
    Comment(s);
  }
  BeginProc(R.full ? full_stages[0] : (Exp_Proc)program, now, rtc);
  Save();

  if (!LogCtrl_IsActive()) (void)LogCtrl_Start(rtc);   /* deney -> kayit otomatik acilir */

  st.resumed = 0;
  st.state = EXP_RUNNING;
  return 1U;
}

void Exp_Stop(void)
{
  if (R.state == EXP_IDLE) return;
  Comment("# DENEY DURDURULDU (kullanici)");
  R.state = EXP_IDLE;
  R.proc  = EXP_P_NONE;
  R.full  = 0;
  Save();
  Peltier_SetCommand(0);
  st.state = EXP_IDLE;
  st.proc  = EXP_P_NONE;
  st.u = 0; st.closed = 0; st.safety = EXP_SAFE_OK;
}

void Exp_PauseToggle(const DS3231_TimeDate *rtc)
{
  uint32_t now = Epoch(rtc);
  if (R.state == EXP_RUNNING)
  {
    R.state = EXP_PAUSED;
    R.paused_at = now;
    Comment("# DENEY DURAKLATILDI");
  }
  else if (R.state == EXP_PAUSED)
  {
    R.paused_total += (now > R.paused_at) ? (now - R.paused_at) : 0U;
    R.state = EXP_RUNNING;
    ResetControllers();
    Comment("# DENEY DEVAM EDIYOR");
  }
  else return;
  Save();
  st.state = (Exp_State)R.state;
}

void Exp_Acknowledge(void)
{
  if (R.state != EXP_DONE) return;
  R.state = EXP_IDLE;
  R.proc  = EXP_P_NONE;
  R.full  = 0;
  Save();
  st.state = EXP_IDLE;
  st.proc  = EXP_P_NONE;
}

void Exp_MarkEvent(Exp_Event ev, const DS3231_TimeDate *rtc)
{
  static const char *const nm[] = { "", "KAPAK ACILDI", "ISIL YUK", "DIS ORTAM DEGISTI" };
  char s[64];
  st.pending_event = (uint8_t)ev;
  snprintf(s, sizeof(s), "# OLAY %u: %s, %02u:%02u:%02u", (unsigned)ev,
           (ev <= EXP_EVT_AMBIENT) ? nm[ev] : "DIGER", rtc->hours, rtc->minutes, rtc->seconds);
  Comment(s);
}

uint8_t Exp_TakeEvent(void)
{
  uint8_t e = st.pending_event;
  st.pending_event = 0;
  return e;
}

/* ========================================================================= */
void Exp_Task1Hz(const Exp_Inputs *in, const DS3231_TimeDate *rtc, uint8_t rtc_ok)
{
  Proc_Out o;
  uint32_t now, t, tod;
  int16_t u;

  if (!rtc_ok) return;                  /* zaman yoksa son komut korunur */
  now = Epoch(rtc);
  if (need_resume)
  {
    tod_at_start = Tod(rtc);
    DoResume(now);
  }

  st.state  = (Exp_State)R.state;
  st.proc   = (Exp_Proc)R.proc;
  st.full   = R.full;
  st.stage  = R.stage;
  st.stages = FULL_STAGES;

  if (R.state == EXP_IDLE) return;      /* bosta: konsoldan elle komut serbest */

  if (R.state == EXP_DONE || R.state == EXP_PAUSED)
  {
    st.u = 0; st.closed = 0; st.next_change_s = 0;
    st.elapsed_s  = (R.state == EXP_PAUSED) ? Elapsed(now) : st.duration_s;
    Peltier_SetCommand(0);
    return;
  }

  /* ---- CALISIYOR ---- */
  t = Elapsed(now);
  if (t >= Proc_Duration((Exp_Proc)R.proc, &R.pr))
  {
    if (R.full && (uint8_t)(R.stage + 1U) < FULL_STAGES)
    {
      R.stage++;
      BeginProc(full_stages[R.stage], now, rtc);
      Save();
      t = 0;
    }
    else
    {
      Comment(R.full ? "# TAM PROTOKOL TAMAMLANDI" : "# DENEY TAMAMLANDI");
      R.state = EXP_DONE;
      Save();
      st.state = EXP_DONE;
      st.u = 0;
      Peltier_SetCommand(0);
      return;
    }
  }

#if EXP_FAST_TEST
  tod = (tod_at_start + t * EXP_FAST_DIV) % 86400U;
#else
  tod = Tod(rtc);
#endif
  Proc_Eval((Exp_Proc)R.proc, &R.pr, R.seed, t, tod, &o);

  if (Proc_IsStepBased((Exp_Proc)R.proc) && o.phase != R.step_idx)
  {
    R.step_idx = o.phase;               /* kesintide bu basamak bastan alinir */
    Save();
  }

  /* ---- Komut ---- */
  if (o.closed != last_closed || o.ctrl != last_ctrl)
  {
    PID_Reset(&pid);
    OnOff_Reset(&onoff);
    last_closed = o.closed;
    last_ctrl   = o.ctrl;
  }

  if (!o.closed)
  {
    u = o.u;
    no_air_s = 0;
    if (st.safety == EXP_SAFE_NO_AIR) st.safety = EXP_SAFE_OK;
  }
  else if (!in->air_valid)
  {
    if (no_air_s < 0xFFFFU) no_air_s++;
    u = (no_air_s >= EXP_NO_AIR_S) ? 0 : st.u;      /* kisa kesintide son komut */
    if (no_air_s >= EXP_NO_AIR_S && st.safety == EXP_SAFE_OK) st.safety = EXP_SAFE_NO_AIR;
  }
  else
  {
    float pv = (float)in->air_x10 / 10.0f;
    float sp = (float)o.sp_x10 / 10.0f;
    no_air_s = 0;
    if (st.safety == EXP_SAFE_NO_AIR) st.safety = EXP_SAFE_OK;
    if (o.ctrl == EXP_CTRL_ONOFF)
    {
      u = OnOff_Step(&onoff, sp, pv);
    }
    else
    {
      int32_t v = PID_Step(&pid, sp, pv, 1.0f);
      if (o.dither) v += Proc_Dither(R.seed, t);
      if (v > 1000) v = 1000;
      if (v < -1000) v = -1000;
      u = (int16_t)v;
    }
  }

#if EXP_SAFETY_ENABLE
  {
    uint8_t air_hi = (in->air_valid && in->air_x10 > EXP_LIMIT_AIR_C * 10) ? 1U : 0U;
    uint8_t pel_hi = (in->peltier_valid && in->peltier_x100 > EXP_LIMIT_PELTIER_C * 100) ? 1U : 0U;
    if (air_hi || pel_hi)
    {
      if (st.safety == EXP_SAFE_OK || st.safety == EXP_SAFE_NO_AIR)
      {
        char s[80];
        st.safety = air_hi ? EXP_SAFE_AIR_HIGH : EXP_SAFE_PELTIER_HIGH;
        snprintf(s, sizeof(s), "# GUVENLIK: %s, Peltier kapatildi", Exp_SafetyName(st.safety));
        Comment(s);
      }
      trip_at = now;
    }
    else if (st.safety == EXP_SAFE_AIR_HIGH || st.safety == EXP_SAFE_PELTIER_HIGH)
    {
      uint8_t cool_ok = (!in->air_valid || in->air_x10 < (EXP_LIMIT_AIR_C - EXP_SAFETY_HYST_C) * 10) &&
                        (!in->peltier_valid || in->peltier_x100 < (EXP_LIMIT_PELTIER_C - EXP_SAFETY_HYST_C) * 100);
      if (!cool_ok) trip_at = now;
      else if ((now - trip_at) >= EXP_SAFETY_HOLD_S)
      {
        st.safety = EXP_SAFE_OK;
        ResetControllers();
        Comment("# GUVENLIK: normale dondu, deney devam ediyor");
      }
    }
    if (st.safety == EXP_SAFE_AIR_HIGH || st.safety == EXP_SAFE_PELTIER_HIGH) u = 0;
  }
#endif

  Peltier_SetCommand(u);

  st.u             = u;
  st.closed        = o.closed;
  st.sp_x10        = o.sp_x10;
  st.ctrl          = o.ctrl;
  st.phase         = o.phase;
  st.elapsed_s     = t;
  st.duration_s    = Proc_Duration((Exp_Proc)R.proc, &R.pr);
  st.next_change_s = o.next_change_s;
  st.total_left_s  = st.duration_s - t;
  if (R.full)
  {
    for (uint8_t i = (uint8_t)(R.stage + 1U); i < FULL_STAGES; i++) st.total_left_s += Proc_Duration(full_stages[i], &R.pr);
  }
}

/* ========================================================================= */
const Exp_Status *Exp_GetStatus(void) { return &st; }
Exp_Params       *Exp_GetParams(void) { return &R.pr; }
void              Exp_SaveParams(void) { Save(); }

uint32_t Exp_ProgramDuration(uint8_t program)
{
  uint32_t s = 0;
  if (program == EXP_P_COUNT)
  {
    for (uint8_t i = 0; i < FULL_STAGES; i++) s += Proc_Duration(full_stages[i], &R.pr);
    return s;
  }
  return Proc_Duration((Exp_Proc)program, &R.pr);
}

const char *Exp_ProcName(Exp_Proc p)
{
  static const char *const n[] = { "--", "P1 STATIK", "P2 BASAMAK", "P3 APRBS", "P4 KAPALI CEVRIM", "P5 BOZUCU", "P6 DOGRULAMA", "TAM PROTOKOL" };
  return (p <= EXP_P_COUNT) ? n[p] : "?";
}

const char *Exp_StateName(Exp_State s)
{
  static const char *const n[] = { "BOSTA", "CALISIYOR", "DURAKLATILDI", "TAMAMLANDI" };
  return (s <= EXP_DONE) ? n[s] : "?";
}

const char *Exp_SafetyName(Exp_Safety s)
{
  static const char *const n[] = { "OK", "HAVA > 65 C", "PELTIER > 80 C", "HAVA OLCUMU YOK" };
  return (s <= EXP_SAFE_NO_AIR) ? n[s] : "?";
}

void Exp_FormatDuration(char *buf, uint32_t n, uint32_t s)
{
  uint32_t d = s / 86400U, h = (s % 86400U) / 3600U, m = (s % 3600U) / 60U;
  if (d > 0U) snprintf(buf, n, "%lug %02lu:%02lu", (unsigned long)d, (unsigned long)h, (unsigned long)m);
  else        snprintf(buf, n, "%02lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)(s % 60U));
}
