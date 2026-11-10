#include <stdint.h>
#ifndef CUSTOMER_FUNCTIONS_H
#define CUSTOMER_FUNCTIONS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <crypt.h>
#include <errno.h>
#include <semaphore.h>
#include "bank_fun.h"
#include "financial_utils.h"
#include "socket_utils.h"
#include "account_storage.h"
#include "txn_journal.h"

/* External functions & variables */

extern int acquire_session_lock(const char *role, int id);
extern void setupSignalHandlers(void);
extern void exitClient(int connection_fd, int id);
extern void release_session_semaphore(void);

void addFeedback(int connection_fd, int accountNumber);
void customerMenu(int connection_fd, int requestedType);
void logout(int connection_fd, int id);
void withdrawMoney(int connection_fd, int accountNumber);
void depositMoney(int connection_fd, int accountNumber);
void customerBal(int connection_fd, int accountNumber);
void viewCustomerDetails(int connection_fd, int accountNumber);
void viewTotalBankMoney(int connection_fd);
void applyLoan(int connection_fd, int accountNumber);
void transferFunds(int connection_fd, int sourceAccount, int destAccount, const char *amt_str);
void transactionHistory(int connection_fd, int accountNumber);
int changePassword(int connection_fd, int accountNumber);
int loginCustomer(int connection_fd, int accountNumber, char *password, int requestedType);

/*
 * record_transaction_history:
 * Appends an audit ledger record with unique txn_id and timestamp to trans_hist.txt.
 */


/*
 * customerMenu:
 * Main interactive loop for Customer and Joint Account users.
 */
void customerMenu(int connection_fd, int requestedType);

/*
 * loginCustomer:
 * Validates credentials, verifies active status, and enforces single active session
 * for Normal Accounts using POSIX named semaphores.
 */
int loginCustomer(int connection_fd, int accountNumber, char *password, int requestedType);

/*
 * viewCustomerDetails:
 * Displays full account information under shared read lock (F_RDLCK).
 */
void viewCustomerDetails(int connection_fd, int accountNumber);

/*
 * customerBal:
 * Displays remaining balance under shared read lock (F_RDLCK).
 */
void customerBal(int connection_fd, int accountNumber);

/*
 * depositMoney:
 * Validates input, acquires exclusive lock, updates balance using checked arithmetic,
 * persists write-ahead journal intent, updates account record, fsyncs, commits, and logs audit record.
 */
void depositMoney(int connection_fd, int accountNumber);

/*
 * withdrawMoney:
 * Validates input, acquires exclusive lock, validates non-negative invariant (overdraft protection),
 * writes journal intent, updates account record, fsyncs, commits, and logs audit record.
 */
void withdrawMoney(int connection_fd, int accountNumber);

/*
 * transferFunds:
 * All-or-nothing transactional fund transfer adhering to ACID properties:
 * 1. Precondition checks: source != dest, amount > 0, valid numeric parsing.
 * 2. Deterministic lock ordering (min offset then max offset) to prevent deadlocks.
 * 3. Re-reads both accounts under exclusive write locks.
 * 4. Verifies active status and balance sufficiency.
 * 5. Conserves total money: Bal_src + Bal_dst = Constant.
 * 6. Write-ahead journal intent logged & fsynced before disk writes.
 * 7. Both account updates applied and fsynced.
 * 8. Journal marked COMMITTED and fsynced.
 * 9. Dual audit history entries created with shared txn_id.
 * 10. Locks released.
 */
void transferFunds(int connection_fd, int sourceAccount, int destAccount, const char *amt_str);

/*
 * applyLoan:
 * Submits a new loan request with unique monotonic Loan ID.
 */
void applyLoan(int connection_fd, int accountNumber);

/*
 * transactionHistory:
 * Displays indexed chronological transaction history with human-readable timestamps.
 */
void transactionHistory(int connection_fd, int accountNumber);

/*
 * viewTotalBankMoney:
 * Calculates and displays aggregated bank financial report across all accounts.
 */
void viewTotalBankMoney(int connection_fd);

/*
 * addFeedback:
 * Appends customer feedback to feedback.txt under write lock.
 */
void addFeedback(int connection_fd, int accountNumber);

/*
 * changePassword:
 * Changes customer password with SHA-512 hashing under exclusive write lock.
 */
int changePassword(int connection_fd, int accountNumber);

/*
 * logout:
 * Releases and unlinks session named semaphore.
 */
void logout(int connection_fd, int id);

#endif /* CUSTOMER_FUNCTIONS_H */
