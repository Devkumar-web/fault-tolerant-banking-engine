#ifndef COMMAND_SERVICE_H
#define COMMAND_SERVICE_H
/* Owns no descriptors. Authenticated identity lives only for this connection. */
void command_session(int fd, int frame_timeout_ms, int session_timeout_ms);
#endif
