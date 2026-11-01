#ifndef PACKET_H_INCLUDED
#define PACKET_H_INCLUDED

#include <stddef.h>  // For size_t

/*
 * Using bitmasks for control flags. This allows combining flags
 * in the future (e.g., ACK + FIN).
 */
#define CTRL_FLAG_DATA 0x1
#define CTRL_FLAG_ACK 0x2

typedef struct {
  int seqno;
  int ackno;
  int ctr_flags;  // Should contain flags like CTRL_FLAG_DATA
  int data_size;
} tcp_header;

#define MSS_SIZE 1500
#define UDP_HDR_SIZE 8
#define IP_HDR_SIZE 20
#define TCP_HDR_SIZE sizeof(tcp_header)
#define MAX_DATA_SIZE (MSS_SIZE - TCP_HDR_SIZE - UDP_HDR_SIZE - IP_HDR_SIZE)

typedef struct {
  tcp_header hdr;
  char data[];
} tcp_packet;

/*
 * Allocates and initializes a TCP packet.
 * Returns NULL on memory allocation failure.
 *
 */
tcp_packet* make_packet(int len);

/* Returns the size of the data payload in the packet. */
int get_data_size(const tcp_packet* pkt);

#endif