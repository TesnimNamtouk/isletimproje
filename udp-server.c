/*
 * OTA Firmware Transfer - Receiver Node (udp-server.c)
 *
 * Node ID 1: DAG root + OTA alıcı (firmware receiver & storage)
 *
 * Protokol (Stop-and-Wait):
 *   <- OTA_PKT_START  : transfer parametrelerini al, CFS dosyasını aç
 *   <- OTA_PKT_BLOCK  : bloğu CRC16 ile doğrula, CFS'e sırayla ekle, ACK/NACK
 *   -> OTA_PKT_ACK    : blok doğru alındı
 *   -> OTA_PKT_NACK   : blok bozuk, yeniden gönder
 *   -> OTA_PKT_DONE   : tüm imaj alındı ve CRC32 doğrulandı
 */

#include "contiki.h"
#include "net/routing/routing.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include "cfs/cfs.h"
#include "cfs/cfs-coffee.h"
#include "ota-metadata.h"
#include "sys/log.h"

#include <stdint.h>
#include <string.h>

#define LOG_MODULE "OTA-Receiver"
#define LOG_LEVEL  LOG_LEVEL_INFO

#define UDP_CLIENT_PORT  8765
#define UDP_SERVER_PORT  5678

#define OTA_BLOCK_SIZE   64
#define OTA_FILENAME     "ota-fw.bin"

/* Paket tipleri */
#define OTA_PKT_START  0x01u
#define OTA_PKT_BLOCK  0x02u
#define OTA_PKT_ACK    0x03u
#define OTA_PKT_NACK   0x04u
#define OTA_PKT_DONE   0x05u

/* Alıcı durum makinesi */
#define STATE_IDLE       0
#define STATE_RECEIVING  1
#define STATE_COMPLETE   2

static struct simple_udp_connection udp_conn;

static uint8_t  ota_state      = STATE_IDLE;
static uint16_t total_blocks   = 0;
static uint32_t total_size     = 0;
static uint32_t expected_crc32 = 0;
static uint16_t next_expected  = 0;
static uint32_t bytes_written  = 0;

/*---------------------------------------------------------------------------*/
/* CRC16-CCITT (polinom 0x1021) - blok bütünlük denetimi */
static uint16_t
crc16_ccitt(const uint8_t *buf, uint16_t len)
{
  uint16_t crc = 0xFFFFu;
  uint16_t i;
  int bit;

  for(i = 0; i < len; i++) {
    crc ^= (uint16_t)buf[i] << 8;
    for(bit = 0; bit < 8; bit++) {
      if(crc & 0x8000u) {
        crc = (crc << 1) ^ 0x1021u;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}
/*---------------------------------------------------------------------------*/
/*
 * Tüm imajı CFS'ten parça parça okuyarak CRC32 hesapla.
 * ota_crc32_buffer ile aynı CRC32/ISO-HDLC algoritması.
 */
static uint32_t
compute_image_crc32(void)
{
  uint8_t buf[OTA_BLOCK_SIZE];
  uint32_t crc = 0xFFFFFFFFu;
  uint32_t remaining = total_size;
  int fd, n;

  fd = cfs_open(OTA_FILENAME, CFS_READ);
  if(fd < 0) {
    LOG_INFO("CFS: okuma icin acilamadi\n");
    return 0;
  }

  while(remaining > 0) {
    uint32_t chunk = (remaining > OTA_BLOCK_SIZE) ? OTA_BLOCK_SIZE : remaining;
    n = cfs_read(fd, buf, (unsigned)chunk);
    if(n <= 0) {
      break;
    }
    int i, bit;
    for(i = 0; i < n; i++) {
      crc ^= buf[i];
      for(bit = 0; bit < 8; bit++) {
        if(crc & 1u) {
          crc = (crc >> 1) ^ 0xEDB88320u;
        } else {
          crc >>= 1;
        }
      }
    }
    remaining -= (uint32_t)n;
  }

  cfs_close(fd);
  return ~crc;
}
/*---------------------------------------------------------------------------*/
static void
send_ack(const uip_ipaddr_t *dest, uint16_t block_num)
{
  uint8_t buf[3];
  buf[0] = OTA_PKT_ACK;
  buf[1] = (uint8_t)(block_num >> 8);
  buf[2] = (uint8_t)(block_num & 0xFF);
  simple_udp_sendto(&udp_conn, buf, sizeof(buf), dest);
}
/*---------------------------------------------------------------------------*/
static void
send_nack(const uip_ipaddr_t *dest, uint16_t block_num)
{
  uint8_t buf[3];
  buf[0] = OTA_PKT_NACK;
  buf[1] = (uint8_t)(block_num >> 8);
  buf[2] = (uint8_t)(block_num & 0xFF);
  simple_udp_sendto(&udp_conn, buf, sizeof(buf), dest);
}
/*---------------------------------------------------------------------------*/
static void
send_done(const uip_ipaddr_t *dest)
{
  uint8_t buf[1];
  buf[0] = OTA_PKT_DONE;
  simple_udp_sendto(&udp_conn, buf, sizeof(buf), dest);
}
/*---------------------------------------------------------------------------*/
static void
udp_rx_callback(struct simple_udp_connection *c,
                const uip_ipaddr_t *sender_addr,
                uint16_t sender_port,
                const uip_ipaddr_t *receiver_addr,
                uint16_t receiver_port,
                const uint8_t *data,
                uint16_t datalen)
{
  (void)c; (void)sender_port; (void)receiver_addr; (void)receiver_port;

  if(datalen < 1) {
    return;
  }

  /* ---- START paketi ---- */
  if(data[0] == OTA_PKT_START && datalen >= 11) {
    int fd;

    total_blocks   = ((uint16_t)data[1] << 8) | data[2];
    total_size     = ((uint32_t)data[3] << 24) | ((uint32_t)data[4] << 16) |
                     ((uint32_t)data[5] << 8)  |  (uint32_t)data[6];
    expected_crc32 = ((uint32_t)data[7] << 24) | ((uint32_t)data[8] << 16) |
                     ((uint32_t)data[9] << 8)  |  (uint32_t)data[10];

    next_expected = 0;
    bytes_written = 0;
    ota_state = STATE_RECEIVING;

    LOG_INFO("OTA basladi: %u blok, %lu bayt, beklenen CRC32=0x%08lx\n",
             total_blocks, (unsigned long)total_size,
             (unsigned long)expected_crc32);

    /* Eski dosyayı sil, yenisini oluştur */
    cfs_remove(OTA_FILENAME);
    fd = cfs_open(OTA_FILENAME, CFS_WRITE);
    if(fd < 0) {
      LOG_INFO("CFS: dosya olusturulamadi!\n");
      ota_state = STATE_IDLE;
      return;
    }
    cfs_close(fd);
    LOG_INFO("CFS: '%s' hazir\n", OTA_FILENAME);
    return;
  }

  if(ota_state != STATE_RECEIVING) {
    return;
  }

  /* ---- BLOCK paketi ---- */
  if(data[0] == OTA_PKT_BLOCK && datalen >= 6) {
    uint16_t block_num = ((uint16_t)data[1] << 8) | data[2];
    uint8_t  data_len  = data[3];
    uint16_t recv_crc  = ((uint16_t)data[4] << 8) | data[5];
    const uint8_t *blk_data = data + 6;
    uint16_t calc_crc;
    int fd;

    if(datalen < (uint16_t)(6 + data_len)) {
      LOG_INFO("Blok %u: eksik veri\n", block_num);
      send_nack(sender_addr, block_num);
      return;
    }

    /* CRC16 bütünlük denetimi */
    calc_crc = crc16_ccitt(blk_data, data_len);
    if(calc_crc != recv_crc) {
      LOG_INFO("Blok %u: CRC16 HATASI (hesap=0x%04x, gelen=0x%04x)\n",
               block_num, calc_crc, recv_crc);
      send_nack(sender_addr, block_num);
      return;
    }

    /* Zaten alınmış blok: ACK tekrarla */
    if(block_num < next_expected) {
      LOG_INFO("Blok %u zaten alindi, ACK tekrarlaniyor\n", block_num);
      send_ack(sender_addr, block_num);
      return;
    }

    /* Sıra dışı blok */
    if(block_num != next_expected) {
      LOG_INFO("Blok sirasi hatasi: beklenen %u, gelen %u\n",
               next_expected, block_num);
      send_nack(sender_addr, next_expected);
      return;
    }

    /* CFS'e sıralı olarak yaz (append) */
    fd = cfs_open(OTA_FILENAME, CFS_WRITE | CFS_APPEND);
    if(fd < 0) {
      LOG_INFO("Blok %u: CFS yazma hatasi\n", block_num);
      send_nack(sender_addr, block_num);
      return;
    }
    if(cfs_write(fd, blk_data, data_len) != (int)data_len) {
      LOG_INFO("Blok %u: CFS yazma eksik\n", block_num);
      cfs_close(fd);
      send_nack(sender_addr, block_num);
      return;
    }
    cfs_close(fd);

    bytes_written += data_len;
    next_expected++;

    LOG_INFO("Blok %u/%u alindi [%lu/%lu bayt]\n",
             block_num, total_blocks - 1,
             (unsigned long)bytes_written,
             (unsigned long)total_size);

    send_ack(sender_addr, block_num);

    /* Tüm bloklar alındıysa CRC32 doğrulaması yap */
    if(next_expected >= total_blocks) {
      uint32_t actual_crc;

      LOG_INFO("Tum %u blok alindi. Imaj CRC32 dogrulanıyor...\n", total_blocks);
      actual_crc = compute_image_crc32();
      LOG_INFO("Hesaplanan CRC32: 0x%08lx\n", (unsigned long)actual_crc);

      if(actual_crc == expected_crc32) {
        ota_state = STATE_COMPLETE;
        LOG_INFO("Yuklenmeye hazir yeni firmware alimi tamamlandi.\n");
        send_done(sender_addr);
      } else {
        LOG_INFO("HATA: CRC32 eslesmedi! Beklenen=0x%08lx, Hesaplanan=0x%08lx\n",
                 (unsigned long)expected_crc32,
                 (unsigned long)actual_crc);
        ota_state = STATE_IDLE;
      }
    }
  }
}
/*---------------------------------------------------------------------------*/
PROCESS(udp_server_process, "OTA Receiver");
AUTOSTART_PROCESSES(&udp_server_process);

PROCESS_THREAD(udp_server_process, ev, data)
{
  PROCESS_BEGIN();

  NETSTACK_ROUTING.root_start();

  simple_udp_register(&udp_conn, UDP_SERVER_PORT, NULL,
                      UDP_CLIENT_PORT, udp_rx_callback);

  LOG_INFO("OTA alici hazir (DAG root). Baglanti bekleniyor...\n");

  PROCESS_END();
}
