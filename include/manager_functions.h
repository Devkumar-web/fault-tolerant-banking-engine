#ifndef MANAGER_FUNCTIONS_H
#define MANAGER_FUNCTIONS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <crypt.h>
#include <errno.h>
#include <semaphore.h>

#include "bank_fun.h"
#include "socket_utils.h"
#include "financial_utils.h"
#include "account_storage.h"
#include "txn_journal.h"

/* Forward declarations */
void managerMenu(int connection_fd);
int loginManager(int connection_fd, int mngID, const char *password);
void changeStatus(int connection_fd);
void assignLoanApplication(int connection_fd);
void readFeedBack(int connection_fd);
int changeMNGPassword(int connection_fd, int mngID);

/* External functions */
extern void viewTotalBankMoney(int connection_fd);
extern void transactionHistory(int connection_fd, int accountNumber);
extern void approveRejectLoan(int connection_fd, int approverID, const char *approverRole);
extern void logout(int connection_fd, int id);
extern void exitClient(int connection_fd, int id);
extern int acquire_session_lock(const char *role, int id);
extern void setupSignalHandlers(void);
extern void release_session_semaphore(void);


/*
 * managerMenu:
 * Main interactive loop for Bank Manager session.
 */
void managerMenu(int connection_fd);

/*
 * loginManager:
 * Verifies manager credentials first, and only acquires the POSIX named semaphore
 * once credentials are confirmed valid.
 */
int loginManager(int connection_fd, int mngID, const char *password);

/*
 * changeStatus:
 * Enables or disables a customer account under exclusive record lock.
 */
void changeStatus(int connection_fd);

/*
 * readFeedBack:
 * Reviews all customer feedbacks recorded in feedback.txt.
 */
void readFeedBack(int connection_fd);

/*
 * assignLoanApplication:
 * Lists unassigned loans and allows Manager to assign an employee to process the application.
 */
void assignLoanApplication(int connection_fd);

/*
 * changeMNGPassword:
 * Updates the manager's password securely under record lock.
 */
int changeMNGPassword(int connection_fd, int mngID);

#endif /* MANAGER_FUNCTIONS_H */
