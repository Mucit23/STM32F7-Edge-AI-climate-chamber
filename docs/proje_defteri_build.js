const fs = require("fs");
const {
  Document, Packer, Paragraph, TextRun, HeadingLevel, Table, TableRow, TableCell, WidthType,
  ShadingType, AlignmentType, ImageRun, LevelFormat, BorderStyle, TableOfContents, Footer,
  PageNumber, PageBreak,
} = require("docx");

const OUT = process.argv[2];
const IMG = require("path").join(__dirname, "sekiller") + "/";  // node proje_defteri_build.js Proje_Defteri.docx
const FONT = "Calibri";
const W = 9638; // A4, 2 cm kenar boslugu: 11906 - 2*1134

// ---------------- yardimcilar ----------------
const runs = (t) => {
  // **kalin** isaretlerini kalin yaziya cevir
  const parts = String(t).split(/(\*\*[^*]+\*\*|`[^`]+`)/g).filter(Boolean);
  return parts.map((p) => {
    if (p.startsWith("**")) return new TextRun({ text: p.slice(2, -2), bold: true });
    if (p.startsWith("`")) return new TextRun({ text: p.slice(1, -1), font: "Consolas", size: 20 });
    return new TextRun(p);
  });
};
const P = (t, o = {}) => new Paragraph({ children: runs(t), spacing: { after: 120 }, ...o });
const H1 = (t) => new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun(t)] });
const H2 = (t) => new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun(t)] });
const H3 = (t) => new Paragraph({ heading: HeadingLevel.HEADING_3, children: [new TextRun(t)] });
const B = (t, lvl = 0) => new Paragraph({ children: runs(t), numbering: { reference: "bul", level: lvl }, spacing: { after: 60 } });
const N = (t) => new Paragraph({ children: runs(t), numbering: { reference: "num", level: 0 }, spacing: { after: 60 } });
const CODE = (lines) => lines.map((l, i) => new Paragraph({
  children: [new TextRun({ text: l, font: "Consolas", size: 18 })],
  shading: { type: ShadingType.CLEAR, fill: "F2F4F7", color: "auto" },
  spacing: { after: i === lines.length - 1 ? 160 : 0 },
}));
const CAP = (t) => new Paragraph({ children: [new TextRun({ text: t, italics: true, size: 20, color: "555555" })], alignment: AlignmentType.CENTER, spacing: { after: 240 } });
const IMGP = (file, wpx, hpx) => new Paragraph({
  alignment: AlignmentType.CENTER, spacing: { before: 120, after: 60 },
  children: [new ImageRun({ type: "png", data: fs.readFileSync(IMG + file), transformation: { width: wpx, height: hpx } })],
});

const border = { style: BorderStyle.SINGLE, size: 4, color: "B8C2D1" };
const borders = { top: border, bottom: border, left: border, right: border };
function TABLE(header, rows, widths) {
  const total = widths.reduce((a, b) => a + b, 0);
  const scale = W / total;
  const ws = widths.map((w) => Math.round(w * scale));
  ws[ws.length - 1] += W - ws.reduce((a, b) => a + b, 0);
  const cell = (t, i, head) => new TableCell({
    width: { size: ws[i], type: WidthType.DXA }, borders,
    margins: { top: 50, bottom: 50, left: 90, right: 90 },
    shading: head ? { type: ShadingType.CLEAR, fill: "DCE3EE", color: "auto" } : undefined,
    children: [new Paragraph({ children: head ? [new TextRun({ text: t, bold: true, size: 20 })] : runs(t).map((r) => { r.root && 0; return r; }), spacing: { after: 0 } })],
  });
  const mk = (r, head) => new TableRow({ tableHeader: head, children: r.map((t, i) => cell(String(t), i, head)) });
  return new Table({ width: { size: W, type: WidthType.DXA }, columnWidths: ws, rows: [mk(header, true), ...rows.map((r) => mk(r, false))] });
}
const GAP = () => new Paragraph({ children: [], spacing: { after: 120 } });


// ---------------- P2 bolumu (defterde 7.4, ara raporda tek basina) ----------------
function p2Bolum(H, fig0, n0) {
  const A = [];
  A.push(H("P2 – asimetrik basamak testi (24–25.11.2026)"));
  A.push(P("Dosyalar `P2_20261124_0938.txt` + `P2_20261124_0938_2.txt` (86 400 satır sınırında bölündü). 30 sa, 107 999 satır, atlanan saniye 0, güvenlik kesmesi 0, sınırlayıcı devreye girmedi, kapak hiç açılmadı. Parametreler: denge 60 dk, basamak 90 dk, 2 tekrar; sıra [0][+15][0][−50][0][+30][0][−75][0][+50][0][−100] %. Ortam 17.6–24.2 °C arasında değişti. 25.11 01:32'de Ethernet 1 s koptu, 6 s'de Adafruit IO'ya yeniden bağlandı; SD kaydı etkilenmedi."));
  A.push(TABLE(["Komut", "ΔT hava, 1./2. tekrar (K)", "Kazanç (K/%)", "τ, 1./2. (dk)", "Gecikme (s)", "Yüzey − hava (K)", "En yüksek nem (%)"], [
    ["+15 %", "+10.1 / +9.8", "0.66", "21 / 22", "82 / 126", "+2.0 / +2.2", "34"],
    ["+30 %", "+20.9 / +17.9", "0.65", "27 / 17", "64 / 75", "+3.8", "43"],
    ["+50 %", "+29.0 / +29.4", "0.58", "18 / 19", "54 / 53", "+6.0 / +5.8", "35"],
    ["−50 %", "−6.9 / −6.3", "0.13", "20 / 17", "94 / 91", "−0.3", "58"],
    ["−75 %", "−11.4 / −13.6", "0.17", "20 / 25", "70 / 60", "−0.9 / −1.0", "63"],
    ["−100 %", "−15.7 / −13.4", "0.15", "17 / 13", "44 / 48", "−1.7", "77"],
  ], [1, 2.1, 1.2, 1.2, 1.2, 1.5, 1.3]));
  A.push(GAP());
  A.push(P("ΔT: basamağın son 5 dk ortalaması − basamak öncesi son 2 dk ortalaması. τ: 1. derece + ölü zaman modeli uydurması. Gecikme: komuttan sonra havanın 0.3 K değiştiği an (DHT22 0.1 K çözünürlüklü olduğu için üst sınır)."));
  A.push(H3("Bulgular"));
  A.push(B("**Kabin, 1. derece + ölü zaman modeliyle iyi tanımlanıyor:** 12 basamağın hepsinde RMSE 0.05–0.28 K. Baskın zaman sabiti τ ≈ 19 dk (13–27), ölü zaman ~1 dk. İki zaman sabitli model tutarlı bir iyileşme getirmedi; yavaş (duvar) bileşen 90 dk içinde küçük ve ortam değişimiyle karışıyor."));
  A.push(B("**Isıtma neredeyse doğrusal, hafif doyuyor:** 0.66 → 0.58 K/% (+15 → +50 %). +50 %'de yüzey en çok 58.4 °C; P3'teki +60 % için yüzey ~65 °C beklenir, sınırlayıcı (70 °C) yedekte kalır."));
  A.push(B("**Soğutma ısıtmanın ~1/4'ü ve doyuyor:** −50 % → −6.6 K, −75 % → −12.5 K, −100 % → −14.6 K (iki tekrar ortalaması). −75'ten −100 %'e geçiş yalnızca ~2 K kazandırıyor (Joule ısısı). Soğutma tarafı modelde ayrı bir kol olmalı."));
  A.push(B("**\"−25 % ölü bölge\" yeniden yorumlandı:** u = 0'da kabin ortamın 1.3–3.0 K (ort. ~2 K) üstünde dengeleniyor (iç fanın ısısı). P1'deki −25 % ≈ ortam sonucu, dinlenme durumuna göre aslında ~−2 K soğutma demek. Yani −25 % etkisiz değil, iç fan ısısını karşılıyor."));
  A.push(B("**Dinamik yön asimetrisi:** sürülen soğutma basamaklarında τ 13–25 dk, pasif dönüşte (u = 0) ısıtmadan sonra 21–25 dk, soğutmadan sonra 12–19 dk. Kabin soğuktan ortama ısıdan ortama döndüğünden daha hızlı dönüyor."));
  A.push(B("**Tekrarlanabilirlik:** +15 ve +50 %'de fark < 0.4 K; +30, −75, −100 %'de 2–3 K. Farkın kaynağı başlangıç durumu ve ortam değişimi (ör. 1. tekrar +30 %'de ortam 2.9 K yükseldi). Ortam ve başlangıç durumu modelde girdi olmalı."));
  A.push(B("**Ortam sensörü dış soğutucunun sıcak havasından etkileniyor:** −100 % basamaklarında T_ambient 2.5–4 K yükseliyor, basamak bitince 10 dk içinde ~1 K düşüyor; +50 %'de böyle bir etki yok. Ortam modelin bozucu girdisi olacağı için P3'ten önce sensör dış fanın emiş tarafına/uzağına taşınmalı."));
  A.push(B("**Hava homojen:** karşı duvar − hava farkı ±0.4 K. Yüzey − hava farkı komutla orantılı (+50 %'de 6 K, −100 %'de −1.7 K)."));
  A.push(B("**Nem ve yoğuşma:** −100 %'de nem %77'ye çıktı, iç yüzey (9.3 °C) çiğ noktasına ~2 K yaklaştı; soğutma bitince nem kısa süre %80'e sıçradı (yoğuşan suyun buharlaşması)."));
  A.push(B("**PID ayarı:** ölçülen modelle (kazanç tablosu, τ = 19 dk, ölü zaman 60 s) P6 set profili benzetildi. Mevcut Kp = 150 komut/K, Ti = 600 s: ortalama hata 0.47 K, aşma 0.85 K, ±0.5 K'ye oturma 11–38 dk. SIMC/IMC ayarları (Kp 50–100, Ti 1140 s) daha yavaş; Kp 200/Ti 450 benzer. **Karar: mevcut ayar korunuyor.**"));
  A.push(IMGP(fig0, 620, 399));
  A.push(CAP(`Şekil ${n0}. P2 genel görünüm: sıcaklıklar, Peltier komutu, nem.`));
  A.push(IMGP("P2_basamak_tepkileri.png", 620, 331));
  A.push(CAP(`Şekil ${n0 + 1}. P2 basamak tepkileri, iki tekrar üst üste; kesikli çizgi 1. derece + ölü zaman modeli.`));
  A.push(IMGP("P2_kazanc_egrisi.png", 620, 289));
  A.push(CAP(`Şekil ${n0 + 2}. Kazanç eğrisi: P1 #2 (ortama göre, 60 dk) ve P2 (basamak öncesine göre, 90 dk). P2 soğutmada daha büyük çünkü dinlenme durumu ortamın ~2 K üstünde.`));
  A.push(H3("P3 için öneriler"));
  A.push(N("Ortam sensörünü dış fanın sıcak hava çıkışından uzaklaştır (girdi temizliği)."));
  A.push(N("Aralık −100…+60 % uygun (yüzey ~65 °C, sınırlayıcı yedekte)."));
  A.push(N("Tutma süresi 3–40 dk yerine 3–60 dk: en uzun segment ~3τ'ya ulaşır, kalıcı kazanç verisi de toplanır; en kısa 3 dk ölü zamanın 3 katı."));
  A.push(N("Modelde girdi olarak u, T_ambient ve geçmiş T_air; giriş penceresi en az ~1 τ (≈ 20 dk), ısıtma ve soğutma için farklı kazanç."));
  return A;
}

// ---------------- icerik ----------------
const C = [];

C.push(new Paragraph({ heading: HeadingLevel.TITLE, children: [new TextRun("Edge AI İklim Kabini – Proje Defteri")] }));
C.push(P("Deney düzeneği, yazılım, veri formatı, test günlüğü ve kararlar", { spacing: { after: 60 } }));
C.push(P("Tez: **Derin Öğrenme Tabanlı Gömülü Sistemler ile Akıllı Ortam Atmosferi Kontrolü için Gerçek Zamanlı Uç Birim (Edge AI) Sistem Tasarımı ve Gerçekleştirilmesi** (Yüksek Lisans) ve ilişkili TÜBİTAK 1002-A önerisi."));
C.push(P("Son güncelleme: 25.11.2026 (P2 sonuçları). Kaynak kod: https://github.com/Mucit23/STM32F7-Edge-AI-climate-chamber"));
C.push(new TableOfContents("İçindekiler", { hyperlink: true, headingStyleRange: "1-2" }));
C.push(new Paragraph({ children: [new PageBreak()] }));

// 0
C.push(H1("0. Bu belge nasıl kullanılır"));
C.push(P("Bu belge, düzenekte yapılan her şeyin tek yerde tutulduğu yaşayan bir laboratuvar defteridir. Hem tez yazımında (yöntem, deney düzeneği, bulgular bölümleri) hem de çalışmaya ara verildikten sonra kaldığı yerden devam etmek için kullanılır. Kurallar:"));
C.push(B("**Bölüm 1–6** sistemin güncel halini anlatır. Donanım ya da yazılım değişince bu bölümler güncellenir, eski hal Bölüm 9'daki karar günlüğünde kalır."));
C.push(B("**Bölüm 7** kronolojik test günlüğüdür. Her test için: tarih, düzenek durumu, dosya adı, ayarlar, sayısal sonuçlar ve çıkarımlar. Eski kayıtlar silinmez."));
C.push(B("**Bölüm 8** model ve kontrolcü için ölçülmüş parametreleri toplar. Sayılar her zaman hangi testten geldiğiyle birlikte yazılır."));
C.push(B("Sayılar: sıcaklık °C, sıcaklık farkı K, Peltier komutu % (−100…+100; dosyada −1000…+1000). Saatler yerel saat (UTC+3)."));
C.push(B("Belgedeki işaret kuralı: **pozitif komut = ısıtma, negatif komut = soğutma** (18.11.2026'da testle doğrulandı)."));

// 1
C.push(H1("1. Amaç ve kapsam"));
C.push(P("Küçük hacimli bir kabinin iç atmosferini (sıcaklık, nem) Peltier modülüyle kontrol eden, verisini kendisi toplayan ve ileride derin öğrenme modelini doğrudan mikrodenetleyici üzerinde (Edge AI) çalıştıracak bir test düzeneği kurulmaktadır. Çalışmanın aşamaları:"));
C.push(N("Düzeneğin kurulması, sensörlerin ve kayıt altyapısının doğrulanması."));
C.push(N("Sistem tanımlama deneyleri (P1 statik, P2 basamak, P3 APRBS) ile eğitim verisinin toplanması."));
C.push(N("Klasik kontrolcülerle (PID, ON/OFF) karşılaştırma verisinin toplanması (P4, P5)."));
C.push(N("Derin öğrenme modelinin eğitilmesi, STM32F7 üzerinde çalıştırılması ve P6 doğrulama senaryosuyla klasik kontrolcülerle karşılaştırılması."));

// 2
C.push(H1("2. Donanım"));
C.push(H2("2.1 Kabin ve ısıl yapı"));
C.push(B("Kabin: 5 L oto buzdolabı, iç ölçü 135 × 210 × 200 mm. Isıtma ve soğutma dolabın kendi Peltier modülüyle yapılır."));
C.push(B("Dış taraf: dolabın alüminyum soğutucusu ve fanı. **21.11.2026'dan itibaren** fan Peltier'den ayrıldı ve sabit 12 V'a bağlandı (önceden hızı Peltier gücüne bağlıydı)."));
C.push(B("İç taraf: **21.11.2026'ya kadar** iç yüzeyde soğutucu ve fan yoktu, Peltier'in iç plakası doğrudan dolabın iç yüzeyini ısıtıp soğutuyordu. **21.11.2026'da** iç yüzeye termal macunla alüminyum soğutucu ve sabit 12 V fan eklendi."));
C.push(B("Kapak anahtarı (21.11.2026): kapak kapalıyken kontak kapanır."));
C.push(H2("2.2 Elektronik"));
C.push(TABLE(["Birim", "Bağlantı", "Görev / not"], [
  ["STM32F746G-DISCO", "—", "Ana kontrol, 480×272 dokunmatik LCD, SD kart, Ethernet. 216 MHz, Keil MDK (ArmClang 6.24), CubeMX 6.15, FW_F7 1.17.4"],
  ["Peltier sürücü kartı (STM32F072C8)", "USART6, 115200 8N1", "H köprüsü, TIM3 10 kHz PWM, görev oranı ≤ %95. PB0/PB1 kollar, PB2 DRIVER_EN. 2 s komut gelmezse çıkışı kapatır"],
  ["DHT22", "D5", "Kabin havası sıcaklığı ve bağıl nem (kabin ortası)"],
  ["DS18B20", "D3", "Peltier iç yüzeyi (iç soğutucu tabanı)"],
  ["DS18B20", "D4", "Karşı duvar yüzeyi"],
  ["DS18B20", "D2", "Dış ortam (dolap dışı)"],
  ["DS3231 + AT24C32", "I2C1", "Gerçek zaman saati; EEPROM'da kayıt ve deney durumu (çift kopya + CRC)"],
  ["Kapak anahtarı", "D7 (PI3)", "0 = kapalı, 1 = açık; iç pull-up, 50 ms debounce"],
  ["SD kart", "SDMMC1", "FAT32 (exFAT desteklenmiyor), uzun dosya adı açık"],
  ["Ethernet", "RMII, LAN8742", "LwIP (RTOS'suz), DHCP; Adafruit IO'ya MQTT"],
  ["Güç kaynağı", "—", "12 V 20 A ayarlı SMPS (10–14.7 V), şebekeden; akü yok. Gerilim ölçülmüyor (v_bus sütunu boş)"],
], [2.2, 1.6, 5]));
C.push(GAP());
C.push(P("Peltier sürücü protokolü: ana kart `*<v>#` gönderir (v = −1000…+1000), sürücü `*A<v>#` ile onaylar; `*S#` durum sorgusu, `*T#` zaman aşımı bildirimi. Ana kart komutu yaklaşık 500 ms'de bir tekrarlar."));

// 3
C.push(H1("3. Yazılım"));
C.push(H2("3.1 Modüller (firmware-f746/Core)"));
C.push(TABLE(["Dosya", "Görev"], [
  ["main.c", "Başlatma, ana döngü, 1 Hz örnekleme (RTC saniyesine kilitli), log satırı, LCD güncelleme, kapak anahtarı"],
  ["exp.c / exp.h", "Deney yöneticisi: RTC tabanlı durum makinesi, EEPROM'da kalıcı durum, kesinti sonrası devam, güvenlik, yüzey sınırlayıcısı"],
  ["exp_proc.c", "P1–P6 profillerinin durumsuz üreticileri (zamana göre komut/set değeri)"],
  ["control.c", "PID (koşullu integral anti-windup, türev ölçümden), ON/OFF (±0.5 °C histerezis), yüzey sınırlayıcısı (hız biçimli PI)"],
  ["sdlog.c", "SD kayıt: deney dosyaları ve günlük dosyalar, açıklama satırı kuyruğu, kart tak/çıkar"],
  ["logctrl.c", "Kayıt aç/kapa durumunun EEPROM'da saklanması"],
  ["ui.c, screen.c, touch.c", "LCD çizimi, dokunmatik ekran durum makinesi, menüler, ekran ışığı zaman aşımı"],
  ["net.c, aio.c", "Ağ durumu (kablo, DHCP), Adafruit IO MQTT istemcisi"],
  ["peltier.c", "Peltier sürücü kartıyla haberleşme"],
], [2, 7]));
C.push(GAP());
C.push(H2("3.2 Önemli mekanizmalar"));
C.push(B("**Örnekleme:** her gerçek saniye için tam bir satır. 1 Hz blok RTC saniyesi değişince çalışır (19.11.2026'dan itibaren). Önceki sürümde HAL tick ile 1000 ms beklemek 22 saatte 564 saniyeyi atlatıyordu."));
C.push(B("**Kesinti sonrası devam:** deney durumu EEPROM'da. P3, P4, P5, P6 RTC saatine göre kaldığı yerden devam eder; P1 ve P2'de yarım kalan basamak baştan alınır. Kayıt aynı deney dosyasına eklenir."));
C.push(B("**Güvenlik kesmesi:** kabin havası > 70 °C (23.11'e kadar 65 °C) veya Peltier yüzeyi > 80 °C ise çıkış 0 yapılır, 10 dakika sonra yeniden açılır. Kapalı çevrimde hava ölçümü 30 s gelmezse çıkış 0."));
C.push(B("**Yüzey sınırlayıcısı (19.11.2026):** yüzey ısıtmada 70 °C'ye, soğutmada +1 °C'ye yaklaşınca güç kapatılmaz, kademeli kısılır. Hız biçimli PI: izin += Kp·(Δmesafe) + Kp/Ti·mesafe·dt; ısıtmada Kp = 40 komut/K (4 %/K), soğutmada 100 komut/K; Ti = 300 s; devreye giriş 64 °C / 5 °C. PID'ye anlık sınırlar verilir (integral şişmez). Deneyin istediği komut `u_req`, uygulanan `u_cmd` sütununa yazılır."));
C.push(B("**Ekran ışığı:** 5 dk dokunulmazsa kapanır (PK3); deney durumu, güvenlik, kapak açılması ve mavi buton ışığı açar."));
C.push(B("**Uzaktan izleme:** 20 s'de bir Adafruit IO'ya gönderim, web sayfası (Bölüm 6)."));

// 4
C.push(H1("4. Veri kaydı formatı"));
C.push(H2("4.1 Dosyalar"));
C.push(B("Deney dosyası: `P<n>_YYYYMMDD_HHMM.txt` (deney ve başlangıç zamanı). Tam protokolde aşama başına `T<aşama>P<n>_...` (örnek `T3P3_20261201_1000.txt`)."));
C.push(B("86 400 satırda (1 gün) `_2`, `_3` … ile devam eder; gece yarısında bölünmez. Analizde parçalar birleştirilir."));
C.push(B("Deney yokken (KAYIT BAŞLAT ile) günlük dosya: `LOG_YYYYMMDD.txt`, gece yarısında yeni dosya."));
C.push(B("Satırlar CSV; `#` ile başlayan satırlar açıklamadır (kayıt başladı/devam, deney başladı/bitti, aşama, olay, ağ, güvenlik)."));
C.push(H2("4.2 Sütunlar"));
C.push(TABLE(["Sütun", "Birim", "Anlamı"], [
  ["date, time", "gg/aa/yyyy, ss:dd:sn", "RTC zamanı (yerel)"],
  ["uptime_s", "s", "Kart açılışından beri süre (HAL tick; DHT22 okumasında kesme kapandığı için ~%0.15 yavaş, zaman ekseni olarak kullanılmaz)"],
  ["T_air_dht, RH_dht", "°C, %", "Kabin havası ve bağıl nem (DHT22, 0.1 çözünürlük)"],
  ["T_heatsink", "°C", "Peltier iç yüzeyi (D3, 0.0625 çözünürlük)"],
  ["T_wall, T_ambient", "°C", "Karşı duvar (D4), dış ortam (D2)"],
  ["u_cmd", "−1000…+1000", "Peltier'e uygulanan komut (sınırlayıcı ve güvenlik sonrası). Model girdisi olarak bu kullanılır"],
  ["peltier_ok", "0/1", "Sürücü kartıyla haberleşme"],
  ["proc", "0–6", "Çalışan deney (0 yok, 1 P1 … 6 P6)"],
  ["phase", "—", "Deney içi basamak/segment numarası (P1'de 0–21)"],
  ["setpoint", "°C", "Kapalı çevrimde set değeri, aksi halde boş"],
  ["event", "kod", "1 kapak açıldı, 4 kapak kapandı (otomatik), 2 ısıl yük, 3 dış ortam değişti, 9 diğer (elle)"],
  ["safety", "kod", "0 normal, 1 hava yüksek, 2 yüzey yüksek, 3 hava ölçümü yok"],
  ["v_bus", "—", "Ayrılmış (besleme gerilimi), şu an boş"],
  ["u_req", "−1000…+1000", "Deneyin istediği komut (19.11'den itibaren); deney yokken boş"],
  ["door", "0/1", "Kapak durumu her saniye (21.11'den itibaren)"],
], [2.1, 1.8, 5.5]));
C.push(GAP());
C.push(P("Sütunlar sürüm içinde sona eklenerek genişledi; eski dosyalarda son sütunlar yoktur. Analizde sütunlar başlık satırındaki adlarla okunur (`analysis/plot_run.py`)."));

// 5
C.push(H1("5. Deney protokolü"));
C.push(TABLE(["Deney", "Tanım", "Varsayılan", "Süre"], [
  ["P1 Statik", "Seviyeler 0, +25, +50, +75, +100, 0, −25, −50, −75, −100, 0 %; her biri sabit tutulur", "60 dk × 2 tekrar", "22 sa"],
  ["P2 Basamak", "Asimetrik genlik çiftleri: [0][+15] [0][−50] [0][+30] [0][−75] [0][+50] [0][−100] % (24.11'den itibaren; önceden ±25/50/100)", "denge 60 dk, basamak 90 dk, × 2 (cihaz varsayılanı 30/60/3)", "30 sa"],
  ["P3 APRBS", "10'luk adımlarla rastgele seviye −100…+H %, rastgele tutma süresi (deterministik xorshift32, kesintide yeniden üretilir). H = ısıtma üst sınırı (24.11'den itibaren; önceden simetrik ±100)", "10 gün, tutma 3–40 dk, H = %60", "10 gün"],
  ["P4 Kapalı çevrim", "Günlere göre dönen profiller: basamak tablosu, rampa (0.05 °C/dk), gece/gündüz (18/28 °C). Kontrol PID/ON-OFF dönüşümlü (3 günde bir)", "7 gün, karışık", "7 gün"],
  ["P5 Sabit set", "PID ile sabit set değeri", "25 °C, 5 gün", "5 gün"],
  ["P6 Doğrulama", "Sabit tohumlu APRBS (P3 ile aynı −100…+H aralığı) + set değerleri {22, 32, 17, 27, 37, 24} °C (2.5 sa)", "36 sa + 36 sa", "3 gün"],
  ["TAM PROTOKOL", "P1 → P2 → P3 → P4 → P6", "—", "~22 gün"],
], [1.6, 4.6, 2.2, 0.9]));
C.push(GAP());
C.push(P("PID başlangıç kazançları Kp = 150 komut/K, Ti = 600 s (P2 verisinden yeniden ayarlanacak). P2 ve P3 aralıklarının gerekçesi Bölüm 9 ve 10'dadır. P3 ısıtma üst sınırı parametre sayfasından (\"Isitma ust (%)\") değiştirilebilir; soğutma alt sınırı sabit −100 % (`EXP_APRBS_COOL_PCT`)."));

// 6
C.push(H1("6. Uzaktan izleme"));
C.push(B("Adafruit IO (ücretsiz hesap, kullanıcı Mucit23), grup `iklim`. Kart 20 s'de bir tek grup mesajı gönderir (MQTT, port 1883, QoS 1)."));
C.push(B("Feed'ler (10, ücretsiz sınır): t-air, rh, t-peltier, t-wall, t-ambient, u-cmd (%), setpoint, safety, status (metin), sd (durum|dosya|kayıt|hata|boş MB|boyut KB). Feed'ler public."));
C.push(B("Sınırlar: dakikada 30 veri noktası (kullanım ~25), veri saklama 30 gün. Kalıcı ve 1 s çözünürlüklü veri SD karttadır."));
C.push(B("Web sayfası: https://mucit23.github.io/STM32F7-Edge-AI-climate-chamber/ (repo `docs/index.html`). Anahtarsız okur; 1 sa/6 sa ham veri, 24 sa/7 g/30 g ortalamalı grafikler."));
C.push(B("Adafruit IO anahtarı yalnızca yerel `Core/Inc/netcfg.h` dosyasındadır, repoya girmez."));

// 7
C.push(new Paragraph({ children: [new PageBreak()] }));
C.push(H1("7. Test günlüğü"));

C.push(H2("7.1 P1 #1 – fansız iç yüzey (18–19.11.2026)"));
C.push(P("Dosya `P1_20261118_1656.txt` (22 sa, 78 637 satır). Düzenek: iç soğutucu/fan yok, dış fan Peltier'e bağlı. Yazılım: sınırlayıcı yok, 80 °C kesme."));
C.push(TABLE(["Komut", "Hava − ortam (K)", "Yüzey − hava (K)", "Not"], [
  ["+25 %", "+10.5", "+13.6", ""],
  ["+50 %", "+20.2", "+23.9", "yüzey 64 °C"],
  ["+75 %", "—", "—", "yüzey ~20 dk'da 80 °C, koruma"],
  ["+100 %", "—", "—", "koruma, basamak boyunca"],
  ["−25 %", "−0.7", "−1.3", ""],
  ["−50 %", "−4.2", "−4.8", ""],
  ["−75 %", "−7.4", "−8.8", ""],
  ["−100 %", "−10.7", "−13.3", "yüzey −3.4 °C, buzlanma"],
], [1.3, 2, 2, 4]));
C.push(GAP());
C.push(B("Kabin havası zaman sabiti (t63) 22–25 dk, iç yüzey 4–5 dk. 60 dk sonunda hava hâlâ ~0.04 K/dk değişiyordu."));
C.push(B("Koruma toplam 7 217 s, 11 kez kesip açtı (testere dişi veri)."));
C.push(B("Soğutmada yüzey 0 °C altı: nem %51 → %41 düştü (yoğuşma/buz), basamak bitince %80'e sıçradı."));
C.push(B("Kayıtta 564 atlanan saniye (örnekleme hatası, sonra düzeltildi). Kapak açma (09:20) kayda geçti: hava −0.5 K, nem +2.6 puan."));
C.push(B("**Çıkarım:** iç yüzeyden havaya ısı aktarımı zayıf; ısıtma soğutmadan çok güçlü; koruma veriyi bozuyor → iç soğutucu + fan ve yüzey sınırlayıcısı kararı."));
C.push(IMGP("P1_20261118_1656.png", 620, 399));
C.push(CAP("Şekil 1. P1 #1 (fansız): sıcaklıklar, Peltier komutu, nem."));

C.push(H2("7.2 P1 kısmi – iç soğutucu + fan, ısıtma (21.11.2026)"));
C.push(P("Dosya `P1_20261121_1208.txt` (5 sa, 18 483 satır, ısıtma basamakları). İlk kez yüzey sınırlayıcısı, `u_req` ve kapak anahtarı ile."));
C.push(B("+25 %: hava − ortam +15.2 K, yüzey − hava 3.4 K (eskiden 13.6). +50 %: +28.7 K, 6.1 K (eskiden 23.9)."));
C.push(B("+75/+100 %: sınırlayıcı yüzeyi 70.0 ± 0.13 °C'de tuttu (en yüksek 71.4), uygulanan güç ~%68; kabin 62 °C, nem %9. 80 °C kesmesi yok."));
C.push(B("Atlanan saniye 0 (RTC kilidi çalışıyor). Kapak açılışında hava 3–5 K düştü, ~10 dk'da toparlandı."));
C.push(IMGP("P1_20261121_1208.png", 620, 399));
C.push(CAP("Şekil 2. Fanlı yapıda ilk ısıtma basamakları ve sınırlayıcının devreye girişi."));

C.push(H2("7.3 P1 #2 – iç soğutucu + fan, tam (23–24.11.2026)"));
C.push(P("Dosya `P1_20261123_0958.txt` (22 sa, 79 199 satır, atlanan saniye 0, güvenlik kesmesi 0, kapak 1 kez 15 s). Kabin hava kesmesi 70 °C. Ortam 17–22 °C arasında değişti."));
C.push(TABLE(["Komut", "Hava − ortam, 1./2. tekrar (K)", "Yüzey − hava (K)", "Hava t63 (dk)", "Not"], [
  ["+25 %", "+16.5 / +15.9", "+3.3", "18", ""],
  ["+50 %", "+29.8 / +29.8", "+6.0", "17", ""],
  ["+75 %", "+41.6 / +42.0", "+8.4", "17", "sınırlayıcı kısmen"],
  ["+100 %", "+40.1 / +43.4", "+8.2", "—", "sınırlayıcı, uygulanan ~%70"],
  ["−25 %", "+0.1 / −0.7", "+0.2", "19", "ölü bölge"],
  ["−50 %", "−3.1 / −4.4", "−0.3", "12–17", ""],
  ["−75 %", "−8.1 / −8.3", "−1.0", "17–32", ""],
  ["−100 %", "−12.8 / −12.4", "−1.7", "16–19", "hava 5.4 °C, yüzey min 3.7 °C"],
], [1.1, 2.6, 1.6, 1.3, 2.6]));
C.push(GAP());
C.push(B("Tekrarlanabilirlik çok iyi (çoğu basamakta < 0.5 K fark)."));
C.push(B("Isıtma ~0.6 K/% ile neredeyse doğrusal (%75'e kadar). Soğutma doğrusal değil: −25 % ölü bölge, etki −50 %'den sonra artıyor. %50'de ısıtma/soğutma oranı ~7–9."));
C.push(B("İç fan soğutmayı da iyileştirdi (−10.7 → −12.6 K) ve yüzey-hava farkını küçülttü; buzlanma yok. Yoğuşma sürüyor: −100 % bitince nem %82 → %95."));
C.push(B("60 dk basamak dengeye yetmiyor: basamak sonunda hava 1–4 K/sa değişiyor. Kalıcı durum değerleri tablodakinden birkaç K büyük olabilir."));
C.push(IMGP("P1_20261123_0958.png", 620, 399));
C.push(CAP("Şekil 3. P1 #2 (fanlı, tam): iki tekrar, ısıtma ve soğutma."));
C.push(IMGP("P1_kazanc_egrisi.png", 620, 238));
C.push(CAP("Şekil 4. Basamak sonu kazanç eğrisi: eski (fansız) ve yeni (iç soğutucu + fan) yapı."));
C.push(...p2Bolum((t) => H2("7.4 " + t), "P2_20261124_0938.png", 5));

// 8
C.push(H1("8. Ölçülmüş model parametreleri"));
C.push(TABLE(["Parametre", "Değer", "Kaynak"], [
  ["Isıtma kazancı", "0.66 K/% (+15 %) → 0.58 K/% (+50 %); ~0.6 K/% (0…+75 %)", "P2, P1 #2"],
  ["Soğutma kazancı (dinlenme durumuna göre)", "−50 %: 0.13; −75 %: 0.17; −100 %: 0.15 K/% (−75 → −100 % doyum)", "P2"],
  ["Dinlenme ofseti (u = 0)", "Hava ortamın +1.3…+3.0 K üstünde (iç fan ısısı)", "P2"],
  ["Ölü zaman", "~1 dk (DHT22 0.1 K adımıyla 44–126 s)", "P2"],
  ["Kabin havası zaman sabiti", "τ ≈ 19 dk (13–27), 1. derece + ölü zaman RMSE ≤ 0.28 K; pasif dönüş: ısıdan 21–25, soğuktan 12–19 dk; fansız yapıda 22–25 dk", "P2, P1"],
  ["İç yüzey zaman sabiti", "4–5 dk (fansız); fanlı yapıda havayla birlikte (~13–16 dk)", "P1 #1, #2"],
  ["Yüzey − hava farkı", "+50 %'de 6 K, −100 %'de −1.7 K (fanlı)", "P1 #2"],
  ["Ulaşılabilir aralık (ortam ~20 °C)", "~7 °C … ~62 °C", "P1 #2"],
  ["PID (kapalı çevrim, benzetim)", "Kp = 150 komut/K, Ti = 600 s korunuyor: P6 profilinde ort. hata 0.47 K, aşma 0.85 K", "P2 modeli"],
  ["Kapak açılması", "Hava −3…−5 K, ~10 dk toparlanma", "P1 kısmi"],
  ["Sınırlayıcı ayarı (fansız modelden, IMC)", "Kc = τ/(K·λ) = 300/(0.9·120) ≈ 2.8 %/K → 4 %/K seçildi; Ti = 300 s", "P1 #1"],
], [3.2, 4.2, 1.5]));
C.push(GAP());

// 9
C.push(H1("9. Karar günlüğü"));
C.push(TABLE(["Tarih", "Karar", "Gerekçe"], [
  ["16.11", "Her deney kendi dosyasında (P<n>_tarih_saat), günlük LOG_ dosyası yalnızca deney dışı", "Tam protokolde dosya-deney eşleşmesi"],
  ["16.11", "Güvenlik sınırları hava 65 °C, yüzey 80 °C; batarya yok", "Atölye, sürekli şebeke"],
  ["18.11", "Adafruit IO + GitHub Pages ile uzaktan izleme", "CGNAT, statik IP yok; giden bağlantı çalışır"],
  ["19.11", "İç soğutucu + fan, iki fan sabit 12 V", "P1 #1: yüzey-hava farkı 25 K, koruma kesmeleri; fan hızı gizli girdiydi"],
  ["19.11", "Yüzey PI sınırlayıcısı 70 °C / +1 °C, u_req sütunu", "Kapat-aç koruması testere dişi veri üretiyordu; buzlanma"],
  ["19.11", "1 Hz örnekleme RTC saniyesine kilitli", "564 atlanan saniye"],
  ["19.11", "v_bus ölçülmeyecek", "Akü yok, SMPS sabit; etki ihmal edilebilir"],
  ["21.11", "Kapak anahtarı D7, otomatik olay + door sütunu", "Elle olay işaretleme güvenilmez"],
  ["23.11", "Kabin hava kesmesi 65 → 70 °C", "Fanlı yapıda +75/+100 %'de kabin ~62 °C"],
  ["23.11", "P1 tam ölçülmeden P3 aralığı değiştirilmeyecek", "Soğutma kapasitesi ölçülmeliydi"],
  ["24.11", "P3 ve P6 APRBS aralığı −100…+60 %", "P1 #2: ısıtma/soğutma oranı 7–9; simetrik aralıkta zamanın %27'si 40 °C üstü (benzetim)"],
  ["24.11", "P2 asimetrik: +15/+30/+50 %, −50/−75/−100 %", "−25 % ölü bölge; benzer büyüklükte sıcaklık değişimi"],
  ["24.11", "Deney kaydı EEPROM sürümü 3", "Eski kayıtta P3 genliği simetrikti; yüklemede %60'a çevrilir"],
  ["24.11", "P2 parametreleri denge 60 / basamak 90 dk / 2 tekrar (30 sa)", "P1: τ ≈ 18 dk; 30/60 dk dengeye ve dönüşe yetmiyordu"],
  ["25.11", "PID Kp 150 / Ti 600 s değişmiyor", "P2 modeliyle benzetimde alternatiflerden iyi veya eşit"],
  ["25.11", "\"−25 % ölü bölge\" yorumu geri alındı", "u = 0'da kabin ortamın ~2 K üstünde; −25 % bu ofseti karşılıyor"],
  ["25.11", "Önerilen: ortam sensörü taşınacak, P3 tutma 3–60 dk", "−100 %'de T_ambient 2.5–4 K yükseliyor; en uzun segment ~3τ olmalı"],
], [0.9, 4.3, 4]));
C.push(GAP());

// 10
C.push(H1("10. Açık konular ve sonraki adımlar"));
C.push(B("**P3 APRBS aralığı (24.11'de uygulandı):** −100…+60 %. Ölçülen kazançlarla 10 günlük benzetim (aşağıda) zamanın %90'ının 10–40 °C arasında geçtiğini gösteriyor (simetrik aralıkta %72). P3 sonrası gerçek dağılım kontrol edilmeli."));
C.push(TABLE(["APRBS aralığı", "Ort. kabin", "Ortam altında", "10–40 °C", "40 °C üstü"], [
  ["−100…+100 %", "31 °C", "%24", "%72", "%27"],
  ["−100…+75 %", "26 °C", "%36", "%85", "%14"],
  ["−100…+60 %", "24 °C", "%41", "%90", "%9"],
  ["−100…+50 %", "22 °C", "%48", "%94", "%4"],
], [2, 1.5, 1.6, 1.4, 1.4]));
C.push(GAP());
C.push(B("**P2 tamamlandı (25.11):** sonuçlar 7.4'te. Sıradaki test P3 APRBS (10 gün). Öncesinde ortam sensörünün yeri ve tutma süresi (3–60 dk) kararı."));
C.push(B("**Kabin hava sınırlayıcısı:** ısıtmada kabin havasını ≤ 50 °C tutan ikinci sınırlayıcı önerildi; şimdilik eklenmedi (60 °C donanım için sorun değil)."));
C.push(B("**Nem/yoğuşma:** −100 %'de iç soğutucuda yoğuşma (P2'de yüzey çiğ noktasına ~2 K); nem modelde girdi olmalı."));
C.push(B("**PID ayarı:** P2 modeliyle kontrol edildi, mevcut ayar korunuyor; P4/P5 gerçek verisiyle doğrulanacak."));
C.push(B("Adafruit IO anahtarı sohbet geçmişinde açık yazıldı; tez bitiminde veya kod paylaşımından önce yenilenmeli."));

// 11
C.push(H1("11. Dosyalar ve konumlar"));
C.push(TABLE(["Ne", "Nerede"], [
  ["Ana kart projesi", "Dropbox\\Yuksek Lisans\\Software\\STM32F7 Proje (Keil: MDK-ARM\\Test.uvprojx, CubeMX: Test.ioc)"],
  ["Peltier sürücü projesi", "Dropbox\\Yuksek Lisans\\Software\\Peltier Driver\\Software"],
  ["Test verileri", "Dropbox\\Yuksek Lisans\\Test Datası"],
  ["Kaynak kod (git)", "github.com/Mucit23/STM32F7-Edge-AI-climate-chamber (firmware-f746, firmware-peltier, docs, analysis)"],
  ["Analiz betiği", "analysis/plot_run.py: `python plot_run.py P1_....txt` → özet tablo + grafik"],
  ["Web izleme", "mucit23.github.io/STM32F7-Edge-AI-climate-chamber"],
], [2.2, 7]));
C.push(GAP());
C.push(P("CubeMX ile yeniden üretmede dikkat: \"Delete previously generated files\" kapalı olmalı (Utilities/Fonts silinir); FATFS USE_LFN açık; `Core/Inc/sys/time.h` (LwIP cc.h uyumu) ve `main.c` içindeki `sio_*` boş fonksiyonları silinmemeli; yeni .c dosyaları Keil'e elle eklenir."));

// ---------------- belge ----------------
const RAPOR = process.argv[3];   // "--p2" -> yalnizca P2 ara raporu
let BODY = C, BASLIK = "Edge AI İklim Kabini – Proje Defteri";
if (RAPOR === "--p2") {
  BASLIK = "Edge AI İklim Kabini – P2 Ara Raporu";
  BODY = [
    new Paragraph({ heading: HeadingLevel.TITLE, children: [new TextRun(BASLIK)] }),
    P("Tarih: 25.11.2026. Bu rapor proje defterinin 7.4 bölümüyle aynı içeriktedir; deney düzeneği, veri formatı ve önceki testler için proje defterine bakınız."),
    ...p2Bolum(H1, "P2_20261124_0938.png", 1),
  ];
}
const doc = new Document({
  features: { updateFields: true },   // Word acilista icindekiler tablosunu gunceller
  creator: "Edge AI İklim Kabini",
  title: BASLIK,
  styles: {
    default: { document: { run: { font: FONT, size: 22 } } },
    paragraphStyles: [
      { id: "Title", name: "Title", basedOn: "Normal", run: { font: FONT, size: 40, bold: true, color: "1F3A5F" }, paragraph: { spacing: { before: 0, after: 120 }, keepNext: true } },
      { id: "Heading1", name: "Heading 1", basedOn: "Normal", next: "Normal", quickFormat: true, run: { font: FONT, size: 30, bold: true, color: "1F3A5F" }, paragraph: { spacing: { before: 320, after: 140 }, keepNext: true, outlineLevel: 0 } },
      { id: "Heading2", name: "Heading 2", basedOn: "Normal", next: "Normal", quickFormat: true, run: { font: FONT, size: 25, bold: true, color: "2E5A88" }, paragraph: { spacing: { before: 220, after: 100 }, keepNext: true, outlineLevel: 1 } },
      { id: "Heading3", name: "Heading 3", basedOn: "Normal", next: "Normal", quickFormat: true, run: { font: FONT, size: 22, bold: true }, paragraph: { spacing: { before: 160, after: 80 }, keepNext: true, outlineLevel: 2 } },
    ],
  },
  numbering: {
    config: [
      { reference: "bul", levels: [
        { level: 0, format: LevelFormat.BULLET, text: "•", alignment: AlignmentType.LEFT, style: { paragraph: { indent: { left: 540, hanging: 270 } } } },
        { level: 1, format: LevelFormat.BULLET, text: "–", alignment: AlignmentType.LEFT, style: { paragraph: { indent: { left: 1080, hanging: 270 } } } },
      ] },
      { reference: "num", levels: [
        { level: 0, format: LevelFormat.DECIMAL, text: "%1.", alignment: AlignmentType.LEFT, style: { paragraph: { indent: { left: 540, hanging: 300 } } } },
      ] },
    ],
  },
  sections: [{
    properties: { page: { size: { width: 11906, height: 16838 }, margin: { top: 1134, bottom: 1134, left: 1134, right: 1134 } } },
    footers: { default: new Footer({ children: [new Paragraph({ alignment: AlignmentType.CENTER, children: [new TextRun({ text: BASLIK + "   |   ", size: 18, color: "777777" }), new TextRun({ children: [PageNumber.CURRENT], size: 18, color: "777777" })] })] }) },
    children: BODY,
  }],
});
Packer.toBuffer(doc).then((b) => { fs.writeFileSync(OUT, b); console.log("yazildi", OUT); });
