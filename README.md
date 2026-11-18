# STM32F7 Edge AI İklim Kabini

Yüksek lisans tezi ve TÜBİTAK 1002-A projesi kapsamında geliştirilen test düzeneği:

> *Derin Öğrenme Tabanlı Gömülü Sistemler ile Akıllı Ortam Atmosferi Kontrolü için
> Gerçek Zamanlı Uç Birim (Edge AI) Sistem Tasarımı ve Gerçekleştirilmesi*

5 litrelik bir oto buzdolabı (iç ölçü 135 × 210 × 200 mm) Peltier modülüyle ısıtılıp
soğutulur. Ölçümler SD karta 1 saniyede bir kaydedilir. Kayıtlar modelin eğitimi ve
kontrolcü testleri için kullanılır.

## Donanım

| Birim | Görev |
|---|---|
| STM32F746G-DISCO | Ana kontrol, 480×272 dokunmatik LCD, SD kart, Ethernet |
| STM32F072C8 Peltier sürücü kartı | H köprüsü, 10 kHz PWM, ana karttan USART6 ile komut |
| DHT22 (D5) | Kabin hava sıcaklığı ve bağıl nem |
| DS18B20 (D3) | Peltier iç soğutucu yüzeyi |
| DS18B20 (D4) | Karşı duvar yüzeyi |
| DS18B20 (D2) | Dış ortam |
| DS3231 + AT24C32 | Gerçek zaman saati, kalıcı ayarlar (I2C1) |
| 12 V 20 A SMPS (10–14,7 V ayarlı) | Peltier beslemesi |

## Klasörler

```
firmware-f746/      STM32F746G-DISCO ana kontrol yazılımı (Keil MDK, CubeMX)
firmware-peltier/   STM32F072 Peltier sürücü kartı yazılımı
docs/               Uzaktan izleme sayfası (index.html, GitHub Pages) ve belgeler
```

## Uzaktan izleme
`docs/index.html` tek dosyalık bir web sayfası. Adafruit IO'daki `iklim` grubunun son
değerlerini 20 s'de bir, grafik verisini 5 dakikada bir REST API'den okur. Sayfa
anahtarsız çalışır: kullanıcının public feed listesinden `iklim.` ile başlayanları alır,
bunun için feed'ler Adafruit IO'da public olmalı (grubun ayrı bir gizlilik ayarı yok).
Yayın: GitHub Pages, `main` dalı, `/docs` klasörü.

## firmware-f746

### Özellikler
- Dokunmatik arayüz: sensör kartları, Peltier çıkışı, deney ve SD durumu, başlıkta ağ durumu
- SD kayıt: günlük `LOG_YYYYMMDD.txt` dosyaları, 1 Hz, elektrik kesintisinden sonra devam
- Deney modu: P1 statik, P2 basamak, P3 APRBS, P4 kapalı çevrim, P5 sabit set değeri,
  P6 doğrulama ve TAM PROTOKOL. RTC tabanlı durum makinesi, durum EEPROM'da saklanır.
- PID (anti-windup) ve histerezisli ON/OFF kontrol
- Ethernet (LwIP, DHCP), başlıkta IP ve sunucu durumu
- Adafruit IO'ya MQTT ile uzaktan izleme: 20 s'de bir `iklim` grubuna gönderim
  (feed'ler: `t-air`, `rh`, `t-peltier`, `t-wall`, `t-ambient`, `u-cmd` [%],
  `setpoint`, `safety`, `status`)

### Derleme
Araçlar: STM32CubeMX 6.15, STM32Cube FW_F7 V1.17.4, Keil MDK 5 (ArmClang V6.24).

1. `firmware-f746/Test.ioc` dosyasını CubeMX'te açıp **Generate Code** deyin.
   HAL, CMSIS ve Middlewares (FatFs, LwIP) klasörleri repoda yok, CubeMX bunları üretir.
2. `Core/Inc/netcfg_example.h` dosyasını `Core/Inc/netcfg.h` adıyla kopyalayıp kendi
   Adafruit IO kullanıcı adınızı ve anahtarınızı yazın, `AIO_ENABLE` değerini 1 yapın.
   `netcfg.h` repoya gönderilmez. Dosya yoksa derleme örnekle yapılır, gönderim kapalı kalır.
3. `MDK-ARM/Test.uvprojx` projesini Keil'de açıp derleyin.

### CubeMX notları
- Project Manager → Code Generator: **"Delete previously generated files when not
  re-generated"** kapalı olmalı. Açık olursa `Utilities/Fonts` silinir.
- FATFS → `USE_LFN` = Enabled (static buffer on BSS). Günlük dosya adları için gerekli.
- `Core/Inc/sys/time.h`: CubeMX'in LwIP `cc.h` dosyası ArmClang ile `sys/time.h` arar.
  Bu dosya o eksikliği giderir. Silmeyin.
- `main.c` içindeki `sio_*` fonksiyonları: CubeMX'in eklediği `slipif.c` için gerekli,
  boş fonksiyonlar.

## Lisans
Henüz belirlenmedi.
