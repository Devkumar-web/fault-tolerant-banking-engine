#ifndef ADMIN_FUNCTIONS_H
#define ADMIN_FUNCTIONS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <crypt.h>
#include <time.h>
#include <errno.h>

#include "bank_fun.h"
#include "socket_utils.h"
#include "financial_utils.h"
#include "account_storage.h"
#include "txn_journal.h"

#define ADMINNAME "admin"
#define PASSWORD "admin@123"

#ifndef ADMINPATH
#define ADMINPATH "admin.txt"
#endif

/* Forward declarations */
void adminMenu(int connection_fd);
int addAccountAdmin(int connection_fd);
int deleteAccountAdmin(int connection_fd);
int modifyAccountAdmin(int connection_fd);
int searchAccountAdmin(int connection_fd);
int addEmployee(int connection_fd);
void modifyEmployeeAdmin(int connection_fd);
void manageRole(int connection_fd);
int changeAdminPassword(int connection_fd);

/* External functions */
extern void viewTotalBankMoney(int connection_fd);
extern void transactionHistory(int connection_fd, int accountNumber);
extern void exitClient(int connection_fd, int id);
extern int acquire_session_lock(const char *role, int id);

extern void release_session_semaphore(void);
extern void setupSignalHandlers(void);





int changeAdminPassword(int connection_fd);

/*
 * adminMenu:
 * Main interactive loop for Administrator console.
 */
void adminMenu(int connection_fd);

/*
 * addAccountAdmin:
 * Creates a new customer/joint account with validated integer minor balance,
 * uniqueness verification, and initial audit log.
 */
int addAccountAdmin(int connection_fd);

/*
 * deleteAccountAdmin:
 * Deactivates/deletes customer account record under exclusive lock.
 */
int deleteAccountAdmin(int connection_fd);

/*
 * modifyAccountAdmin:
 * Modifies account details (name, password, type, status) under exclusive lock.
 */
int modifyAccountAdmin(int connection_fd);

/*
 * searchAccountAdmin:
 * Looks up and displays customer details under shared read lock.
 */
int searchAccountAdmin(int connection_fd);

/*
 * addEmployee:
 * Registers a new employee in employees.txt.
 */
int addEmployee(int connection_fd);

/*
 * modifyEmployeeAdmin:
 * Updates employee first name under record lock.
 */
void modifyEmployeeAdmin(int connection_fd);

/*
 * manageRole:
 * Changes an employee's role between Employee (1) and Manager (0).
 */
void manageRole(int connection_fd);

#endif /* ADMIN_FUNCTIONS_H */
