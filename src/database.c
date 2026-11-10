#include "app_config.h"
/*
 * database.c - Database Inspection, Maintenance & Migration Utility
 * Part of the Online Banking Management System (System Software Mini Project)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

#include "bank_fun.h"
#include "financial_utils.h"
#include "account_storage.h"
#include "txn_journal.h"

#define EMPPATH "employees.txt"
#define CUSPATH "customers.txt"
#define LOANPATH "loanDetails.txt"
#define COUNTERPATH "loanCounter.txt"
#define HISTORYPATH "trans_hist.txt"
#define FEEDPATH "feedback.txt"

void print_separator(void) {
    printf("========================================================================================\n");
}

void view_customers(void) {
    print_separator();
    printf("                                CUSTOMER DATABASE (customers.txt)\n");
    print_separator();
    int file = open(CUSPATH, O_RDONLY);
    if (file == -1) {
        printf("Error: Cannot open %s: %s\n", CUSPATH, strerror(errno));
        return;
    }

    struct Customer c;
    int count = 0;
    long long total_paise = 0;

    printf("%-10s | %-24s | %-14s | %-12s | %-16s\n",
           "Account No", "Customer Name", "Account Type", "Status", "Balance");
    print_separator();

    while (read(file, &c, sizeof(c)) == (ssize_t)sizeof(c)) {
        count++;
        total_paise += c.balance;
        char bal_str[64];
        format_paise(c.balance, bal_str, sizeof(bal_str));

        printf("%-10d | %-24s | %-14s | %-12s | %-16s\n",
               c.accountNumber,
               c.customerName,
               c.accountType == 2 ? "Joint" : "Normal",
               c.activeStatus == 1 ? "Active" : "Deactivated",
               bal_str);
    }
    close(file);

    print_separator();
    char total_str[64];
    format_paise(total_paise, total_str, sizeof(total_str));
    printf("Total Accounts: %d | Total Money in Bank: %s\n", count, total_str);
    print_separator();
}

void view_employees(void) {
    print_separator();
    printf("                                EMPLOYEE DATABASE (employees.txt)\n");
    print_separator();
    int file = open(EMPPATH, O_RDONLY);
    if (file == -1) {
        printf("Error: Cannot open %s: %s\n", EMPPATH, strerror(errno));
        return;
    }

    struct Employee e;
    int count = 0;

    printf("%-10s | %-18s | %-18s | %-12s\n",
           "Emp ID", "First Name", "Last Name", "Role");
    print_separator();

    while (read(file, &e, sizeof(e)) == (ssize_t)sizeof(e)) {
        count++;
        printf("%-10d | %-18s | %-18s | %-12s\n",
               e.empID,
               e.firstName,
               e.lastName,
               e.role == 0 ? "Manager" : "Employee");
    }
    close(file);
    print_separator();
    printf("Total Employees / Managers: %d\n", count);
    print_separator();
}

void view_loans(void) {
    print_separator();
    printf("                                LOAN DATABASE (loanDetails.txt)\n");
    print_separator();
    int file = open(LOANPATH, O_RDONLY);
    if (file == -1) {
        printf("Error: Cannot open %s: %s\n", LOANPATH, strerror(errno));
        return;
    }

    struct LoanDetails ld;
    int count = 0;

    printf("%-10s | %-12s | %-12s | %-16s | %-14s\n",
           "Loan ID", "Account No", "Assigned Emp", "Amount (Rs.)", "Status");
    print_separator();

    while (read(file, &ld, sizeof(ld)) == (ssize_t)sizeof(ld)) {
        count++;
        const char *st = "Unknown";
        if (ld.status == 0) st = "Requested";
        else if (ld.status == 1) st = "Pending";
        else if (ld.status == 2) st = "Approved";
        else if (ld.status == 3) st = "Rejected";

        printf("%-10d | %-12d | %-12d | Rs. %-12d | %-14s\n",
               ld.loanID, ld.accountNumber, ld.empID, ld.loanAmount, st);
    }
    close(file);
    print_separator();
    printf("Total Loan Applications: %d\n", count);
    print_separator();
}

void view_journal(void) {
    print_separator();
    printf("                    WRITE-AHEAD TRANSACTION JOURNAL (trans_journal.bin)\n");
    print_separator();
    int file = open(JOURNAL_PATH, O_RDONLY);
    if (file == -1) {
        printf("Journal is empty or no transactions logged yet.\n");
        return;
    }

    struct JournalEntry j;
    int count = 0;

    printf("%-10s | %-10s | %-12s | %-8s | %-8s | %-16s\n",
           "Txn ID", "Type", "State", "Src Acc", "Dst Acc", "Amount");
    print_separator();

    while (read(file, &j, sizeof(j)) == (ssize_t)sizeof(j)) {
        count++;
        const char *tname = (j.txn_type == TXN_TYPE_TRANSFER) ? "Transfer" :
                            (j.txn_type == TXN_TYPE_DEPOSIT) ? "Deposit" :
                            (j.txn_type == TXN_TYPE_WITHDRAW) ? "Withdraw" :
                            (j.txn_type == TXN_TYPE_LOAN_DISBURSE) ? "LoanCredit" : "Other";
        const char *sname = (j.state == TXN_STATE_COMMITTED) ? "COMMITTED" :
                            (j.state == TXN_STATE_PREPARED) ? "PREPARED" : "ABORTED";
        char amt_str[64];
        format_paise(j.amount_paise, amt_str, sizeof(amt_str));

        printf("%-10llu | %-10s | %-12s | %-8d | %-8d | %-16s\n",
               (unsigned long long)j.txn_id, tname, sname, j.src_acc, j.dst_acc, amt_str);
    }
    close(file);
    print_separator();
    printf("Total Journal Entries: %d\n", count);
    print_separator();
}

int check_and_recover(void) {
    printf("[Database Utility] Checking customer database migration...\n");
    int migrated = migrate_customers_if_legacy(CUSPATH);
    if (migrated < 0) {
        fprintf(stderr, "[Database Utility] Migration failed; recovery was not started.\n");
        return -1;
    }
    printf("[Database Utility] Running write-ahead journal crash recovery...\n");
    int recovered = journal_recover_at_startup(CUSPATH);
    if (recovered < 0) {
        fprintf(stderr, "[Database Utility] Recovery failed. Preserve the files and investigate before further financial operations.\n");
        return -1;
    }
    printf("[Database Utility] Recovery completed. %d interrupted transactions recovered.\n", recovered);
    return 0;
}

int main(int argc, char *argv[]) {
    printf("\n=== Bank Management System Database Administration Utility ===\n");
    if (argc > 1) {
        if (strcmp(argv[1], "customers") == 0) {
            view_customers();
            return 0;
        } else if (strcmp(argv[1], "employees") == 0) {
            view_employees();
            return 0;
        } else if (strcmp(argv[1], "loans") == 0) {
            view_loans();
            return 0;
        } else if (strcmp(argv[1], "journal") == 0) {
            view_journal();
            return 0;
        } else if (strcmp(argv[1], "recover") == 0) {
            return check_and_recover() == 0 ? 0 : EXIT_FAILURE;
        }
    }

    printf("1. View Customers & Balances\n");
    printf("2. View Employees & Managers\n");
    printf("3. View Loan Applications\n");
    printf("4. View Transaction Journal (WAL)\n");
    printf("5. Run Startup Recovery & Legacy Migration\n");
    printf("6. View Complete System Summary\n");
    printf("0. Exit\n");
    printf("Enter choice (0-6): ");

    int choice = 0;
    if (scanf("%d", &choice) != 1) return 0;

    switch (choice) {
        case 1: view_customers(); break;
        case 2: view_employees(); break;
        case 3: view_loans(); break;
        case 4: view_journal(); break;
        case 5: if (check_and_recover() != 0) return EXIT_FAILURE; break;
        case 6:
            view_customers();
            view_employees();
            view_loans();
            view_journal();
            break;
        default: printf("Exiting.\n"); break;
    }

    return 0;
}
