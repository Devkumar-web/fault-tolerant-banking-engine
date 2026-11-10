#include <sys/file.h>
#include "session.h"
#include "app_config.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sem.h>
#include <netinet/ip.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <crypt.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <wait.h>
#include <semaphore.h>
#include <limits.h>
#include <dirent.h>
#include <stdint.h>







void employeeMenu(int connection_fd);
void managerMenu(int connection_fd);
void adminMenu(int connection_fd);
void connectionHandler(int connection_fd);
void exitClient(int connection_fd, int id);
void cleanupSemaphore(int signum);
void setupSignalHandlers(void);
sem_t *initializeSemaphore(int accountNumber);
void release_session_semaphore(void);
int tryAcquireSessionSemaphore(void);
void cleanup_stale_semaphores(void);

#include "bank_fun.h"
#include "socket_utils.h"
#include "financial_utils.h"
#include "account_storage.h"
#include "txn_journal.h"
#include "customer_functions.h"
#include "employee_functions.h"
#include "manager_functions.h"
#include "admin_functions.h"
















static void server_sigint_handler(int signum) {
    /* Keep the signal handler async-signal-safe. Named semaphores are cleaned
       on the next startup, before the server accepts any client. */
    (void)signum;
    _exit(0);
}


/* Upgrade the old feedback record layout (text only) to the account-attributed
   layout. Legacy entries are preserved and marked with accountNumber = -1. */
static int migrate_feedback_file(void) {
    const char *path = FEEDPATH;
    const char *tmp_path = "feedback.txt.migrate.tmp";
    struct stat st;
    if (stat(path, &st) != 0) return errno == ENOENT ? 0 : -1;
    if (st.st_size == 0 || st.st_size % (off_t)sizeof(struct FeedBack) == 0) return 0;

    struct LegacyFeedback { char feedback[1024]; } old_fb;
    if (st.st_size % (off_t)sizeof(old_fb) != 0) {
        fprintf(stderr, "[STARTUP] Feedback file has an unsupported size.\n");
        return -1;
    }

    int in = open(path, O_RDONLY);
    if (in < 0) return -1;
    int out = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (out < 0) { close(in); return -1; }

    int rc = 0;
    while (read(in, &old_fb, sizeof(old_fb)) == (ssize_t)sizeof(old_fb)) {
        struct FeedBack current;
        memset(&current, 0, sizeof(current));
        current.accountNumber = -1; /* Sender was not stored in the old format. */
        memcpy(current.feedback, old_fb.feedback, sizeof(old_fb.feedback));
        current.feedback[sizeof(current.feedback) - 1] = '\0';
        if (write(out, &current, sizeof(current)) != (ssize_t)sizeof(current)) {
            rc = -1; break;
        }
    }
    if (fsync(out) != 0) rc = -1;
    if (close(in) != 0 || close(out) != 0) rc = -1;
    if (rc == 0 && rename(tmp_path, path) != 0) rc = -1;
    if (rc != 0) unlink(tmp_path);
    return rc;
}

int main(int argc, char *argv[]) {
    int socket_fd, connection_fd;
    int isBind, isListen;
    socklen_t clientSize;
    struct sockaddr_in address, client;
    int port = 8080;
    if (argc > 1) {
        char *end = NULL;
        errno = 0;
        long parsed_port = strtol(argv[1], &end, 10);
        if (errno != 0 || end == argv[1] || *end != '\0' ||
            parsed_port < 1 || parsed_port > 65535) {
            fprintf(stderr, "Invalid port '%s': expected an integer in [1, 65535].\n", argv[1]);
            return EXIT_FAILURE;
        }
        port = (int)parsed_port;
    }

    int guard = open(".server_v2.lock", O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
    if (guard < 0 || flock(guard, LOCK_EX | LOCK_NB) != 0) {
        fprintf(stderr, "Data directory already in use\n");
        return EXIT_FAILURE;
    }
    // Register server-level shutdown handlers for graceful semaphore cleanup
    signal(SIGINT, server_sigint_handler);
    signal(SIGTERM, server_sigint_handler);

    // Ignore SIGCHLD to avoid zombie child processes
    signal(SIGCHLD, SIG_IGN);
    // Ignore SIGPIPE to avoid termination when client abruptly disconnects
    signal(SIGPIPE, SIG_IGN);

    /* Master database legacy migration check (detects 324-byte float structs) */
    printf("[STARTUP] Checking customer database format...\n");
    int migration_status = migrate_customers_if_legacy(CUSPATH);
    if (migration_status < 0) {
        fprintf(stderr, "[STARTUP] ERROR: customer database migration/validation failed.\n");
        return EXIT_FAILURE;
    }
    if (migration_status > 0) {
        printf("[STARTUP] Migration to integer minor units (paise) completed successfully.\n");
    }

    printf("[STARTUP] Checking customer feedback record format...\n");
    if (migrate_feedback_file() != 0) {
        fprintf(stderr, "[STARTUP] ERROR: feedback migration failed; server will not start.\n");
        return EXIT_FAILURE;
    }

    /* Automatic Write-Ahead Journal roll-forward crash recovery */
    printf("[STARTUP] Checking transaction journal for pending crash recovery...\n");
    int recovered = journal_recover_at_startup(CUSPATH);
    if (recovered < 0) {
        fprintf(stderr, "[STARTUP] ERROR: transaction journal recovery failed.\n"
                        "The server will not accept clients until persistent data is repaired.\n");
        return EXIT_FAILURE;
    }
    if (recovered > 0) {
        printf("[STARTUP] Recovered and reconciled %d interrupted transactions.\n", recovered);
    }

    /* Reconcile committed journal entries, then add honest baseline snapshots
       for legacy accounts whose previous transaction history is unavailable. */
    if (journal_reconcile_history() != 0 ||
        journal_initialize_missing_account_history(CUSPATH) != 0) {
        fprintf(stderr, "[STARTUP] ERROR: transaction-history initialization failed.\n"
                        "The server will not accept clients until history storage is repaired.\n");
        return EXIT_FAILURE;
    }

    memset(&address, 0, sizeof(address));
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);

    // Creating & Binding Socket with retry for transient EADDRINUSE during rapid restart
    for (int retry = 0; retry < 10; retry++) {
        socket_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd == -1) {
            perror("Socket creation failed");
            exit(-1);
        }

        int opt = 1;
        setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        isBind = bind(socket_fd, (struct sockaddr *)&address, sizeof(address));
        if (isBind == 0) break;

        close(socket_fd);
        if (errno == EADDRINUSE && retry < 9) {
            usleep(300000); /* wait 300ms for kernel port release */
            continue;
        }
        break;
    }

    if (isBind == -1 && argc <= 1 && errno == EADDRINUSE) {
        printf("Port 8080 is already in use, trying fallback port 8081...\n");
        port = 8081;
        address.sin_port = htons((uint16_t)port);
        socket_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd != -1) {
            int opt = 1;
            setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            isBind = bind(socket_fd, (struct sockaddr *)&address, sizeof(address));
        }
    }
    if (isBind == -1) {
        perror("Binding failed");
        exit(-1);
    }
    printf("Server Socket Created successfully\n");
    printf("Binding successful on port %d!\n", port);

    // Listening for connection with backlog 10
    isListen = listen(socket_fd, 10);
    if (isListen == -1) {
        perror("Listen failed");
        exit(-1);
    }
    printf("Server listening for incoming connections...\n");

    /* Clean stale session names only after this process successfully owns the port. */
    printf("[STARTUP] Checking for stale session semaphores...\n");
    cleanup_stale_semaphores();

    while (1) {
        clientSize = sizeof(client);
        connection_fd = accept(socket_fd, (struct sockaddr *)&client, &clientSize);
    
        if (connection_fd == -1) {
            if (errno == EINTR) continue;
            perror("Accept Failed!");
            continue;
        }

        pid_t pid = fork();
        if (pid == 0) {
            // Child process handles client
            close(socket_fd);
            atexit(release_session_semaphore);
            setupSignalHandlers();
            connectionHandler(connection_fd);
            release_session_semaphore();
            close(connection_fd);
            exit(0);
        } else if (pid > 0) {
            // Parent closes connected socket descriptor
            close(connection_fd);
        } else {
            perror("Fork failed");
            close(connection_fd);
        }
    }
    close(socket_fd);
    return 0;
}

// Handling client connection
void connectionHandler(int connection_fd) {
    char readBuffer[PACKET_SIZE];
    int choice;

    while (1) {
        if (socket_write_packet(connection_fd, MAINMENU) != 0) {
            printf("Client disconnected.\n");
            return;
        }

        if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
            printf("Client disconnected or unable to read data.\n");
            return;
        }

        choice = atoi(readBuffer);
        printf("Client selected main menu option: %d\n", choice);
        switch (choice) {
            case 1:
                // Normal User Login (Requirement d)
                customerMenu(connection_fd, 1);
                break;
            case 2:
                // Joint Account User Login (Requirement d)
                customerMenu(connection_fd, 2);
                break;
            case 3:
                // Administrator Login (Requirement d & e)
                adminMenu(connection_fd);
                break;
            case 4:
                // Bank Employee Login (Senior Feature)
                employeeMenu(connection_fd);
                break;
            case 5:
                // Bank Manager Login (Senior Feature)
                managerMenu(connection_fd);
                break;
            case 6:
                // Exit
                exitClient(connection_fd, 0);
                return;
            default:
                printf("Invalid main menu input from client: %s\n", readBuffer);
                socket_send_ack(connection_fd, "Invalid choice for main menu! Please select 1-6.^");
                break;
        }
    }
}
