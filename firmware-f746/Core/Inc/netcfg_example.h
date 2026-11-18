/**
  ******************************************************************************
  * @file    netcfg_example.h
  * @brief   Adafruit IO baglanti ayarlari - ORNEK DOSYA.
  *
  *          Bu dosyayi ayni klasore "netcfg.h" adiyla kopyalayin ve kendi
  *          kullanici adinizi / anahtarinizi yazin. netcfg.h .gitignore'da
  *          oldugu icin GitHub'a gonderilmez.
  *
  *          netcfg.h bulunamazsa derleme bu ornekle yapilir ve gonderim
  *          kapali kalir (AIO_ENABLE 0).
  ******************************************************************************
  */
#ifndef __NETCFG_H
#define __NETCFG_H

#define AIO_ENABLE         0                    /* 1: Adafruit IO'ya gonder     */
#define AIO_USERNAME       "kullanici-adi"      /* io.adafruit.com > My Key     */
#define AIO_KEY            "aio_xxxxxxxxxxxxxxxxxxxxxxxxxxxx"

#define AIO_HOST           "io.adafruit.com"
#define AIO_PORT           1883                 /* sifresiz MQTT               */
#define AIO_CLIENT_ID      "f746-iklim-kabini"
#define AIO_GROUP          "iklim"              /* feed'ler bu grupta toplanir  */

/* Ucretsiz hesap: dakikada 30 veri noktasi. Her gonderim 8-9 feed icerir,
 * 20 s aralikla dakikada en fazla ~25 nokta olur. 20'nin altina inmeyin. */
#define AIO_PUB_PERIOD_S   20

#endif /* __NETCFG_H */
