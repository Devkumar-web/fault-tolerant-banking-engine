#ifndef AUTHENTICATION_H
#define AUTHENTICATION_H
/* Compatibility statuses for existing menu callers. Successful login owns
 * one process-local session lock until release_session_semaphore() or exit.
 * The connection argument is retained for source compatibility, not used.
 */
enum AuthStatus { AUTH_OK = 1, AUTH_NOT_FOUND = -1, AUTH_BUSY = -2,
    AUTH_WRONG_ROLE = -3, AUTH_BAD_PASSWORD = -4, AUTH_INACTIVE = -5,
    AUTH_SYSTEM_ERROR = -6 };
int loginCustomer(int connection_fd, int accountNumber, char *password, int requestedType);
int loginEmployee(int connection_fd, int empID, char *password);
int loginManager(int connection_fd, int mngID, const char *password);
#endif
