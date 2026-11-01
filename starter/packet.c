#include "packet.h"

#include <stdlib.h>
#include <string.h>

#include "common.h"

/*
 * create TCP packet with header and space for data of size 'len'
 */
tcp_packet* make_packet(int len) {
  if (len < 0) return NULL;

  size_t packet_size = TCP_HDR_SIZE + len;
  tcp_packet* pkt = malloc(packet_size);

  if (pkt == NULL) {
    VLOG(LOG_WARNING, "Failed to allocate memory for packet");
    return NULL;
  }
  memset(pkt, 0, packet_size);
  pkt->hdr.data_size = len;
  return pkt;
}

/*
 * A simple "getter" to retrieve data size from the packet header.
 */
int get_data_size(const tcp_packet* pkt) {
  return pkt->hdr.data_size;
}