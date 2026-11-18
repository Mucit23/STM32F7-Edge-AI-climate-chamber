/**
  ******************************************************************************
  * @file    aio.c
  * @brief   Adafruit IO MQTT istemcisi (LwIP MQTT, RTOS'suz).
  *
  *  Durum makinesi (Aio_Task1Hz, saniyede bir):
  *    AG_BEKLE  -> ag (IP) hazir olunca DNS
  *    DNS       -> io.adafruit.com cozulur (en fazla 10 s)
  *    BAGLANIYOR-> TCP + MQTT CONNECT (en fazla 15 s)
  *    BAGLI     -> AIO_PUB_PERIOD_S'de bir grup gonderimi (QoS 1)
  *    BEKLEME   -> hata sonrasi artan bekleme: 5, 10, 30, 60 s
  *                 (kullanici adi / anahtar reddi: 120 s)
  *
  *  LwIP geri cagirmalari MX_LWIP_Process() icinde, ana dongu baglaminda
  *  calisir; kesme yoktur, kilit gerekmez.
  *
  *  Adafruit IO kurallari (ucretsiz hesap):
  *    - dakikada 30 veri noktasi; grup gonderimi feed basina 1 sayilir
  *    - dakikada 20'den fazla baglanti denemesi yasaklanir (bekleme bunu onler)
  *    - uyarilar <kullanici>/throttle, hatalar <kullanici>/errors konusuna gelir;
  *      ikisine de abone olunur ve gelen mesaj log'a yazilir
  ******************************************************************************
  */
#include "aio.h"
#include "net.h"
#include "sdlog.h"
#include "lwip/apps/mqtt.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include <stdio.h>
#include <string.h>

#if defined(__has_include)
#  if __has_include("netcfg.h")
#    include "netcfg.h"
#  else
#    include "netcfg_example.h"
#    warning "netcfg.h yok: netcfg_example.h kullaniliyor, Adafruit IO gonderimi kapali"
#  endif
#else
#  include "netcfg.h"
#endif

#ifndef AIO_PUB_PERIOD_S
#define AIO_PUB_PERIOD_S 20
#endif
#if AIO_PUB_PERIOD_S < 15
#error "AIO_PUB_PERIOD_S en az 15 olmali (ucretsiz hesap: dakikada 30 veri noktasi)"
#endif

#define DNS_TIMEOUT_S        10U
#define CONNECT_TIMEOUT_S    15U
#define STATUS_EVERY_N       3U      /* durum metni degismediyse 3 gonderimde bir */
#define PAYLOAD_MAX          400U
#define INMSG_MAX            96U

typedef enum
{
  ST_OFF = 0,
  ST_WAIT_NET,
  ST_DNS,
  ST_DNS_WAIT,
  ST_CONNECTING,
  ST_CONNECTED,
  ST_BACKOFF
} Aio_State;

static const uint16_t backoff_s[] = { 5U, 10U, 30U, 60U };

static mqtt_client_t *client;
static struct mqtt_connect_client_info_t ci;
static ip_addr_t  broker_ip;
static Aio_State  st = ST_OFF;
static uint32_t   st_timer;            /* durumda gecen saniye          */
static uint32_t   wait_s;              /* BEKLEME suresi                */
static uint8_t    backoff_idx;
static uint32_t   pub_timer;
static uint8_t    pub_busy;            /* PUBACK bekleniyor             */
static uint8_t    status_cnt;
static char       last_status[AIO_STATUS_LEN];

static volatile uint8_t dns_done;      /* 1: cozuldu, 2: hata */
static volatile uint8_t conn_event;    /* 1: kabul, 2: ret/kopma */
static volatile mqtt_connection_status_t conn_status;

static char     topic_pub[64];
static char     topic_thr[48];
static char     topic_err[48];
static uint8_t  in_kind;               /* 1: throttle, 2: errors, 0: diger */
static char     in_msg[INMSG_MAX];
static uint16_t in_len;

/* ------------------------------------------------------------------------- */
static void Log(const char *s)
{
  printf("%s\r\n", s);
  SdLog_Comment(s);
}

static void Go(Aio_State s)
{
  st = s;
  st_timer = 0U;
  switch (s)
  {
    case ST_OFF:       Net_SetServer(NET_SRV_OFF);        break;
    case ST_CONNECTED: Net_SetServer(NET_SRV_CONNECTED);  break;
    case ST_BACKOFF:   Net_SetServer(NET_SRV_ERROR);      break;
    default:           Net_SetServer(NET_SRV_CONNECTING); break;
  }
}

static void Backoff(const char *why, uint32_t force_s)
{
  char line[96];
  wait_s = force_s ? force_s : backoff_s[backoff_idx];
  if (!force_s && backoff_idx < (sizeof(backoff_s) / sizeof(backoff_s[0])) - 1U) backoff_idx++;
  snprintf(line, sizeof(line), "# AG: %s %s, %lu s sonra tekrar denenecek",
           NET_SERVER_NAME, why, (unsigned long)wait_s);
  Log(line);
  if (client) mqtt_disconnect(client);    /* baglanti kuruluyorsa da iptal eder */
  pub_busy = 0U;
  Go(ST_BACKOFF);
}

/* ---------------- LwIP geri cagirmalari ---------------- */
static void Dns_Cb(const char *name, const ip_addr_t *ipaddr, void *arg)
{
  (void)name; (void)arg;
  if (ipaddr != NULL) { ip_addr_copy(broker_ip, *ipaddr); dns_done = 1U; }
  else                { dns_done = 2U; }
}

static void Conn_Cb(mqtt_client_t *c, void *arg, mqtt_connection_status_t status)
{
  (void)c; (void)arg;
  conn_status = status;
  conn_event  = (status == MQTT_CONNECT_ACCEPTED) ? 1U : 2U;
}

static void Pub_Cb(void *arg, err_t result)
{
  (void)arg;
  pub_busy = 0U;
  if (result == ERR_OK) Net_ServerSent();
}

static void Sub_Cb(void *arg, err_t result)
{
  (void)arg; (void)result;
}

static void InPub_Cb(void *arg, const char *topic, u32_t tot_len)
{
  (void)arg; (void)tot_len;
  if      (strcmp(topic, topic_thr) == 0) in_kind = 1U;
  else if (strcmp(topic, topic_err) == 0) in_kind = 2U;
  else                                    in_kind = 0U;
  in_len = 0U;
}

static void InData_Cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
  (void)arg;
  while (len-- > 0U)
  {
    char ch = (char)*data++;
    if (ch < ' ' || ch > '~') ch = ' ';
    if (in_len < INMSG_MAX - 1U) in_msg[in_len++] = ch;
  }
  if (flags & MQTT_DATA_FLAG_LAST)
  {
    char line[INMSG_MAX + 32U];
    in_msg[in_len] = '\0';
    snprintf(line, sizeof(line), "# AG: AIO %s: %s",
             (in_kind == 1U) ? "UYARI" : ((in_kind == 2U) ? "HATA" : "MESAJ"), in_msg);
    Log(line);
    in_len = 0U;
  }
}

/* ---------------- Gonderim ---------------- */
/* Sabit noktali sayiyi JSON sayisina cevirir: v=-234, scale=10 -> "-23.4" */
static int Num(char *b, size_t n, int32_t v, int32_t scale)
{
  const char *sg = (v < 0) ? "-" : "";
  uint32_t a = (uint32_t)((v < 0) ? -v : v);
  if (scale == 1)   return snprintf(b, n, "%s%lu", sg, (unsigned long)a);
  if (scale == 10)  return snprintf(b, n, "%s%lu.%lu", sg, (unsigned long)(a / 10U), (unsigned long)(a % 10U));
  return snprintf(b, n, "%s%lu.%02lu", sg, (unsigned long)(a / 100U), (unsigned long)(a % 100U));
}

/* "anahtar":deger ekler (ilk alan degilse basina virgul) */
static size_t AddNum(char *p, size_t pos, uint8_t *first, const char *key, int32_t v, int32_t scale)
{
  char num[20];
  int k;
  Num(num, sizeof(num), v, scale);
  k = snprintf(p + pos, PAYLOAD_MAX - pos, "%s\"%s\":%s", *first ? "" : ",", key, num);
  *first = 0U;
  return (k > 0 && pos + (size_t)k < PAYLOAD_MAX) ? pos + (size_t)k : pos;
}

static size_t AddStr(char *p, size_t pos, uint8_t *first, const char *key, const char *s)
{
  int k = snprintf(p + pos, PAYLOAD_MAX - pos, "%s\"%s\":\"", *first ? "" : ",", key);
  *first = 0U;
  if (k <= 0 || pos + (size_t)k >= PAYLOAD_MAX) return pos;
  pos += (size_t)k;
  for (; *s && pos < PAYLOAD_MAX - 3U; s++)
  {
    char ch = *s;
    if (ch == '"' || ch == '\\') ch = '\'';
    if (ch < ' ' || ch > '~') ch = ' ';
    p[pos++] = ch;
  }
  p[pos++] = '"';
  p[pos] = '\0';
  return pos;
}

static void Publish(const Aio_Data *d)
{
  static char p[PAYLOAD_MAX];
  uint8_t first = 1U;
  size_t pos;
  err_t err;
  uint8_t send_status;

  pos = (size_t)snprintf(p, sizeof(p), "{\"feeds\":{");
  if (d->air_ok)  pos = AddNum(p, pos, &first, "t-air",     d->air_x10,  10);
  if (d->rh_ok)   pos = AddNum(p, pos, &first, "rh",        d->rh_x10,   10);
  if (d->pel_ok)  pos = AddNum(p, pos, &first, "t-peltier", d->pel_x100, 100);
  if (d->wall_ok) pos = AddNum(p, pos, &first, "t-wall",    d->wall_x100, 100);
  if (d->amb_ok)  pos = AddNum(p, pos, &first, "t-ambient", d->amb_x100, 100);
  pos = AddNum(p, pos, &first, "u-cmd", d->u_cmd, 10);           /* -100.0 .. +100.0 % */
  if (d->sp_ok)   pos = AddNum(p, pos, &first, "setpoint",  d->sp_x10,   10);
  pos = AddNum(p, pos, &first, "safety", d->safety, 1);

  /* Durum metni: degisince hemen, degismezse her STATUS_EVERY_N gonderimde bir */
  send_status = (strcmp(d->status, last_status) != 0) || (++status_cnt >= STATUS_EVERY_N);
  if (send_status)
  {
    status_cnt = 0U;
    strncpy(last_status, d->status, sizeof(last_status) - 1U);
    pos = AddStr(p, pos, &first, "status", d->status);
  }
  if (pos + 3U > sizeof(p)) return;
  p[pos++] = '}'; p[pos++] = '}'; p[pos] = '\0';

  err = mqtt_publish(client, topic_pub, p, (u16_t)pos, 1U, 0U, Pub_Cb, NULL);
  if (err == ERR_OK) pub_busy = 1U;
  else
  {
    char line[64];
    snprintf(line, sizeof(line), "# AG: AIO gonderim reddedildi (err %d)", (int)err);
    Log(line);
  }
}

/* ------------------------------------------------------------------------- */
void Aio_Init(void)
{
#if AIO_ENABLE
  snprintf(topic_pub, sizeof(topic_pub), "%s/groups/%s/json", AIO_USERNAME, AIO_GROUP);
  snprintf(topic_thr, sizeof(topic_thr), "%s/throttle", AIO_USERNAME);
  snprintf(topic_err, sizeof(topic_err), "%s/errors",   AIO_USERNAME);

  memset(&ci, 0, sizeof(ci));
  ci.client_id   = AIO_CLIENT_ID;
  ci.client_user = AIO_USERNAME;
  ci.client_pass = AIO_KEY;
  ci.keep_alive  = 60U;

  client = mqtt_client_new();
  if (client == NULL)
  {
    Log("# AG: MQTT istemcisi olusturulamadi (LwIP bellek)");
    Go(ST_OFF);
    return;
  }
  mqtt_set_inpub_callback(client, InPub_Cb, InData_Cb, NULL);
  Go(ST_WAIT_NET);
#else
  (void)InPub_Cb; (void)InData_Cb;           /* gonderim kapali: kullanilmiyor */
  Go(ST_OFF);
#endif
}

void Aio_Task1Hz(const Aio_Data *d)
{
  char line[96];
  err_t err;

  if (st == ST_OFF || client == NULL) return;
  st_timer++;

  /* Ag gittiyse her durumdan AG_BEKLE'ye */
  if (!Net_IsUp() && st != ST_WAIT_NET)
  {
    mqtt_disconnect(client);
    pub_busy = 0U;
    Go(ST_WAIT_NET);
    return;
  }

  switch (st)
  {
    case ST_WAIT_NET:
      if (Net_IsUp()) Go(ST_DNS);
      break;

    case ST_DNS:
      dns_done = 0U;
      err = dns_gethostbyname(AIO_HOST, &broker_ip, Dns_Cb, NULL);
      if (err == ERR_OK)               dns_done = 1U;          /* onbellekte vardi */
      else if (err != ERR_INPROGRESS)  { Backoff("DNS hatasi", 0U); break; }
      Go(ST_DNS_WAIT);
      /* fall through */
    case ST_DNS_WAIT:
      if (dns_done == 1U)
      {
        conn_event = 0U;
        err = mqtt_client_connect(client, &broker_ip, AIO_PORT, Conn_Cb, NULL, &ci);
        if (err != ERR_OK)
        {
          snprintf(line, sizeof(line), "baglanti baslatilamadi (err %d)", (int)err);
          Backoff(line, 0U);
        }
        else Go(ST_CONNECTING);
      }
      else if (dns_done == 2U || st_timer > DNS_TIMEOUT_S)
      {
        Backoff("adi cozulemedi (DNS)", 0U);
      }
      break;

    case ST_CONNECTING:
      if (conn_event == 1U)
      {
        conn_event  = 0U;
        backoff_idx = 0U;
        pub_timer   = AIO_PUB_PERIOD_S;   /* ilk veri hemen gitsin */
        status_cnt  = STATUS_EVERY_N;
        last_status[0] = '\0';
        mqtt_subscribe(client, topic_thr, 0U, Sub_Cb, NULL);
        mqtt_subscribe(client, topic_err, 0U, Sub_Cb, NULL);
        snprintf(line, sizeof(line), "# AG: %s MQTT baglandi (%s:%u)", NET_SERVER_NAME, AIO_HOST, (unsigned)AIO_PORT);
        Log(line);
        Go(ST_CONNECTED);
      }
      else if (conn_event == 2U)
      {
        conn_event = 0U;
        if (conn_status == MQTT_CONNECT_REFUSED_USERNAME_PASS || conn_status == MQTT_CONNECT_REFUSED_NOT_AUTHORIZED_)
        {
          Backoff("kullanici/anahtar reddedildi (netcfg.h)", 120U);
        }
        else
        {
          snprintf(line, sizeof(line), "baglanti reddedildi (kod %d)", (int)conn_status);
          Backoff(line, 0U);
        }
      }
      else if (st_timer > CONNECT_TIMEOUT_S)
      {
        Backoff("baglanti zaman asimi", 0U);
      }
      break;

    case ST_CONNECTED:
      if (conn_event == 2U || !mqtt_client_is_connected(client))
      {
        conn_event = 0U;
        Backoff("baglantisi koptu", 0U);
        break;
      }
      if (++pub_timer >= AIO_PUB_PERIOD_S && !pub_busy)
      {
        pub_timer = 0U;
        Publish(d);
      }
      break;

    case ST_BACKOFF:
      if (st_timer >= wait_s) Go(ST_DNS);
      break;

    default:
      break;
  }
}
