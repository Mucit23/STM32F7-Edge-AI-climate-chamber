# firmware-peltier

Peltier sürücü kartı yazılımı. Kartta STM32F072C8 ve bir H köprüsü var. Ana karttan
(STM32F746G-DISCO, USART6) gelen güç komutunu 10 kHz PWM olarak Peltier'e uygular.

## Donanım

| Pin | Görev |
|---|---|
| PB0 (TIM3_CH3) | H köprüsü A kolu PWM |
| PB1 (TIM3_CH4) | H köprüsü B kolu PWM |
| PB2 | DRIVER_EN (komut 0'dan farklıyken 1) |
| PB10 | LED (normalde 100 ms, zaman aşımında 1 s yanıp söner) |
| PA9 / PA10 | USART1 TX / RX, 115200 8N1 |

- TIM3: 48 MHz, PSC = 0, ARR = 4799, yani 10 kHz PWM.
- Görev oranı en fazla %95 (bootstrap kondansatörünün şarj olabilmesi için).
- Pozitif komut A kolunu, negatif komut B kolunu sürer.

## Haberleşme protokolü

| Yön | Paket | Anlamı |
|---|---|---|
| Ana kart → sürücü | `*<v>#` | Güç komutu, v = −1000 … +1000 (örnek `*-450#`, `*0#`, `*1000#`) |
| Ana kart → sürücü | `*S#` | Durum sorgusu |
| Sürücü → ana kart | `*A<v>#` | Onay: o anda uygulanan değer (her komuta ve `*S#`'ye cevap) |
| Sürücü → ana kart | `*T#` | Zaman aşımı: 2 s komut gelmedi, çıkış kapatıldı |

Ana kart komutu yaklaşık 500 ms'de bir tekrarlar. Sürücü 2 s boyunca komut alamazsa
PWM'i ve DRIVER_EN'i kapatır (güvenlik). Paketlerin sonundaki `\n` okunabilirlik
içindir, alıcı onu yok sayar.

Yön testle doğrulandı: 0 … +1000 ısıtma, 0 … −1000 soğutma. Ana karttaki kontrolcüler
(PID, ON/OFF) bu işaret kuralına göre yazıldı: hata = set − ölçüm > 0 ise ısıtma.

## Derleme
Araçlar: STM32CubeMX 6.15, STM32Cube FW_F0 V1.11.6, Keil MDK 5.

1. `Software.ioc` dosyasını CubeMX'te açıp **Generate Code** deyin. HAL ve CMSIS
   klasörleri repoda yok, CubeMX bunları üretir.
2. `MDK-ARM/Software.uvprojx` projesini Keil'de açıp derleyin.
