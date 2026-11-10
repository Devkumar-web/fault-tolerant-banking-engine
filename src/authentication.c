#include "authentication.h"
#include "app_config.h"
#include "session.h"
#include "customer_functions.h"
#include "employee_functions.h"
#include "manager_functions.h"
#include <crypt.h>
int loginCustomer(int connection_fd, int accountNumber, char *password, int requestedType) {
    (void)connection_fd;
    if (accountNumber <= 0 || !password) return -1;

    int file = open(CUSPATH, O_RDONLY, 0644);
    if (file == -1) {
        perror("Error opening customer file");
        return -1;
    }

    off_t offset = -1;
    struct Customer customer;
    int found = find_customer_record(file, accountNumber, &offset, &customer);
    close(file);

    if (found != 0) {
        return -1; /* Account not found */
    }

    /* Verify active status */
    if (customer.activeStatus != 1) {
        return -5; /* Deactivated account */
    }

    /* Verify hashed password */
    char *hashed = crypt(password, HASHKEY);
    if (!hashed || strcmp(customer.password, hashed) != 0) {
        return -4; /* Incorrect password */
    }

    /* Verify requested login type matches account type */
    if (customer.accountType != requestedType) {
        return -3; /* Account type mismatch (Normal vs Joint) */
    }

    /* Enforce one session for this account for both normal and joint login.
       The lock-file namespace is shared by all server ports. */
    if (acquire_session_lock("customer", accountNumber) != 0) {
        if (errno == EACCES || errno == EAGAIN) return -2;
        perror("Unable to acquire customer session lock");
        return -6;
    }

    printf("Customer %d (%s) logged in as %s\n",
           accountNumber, customer.customerName,
           requestedType == 2 ? "Joint Account User" : "Normal User");
    return 1;
}

int loginEmployee(int connection_fd, int empID, char *password) {
    (void)connection_fd;
    if (empID <= 0 || !password) return -1;

    struct Employee employee;
    int file = open(EMPPATH, O_RDONLY, 0644);
    if (file == -1) return -1;

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_RDLCK;
    fcntl(file, F_SETLKW, &fl);

    int found = 0;
    int is_employee = 0;
    int pw_match = 0;
    char *h = crypt(password, HASHKEY);

    while (read(file, &employee, sizeof(employee)) == (ssize_t)sizeof(employee)) {
        if (employee.empID == empID) {
            found = 1;
            if (employee.role == 1) {
                is_employee = 1;
                if (h && strcmp(employee.password, h) == 0) {
                    pw_match = 1;
                }
            }
            break;
        }
    }

    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    if (!found) return -1;       /* Employee ID not found */
    if (!is_employee) return -3;  /* ID belongs to manager, not employee */
    if (!pw_match) return -4;    /* Password incorrect */

    /* One active session per employee identity across all server ports. */
    if (acquire_session_lock("employee", empID) != 0) {
        if (errno == EACCES || errno == EAGAIN) return -2;
        perror("Unable to acquire employee session lock");
        return -6;
    }

    return 1; /* Success */
}

int loginManager(int connection_fd, int mngID, const char *password) {
    (void)connection_fd;
    if (mngID <= 0 || !password) return -1;

    struct Employee mng;
    int file = open(EMPPATH, O_RDONLY, 0644);
    if (file == -1) {
        perror("Error opening employee file");
        return -1;
    }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_RDLCK;
    fcntl(file, F_SETLKW, &fl);

    int found = 0;
    int is_manager = 0;
    int pw_match = 0;
    char *h = crypt(password, HASHKEY);

    while (read(file, &mng, sizeof(mng)) == (ssize_t)sizeof(mng)) {
        if (mng.empID == mngID) {
            found = 1;
            if (mng.role == 0) {
                is_manager = 1;
                if (h && strcmp(mng.password, h) == 0) {
                    pw_match = 1;
                }
            }
            break;
        }
    }

    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    if (!found) return -1;       /* Manager ID not found */
    if (!is_manager) return -3;  /* ID exists but is not a Manager */
    if (!pw_match) return -4;    /* Password incorrect */

    /* One active session per manager identity across all server ports. */
    if (acquire_session_lock("manager", mngID) != 0) {
        if (errno == EACCES || errno == EAGAIN) return -2;
        perror("Unable to acquire manager session lock");
        return -6;
    }

    return 1; /* Success */
}
