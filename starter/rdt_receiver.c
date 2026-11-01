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

    // handle in-order vs. duplicate packets
    int is_eof = 0;
    if (recvpkt->hdr.seqno == expected_seqno) {
      // This is the packet we were waiting for
      VLOG(LOG_DEBUG, "Received expected packet %d", expected_seqno);
      if (recvpkt->hdr.data_size > 0) {
        if (pwrite(fd, recvpkt->data, recvpkt->hdr.data_size, recvpkt->hdr.seqno) < 0) {
          error("ERROR in pwrite");
        }
      } else {
        is_eof = 1;
      }
      // Update our state to expect the next packet
      expected_seqno += recvpkt->hdr.data_size;
    } else {
      VLOG(LOG_INFO, "Received duplicate/old packet %d (expected %d). Re-sending ACK.",
           recvpkt->hdr.seqno, expected_seqno);
    }

    // Send ACK for what the sender *thinks* it sent.
    // If it was a duplicate, this re-sends the needed ACK.
    sndpkt = make_packet(0);
    sndpkt->hdr.ackno = recvpkt->hdr.seqno + recvpkt->hdr.data_size;
    sndpkt->hdr.ctr_flags = CTRL_FLAG_ACK;
    if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (struct sockaddr*)&clientaddr, clientlen) < 0) {
      error("ERROR in sendto");
    }
    free(sndpkt);

    if (is_eof) {
      VLOG(LOG_INFO, "End Of File packet acknowledged. Transfer complete. Shutting down.");
      close(fd);
      break;
    }
  }

  return 0;
}
