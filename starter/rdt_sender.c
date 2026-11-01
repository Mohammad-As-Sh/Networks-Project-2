#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>  // Required for open()
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "packet.h"

#define WND 10           // Sliding window size (10 packets in flight)
#define RETRY 150        // Timeout in milliseconds (150 ms)
#define MAX_TIMEOUTS 30  // Maximum number of re-transmissions per packet before giving up

/* Structure to track each packet inside the sender's sliding window */
typedef struct {
    tcp_packet* pkt;     // Pointer to the allocated packet
    int len;             // Data payload length
    int seqno;           // Sequence number (byte offset)
    int acked;           // 0 if unacknowledged, 1 if acknowledged
    long long send_time; // Timestamp (in ms) when the packet was last sent
    int retry_count;     // Number of times this packet has timed out
} WindowPacket;

/* Helper function to get the current time in milliseconds */
long long get_current_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int main(int argc, char** argv) {
  int sockfd;
  int portno, len;
  char* hostname;
  char buffer[MSS_SIZE];
  int fd;
  struct sockaddr_in serveraddr;
  socklen_t serverlen;

  int next_seqno = 0;
  int send_base = 0;
  int eof_sent = 0;
  int eof_acked = 0;
  int total_packets_sent = 0;
  
  WindowPacket window[WND];  // Array representing our sliding window of size 10

  // Initialize all window slots as empty/null
  for (int i = 0; i < WND; i++) {
      window[i].pkt = NULL;
      window[i].acked = 1;
      window[i].retry_count = 0;
  }

  if (argc != 4) {
    fprintf(stderr, "usage: %s <hostname> <port> <FILE>\n", argv[0]);
    exit(1);
  }
  hostname = argv[1];
  portno = atoi(argv[2]);

  // Make file name with path
  char filepath[256];
  snprintf(filepath, sizeof(filepath), "sampledata/%s", argv[3]);

  VLOG(LOG_INFO, "Opening file: %s", filepath);
  fd = open(filepath, O_RDONLY);
  if (fd < 0) error(argv[3]);

  /* socket: create the socket */
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0) error("ERROR opening socket");

  /* initialize server details */
  memset(&serveraddr, 0, sizeof(serveraddr));
  serverlen = sizeof(serveraddr);
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_port = htons(portno);
  if (inet_aton(hostname, &serveraddr.sin_addr) == 0) {
    fprintf(stderr, "ERROR, invalid host %s\n", hostname);
    exit(1);
  }
  assert(MAX_DATA_SIZE > 0);

  VLOG(LOG_INFO, "Starting Selective Repeat RDT Sender (WND = %d)", WND);

  // Main transmission loop: runs until the EOF packet is successfully acknowledged
  while (!eof_acked) {
    long long current_time = get_current_time_ms();

    // 1. SLIDING WINDOW SENDER (Pipelining):
    while (!eof_sent && (next_seqno < send_base + WND * MAX_DATA_SIZE)) {
      // FIX: Use sequential slot assignment instead of sequence number division to prevent overwriting!
      int index = total_packets_sent % WND;
      total_packets_sent++;

      len = read(fd, buffer, MAX_DATA_SIZE);
      if (len < 0) error("Error reading from file");

      tcp_packet* sndpkt = make_packet(len);
      if (len > 0) {
          memcpy(sndpkt->data, buffer, len);
      }
      sndpkt->hdr.seqno = next_seqno;
      sndpkt->hdr.ctr_flags = CTRL_FLAG_DATA;

      window[index].pkt = sndpkt;
      window[index].len = len;
      window[index].seqno = next_seqno;
      window[index].acked = 0;
      window[index].send_time = current_time;
      window[index].retry_count = 0;

      VLOG(LOG_DEBUG, "Sending packet with seqno %d (len %d) at window index %d", next_seqno, len, index);
      if (sendto(sockfd, sndpkt, TCP_HDR_SIZE + len, 0, (const struct sockaddr*)&serveraddr, serverlen) < 0) {
        error("sendto");
      }

      if (len == 0) {
        eof_sent = 1;
        VLOG(LOG_INFO, "End of file packet sent.");
        break;
      }

      next_seqno += len;
    }

    // 2. ACKNOWLEDGMENTS (ACKs) & NON-BLOCKING RECV:
    fd_set readfds;
    struct timeval tv;
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);
    tv.tv_sec = 0;
    tv.tv_usec = 10000; // 10 ms check interval

    int activity = select(sockfd + 1, &readfds, NULL, NULL, &tv);
    if (activity > 0 && FD_ISSET(sockfd, &readfds)) {
      char recv_buf[MSS_SIZE];
      ssize_t n = recvfrom(sockfd, recv_buf, MSS_SIZE, 0, (struct sockaddr*)&serveraddr, &serverlen);
      if (n > 0) {
        tcp_packet* recvpkt = (tcp_packet*)recv_buf;
        int ackno = recvpkt->hdr.ackno;
        VLOG(LOG_DEBUG, "Received ACK %d", ackno);

        // SELECTIVE REPEAT: Match the exact packet boundary being acknowledged
        for (int i = 0; i < WND; i++) {
          if (window[i].pkt != NULL && !window[i].acked) {
            int expected_ack = window[i].seqno + (window[i].len > 0 ? window[i].len : 0);
            if (ackno == expected_ack) {
              window[i].acked = 1;
              VLOG(LOG_DEBUG, "Packet %d successfully acknowledged", window[i].seqno);
              if (window[i].len == 0) {
                eof_acked = 1;
              }
            }
          }
        }

        // Slide the window base forward past all consecutively acknowledged packets starting from send_base
        while (1) {
          int found_base = 0;
          for (int i = 0; i < WND; i++) {
            if (window[i].pkt != NULL && window[i].seqno == send_base) {
              if (window[i].acked) {
                if (window[i].len == 0) {
                  eof_acked = 1;
                }
                send_base += window[i].len;
                free(window[i].pkt);
                window[i].pkt = NULL;
                found_base = 1; // Found base, loop again to check next contiguous sequence number
                break;
              }
            }
          }
          if (!found_base) {
            break; // Stop sliding if the current send_base packet is not yet acknowledged!
          }
        }
      }
    }

    if (eof_acked) {
      VLOG(LOG_INFO, "End of file packet acknowledged. Transfer complete.");
      break;
    }

    // 3. TIMEOUT AND RETRANSMISSION:
    current_time = get_current_time_ms();
    for (int i = 0; i < WND; i++) {
      if (window[i].pkt != NULL && !window[i].acked) {
        if (current_time - window[i].send_time > RETRY) {
          window[i].retry_count++;
          if (window[i].retry_count > MAX_TIMEOUTS) {
            if (window[i].len == 0) {
              VLOG(LOG_INFO, "EOF ACK timeout after server shutdown. Transfer successfully completed.");
              eof_acked = 1;
              break;
            }
            VLOG(LOG_WARNING, "Packet %d failed after %d retries. Aborting.", window[i].seqno, window[i].retry_count);
            exit(1);
          }

          VLOG(LOG_INFO, "Timeout! Retransmitting packet %d (Retry %d)", window[i].seqno, window[i].retry_count);
          if (sendto(sockfd, window[i].pkt, TCP_HDR_SIZE + window[i].len, 0,
                     (const struct sockaddr*)&serveraddr, serverlen) < 0) {
            error("sendto");
          }
          window[i].send_time = current_time;
        }
      }
    }
  }

  VLOG(LOG_INFO, "Transfer complete. Closing file and socket.");
  close(fd);
  close(sockfd);
  return 0;
}
