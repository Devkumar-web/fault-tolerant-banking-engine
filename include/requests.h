#ifndef REQUESTS_H
#define REQUESTS_H
#include "protocol.h"
#define REQUEST_STORE_PATH "requests_v2.bin"
#define REQUEST_SLOT_SIZE 128
#define REQUEST_CAPACITY 4096
/* Startup only, before workers. Preserve files; synchronize new directory entries. */
int requests_init(void);
/* identity must come from successful authentication, never from transaction payload.
 * Serialized across processes. Exact same key and payload returns saved result.
 * Conflict never mutates data. Errors/uncertain commits are not cached as final.
 */
WireReply request_execute(int identity, const WireFrame *request, int64_t deadline);
#endif
