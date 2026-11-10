#include "text_utils.h"
#include "app_config.h"
#include "socket_utils.h"

ssize_t send_all(int fd, const void *buf, size_t total_bytes) {
    size_t bytes_sent = 0;
    const char *ptr = (const char *)buf;

    while (bytes_sent < total_bytes) {
        ssize_t n = write(fd, ptr + bytes_sent, total_bytes - bytes_sent);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
            return (ssize_t)bytes_sent;
        }
        bytes_sent += (size_t)n;
    }
    return (ssize_t)bytes_sent;
}

ssize_t recv_all(int fd, void *buf, size_t total_bytes) {
    size_t bytes_read = 0;
    char *ptr = (char *)buf;

    while (bytes_read < total_bytes) {
        ssize_t n = read(fd, ptr + bytes_read, total_bytes - bytes_read);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
            /* Peer closed connection */
            return (ssize_t)bytes_read;
        }
        bytes_read += (size_t)n;
    }
    return (ssize_t)bytes_read;
}

int socket_write_packet(int fd, const char *msg) {
    char packet[PACKET_SIZE];
    memset(packet, 0, sizeof(packet));
    if (msg) {
        copy_text(packet, msg, sizeof(packet));
    }
    ssize_t sent = send_all(fd, packet, sizeof(packet));
    return (sent == (ssize_t)sizeof(packet)) ? 0 : -1;
}

int socket_read_packet(int fd, char *out_buf, size_t max_len) {
    char packet[PACKET_SIZE];
    memset(packet, 0, sizeof(packet));
    ssize_t received = recv_all(fd, packet, sizeof(packet));
    if (received != (ssize_t)sizeof(packet)) {
        return -1;
    }
    packet[sizeof(packet) - 1] = '\0';
    if (out_buf && max_len > 0) {
        copy_text(out_buf, packet, max_len);
        out_buf[max_len - 1] = '\0';
    }
    return 0;
}

int socket_send_ack(int fd, const char *msg) {
    char packet[PACKET_SIZE];
    memset(packet, 0, sizeof(packet));
    if (msg) {
        size_t len = strlen(msg);
        if (len > 0 && msg[len - 1] == '^') {
            copy_text(packet, msg, sizeof(packet));
        } else {
            snprintf(packet, sizeof(packet), "%s^", msg);
        }
    } else {
        strcpy(packet, "^");
    }

    if (socket_write_packet(fd, packet) != 0) return -1;

    char ack[PACKET_SIZE];
    if (socket_read_packet(fd, ack, sizeof(ack)) != 0) return -1;
    return 0;
}
