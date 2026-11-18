/* sys/time.h  -  Keil ArmClang icin uyumluluk dosyasi
 *
 * CubeMX'in LwIP cc.h dosyasi, __GNUC__ tanimli derleyicilerde <sys/time.h>'i
 * ister. ArmClang de __GNUC__ tanimlar ama kendi kutuphanesinde bu dosya yoktur.
 * cc.h her "Generate Code"da paket surumuyle yeniden yazildigi icin duzeltme
 * burada, CubeMX'in dokunmadigi Core/Inc/sys altinda yapilir.
 * LWIP_TIMEVAL_PRIVATE 0 oldugu icin struct timeval burada tanimlanmalidir
 * (yalnizca LwIP soket API'si kullanir; projede kapali). */
#ifndef SYS_TIME_COMPAT_H
#define SYS_TIME_COMPAT_H

#include <time.h>

struct timeval
{
  long tv_sec;   /* saniye */
  long tv_usec;  /* mikrosaniye */
};

#endif /* SYS_TIME_COMPAT_H */
