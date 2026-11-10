#include "session.h"
#include "socket_utils.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
static int session_lock_fd = -1;
static char semName[128];
void release_session_semaphore(void) {
    /* Kernel releases the process-associated record lock when this FD closes
       or the child exits, including abnormal process termination. */
    if (session_lock_fd >= 0) {
        close(session_lock_fd);
        session_lock_fd = -1;
    }
    semName[0] = '\0';
}

int acquire_session_lock(const char *role, int id) {
    if (!role || id <= 0 || session_lock_fd >= 0) { errno = EINVAL; return -1; }
    if (mkdir(".session_locks", 0700) == -1 && errno != EEXIST) return -1;
    char path[PATH_MAX];
    int n = snprintf(path, sizeof(path), ".session_locks/%s_%d.lock", role, id);
    if (n < 0 || (size_t)n >= sizeof(path)) { errno = ENAMETOOLONG; return -1; }
#ifdef O_NOFOLLOW
    int fd = open(path, O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
#else
    int fd = open(path, O_RDWR | O_CREAT, 0600);
#endif
    if (fd < 0) return -1;
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK; fl.l_whence = SEEK_SET; fl.l_start = 0; fl.l_len = 0;
    if (fcntl(fd, F_SETLK, &fl) == -1) {
        int saved = errno; close(fd); errno = saved; return -1;
    }
    session_lock_fd = fd;
    snprintf(semName, sizeof(semName), "%s:%d", role, id);
    return 0;
}

void exitClient(int connection_fd, int id) {
    if (id > 0) {
        release_session_semaphore();
    }

    socket_write_packet(connection_fd, "Client logging out...\n");
}

void cleanupSemaphore(int signum) {
    if (session_lock_fd >= 0) close(session_lock_fd);
    _exit(signum);
}

void setupSignalHandlers(void) {
    signal(SIGINT, cleanupSemaphore); 
    signal(SIGTERM, cleanupSemaphore); 
    signal(SIGHUP, cleanupSemaphore);  
    signal(SIGQUIT, cleanupSemaphore);
}

void cleanup_stale_semaphores(void) { /* fcntl session locks need no stale-name cleanup. */ }
