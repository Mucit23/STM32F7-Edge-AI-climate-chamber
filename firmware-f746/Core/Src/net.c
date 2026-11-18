/**
  ******************************************************************************
  * @file    net.c
  * @brief   Ag durumu izleme (Ethernet kablosu, DHCP IP, sunucu durumu).
  *          Durum degisiklikleri konsola ve SD log'a "# AG: ..." yorum satiri
  *          olarak yazilir.
  ******************************************************************************
  */
#include "net.h"
#include "sdlog.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include <stdio.h>
#include <string.h>

extern struct netif gnetif;          /* lwip.c (CubeMX) */

static Net_Status st;
static Net_Srv    srv_logged = NET_SRV_OFF;

static void Log(const char *line)
{
  printf("%s\r\n", line);
  SdLog_Comment(line);
}

void Net_Task1Hz(void)
{
  static uint8_t  first = 1U;
  static Net_Link last_link;
  static uint32_t last_ip;
  char line[80];
  const ip4_addr_t *a = netif_ip4_addr(&gnetif);
  uint32_t ip = ip4_addr_get_u32(a);
  Net_Link link;

  if (!netif_is_link_up(&gnetif))  link = NET_LINK_DOWN;
  else if (ip == 0U)               link = NET_LINK_NOIP;
  else                             link = NET_LINK_UP;

  st.link  = link;
  st.ip[0] = ip4_addr1(a);
  st.ip[1] = ip4_addr2(a);
  st.ip[2] = ip4_addr3(a);
  st.ip[3] = ip4_addr4(a);
  if (st.sent_valid && st.sent_age_s < 0xFFFFFFFFUL) st.sent_age_s++;

  /* Degisiklikleri logla */
  if (first || link != last_link || (link == NET_LINK_UP && ip != last_ip))
  {
    switch (link)
    {
      case NET_LINK_DOWN: snprintf(line, sizeof(line), "# AG: Ethernet kablosu YOK"); break;
      case NET_LINK_NOIP: snprintf(line, sizeof(line), "# AG: Ethernet bagli, DHCP IP bekleniyor"); break;
      default:
        snprintf(line, sizeof(line), "# AG: IP %u.%u.%u.%u (DHCP)", st.ip[0], st.ip[1], st.ip[2], st.ip[3]);
        break;
    }
    Log(line);
    first     = 0U;
    last_link = link;
    last_ip   = ip;
  }

  if (st.srv != srv_logged)
  {
    static const char *const nm[] = { "KAPALI", "BAGLANIYOR", "BAGLANDI", "BAGLANTI HATASI" };
    srv_logged = st.srv;
    snprintf(line, sizeof(line), "# AG: %s %s", NET_SERVER_NAME, nm[st.srv]);
    Log(line);
  }
}

const Net_Status *Net_GetStatus(void)
{
  return &st;
}

uint8_t Net_IsUp(void)
{
  return (st.link == NET_LINK_UP) ? 1U : 0U;
}

void Net_SetServer(Net_Srv s)
{
  st.srv = s;
}

void Net_ServerSent(void)
{
  st.sent_valid = 1U;
  st.sent_age_s = 0U;
}
