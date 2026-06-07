# BİL 304 — OTA Firmware Transfer Projesi

**OMÜ Bilgisayar Mühendisliği | BİL 304 İşletim Sistemleri | 2025/2026 Bahar**

Contiki-NG işletim sistemi üzerinde, Cooja simülatöründe Z1 mote'lar arasında OTA (Over-the-Air) firmware transfer sistemi.

>  **Simülasyon Videosu:** [YouTube Linki](#)  
>  **ELF Analiz Raporu:** [OTA-ANALIZ.md](OTA-ANALIZ.md)

---

## Proje Yapısı

```
├── udp-client.c            — OTA gönderici düğüm (Node 2) + röle (Node 3)
├── udp-server.c            — OTA alıcı düğüm + DAG root (Node 1)
├── ota-metadata.c/h        — Slot A/B metadata yönetimi, CRC32
├── new-firmware-data.c/h   — Gönderilecek firmware verisi
├── Makefile                — Derleme yapılandırması (TARGET=z1)
├── BIL304-OS-Project-1.csc — Cooja simülasyon senaryosu
└── build/z1/
    ├── udp-client.z1       — Derlenmiş gönderici firmware
    └── udp-server.z1       — Derlenmiş alıcı firmware
```

---

## Derleme

```bash
# Contiki-NG klonla (yoksa)
git clone --depth=1 https://github.com/contiki-ng/contiki-ng.git ~/Desktop/contiki-ng

# Docker ile derle
docker run --rm \
  --platform linux/amd64 \
  -v "$HOME/Desktop/contiki-ng":/home/user/contiki-ng \
  -v "$(pwd)":/home/user/contiki-ng/examples/ota-proje \
  -w /home/user/contiki-ng/examples/ota-proje \
  contiker/contiki-ng \
  bash -c "export CCACHE_DISABLE=1 && make clean TARGET=z1 && make TARGET=z1 CCACHE="
```

---

## Sistem Mimarisi

```
┌─────────────────────┐       UDP/IPv6/RPL         ┌─────────────────────┐
│   Node 2 (ID:2)     │  ──────────────────────►  │   Node 1 (ID:1)     │
│   OTA Gönderici     │  ◄──────────────────────  │   OTA Alıcı         │
│   udp-client.z1     │     ACK / NACK / DONE      │   udp-server.z1     │
│                     │                            │   DAG Root          │
│  new-firmware-data  │         Node 3             │  CFS: ota-fw.bin    │
│  (firmware verisi)  │       (röle düğüm)         │  (alınan firmware)  │
└─────────────────────┘                            └─────────────────────┘
```

**Node rolleri:**

- **Node 1:** RPL DAG root olarak ağı yönetir. Gelen firmware bloklarını CRC16 ile doğrular, CFS Coffee dosya sistemine yazar. Transfer sonunda CRC32 ile tüm imajı doğrular.
- **Node 2:** OTA gönderici. Firmware verisini 64 baytlık bloklara böler, Stop-and-Wait protokolü ile blok blok gönderir.
- **Node 3:** Aynı `udp-client.z1` yüklü ama `node_id != 2` kontrolü sayesinde OTA başlatmaz. RPL ağında röle (relay) görevi yapar — Node 2'den gelen paketleri Node 1'e iletir.

```c
/* udp-client.c — node_id kontrolü */
if(node_id != 2) {
    LOG_INFO("Node %u: komsu role modunda (relay)\n", node_id);
    while(1) {
        PROCESS_WAIT_EVENT();  /* sadece bekler, OTA başlatmaz */
    }
}
```

---

## Paket Formatları

### OTA_PKT_START (0x01) — Transfer Başlatma (11 bayt)

```
 0        1        2        3        4        5        6        7        8        9        10
┌────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┐
│  0x01  │ BLK_H  │ BLK_L  │ SZ_3   │ SZ_2   │ SZ_1   │ SZ_0   │ CRC_3  │ CRC_2  │ CRC_1  │ CRC_0  │
└────────┴────────┴────────┴────────┴────────┴────────┴────────┴────────┴────────┴────────┴────────┘
  tip     toplam blok sayısı (2B)   toplam imaj boyutu (4B)      tüm imaj CRC32 (4B)
```

### OTA_PKT_BLOCK (0x02) — Veri Bloğu (6 + max 64 bayt)

```
 0        1        2        3        4        5       6..N
┌────────┬────────┬────────┬────────┬────────┬────────┬──────────────────────┐
│  0x02  │ BLK_H  │ BLK_L  │ LEN    │ CRC_H  │ CRC_L  │  veri (max 64 bayt)  │
└────────┴────────┴────────┴────────┴────────┴────────┴──────────────────────┘
  tip     blok numarası (2B)  uzunluk  CRC16 (2B)       blok verisi
```

### OTA_PKT_ACK (0x03) / OTA_PKT_NACK (0x04) — Onay/Red (3 bayt)

```
 0        1        2
┌────────┬────────┬────────┐
│0x03/04 │ BLK_H  │ BLK_L  │
└────────┴────────┴────────┘
  tip     onaylanan/reddedilen blok numarası (2B)
```

### OTA_PKT_DONE (0x05) — Transfer Tamamlandı (1 bayt)

```
┌────────┐
│  0x05  │
└────────┘
  (tüm imaj CRC32 doğrulandı, transfer başarılı)
```

---

## Stop-and-Wait Protokolü

### Neden Stop-and-Wait?

Z1 mote yalnızca **8 KB RAM**'e sahiptir. Sliding window gibi protokoller birden fazla bloğu aynı anda bellekte tutmayı gerektirir. Stop-and-Wait her seferinde tek blok gönderir — basit, güvenilir ve bellek dostudur.

### Akış Diyagramı

```
Gönderici (Node 2)                        Alıcı (Node 1)
      │                                         │
      ├──── OTA_PKT_START ──────────────────►  │ CFS dosyası aç
      │                                         │
      ├──── OTA_PKT_BLOCK[0] ───────────────►  │ CRC16 doğrula → CFS'e yaz
      │◄─── OTA_PKT_ACK[0] ────────────────── │
      │                                         │
      ├──── OTA_PKT_BLOCK[1] ───────────────►  │ (kayıp/bozuk)
      │     (5 sn timeout → yeniden gönder)    │
      ├──── OTA_PKT_BLOCK[1] ───────────────►  │ CRC16 doğrula → CFS'e yaz
      │◄─── OTA_PKT_ACK[1] ────────────────── │
      │              ...                        │
      ├──── OTA_PKT_BLOCK[N] ───────────────►  │ CRC32 doğrula
      │◄─── OTA_PKT_DONE ─────────────────── │
```

### Stop-and-Wait Döngüsü (Kod)

```c
/* udp-client.c */
while(current_block < total_blocks && !done_received) {
    retries = 0;
    ack_received = 0;
    nack_received = 0;

    while(retries < OTA_MAX_RETRIES) {        /* max 8 deneme */
        send_ota_block(current_block);
        etimer_set(&ack_timer, OTA_TIMEOUT_S * CLOCK_SECOND);  /* 5 sn timeout */

        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&ack_timer) ||
                                 ev == PROCESS_EVENT_POLL);

        if(ack_received && acked_block_num == current_block) {
            current_block++;    /* sonraki bloğa geç */
            break;
        } else if(nack_received) {
            current_block = acked_block_num;   /* NACK'lanan bloğa dön */
            retries++;
        } else {
            retries++;          /* timeout — yeniden gönder */
        }
    }
}
```

---

## Bütünlük Denetimi (Alınan Önlemler)

### CRC16-CCITT — Blok Düzeyinde Doğrulama

Her 64 baytlık blok için ayrı ayrı hesaplanır. Alıcı aynı hesabı yaparak karşılaştırır — uyuşmazsa NACK gönderir, blok yeniden istenir.

**Polinom:** `0x1021` | **Başlangıç:** `0xFFFF`

```c
/* udp-server.c — satır 59 */
static uint16_t
crc16_ccitt(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFFu;
    uint16_t i;
    int bit;
    for(i = 0; i < len; i++) {
        crc ^= (uint16_t)buf[i] << 8;
        for(bit = 0; bit < 8; bit++) {
            if(crc & 0x8000u)
                crc = (crc << 1) ^ 0x1021u;
            else
                crc <<= 1;
        }
    }
    return crc;
}
```

**Alıcıda doğrulama:**
```c
/* udp-server.c — satır 214 */
calc_crc = crc16_ccitt(blk_data, data_len);
if(calc_crc != recv_crc) {
    LOG_INFO("Blok %u: CRC16 HATASI\n", block_num);
    send_nack(sender_addr, block_num);
    return;
}
```

### CRC32/ISO-HDLC — İmaj Düzeyinde Doğrulama

Tüm transfer tamamlandıktan sonra alıcı, CFS'teki dosyayı baştan sona okuyarak CRC32 hesaplar. Gönderici START paketinde bu değeri önceden bildirmiştir.

**Polinom:** `0xEDB88320` (yansıtılmış) | **Başlangıç:** `0xFFFFFFFF` | **Son:** `~crc`

```c
/* ota-metadata.c */
uint32_t
ota_crc32_buffer(const void *buf, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;
    int bit;
    for(i = 0; i < len; i++) {
        crc ^= p[i];
        for(bit = 0; bit < 8; bit++) {
            if(crc & 1u)
                crc = (crc >> 1) ^ 0xEDB88320u;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}
```

### CRC16 ve CRC32 Neden İkisi Birden?

| | CRC16-CCITT | CRC32/ISO-HDLC |
|--|-------------|----------------|
| Kapsam | Her blok (64 bayt) | Tüm imaj |
| Amaç | Anlık hata tespiti | Uçtan uca bütünlük |
| Yanlış pozitif | 1/65.536 | 1/4 milyar |
| Maliyet | Düşük | Yüksek (1 kez) |

CRC16 her blokta çalışır ve bozuk bloğu anında tespit eder. CRC32 tüm transfer sonunda bir kez çalışır ve imajın eksiksiz alındığını garanti eder.

---

## CFS Coffee Dosya Sistemi

Contiki-NG'nin gömülü dosya sistemidir. Z1'de harici Flash (M25P80, 1 MB) üzerinde çalışır.

```c
/* udp-server.c — Transfer başlangıcı */
cfs_remove("ota-fw.bin");                         /* eski dosyayı sil */
fd = cfs_open("ota-fw.bin", CFS_WRITE);           /* yeni oluştur */
cfs_close(fd);

/* Her ACK'lı bloktan sonra */
fd = cfs_open("ota-fw.bin", CFS_WRITE | CFS_APPEND);
cfs_write(fd, blk_data, data_len);                /* sona ekle */
cfs_close(fd);

/* CRC32 doğrulaması için */
fd = cfs_open("ota-fw.bin", CFS_READ);
n = cfs_read(fd, buf, chunk);
cfs_close(fd);
```

Stop-and-Wait garantisi sayesinde bloklar her zaman sıralı gelir. `CFS_APPEND` ile her blok dosyanın sonuna eklenir, `seek` gerekmez.

---

## Simülasyon Sonuçları

### Cooja Log Çıktısı

```
00:01.532  ID:1  [OTA-Receiver] OTA alici hazir (DAG root). Baglanti bekleniyor...
00:01.835  ID:2  [OTA-Sender]   Node 2: ag hazir olana kadar bekleniyor...
00:01.849  ID:3  [OTA-Sender]   Node 3: komsu role modunda (relay)

-- RPL ağı oluşuyor (~30 saniye) --

01:11.985  ID:2  [OTA-Sender]   OTA basliyor: 128 blok, toplam 8192 bayt, CRC32=0xb6675307
01:12.021  ID:1  [OTA-Receiver] OTA basladi: 128 blok, 8192 bayt, beklenen CRC32=0xb6675307
01:12.031  ID:1  [OTA-Receiver] CFS: 'ota-fw.bin' hazir

01:14.030  ID:1  [OTA-Receiver] Blok 0/127 alindi [64/8192 bayt]
01:14.063  ID:2  [OTA-Sender]   ACK: blok 0/127 onaylandi
01:14.136  ID:1  [OTA-Receiver] Blok 1/127 alindi [128/8192 bayt]
01:14.153  ID:2  [OTA-Sender]   ACK: blok 1/127 onaylandi
           ...
01:29.188  ID:1  [OTA-Receiver] Blok 127/127 alindi [8192/8192 bayt]
01:29.195  ID:1  [OTA-Receiver] Tum 128 blok alindi. Imaj CRC32 dogrulanıyor...
01:29.415  ID:1  [OTA-Receiver] Hesaplanan CRC32: 0xb6675307
01:29.420  ID:1  [OTA-Receiver] Yuklenmeye hazir yeni firmware alimi tamamlandi.
01:29.454  ID:2  [OTA-Sender]   ACK: blok 127/127 onaylandi
01:29.460  ID:2  [OTA-Sender]   Tum bloklar gonderildi. Alici dogrulamasi bekleniyor...
01:39.458  ID:2  [OTA-Sender]   OTA gonderi sureci tamamlandi.
```

### Parametreler

| Parametre | Değer |
|-----------|-------|
| Blok boyutu | 64 bayt |
| Timeout süresi | 5 saniye |
| Max yeniden deneme | 8 |
| Firmware boyutu | 8.192 bayt |
| Blok sayısı | 128 blok |
| Blok doğrulama | CRC16-CCITT (0x1021) |
| İmaj doğrulama | CRC32/ISO-HDLC (0xEDB88320) |
| Depolama | CFS Coffee (ota-fw.bin) |
| Ağ protokolü | UDP/IPv6/RPL (6LoWPAN) |

---

*Tesnim Namtouk 20061030 /Zeynep Yolcu 23060736 *  
*OMÜ Bilgisayar Mühendisliği — BİL 304 İşletim Sistemleri — 2025/2026 Bahar*
