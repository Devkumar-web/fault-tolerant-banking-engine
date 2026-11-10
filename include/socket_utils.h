#ifndef SOCKET_UTILS_H
#define SOCKET_UTILS_H

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#define PACKET_SIZE 4096

/*
 * send_all:
 * Robust loop guaranteeing that all 'total_bytes' are sent over TCP stream,
 * even if fragmented across multiple kernel write calls or interrupted by signals.
 */
ssize_t send_all(int fd, const void *buf, size_t total_bytes);

/*
 * recv_all:
 * Robust loop guaranteeing that all 'total_bytes' are read from TCP stream,
 * handling partial packet reads and signal interruptions.
 * Returns total_bytes on success, 0 on clean peer disconnect (EOF), -1 on error.
 */
ssize_t recv_all(int fd, void *buf, size_t total_bytes);

/*
 * socket_write_packet:
 * Formats and transmits exactly PACKET_SIZE (4096) bytes.
 */
int socket_write_packet(int fd, const char *msg);

/*
 * socket_read_packet:
 * Receives exactly PACKET_SIZE (4096) bytes and safely null-terminates.
 * Returns 0 on success, -1 on disconnect/error.
 */
int socket_read_packet(int fd, char *out_buf, size_t max_len);

/*
 * socket_send_ack:
 * Sends an acknowledgement / notification message ending with '^' delimiter,
 * and waits for client's lock-step acknowledgement response.
 */
int socket_send_ack(int fd, const char *msg);

#endif /* SOCKET_UTILS_H */

