#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>  // Required for open()
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"
#include "packet.h"

#define WND 10  // Receiver buffer size, same as the sender's window

/* One slot of the receiver buffer: an out-of-order packet we are holding */
typedef struct {
  int used;                  // 0 = empty slot, 1 = holding a packet
  int seqno;                 // Sequence number (byte offset) of the packet
  int len;                   // Payload size
  char data[MAX_DATA_SIZE];  // The payload itself
} BufferSlot;

BufferSlot buffer_slots[WND];

int main(int argc, char** argv) {
  int sockfd;
  int portno;
  socklen_t clientlen;
  struct sockaddr_in serveraddr, clientaddr;
  int optval;
  int fd;
  char buffer[MSS_SIZE];
  tcp_packet* recvpkt;
  tcp_packet* sndpkt;

  int expected_seqno = 0;
  // Variables to "pin" the connection to a single client
  struct sockaddr_in current_client_addr;
  int client_connected = 0;  // Flag to indicate if we are in a session

  if (argc != 3) {
    fprintf(stderr, "usage: %s <port> FILE_RECVD\n", argv[0]);
    exit(1);
  }
  portno = atoi(argv[1]);

  char filepath[256];
  snprintf(filepath, sizeof(filepath), "receiveddata/%s", argv[2]);

  VLOG(LOG_INFO, "Output file will be saved to: %s", filepath);
  fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) error(argv[2]);

  // All buffer slots start out empty
  for (int i = 0; i < WND; i++) {
    buffer_slots[i].used = 0;
  }

  // socket: create the socket
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0)
    error("ERROR opening socket");

  // setsockopt: Handy debugging trick that lets us rerun the server immediately
  // after we kill it; otherwise we have to wait about 20 secs.
  // Eliminates "ERROR on binding: Address already in use" error.
  optval = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));

  memset(&serveraddr, 0, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serveraddr.sin_port = htons((unsigned short)portno);

  if (bind(sockfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr)) < 0)
    error("ERROR on binding");

  VLOG(LOG_INFO, "Server listening on port %d...", portno);

  // Main loop to receive packets and send ACKs
  clientlen = sizeof(clientaddr);
  while (1) {
    if (recvfrom(sockfd, buffer, MSS_SIZE, 0, (struct sockaddr*)&clientaddr, &clientlen) < 0) error("ERROR in recvfrom");

    // Pin connection to the first client that connects
    if (!client_connected) {
      memcpy(&current_client_addr, &clientaddr, sizeof(struct sockaddr_in));
      client_connected = 1;
      VLOG(LOG_INFO, "Connection started with client %s:%d", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
    } else if (memcmp(&clientaddr, &current_client_addr, sizeof(struct sockaddr_in)) != 0) {
      VLOG(LOG_WARNING, "Ignoring packet from unexpected source %s:%d",
           inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
      continue;  // Ignore this packet and wait for the next one
    }

    recvpkt = (tcp_packet*)buffer;
    assert(get_data_size(recvpkt) <= MAX_DATA_SIZE);

    int seqno = recvpkt->hdr.seqno;
    int len = recvpkt->hdr.data_size;
    int is_eof = 0;
    int send_ack = 1;

    if (seqno == expected_seqno) {
      // This is the packet we were waiting for, so write it straight to the file
      VLOG(LOG_DEBUG, "Received expected packet %d", seqno);
      if (len > 0) {
        if (pwrite(fd, recvpkt->data, len, seqno) < 0) error("ERROR in pwrite");
        expected_seqno += len;
      } else {
        is_eof = 1;  // A packet with no data marks the end of the file
      }

      // Now deliver any buffered packets that come right after it.
      // Keep looping because each delivery can unlock the next one.
      int found = 1;
      while (found && !is_eof) {
        found = 0;
        for (int i = 0; i < WND; i++) {
          if (buffer_slots[i].used && buffer_slots[i].seqno == expected_seqno) {
            if (buffer_slots[i].len > 0) {
              if (pwrite(fd, buffer_slots[i].data, buffer_slots[i].len, buffer_slots[i].seqno) < 0) {
                error("ERROR in pwrite");
              }
              VLOG(LOG_DEBUG, "Delivered buffered packet %d", buffer_slots[i].seqno);
              expected_seqno += buffer_slots[i].len;
            } else {
              is_eof = 1;  // The end of file packet was buffered earlier
            }
            buffer_slots[i].used = 0;
            found = 1;
            break;
          }
        }
      }
    } else if (seqno > expected_seqno) {
      // Out of order packet. We must not throw it away, so keep it in memory.
      int already_buffered = 0;
      for (int i = 0; i < WND; i++) {
        if (buffer_slots[i].used && buffer_slots[i].seqno == seqno) already_buffered = 1;
      }

      if (already_buffered) {
        VLOG(LOG_DEBUG, "Packet %d is already in the buffer. Re-sending ACK.", seqno);
      } else {
        // Find a free slot to store it in
        int slot = -1;
        for (int i = 0; i < WND; i++) {
          if (!buffer_slots[i].used) {
            slot = i;
            break;
          }
        }

        if (slot == -1) {
          // No room left. Drop it without an ACK so the sender sends it again later.
          VLOG(LOG_WARNING, "Buffer is full, dropping packet %d", seqno);
          send_ack = 0;
        } else {
          buffer_slots[slot].used = 1;
          buffer_slots[slot].seqno = seqno;
          buffer_slots[slot].len = len;
          if (len > 0) memcpy(buffer_slots[slot].data, recvpkt->data, len);
          VLOG(LOG_INFO, "Buffered out of order packet %d (expected %d) in slot %d", seqno, expected_seqno, slot);
        }
      }
    } else {
      // Old packet we have already written. Just ACK it again so the sender stops resending.
      VLOG(LOG_INFO, "Received duplicate/old packet %d (expected %d). Re-sending ACK.", seqno, expected_seqno);
    }

    // ACK every packet we accepted, in order or not.
    // The ACK carries seqno + data_size, which is what the sender matches against.
    if (send_ack) {
      sndpkt = make_packet(0);
      sndpkt->hdr.ackno = seqno + len;
      sndpkt->hdr.ctr_flags = CTRL_FLAG_ACK;
      if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (struct sockaddr*)&clientaddr, clientlen) < 0) {
        error("ERROR in sendto");
      }
      free(sndpkt);
    }

    if (is_eof) {
      VLOG(LOG_INFO, "End Of File packet acknowledged. Transfer complete. Shutting down.");
      close(fd);
      break;
    }
  }

  return 0;
}
