#ifndef EMPLOYEE_FUNCTIONS_H
#define EMPLOYEE_FUNCTIONS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <crypt.h>
#include <errno.h>
#include <time.h>
#include "bank_fun.h"
#include "financial_utils.h"
#include "socket_utils.h"
#include "account_storage.h"
#include "txn_journal.h"

void employeeMenu(int connection_fd);
int loginEmployee(int connection_fd, int empID, char *password);
void addCustomer(int connection_fd);
void modifyCustomerInfo(int connection_fd);
void approveRejectLoan(int connection_fd, int approverID, const char *approverRole);
void viewAssignedLoan(int connection_fd, int empID);
int changeEMPPassword(int connection_fd, int empID);

/* External functions & variables */
extern void viewTotalBankMoney(int connection_fd);
extern void transactionHistory(int connection_fd, int accountNumber);
extern void logout(int connection_fd, int id);
extern void exitClient(int connection_fd, int id);
extern int acquire_session_lock(const char *role, int id);
extern void setupSignalHandlers(void);
extern void release_session_semaphore(void);


void employeeMenu(int connection_fd);

int loginEmployee(int connection_fd, int empID, char *password);

void addCustomer(int connection_fd);

void modifyCustomerInfo(int connection_fd);

void approveRejectLoan(int connection_fd, int approverID, const char *approverRole);

void viewAssignedLoan(int connection_fd, int empID);

int changeEMPPassword(int connection_fd, int empID);

#endif /* EMPLOYEE_FUNCTIONS_H */
