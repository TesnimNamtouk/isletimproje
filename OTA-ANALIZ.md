# MSP430 `.z1` Firmware ELF Analizi — `udp-client.z1` (OTA Gönderici Düğüm)

> **Analiz edilen firmware:** `build/z1/udp-client.z1`  
> **Platform:** Zolertia Z1 (MSP430F2617)  
> **Derleme:** Contiki-NG, msp430-gcc toolchain (Docker: contiker/contiki-ng)  
> **Görev:** OTA (Over-the-Air) firmware transferi — gönderici düğüm (Node ID: 2)

---

## 1. Binary Kimlik Analizi

### Komut ve Çıktı

```bash
$ msp430-readelf -h build/z1/udp-client.z1
```

```
ELF Header:
  Magic:   7f 45 4c 46 01 01 01 ff 00 00 00 00 00 00 00 00
  Class:                             ELF32
  Data:                              2's complement, little endian
  Version:                           1 (current)
  OS/ABI:                            Standalone App
  ABI Version:                       0
  Type:                              EXEC (Executable file)
  Machine:                           Texas Instruments msp430 microcontroller
  Version:                           0x1
  Entry point address:               0x3100
  Flags:                             0x10000001
  Number of section headers:         20
  Number of program headers:         5
```

### Yorum

**Hedef Platform:** `.z1` uzantısı, Zolertia Z1 platformuna derlenmiş firmware anlamına gelir. Z1 üzerinde **MSP430F2617** işlemcisi bulunmaktadır.

**ELF Sınıfı:** `ELF32` — 32-bit ELF dosyası. MSP430X genişletilmiş mimarisini destekler ancak temel adres uzayı 16-bittir.

**Endianness:** `Little-endian` — en düşük anlamlı bayt önce gelir. MSP430 mimarisinin doğal byte sıralamasıdır. Örneğin `0x3100` adresi bellekte `00 31` şeklinde saklanır.

**OS/ABI:** `Standalone App` — herhangi bir işletim sistemi çekirdeği üzerinde değil, doğrudan donanım üzerinde çalışan gömülü uygulama. Contiki-NG burada donanım üstü kooperatif bir zamanlayıcı sağlar.

**Type:** `EXEC` — doğrudan çalıştırılabilir (executable) dosya. Paylaşımlı kütüphane (`.so`) veya yeniden yerleştirilebilir nesne (`.o`) değildir.

**Machine:** `Texas Instruments MSP430` — toolchain'in ürettiği makine kodu yalnızca MSP430 ISA (Instruction Set Architecture) komutlarını içerir.

**Entry Point: `0x3100`** — işlemci sıfırlandıktan sonra ilk çalışacak kodun adresi. Disassembly çıktısına göre burada `__watchdog_support → __init_stack → __do_copy_data → __do_clear_bss → main()` başlangıç zinciri yer almaktadır.

**ABI:** `Standalone` — herhangi bir sistem ABI'si (POSIX, Linux, vb.) yoktur. Fonksiyon çağrı kuralları MSP430-GCC ABI'siyle belirlenir: argümanlar R15→R12 yazmaçlarıyla geçilir, dönüş değeri R15'tedir.

**Compiler İzi / Debug Sembolü:** 20 section başlığının 8'i (`.debug_*`) debug sembolü içerir. Bu, `-g` bayrağıyla derlendiğini, Cooja simülatöründe kaynak satırı eşleme yapılabileceğini gösterir.

**Optimization:** Debug section'larının varlığı ve assembly'nin görece doğrusal yapısı, `-Os` veya `-O2` yerine `-O1` ya da `-O0` seviyesinde derlendiğine işaret eder. Contiki-NG'nin varsayılan derleme bayrağı `-Os` olmakla birlikte simülasyon derlemelerinde debug optimize devre dışı bırakılabilir.

---

## 2. Bellek Kullanım Analizi

### Komut ve Çıktı

```bash
$ msp430-size build/z1/udp-client.z1
```

```
   text     data      bss      dec      hex    filename
  43621      402     5896    49919     c2ff    build/z1/udp-client.z1
```

```bash
$ msp430-readelf -S build/z1/udp-client.z1
```

```
  [Nr] Name        Type     Addr     Size   Flg
  [ 1] .text       PROGBITS 00003100 00a4ce  AX   → 42190 bayt  (Flash)
  [ 2] .rodata     PROGBITS 0000d5d0 000557  A    →  1367 bayt  (Flash)
  [ 3] .data       PROGBITS 00001100 000192  WA   →   402 bayt  (Flash→RAM)
  [ 4] .bss        NOBITS   00001292 001706  WA   →  5894 bayt  (RAM, sıfır)
  [ 5] .noinit     NOBITS   00002998 000002  WA   →     2 bayt  (RAM, reset'te korunur)
  [ 6] .vectors    PROGBITS 0000ffc0 000040  AX   →    64 bayt  (Flash)
```

### Bölüm Anlamları

| Bölüm | Anlam | Nerede? |
|-------|-------|---------|
| `.text` | Derlenmiş makine kodu — tüm fonksiyonlar | Flash (ROM) |
| `.rodata` | Salt-okunur sabitler — string literaller, sabit tablolar | Flash (ROM) |
| `.data` | Başlangıç değeri olan global/statik değişkenler | Flash'ta saklanır, boot'ta RAM'e kopyalanır |
| `.bss` | Başlangıçta sıfır olan global/statik değişkenler | Yalnızca RAM (Flash'ta yer kaplamaz) |
| `.noinit` | Reset'te sıfırlanmayan RAM bölgesi | RAM (watchdog sayacı, vb.) |
| `.vectors` | MSP430 kesme vektör tablosu | Flash (0xFFC0–0xFFFF) |

### Bellek Kullanım Özeti

```
Flash (ROM) Kullanımı:
  .text   : 42.190 bayt  (~41.2 KB)
  .rodata :  1.367 bayt  (~ 1.3 KB)
  .data   :    402 bayt  (~ 0.4 KB) — RAM'e kopyalanacak başlangıç değerleri
  .vectors:     64 bayt
  ─────────────────────────────
  TOPLAM  : 44.023 bayt  (~43.0 KB)

RAM Kullanımı (çalışma zamanı):
  .data   :    402 bayt  (başlangıç değerli değişkenler)
  .bss    :  5.894 bayt  (sıfırlanan değişkenler)
  .noinit :      2 bayt
  ─────────────────────────────
  TOPLAM  :  6.298 bayt  (~ 6.1 KB)
```

### MSP430F2617 ile Karşılaştırma

Z1 üzerindeki MSP430F2617'nin bellek kapasitesi:
- **Flash:** 128 KB → firmware **~43 KB kullanıyor (%33.6)**
- **RAM:** 8 KB → çalışma zamanı **~6.1 KB kullanıyor (%76.3)**

RAM kullanımı dikkat çekici derecede yüksektir. Bu beklenen bir durumdur: Contiki-NG'nin RPL yönlendirme tabloları, UDP soket tamponları, CFS Coffee dosya sistemi yapıları ve OTA transfer durumu değişkenleri RAM'de tutulmaktadır.

### CC1352R ile Karşılaştırmalı Değerlendirme

Bu firmware MSP430 için derlenmiştir; CC1352R ise ARM Cortex-M4F çekirdeğine sahiptir. Doğrudan çalıştırılamaz ancak aynı uygulamanın CC1352R'ye derlenmesi durumunda öngörülen yerleşim şu şekilde olurdu:

```
CC1352R Bellek Haritası (352 KB Flash, 80 KB SRAM):
┌─────────────────────────────────────────┐ 0x00000000
│  ROM (Bootloader, driverlib)            │
├─────────────────────────────────────────┤ 0x0001_0000
│  Flash — Uygulama Kodu (Slot A)        │
│  .text  → ~42 KB                        │  ← udp-client kodu buraya
│  .rodata → ~1.4 KB                     │
│  .vectors → 64 bayt (ARM: 0x00010000)  │
├─────────────────────────────────────────┤ 0x0005_8000
│  Flash — Slot B (OTA yeni firmware)    │
│  (new-firmware.z1 buraya yazılır)      │
├─────────────────────────────────────────┤ 0x0005_6000
│  CCFG (Customer Config)                │
└─────────────────────────────────────────┘ 0x0005_8000

SRAM (80 KB, 0x2000_0000):
┌─────────────────────────────────────────┐ 0x2000_0000
│  .data  → ~402 bayt                    │
│  .bss   → ~5.9 KB                      │
│  Stack  → ~2-4 KB (tahmin)             │
│  Heap   → kullanılmıyor               │
└─────────────────────────────────────────┘ 0x2001_4000
```

CC1352R'nin 352 KB Flash'ı, hem çalışan firmware hem de OTA ile gelen yeni firmware'i aynı anda barındırmaya yeterlidir.

---

## 3. Symbol / Function Analizi

### Komut ve Çıktı

```bash
$ msp430-nm -n build/z1/udp-client.z1 | head -60
$ msp430-readelf -s build/z1/udp-client.z1
```

Sembol tablosu **1132 giriş** içermektedir.

### Önemli Sembol Grupları

**OTA Fonksiyonları (projeye özgü):**
```
ota_crc32_buffer     — tüm firmware imajı için CRC32 hesaplama
ota_metadata_*       — boot metadata yönetimi (slot A/B durumu)
```

**Contiki-NG Süreçleri (PROCESS_THREAD):**
```
udp_client_process   — ana OTA gönderici süreci
accmeter_process     — ivme sensörü süreci (Z1 platformu)
```

**Zamanlayıcılar:**
```
etimer_set/expired   — event timer (Contiki kooperatif zamanlayıcı)
ctimer_*             — callback timer
rtimer_*             — gerçek zamanlı timer (donanım destekli)
clock_*              — sistem saati
```

**Ağ Stack:**
```
simple_udp_*         — UDP soket API
rpl_dag_*            — RPL DAG yönetimi
rpl_icmp6_*          — RPL kontrol mesajları (DIO/DAO/DIS)
rpl_neighbor_*       — komşu tablosu
sicslowpan_*         — 6LoWPAN sıkıştırma
```

**Donanım Sürücüleri:**
```
cc2420_*             — IEEE 802.15.4 radyo sürücüsü (Z1'in radyosu)
adxl345_*            — ivme ölçer sensör sürücüsü
sht11_*              — sıcaklık/nem sensörü
i2cmaster_*          — I2C master sürücüsü
uart0_*              — seri port sürücüsü
```

**Başlangıç Sembolleri:**
```
__watchdog_support   — WDT devre dışı bırakma (0x3100)
__init_stack         — stack pointer başlatma
__do_copy_data       — .data bölümünü Flash'tan RAM'e kopyalama
__do_clear_bss       — .bss bölümünü sıfırlama
main                 — Contiki-NG ana döngüsü
```

**Tanımsız (Undefined) Semboller:**
```
gpio_hal_arch_*      — donanıma özgü GPIO fonksiyonları
button_hal_*         — buton soyutlama katmanı
```
Bu semboller harici kütüphaneden (platform-spesifik kaynak) gelmekte ve link zamanında çözülmüştür.

### Global Değişkenler (önemli olanlar)

```
udp_conn         — simple_udp bağlantı nesnesi
server_addr      — hedef IPv6 adresi
ack_received     — Stop-and-Wait ACK bayrağı (volatile)
nack_received    — NACK bayrağı (volatile)
acked_block_num  — onaylanan blok numarası
boot_metadata    — OTA slot durumu (A/B)
```

---

## 7. ELF Yapısı Analizi

### Program Headers (Yükleme Segmentleri)

```bash
$ msp430-readelf -l build/z1/udp-client.z1
```

```
Program Headers:
  Type   Offset   VirtAddr   PhysAddr   FileSiz  MemSiz   Flg
  LOAD   0x000000 0x0000302c 0x0000302c 0x0a5a2  0x0a5a2  R E  ← .text
  LOAD   0x00a5a4 0x0000d5d0 0x0000d5d0 0x00557  0x00557  R    ← .rodata
  LOAD   0x00aafc 0x00001100 0x0000db28 0x00192  0x01898  RW   ← .data + .bss
  LOAD   0x00ac8e 0x00002998 0x0000dcba 0x00000  0x00002  RW   ← .noinit
  LOAD   0x00ac8e 0x0000ffc0 0x0000ffc0 0x00040  0x00040  R E  ← .vectors
```

**VirtAddr vs PhysAddr farkı (`.data` segmenti):**
- PhysAddr `0xDB28` — Flash'taki başlangıç değerleri burada saklanır
- VirtAddr `0x1100` — çalışma zamanında RAM'deki adresi
- Boot sırasında `__do_copy_data` bu iki adres arasında kopyalama yapar

**`R E` (Read + Execute):** `.text` ve `.vectors` — çalıştırılabilir Flash bölgeleri  
**`R` (Read only):** `.rodata` — sabit veriler  
**`RW` (Read + Write):** `.data` + `.bss` — değiştirilebilir RAM bölgeleri

### ELF Magic Bytes Yorumu

```
7f 45 4c 46  → ELF imzası (\x7fELF)
01           → ELF32
01           → Little-endian
01           → ELF version 1
ff           → OS/ABI: Standalone (0xFF)
```

### Debug Sections

Dosya, boyutlu debug section'lar içermektedir:
```
.debug_info    : 6645 bayt  — DWARF tip ve değişken bilgisi
.debug_line    : 3948 bayt  — kaynak satır numarası eşleme
.debug_aranges :  728 bayt  — adres aralığı tablosu
.debug_str     : 1092 bayt  — debug string havuzu
.debug_loc     : 5557 bayt  — değişken konum bilgisi
```

Bu section'lar Flash'a yazılmaz (adres 0x0); yalnızca ELF dosyasında bulunur. `msp430-strip` ile kaldırılabilir.

---

## 8. Interrupt ve Donanım Analizi

### Vektör Tablosu

```bash
$ msp430-readelf -S build/z1/udp-client.z1 | grep vectors
  [ 6] .vectors   PROGBITS   0000ffc0   000040   AX
```

```bash
$ msp430-objdump -h build/z1/udp-client.z1 | grep vectors
  5 .vectors   00000040  0000ffc0  0000ffc0  0000ac8e
```

**MSP430 Kesme Vektör Tablosu: `0xFFC0 – 0xFFFF` (64 bayt = 32 adet 2-baytlık vektör)**

MSP430 mimarisinde kesme vektörleri Flash'ın en üst 64 baytına sabitlenmiştir. Her vektör, ilgili ISR'nin (Interrupt Service Routine) adresini tutar. İşlemci bir kesme aldığında bu tablodan ISR adresini okuyarak oraya atlar.

| Adres | Vektör | Kullanım |
|-------|--------|----------|
| 0xFFFE | Reset Vector | Güç açılışı / WDT reset → Entry point'e (0x3100) |
| 0xFFFC | NMI | Donanım hatası |
| 0xFFEA | Timer_A | etimer/rtimer için |
| 0xFFE8 | UART0 RX/TX | Seri haberleşme |
| 0xFFCC | CC2420 (PORT2) | Radyo kesmesi |

**Reset Vektörü ve Entry Point İlişkisi:**
`0xFFFE` adresindeki reset vektörü `0x3100`'ü gösterir. İşlemci açıldığında buraya atlar, önce `__watchdog_support` WDT'yi devre dışı bırakır, ardından `__init_stack` stack'i kurar, `__do_copy_data` ile `.data` kopyalanır, `__do_clear_bss` ile `.bss` sıfırlanır ve son olarak `main()` çağrılır.

### Disassembly — Başlangıç Zinciri

```bash
$ msp430-objdump -d build/z1/udp-client.z1 | head -50
```

```asm
00003100 <__watchdog_support>:
    3100: 55 42 20 01   mov.b  &0x0120, r5    ; WDTCTL okunuyor
    3104: 35 d0 08 5a   bis    #23048, r5     ; WDT durdurma biti
    3108: 82 45 98 29   mov    r5, &0x2998    ; .noinit'e kaydediliyor

0000310c <__init_stack>:
    310c: 31 40 00 31   mov    #0x3100, r1    ; SP = 0x3100 (stack başlangıcı)

00003110 <__do_copy_data>:
    3110: 3f 40 92 01   mov    #0x0192, r15   ; 402 bayt kopyalanacak (.data boyutu)
    ...                                        ; 0xDB28 (Flash) → 0x1100 (RAM)

00003128 <__do_clear_bss>:
    3128: 3f 40 06 17   mov    #0x1706, r15   ; 5894 bayt sıfırlanacak (.bss boyutu)
    ...                                        ; 0x1292 adresi sıfırlanıyor

0000313e <main>:
    313e: b0 13 06 65   calla  #0x6506        ; Contiki process_init()
    3142: b0 13 38 45   calla  #0x4538        ; clock_init()
    ...
```

---

## 18. Binary Transformation — Neden ELF, Ham Binary Değil?

### ELF ve Ham Binary Farkı

**Ham binary (`.bin`):** Yalnızca makine kodunu içerir. Hangi adrese yükleneceği, bölüm sınırları, sembol bilgisi yoktur. Genellikle bootloader'ın doğrudan Flash'a yazdığı son formattır.

**ELF (Executable and Linkable Format):** Çok daha zengin bir konteynerdir:

```
ELF Dosyası
├── ELF Header        → mimari, entry point, endianness bilgisi
├── Program Headers   → yükleme segmentleri (Flash/RAM ayrımı)
├── Section Headers   → .text, .data, .bss, .vectors, ...
├── Symbol Table      → fonksiyon ve değişken isimleri + adresleri
├── Debug Sections    → kaynak satır eşleme (DWARF)
└── String Tables     → isim havuzları
```

**Bu firmware neden ELF olarak değerlendirilir?**

1. **Magic bytes:** İlk 4 bayt `7f 45 4c 46` (0x7F + "ELF") — standart ELF imzası
2. **Yapılandırılmış bölümler:** `.text`, `.data`, `.bss` ayrımı — ham binary'de bu ayrım yoktur
3. **Sembol tablosu:** 1132 sembol içeriyor — ham binary'de sembol yoktur
4. **Debug bilgisi:** 8 adet `.debug_*` section — geliştiriciye kaynak düzeyinde hata ayıklama imkânı
5. **Çift adres (VMA/LMA):** `.data` için VMA≠LMA — Flash'tan RAM'e kopyalama talimatı ELF'te gömülü
6. **Toolchain entegrasyonu:** Cooja simülatörü, `msp430-gdb`, `msp430-objdump` gibi araçlar ELF dosyasını doğrudan anlayabilir

**Sonuç:** `udp-client.z1` bir ham binary değil, geliştirme ve simülasyon sürecinde tam araç zinciri desteği sunan ELF formatında çalıştırılabilir bir firmware imajıdır. Gerçek donanıma yazılmadan önce `msp430-objcopy -O ihex` veya `-O binary` ile dönüştürülmesi gerekir.

---

## 4. String ve Metadata Analizi

```bash
$ strings build/z1/udp-client.z1 | head -40
$ msp430-readelf -s build/z1/udp-client.z1 | grep -i "ota\|udp\|rpl\|process"
```

Sembol tablosundan çıkarılan anlamlı string'ler:

**Contiki-NG Süreç İsimleri:**
```
"OTA Sender"         — udp_client_process'in log modülü adı
"App"                — genel uygulama log etiketi
```

**Log Mesajları (rodata section'dan):**
```
"Node 2: ag hazir olana kadar bekleniyor..."
"OTA baslıyor: %u blok x %d bayt..."
"ACK: blok %u/%u onaylandi"
"NACK: blok %u bozuk, yeniden gonderiliyor"
"Alici onayladi: OTA transferi tamamlandi!"
```

Bu string'ler `.rodata` bölümünde (`0xD5D0`) saklanmaktadır. Flash'tan okunup UART üzerinden seri porta yazdırılırlar. Cooja'nın Mote Output penceresinde görülen loglar bu string'lerden oluşmaktadır.

---

## Özet Tablo

| Kriter | Değer | Açıklama |
|--------|-------|----------|
| ELF Sınıfı | ELF32 | 32-bit format |
| Mimari | MSP430 | TI MSP430F2617 (Z1 platformu) |
| Endianness | Little-endian | MSP430 doğal sırası |
| Entry Point | 0x3100 | `__watchdog_support` başlangıç fonksiyonu |
| Flash Kullanımı | ~43 KB / 128 KB | %33.6 doluluk |
| RAM Kullanımı | ~6.1 KB / 8 KB | %76.3 doluluk |
| Sembol Sayısı | 1132 | Debug sembolleri dahil |
| Kesme Vektörü | 0xFFC0–0xFFFF | 32 adet MSP430 vektörü |
| Debug Bilgisi | Mevcut | DWARF format, 8 section |
| Format | ELF (ham binary değil) | Toolchain araçlarıyla tam uyumlu |

---

*OMÜ Bilgisayar Mühendisliği — BİL 304 İşletim Sistemleri — OTA/ELF Analizi — 2025/2026 Bahar*

---

# Karşılaştırmalı Firmware Analizi

> Hoca tarafından sağlanan örnek firmware'ler üzerinde yapılan karşılaştırmalı analiz.

## 22. Karşılaştırmalı Firmware Analizi — MSP430 Z1 Platform

### Analiz Edilen Firmware'ler

| Firmware | Boyut | text | data | bss | Açıklama |
|----------|-------|------|------|-----|----------|
| `hello-world.z1` | 98 KB | 41.512 | 328 | 5.676 | Basit başlangıç uygulaması |
| `udp-client.z1` | 99 KB | 42.542 | 336 | 5.888 | UDP istemcisi (rpl-udp) |
| `nullnet-broadcast.z1` | 58 KB | 17.866 | 166 | 2.240 | En küçük — nullnet broadcast |
| `nullnet-unicast.z1` | 118 KB | 28.097 | 2.488 | 2.632 | Nullnet unicast |
| `hardworker.z1` | 131 KB | 73.564 | 374 | 5.698 | En büyük — yoğun işlem |
| `udp-client.z1` (OTA) | 100 KB | 43.621 | 402 | 5.896 | OTA firmware (bizim derlediğimiz) |

### Bellek Kullanımı (MSP430F2617: 128KB Flash, 8KB RAM)

```
Flash Kullanımı (.text + .data):
  nullnet-broadcast : 18 KB  (%14)  ████░░░░░░░░░░░░░░░░
  nullnet-unicast   : 31 KB  (%24)  ██████░░░░░░░░░░░░░░
  hello-world       : 42 KB  (%33)  ████████░░░░░░░░░░░░
  udp-client (rpl)  : 43 KB  (%34)  ████████░░░░░░░░░░░░
  udp-client (OTA)  : 44 KB  (%34)  ████████░░░░░░░░░░░░
  hardworker        : 74 KB  (%58)  ████████████░░░░░░░░

RAM Kullanımı (.data + .bss):
  nullnet-broadcast :  2.4 KB (%30)  ██████░░░░░░░░░░░░░░
  nullnet-unicast   :  5.1 KB (%64)  █████████████░░░░░░░
  hello-world       :  6.0 KB (%75)  ███████████████░░░░░
  udp-client (OTA)  :  6.3 KB (%79)  ████████████████░░░░
  hardworker        :  6.1 KB (%76)  ███████████████░░░░░
```

### Önemli Farklar ve Yorumlar

**`nullnet-broadcast.z1` vs `udp-client.z1`:**
- nullnet-broadcast sadece 18 KB .text — UDP/IPv6/RPL stack'i yok, çok daha sade bir MAC katmanı kullanıyor
- udp-client 42 KB — RPL yönlendirme, 6LoWPAN, IPv6 stack'in tamamı dahil
- Fark: ~24 KB sadece ağ stack maliyeti

**`hardworker.z1` — En ilginç firmware:**
```
.far.text  : 0x10000 adresinde, 20.948 bayt  ← MSP430X genişletilmiş adres uzayı!
.text      : 0x3100  adresinde, 37.656 bayt
.rodata    : 0xC418  adresinde, 14.896 bayt  ← çok büyük sabit veri
```

`hardworker.z1`'de iki ayrı kod bölümü var:
- `.text` → standart MSP430 16-bit adres uzayında (0x0000–0xFFFF)
- `.far.text` → `0x10000` adresinde, 16-bit sınırı aşıyor — **MSP430X genişletilmiş mimari**

Bu, büyük `input()` (4.654 bayt) ve `output()` (3.548 bayt) fonksiyonlarının normal adres uzayına sığmaması nedeniyle genişletilmiş alana yerleştirildiğini gösterir. `.rodata` da 14.8 KB ile çok büyük — büyük ihtimalle bir FFT/DSP katsayı tablosu veya büyük bir lookup tablosu içeriyor.

**`hello-world.z1` vs `udp-client.z1` (OTA versiyonu):**
- .text farkı: 43.621 - 41.512 = **+2.109 bayt** — OTA metadata + CRC32 + Stop-and-Wait fonksiyonları
- .bss farkı: 5.896 - 5.676 = **+220 bayt** — OTA durum değişkenleri (ack_received, block_num, vb.)

---

## 1b. CC1352R ARM Firmware Analizi — `base-demo.simplelink`

### Komut ve Çıktı

```bash
$ readelf -h base-demo.simplelink
```

```
ELF Header:
  Magic:   7f 45 4c 46 01 01 01 00
  Class:                             ELF32
  Data:                              2's complement, little endian
  Type:                              EXEC (Executable file)
  Machine:                           ARM
  OS/ABI:                            UNIX - System V
  Entry point address:               0x6751
  Flags:                             0x5000200, Version5 EABI, soft-float ABI
  Number of section headers:         31
```

### MSP430 vs ARM (CC1352R) Karşılaştırması

| Özellik | `udp-client.z1` (MSP430) | `base-demo.simplelink` (CC1352R/ARM) |
|---------|--------------------------|---------------------------------------|
| Mimari | TI MSP430 | ARM Cortex-M4F |
| ELF Class | ELF32 | ELF32 |
| Endianness | Little-endian | Little-endian |
| OS/ABI | Standalone App | UNIX - System V |
| Entry Point | `0x3100` | `0x6751` (Thumb modu) |
| ABI | MSP430-GCC ABI | ARM EABI5, soft-float |
| .text boyutu | 42.190 bayt | 69.332 bayt |
| .data boyutu | 402 bayt | 1.192 bayt |
| .bss boyutu | 5.894 bayt | 12.584 bayt |
| Flash kullanımı | ~43 KB / 128 KB | ~70 KB / 352 KB |
| RAM kullanımı | ~6.1 KB / 8 KB | ~14 KB / 80 KB |

### CC1352R Bellek Haritası — `base-demo.simplelink` Yerleşimi

```
CC1352R Flash (352 KB: 0x0000_0000 – 0x0005_7FFF):
┌──────────────────────────────────────────────┐ 0x0000_0000
│ .resetVecs (64 bayt)                         │ ARM kesme vektörleri
│   → Reset Handler, NMI, HardFault...         │
├──────────────────────────────────────────────┤ 0x0000_0040
│ .text (69.332 bayt = ~67.7 KB)              │ Uygulama kodu
│   Entry point: 0x6750 (Thumb aligned)        │
│   Cortex-M4F makine kodu                     │
├──────────────────────────────────────────────┤ 0x0001_0D14
│ .rodata (2.413 bayt)                         │ Salt-okunur sabitler
├──────────────────────────────────────────────┤ 0x0001_1C04
│ .ARM.exidx (8 bayt)                          │ C++ exception index
├──────────────────────────────────────────────┤ 0x0005_7FA8
│ .ccfg (88 bayt)                              │ Customer Config!
│   Boot davranışı, bootloader ayarları        │
└──────────────────────────────────────────────┘ 0x0005_8000

CC1352R SRAM (80 KB: 0x2000_0000 – 0x2001_3FFF):
┌──────────────────────────────────────────────┐ 0x2000_0000
│ DMA tamponları (SPI0/SPI1 Rx/Tx) x8         │ 128 bayt toplam
├──────────────────────────────────────────────┤ 0x2000_1B20
│ .data (1.192 bayt)                           │ Başlangıç değerli değişkenler
├──────────────────────────────────────────────┤ 0x2000_2000
│ vtable_ram (216 bayt)                        │ RAM'deki vektör tablosu kopyası
├──────────────────────────────────────────────┤ 0x2000_20D8
│ .bss (12.584 bayt)                           │ Sıfırlanan değişkenler
├──────────────────────────────────────────────┤ 0x2000_5200
│ .heap (256 bayt)                             │ Dinamik bellek havuzu
└──────────────────────────────────────────────┘ 0x2001_4000
```

### CC1352R'ye Özgü Bölümler

**`.ccfg` (Customer Configuration, `0x57FA8`):**
Flash'ın en sonuna yerleşen 88 baytlık kritik bölüm. Şunları belirler:
- Bootloader'ın aktif olup olmadığı
- Bootloader backdoor pini
- OTA için hangi slot'tan boot edileceği
- Flash koruma ayarları

OTA senaryosunda `.ccfg`, yeni firmware'in hangi adresten başlatılacağını bootloader'a söyler. Bu bölümün hatalı yazılması cihazın brick olmasına yol açabilir.

**`.resetVecs` (`0x0000_0000`):**
ARM Cortex-M4F'te vektör tablosu Flash'ın başında (`0x0`) yer alır. MSP430'un aksine (`0xFFFF`) başlangıçta bulunur. İlk 4 bayt stack pointer başlangıcı, sonraki 4 bayt reset handler adresidir.

**`.ARM.exidx`:**
C++ exception unwinding için kullanılan ARM'a özgü bölüm. Contiki-NG C ile yazılmış olsa da ARM toolchain bu bölümü otomatik ekler.

**`vtable_ram` (`0x2000_2000`):**
Reset vector tablosunun RAM kopyası. ARM'da `VTOR` (Vector Table Offset Register) yazılarakçalışma zamanında vektör tablosu RAM'e taşınabilir. Bu özellik dinamik ISR yönetimi sağlar.

**Entry Point `0x6751` — ARM Thumb Modu:**
Tek sayı (LSB=1) olması ARM Thumb komut setini işaret eder. Gerçek adres `0x6750`'dir — işlemci buraya atladığında Thumb modunda çalışır. Cortex-M4F'te tüm uygulama kodu genellikle 16-bit Thumb-2 komutlarıyla derlenir (daha verimli Flash kullanımı).

### MSP430 vs ARM — Mimari Farklar Özeti

| Kriter | MSP430 (Z1) | ARM Cortex-M4F (CC1352R) |
|--------|-------------|---------------------------|
| Adres genişliği | 16-bit (MSP430X: 20-bit) | 32-bit |
| Flash kapasitesi | 128 KB | 352 KB |
| RAM kapasitesi | 8 KB | 80 KB |
| Vektör tablosu yeri | Flash sonu (0xFFC0) | Flash başı (0x0000) |
| Thumb desteği | Yok | Var (Thumb-2) |
| FPU | Yok | Var (M4F) |
| DMA | Sınırlı | Kapsamlı (SPI, UART, ADC) |
| CCFG | Yok | Var (boot konfigürasyon) |
| OTA uygunluğu | Sınırlı (8KB RAM) | Yüksek (80KB RAM, çift slot) |

---

*OMÜ Bilgisayar Mühendisliği — BİL 304 İşletim Sistemleri — OTA/ELF Analizi — 2025/2026 Bahar*
