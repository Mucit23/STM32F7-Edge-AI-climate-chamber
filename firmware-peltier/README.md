# firmware-peltier

STM32F072 Peltier surucu karti yazilimi (eklenecek).

Protokol (USART6, 115200 8N1):
- `*<v>#` komut, v = -1000..+1000
- `*A<v>#` onay, `*S#` durum sorgusu, `*T#` zaman asimi bildirimi (2 s komut gelmezse cikis kapanir)
- PWM 10 kHz
