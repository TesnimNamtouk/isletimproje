#include "contiki.h"
#include "net/routing/routing.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include "sys/node-id.h"
#include "sys/log.h"
#include "ota-metadata.h"
#include "new-firmware-data.h"
#include <stdint.h>
#include <string.h>

#define LOG_MODULE "OTA-Sender"
#define LOG_LEVEL  LOG_LEVEL_INFO
#define UDP_CLIENT_PORT   8765
#define UDP_SERVER_PORT   5678
#define OTA_BLOCK_SIZE    64
#define OTA_TIMEOUT_S     5
#define OTA_MAX_RETRIES   8
#define OTA_PKT_START  0x01u
#define OTA_PKT_BLOCK  0x02u
#define OTA_PKT_ACK    0x03u
#define OTA_PKT_NACK   0x04u
#define OTA_PKT_DONE   0x05u

static struct simple_udp_connection udp_conn;
static uip_ipaddr_t server_addr;
static volatile uint8_t  ack_received;
static volatile uint8_t  nack_received;
static volatile uint8_t  done_received;
static volatile uint16_t acked_block_num;

PROCESS(udp_client_process, "OTA Sender");
AUTOSTART_PROCESSES(&udp_client_process);

static uint16_t crc16_ccitt(const uint8_t *buf, uint16_t len) {
  uint16_t crc = 0xFFFFu; uint16_t i; int bit;
  for(i=0;i<len;i++){crc^=(uint16_t)buf[i]<<8;for(bit=0;bit<8;bit++){if(crc&0x8000u)crc=(crc<<1)^0x1021u;else crc<<=1;}}
  return crc;
}

static void send_ota_start(uint16_t total_blocks, uint32_t total_size, uint32_t crc32) {
  uint8_t buf[11];
  buf[0]=OTA_PKT_START; buf[1]=(uint8_t)(total_blocks>>8); buf[2]=(uint8_t)(total_blocks&0xFF);
  buf[3]=(uint8_t)(total_size>>24); buf[4]=(uint8_t)(total_size>>16); buf[5]=(uint8_t)(total_size>>8); buf[6]=(uint8_t)(total_size&0xFF);
  buf[7]=(uint8_t)(crc32>>24); buf[8]=(uint8_t)(crc32>>16); buf[9]=(uint8_t)(crc32>>8); buf[10]=(uint8_t)(crc32&0xFF);
  simple_udp_sendto(&udp_conn, buf, sizeof(buf), &server_addr);
}

static void send_ota_block(uint16_t block_num) {
  uint8_t buf[6+OTA_BLOCK_SIZE];
  uint32_t offset=(uint32_t)block_num*OTA_BLOCK_SIZE;
  uint16_t data_len, ck;
  if(offset>=NEW_FIRMWARE_SIZE) return;
  data_len=OTA_BLOCK_SIZE;
  if(offset+data_len>NEW_FIRMWARE_SIZE) data_len=(uint16_t)(NEW_FIRMWARE_SIZE-offset);
  ck=crc16_ccitt(new_firmware_data+offset,data_len);
  buf[0]=OTA_PKT_BLOCK; buf[1]=(uint8_t)(block_num>>8); buf[2]=(uint8_t)(block_num&0xFF);
  buf[3]=(uint8_t)data_len; buf[4]=(uint8_t)(ck>>8); buf[5]=(uint8_t)(ck&0xFF);
  memcpy(buf+6, new_firmware_data+offset, data_len);
  simple_udp_sendto(&udp_conn, buf, 6+data_len, &server_addr);
}

static void udp_rx_callback(struct simple_udp_connection *c,
  const uip_ipaddr_t *sender_addr, uint16_t sender_port,
  const uip_ipaddr_t *receiver_addr, uint16_t receiver_port,
  const uint8_t *data, uint16_t datalen) {
  (void)c;(void)sender_addr;(void)sender_port;(void)receiver_addr;(void)receiver_port;
  if(datalen<1) return;
  if(data[0]==OTA_PKT_ACK&&datalen>=3){acked_block_num=((uint16_t)data[1]<<8)|data[2];ack_received=1;process_poll(&udp_client_process);}
  else if(data[0]==OTA_PKT_NACK&&datalen>=3){acked_block_num=((uint16_t)data[1]<<8)|data[2];nack_received=1;process_poll(&udp_client_process);}
  else if(data[0]==OTA_PKT_DONE){LOG_INFO("Alici onayladi: OTA transferi tamamlandi!\n");done_received=1;process_poll(&udp_client_process);}
}

PROCESS_THREAD(udp_client_process, ev, data) {
  static struct etimer wait_timer, ack_timer;
  static uint16_t current_block, total_blocks;
  static uint8_t retries;
  static uint32_t image_crc32;
  PROCESS_BEGIN();
  simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL, UDP_SERVER_PORT, udp_rx_callback);
  if(node_id!=2){LOG_INFO("Node %u: komsu role modunda\n",node_id);while(1){PROCESS_WAIT_EVENT();}}
  LOG_INFO("Node 2: ag hazir olana kadar bekleniyor...\n");
  etimer_set(&wait_timer,30*CLOCK_SECOND);
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&wait_timer));
  while(!NETSTACK_ROUTING.node_is_reachable()||!NETSTACK_ROUTING.get_root_ipaddr(&server_addr)){
    LOG_INFO("Ag hazir degil, 5s bekleniyor...\n");
    etimer_set(&wait_timer,5*CLOCK_SECOND);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&wait_timer));
  }
  total_blocks=(uint16_t)(((uint32_t)NEW_FIRMWARE_SIZE+OTA_BLOCK_SIZE-1)/OTA_BLOCK_SIZE);
  image_crc32=ota_crc32_buffer(new_firmware_data,(uint32_t)NEW_FIRMWARE_SIZE);
  current_block=0; ack_received=0; nack_received=0; done_received=0;
  LOG_INFO("OTA basliyor: %u blok, toplam %lu bayt, CRC32=0x%08lx\n",total_blocks,(unsigned long)NEW_FIRMWARE_SIZE,(unsigned long)image_crc32);
  send_ota_start(total_blocks,(uint32_t)NEW_FIRMWARE_SIZE,image_crc32);
  etimer_set(&wait_timer,2*CLOCK_SECOND);
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&wait_timer));
  while(current_block<total_blocks&&!done_received){
    retries=0; ack_received=0; nack_received=0;
    while(retries<OTA_MAX_RETRIES){
      if(retries>0) LOG_INFO("Blok %u yeniden gonderiliyor (%u/%u)\n",current_block,retries,OTA_MAX_RETRIES);
      send_ota_block(current_block);
      etimer_set(&ack_timer,OTA_TIMEOUT_S*CLOCK_SECOND);
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&ack_timer)||ev==PROCESS_EVENT_POLL);
      if(ack_received&&acked_block_num==current_block){
        LOG_INFO("ACK: blok %u/%u onaylandi\n",current_block,total_blocks-1);
        current_block++; ack_received=0; break;
      } else if(nack_received){
        LOG_INFO("NACK: blok %u bozuk\n",acked_block_num);
        current_block=acked_block_num; nack_received=0; ack_received=0; retries++;
      } else {
        LOG_INFO("Blok %u zaman asimi (deneme %u)\n",current_block,retries+1);
        retries++; ack_received=0; nack_received=0;
      }
    }
    if(retries>=OTA_MAX_RETRIES){LOG_INFO("HATA: Blok %u gonderilemedi.\n",current_block);break;}
  }
  if(current_block>=total_blocks){
    LOG_INFO("Tum bloklar gonderildi. Alici dogrulamasi bekleniyor...\n");
    etimer_set(&wait_timer,10*CLOCK_SECOND);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&wait_timer)||ev==PROCESS_EVENT_POLL);
    LOG_INFO("OTA gonderi sureci tamamlandi.\n");
  }
  PROCESS_END();
}
