#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#define WIRE_MAGIC UINT32_C(0x42414e4b)
#define WIRE_VERSION 2
#define WIRE_HEADER 24
#define WIRE_MAX_PAYLOAD 256
#define WIRE_RESPONSE 0x8000
#define WIRE_TIMEOUT_MS 5000
#define WIRE_SESSION_MS 60000
#define WIRE_MAX_COMMANDS 128
#define WIRE_MAX_CLIENTS 16
/* All wire integers are unsigned, big-endian. No native structs go on wire. */
enum WireCommand { CMD_LOGIN=1, CMD_BALANCE=2, CMD_DEPOSIT=3,
    CMD_WITHDRAW=4, CMD_TRANSFER=5, CMD_LOGOUT=6 };
enum WireStatus { WIRE_OK=0, WIRE_INVALID=1, WIRE_NOT_FOUND=2, WIRE_INACTIVE=3,
    WIRE_INSUFFICIENT=4, WIRE_LIMIT=5, WIRE_IO=6, WIRE_RECOVERY=7,
    WIRE_AUTH=8, WIRE_CONFLICT=9, WIRE_CAPACITY=10, WIRE_BUSY=11 };
typedef struct { uint16_t command; uint32_t length; uint64_t request_id;
    uint8_t payload[WIRE_MAX_PAYLOAD]; } WireFrame;
typedef struct { uint32_t status; uint64_t transaction_id; uint64_t balance; } WireReply;
uint32_t wire_u32(const uint8_t *p);
uint64_t wire_u64(const uint8_t *p);
void wire_put32(uint8_t *p, uint32_t v);
void wire_put64(uint8_t *p, uint64_t v);
int64_t wire_now_ms(void);
/* Deadlines are absolute CLOCK_MONOTONIC milliseconds and cover entire frame.
 * Return 0 success, -1 error/EOF (errno includes ETIMEDOUT or EPROTO).
 * Borrow fd; never allocate, close, or change its blocking mode.
 */
int wire_read(int fd, WireFrame *frame, int64_t deadline);
int wire_write(int fd, const WireFrame *frame, int64_t deadline);
void wire_reply_frame(WireFrame *out, const WireFrame *request, WireReply reply);
#endif
