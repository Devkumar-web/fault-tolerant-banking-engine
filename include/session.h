#ifndef SESSION_H
#define SESSION_H
void release_session_semaphore(void);
int acquire_session_lock(const char *role, int id);
void exitClient(int connection_fd, int id);
void cleanupSemaphore(int signum);
void setupSignalHandlers(void);
void cleanup_stale_semaphores(void);
#endif
