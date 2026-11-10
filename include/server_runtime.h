#ifndef SERVER_RUNTIME_H
#define SERVER_RUNTIME_H
/* Borrow listening socket. Fixed worker limit; reaps every child before return.
 * supervisor_fd remains open in children to hold singleton lock after parent death.
 */
int server_run(int listener, int frame_timeout_ms, int session_timeout_ms);
#endif
