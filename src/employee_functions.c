#include "text_utils.h"
#include "audit_history.h"
#include "app_config.h"
#include "employee_functions.h"
#include "customer_functions.h"
#include "employee_functions.h"
#include "manager_functions.h"
#include "admin_functions.h"

void employeeMenu(int connection_fd) {
    int empID;
    int accountNumber, response = 0;
    char password[64];
    int choice;
    char readBuffer[4096];

start:
    if (socket_write_packet(connection_fd, "\nEnter Employee ID: ") != 0) return;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return;
    if (strcasecmp(readBuffer, "back") == 0 || strcmp(readBuffer, "0") == 0) {
        socket_send_ack(connection_fd, "Returning to the main menu...^");
        return;
    }
    empID = atoi(readBuffer);

    if (socket_write_packet(connection_fd, "Enter password (or type back to return to main menu): ") != 0) return;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return;
    if (strcasecmp(readBuffer, "back") == 0 || strcmp(readBuffer, "0") == 0) {
        socket_send_ack(connection_fd, "Returning to the main menu...^");
        return;
    }
    copy_text(password, readBuffer, sizeof(password));
    password[sizeof(password) - 1] = '\0';

    int loginResult = loginEmployee(connection_fd, empID, password);
    if (loginResult == 1) {
        printf("Employee %d logged In\n", empID);
        if (socket_send_ack(connection_fd, "\nLogin Successfully^") != 0) {
            release_session_semaphore();
            return;
        }

        while (1) {
            if (socket_write_packet(connection_fd, EMPMENU) != 0) {
                release_session_semaphore();
                return;
            }
            if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
                release_session_semaphore();
                return;
            }
            choice = atoi(readBuffer);
            if (choice == 0) {
                release_session_semaphore();
                socket_send_ack(connection_fd, "Returned to the main menu.^");
                return;
            }

            printf("Employee (%d) selected choice: %d\n", empID, choice);
            switch (choice) {
                case 1:
                    addCustomer(connection_fd);
                    break;
                case 2:
                    modifyCustomerInfo(connection_fd);
                    break;
                case 3:
                    approveRejectLoan(connection_fd, empID, "Employee");
                    break;
                case 4:
                    viewAssignedLoan(connection_fd, empID);
                    break;
                case 5: {
                    if (socket_write_packet(connection_fd, "Enter Account Number to View History: ") != 0) {
                        release_session_semaphore();
                        return;
                    }
                    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
                        release_session_semaphore();
                        return;
                    }
                    accountNumber = atoi(readBuffer);
                    transactionHistory(connection_fd, accountNumber);
                    break;
                }
                case 6:
                    response = changeEMPPassword(connection_fd, empID);
                    if (!response) {
                        socket_send_ack(connection_fd, "Unable to change password\n^");
                    } else {
                        socket_send_ack(connection_fd, "Password changed successfully\nLogin with new password...\n^");
                    }
                    logout(connection_fd, empID);
                    goto start;
                case 7:
                    printf("Employee ID: %d Logged Out!\n", empID);
                    logout(connection_fd, empID);
                    return;
                case 8:
                    printf("Employee ID: %d Exited!\n", empID);
                    exitClient(connection_fd, empID);
                    return;
                default:
                    socket_send_ack(connection_fd, "Invalid Choice for Employee menu^");
                    break;
            }
        }
    } else if (loginResult == -2) {
        socket_send_ack(connection_fd, "\nEmployee ID is already logged in on another terminal!^");
        goto start;
    } else if (loginResult == -4) {
        socket_send_ack(connection_fd, "\nIncorrect password for Employee ID! Please try again.^");
        goto start;
    } else if (loginResult == -3) {
        socket_send_ack(connection_fd, "\nThis ID does not belong to a Bank Employee!^");
        goto start;
    } else {
        socket_send_ack(connection_fd, "\nEmployee ID not found! Please check and try again.^");
        goto start;
    }
}



void addCustomer(int connection_fd) {
    char write_buf[4096], readBuffer[4096];
    char err_msg[128];
    long long balance_paise = 0;
    struct Customer customer;
    memset(&customer, 0, sizeof(customer));

    /* Account Number */
    if (socket_write_packet(connection_fd, "Enter Account Number: ") != 0) return;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return;
    customer.accountNumber = atoi(readBuffer);

    if (customer.accountNumber <= 0) {
        socket_send_ack(connection_fd, "Error: Invalid account number!^");
        return;
    }

    /* Check uniqueness */
    int cfile = open(CUSPATH, O_RDWR | O_CREAT, 0644);
    if (cfile != -1) {
        off_t exist_off = -1;
        struct Customer exist_c;
        if (find_customer_record(cfile, customer.accountNumber, &exist_off, &exist_c) == 0) {
            close(cfile);
            socket_send_ack(connection_fd, "Error: Account number already exists!^");
            return;
        }
    }

    /* Customer Name */
    if (socket_write_packet(connection_fd, "Enter Customer Name: ") != 0) { if (cfile != -1) close(cfile); return; }
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) { if (cfile != -1) close(cfile); return; }
    copy_text(customer.customerName, readBuffer, sizeof(customer.customerName));

    /* Password */
    if (socket_write_packet(connection_fd, "Enter Password: ") != 0) { if (cfile != -1) close(cfile); return; }
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) { if (cfile != -1) close(cfile); return; }
    char *h = crypt(readBuffer, HASHKEY);
    if (h) copy_text(customer.password, h, sizeof(customer.password));

    /* Account Type */
    if (socket_write_packet(connection_fd, "Select Account Type:\n1. Normal Account\n2. Joint Account\nEnter choice: ") != 0) { if (cfile != -1) close(cfile); return; }
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) { if (cfile != -1) close(cfile); return; }
    int typeChoice = atoi(readBuffer);
    customer.accountType = (typeChoice == 2) ? 2 : 1;

    /* Opening Balance */
    if (socket_write_packet(connection_fd, "Enter Opening Balance (maximum initial deposit: Rs. 10,000,000.00; maximum account balance: Rs. 1,000,000,000.00): ") != 0) { if (cfile != -1) close(cfile); return; }
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) { if (cfile != -1) close(cfile); return; }

    if (parse_monetary_amount(readBuffer, &balance_paise, err_msg, sizeof(err_msg)) != 0) {
        if (cfile != -1) close(cfile);
        snprintf(write_buf, sizeof(write_buf), "Error: %s^", err_msg);
        socket_send_ack(connection_fd, write_buf);
        return;
    }

    customer.balance = balance_paise;
    customer.activeStatus = 1;

    if (cfile != -1) {
        struct flock fl;
        memset(&fl, 0, sizeof(fl));
        fl.l_type = F_WRLCK;
        fcntl(cfile, F_SETLKW, &fl);

        lseek(cfile, 0, SEEK_END);
        write(cfile, &customer, sizeof(customer));
        fsync(cfile);

        fl.l_type = F_UNLCK;
        fcntl(cfile, F_SETLK, &fl);
        close(cfile);
    }

    uint64_t txn_id = get_next_txn_id();
    record_transaction_history(customer.accountNumber, txn_id, "Opening Balance", customer.balance, 0);

    char bal_str[64];
    format_paise(customer.balance, bal_str, sizeof(bal_str));
    snprintf(write_buf, sizeof(write_buf),
             "Customer account %d (%s) added successfully! Remaining balance: %s^",
             customer.accountNumber,
             customer.accountType == 2 ? "Joint Account" : "Normal Account",
             bal_str);
    socket_send_ack(connection_fd, write_buf);
}

void modifyCustomerInfo(int connection_fd) {
    char write_buf[4096], readBuffer[4096];

    if (socket_write_packet(connection_fd, "Enter Customer Account Number: ") != 0) return;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return;
    int accNo = atoi(readBuffer);

    int file = open(CUSPATH, O_RDWR, 0644);
    if (file == -1) {
        socket_send_ack(connection_fd, "Error opening database!^");
        return;
    }

    off_t offset = -1;
    struct Customer c;
    if (find_customer_record(file, accNo, &offset, &c) != 0) {
        close(file);
        socket_send_ack(connection_fd, "Account not found!^");
        return;
    }

    if (lock_customer_record(file, offset, F_WRLCK) != 0) {
        close(file);
        socket_send_ack(connection_fd, "Error locking record!^");
        return;
    }

    read_customer_record(file, offset, &c);

    if (socket_write_packet(connection_fd, "Select field to modify:\n1. Name\n2. Password\n3. Deactivate/Close Account\nEnter choice: ") != 0) {
        unlock_customer_record(file, offset);
        close(file);
        return;
    }
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
        unlock_customer_record(file, offset);
        close(file);
        return;
    }

    int choice = atoi(readBuffer);
    if (choice == 1) {
        if (socket_write_packet(connection_fd, "Enter new name: ") != 0) { unlock_customer_record(file, offset); close(file); return; }
        if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) { unlock_customer_record(file, offset); close(file); return; }
        copy_text(c.customerName, readBuffer, sizeof(c.customerName));
    } else if (choice == 2) {
        if (socket_write_packet(connection_fd, "Enter new password: ") != 0) { unlock_customer_record(file, offset); close(file); return; }
        if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) { unlock_customer_record(file, offset); close(file); return; }
        char *h = crypt(readBuffer, HASHKEY);
        if (h) copy_text(c.password, h, sizeof(c.password));
    } else if (choice == 3) {
        c.activeStatus = 0; /* Deactivated / Closed */
        printf("Employee modified account %d: set status to Deactivated/Closed\n", accNo);
    } else {
        unlock_customer_record(file, offset);
        close(file);
        socket_send_ack(connection_fd, "Invalid choice for modification!^");
        return;
    }

    write_customer_record(file, offset, &c);
    fsync(file);

    unlock_customer_record(file, offset);
    close(file);

    char bal_str[64];
    format_paise(c.balance, bal_str, sizeof(bal_str));
    snprintf(write_buf, sizeof(write_buf),
             "Account %d updated successfully! Remaining balance: %s^",
             c.accountNumber, bal_str);
    socket_send_ack(connection_fd, write_buf);
}

void approveRejectLoan(int connection_fd, int approverID, const char *approverRole) {
    char write_buf[4096], readBuffer[4096];
    struct LoanDetails ld;

    int file = open(LOANPATH, O_RDWR, 0644);
    if (file == -1) {
        socket_send_ack(connection_fd, "Error opening loan database!^");
        return;
    }

    /* List pending loans */
    int pendingCount = 0;
    lseek(file, 0, SEEK_SET);
    while (read(file, &ld, sizeof(ld)) == (ssize_t)sizeof(ld)) {
        if (ld.status == 0 || ld.status == 1) {
            pendingCount++;
            snprintf(write_buf, sizeof(write_buf), "Pending Loan ID: %d | Account: %d | Amount: Rs. %d | Status: %s^",
                     ld.loanID, ld.accountNumber, ld.loanAmount, ld.status == 0 ? "Requested" : "Under Review");
            socket_send_ack(connection_fd, write_buf);
        }
    }

    if (pendingCount == 0) {
        close(file);
        socket_send_ack(connection_fd, "No pending loan applications available for review.^");
        return;
    }

    if (socket_write_packet(connection_fd, "Enter Loan ID to Approve/Reject: ") != 0) { close(file); return; }
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) { close(file); return; }
    int lID = atoi(readBuffer);

    off_t loan_offset = -1;
    int found = 0;
    lseek(file, 0, SEEK_SET);
    off_t cur = 0;
    while (read(file, &ld, sizeof(ld)) == (ssize_t)sizeof(ld)) {
        if (ld.loanID == lID) {
            loan_offset = cur;
            found = 1;
            break;
        }
        cur += (off_t)sizeof(ld);
    }

    if (!found) {
        close(file);
        socket_send_ack(connection_fd, "Loan ID not found!^");
        return;
    }

    struct flock fl_loan;
    memset(&fl_loan, 0, sizeof(fl_loan));
    fl_loan.l_type = F_WRLCK;
    fl_loan.l_whence = SEEK_SET;
    fl_loan.l_start = loan_offset;
    fl_loan.l_len = sizeof(ld);
    if (fcntl(file, F_SETLK, &fl_loan) == -1) {
        close(file);
        socket_send_ack(connection_fd, "Loan record is currently locked by another process.^");
        return;
    }

    pread(file, &ld, sizeof(ld), loan_offset);

    if (ld.status != 0 && ld.status != 1) {
        fl_loan.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl_loan);
        close(file);
        socket_send_ack(connection_fd, "Loan has already been processed!^");
        return;
    }

    if (socket_write_packet(connection_fd, "Enter 1 to Approve Loan\nEnter 2 to Reject Loan\nEnter choice: ") != 0) {
        fl_loan.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl_loan);
        close(file);
        return;
    }
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
        fl_loan.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl_loan);
        close(file);
        return;
    }

    int choice = atoi(readBuffer);
    if (choice == 1) {
        /* Approve: Credit funds to customer account under transaction protocol */
        int cfile = open(CUSPATH, O_RDWR, 0644);
        off_t cust_offset = -1;
        struct Customer cs;

        if (cfile == -1 || find_customer_record(cfile, ld.accountNumber, &cust_offset, &cs) != 0) {
            if (cfile != -1) close(cfile);
            fl_loan.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl_loan);
            close(file);
            socket_send_ack(connection_fd, "Customer account not found for loan disbursement.^");
            return;
        }

        if (lock_customer_record(cfile, cust_offset, F_WRLCK) != 0 ||
            read_customer_record(cfile, cust_offset, &cs) != 0) {
            fl_loan.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl_loan);
            close(file); close(cfile);
            socket_send_ack(connection_fd, "Unable to lock/read customer account for loan processing.^");
            return;
        }

        if (cs.activeStatus != 1) {
            ld.status = 3; /* Rejected due to inactive account */
            ld.empID = approverID;
            pwrite(file, &ld, sizeof(ld), loan_offset);
            fsync(file);

            unlock_customer_record(cfile, cust_offset);
            close(cfile);
            fl_loan.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl_loan);
            close(file);
            socket_send_ack(connection_fd, "Account is deactivated; loan application rejected.^");
            return;
        }

        if (ld.loanAmount <= 0) {
            unlock_customer_record(cfile, cust_offset); close(cfile);
            fl_loan.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl_loan); close(file);
            socket_send_ack(connection_fd, "Invalid loan amount; no funds were disbursed.^");
            return;
        }
        long long loan_paise = (long long)ld.loanAmount * 100LL;
        long long new_cs_bal = 0;
        if (safe_add_paise(cs.balance, loan_paise, &new_cs_bal) != 0) {
            unlock_customer_record(cfile, cust_offset); close(cfile);
            fl_loan.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl_loan); close(file);
            socket_send_ack(connection_fd, "Loan disbursement rejected: maximum account balance is Rs. 1,000,000,000.00.^");
            return;
        }

        int txn_lock_fd = journal_transaction_lock();
        if (txn_lock_fd < 0 || journal_recovery_is_required() || journal_has_prepared_transactions() != 0) {
            if (txn_lock_fd >= 0) journal_transaction_unlock(txn_lock_fd);
            unlock_customer_record(cfile, cust_offset); close(cfile);
            fl_loan.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl_loan); close(file);
            socket_send_ack(connection_fd, "Financial operations are temporarily disabled; restart the server for transaction recovery.^");
            return;
        }
        /* Write-ahead journal entry */
        uint64_t txn_id = get_next_txn_id();
        if (txn_id == 0) {
            journal_transaction_unlock(txn_lock_fd); unlock_customer_record(cfile, cust_offset); close(cfile);
            fl_loan.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl_loan); close(file);
            socket_send_ack(connection_fd, "Unable to allocate transaction ID; loan was not disbursed.^");
            return;
        }
        struct JournalEntry je;
        memset(&je, 0, sizeof(je));
        je.txn_id = txn_id;
        je.txn_type = TXN_TYPE_LOAN_DISBURSE;
        /* For loan-disbursement journal entries, src_acc stores loanID,
           src_before_bal stores the prior loan status, and src_after_bal stores
           the approving employee ID. dst_* fields remain the account balances. */
        je.src_acc = ld.loanID;
        je.dst_acc = cs.accountNumber;
        je.amount_paise = loan_paise;
        je.src_before_bal = ld.status;
        je.src_after_bal = approverID;
        je.dst_before_bal = cs.balance;
        je.dst_after_bal = new_cs_bal;
        je.timestamp = time(NULL);

        off_t j_off = journal_log_prepared(&je);
        if (j_off < 0) {
            journal_transaction_unlock(txn_lock_fd); unlock_customer_record(cfile, cust_offset); close(cfile);
            fl_loan.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl_loan); close(file);
            socket_send_ack(connection_fd, "Unable to persist loan-disbursement intent; no funds were credited.^");
            return;
        }

        /* Update customer balance and loan status; any I/O failure leaves a
           durable PREPARED intent and disables further financial operations. */
        cs.balance = new_cs_bal;
        ld.status = 2;
        ld.empID = approverID;
        if (write_customer_record(cfile, cust_offset, &cs) != 0 || fsync(cfile) != 0 ||
            pwrite(file, &ld, sizeof(ld), loan_offset) != (ssize_t)sizeof(ld) || fsync(file) != 0) {
            (void)journal_mark_recovery_required();
            journal_transaction_unlock(txn_lock_fd); unlock_customer_record(cfile, cust_offset); close(cfile);
            fl_loan.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl_loan); close(file);
            socket_send_ack(connection_fd, "Loan disbursement interrupted by a storage error; restart the server for recovery.^");
            return;
        }

        if (journal_mark_state(j_off, txn_id, TXN_STATE_COMMITTED) != 0) {
            (void)journal_mark_recovery_required();
            journal_transaction_unlock(txn_lock_fd); unlock_customer_record(cfile, cust_offset); close(cfile);
            fl_loan.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl_loan); close(file);
            socket_send_ack(connection_fd, "Loan outcome requires recovery; restart the server.^");
            return;
        }
        journal_transaction_unlock(txn_lock_fd);

        /* Audit history */
        char loan_hist[1024];
        time_t s = time(NULL);
        struct tm *ct = localtime(&s);
        snprintf(loan_hist, sizeof(loan_hist),
                 "%d credited by loan id %d approved by %s (ID %d) at %02d:%02d:%02d %04d-%02d-%02d\n",
                 ld.loanAmount, ld.loanID, approverRole, approverID,
                 ct->tm_hour, ct->tm_min, ct->tm_sec, ct->tm_year + 1900, ct->tm_mon + 1, ct->tm_mday);

        int fp = open(HISTORYPATH, O_RDWR | O_APPEND | O_CREAT, 0644);
        if (fp != -1) {
            struct flock fl_h;
            memset(&fl_h, 0, sizeof(fl_h));
            fl_h.l_type = F_WRLCK;
            fcntl(fp, F_SETLKW, &fl_h);

            struct trans_histroy th;
            memset(&th, 0, sizeof(th));
            th.acc_no = cs.accountNumber;
            copy_text(th.hist, loan_hist, sizeof(th.hist));
            write(fp, &th, sizeof(th));
            fsync(fp);

            fl_h.l_type = F_UNLCK;
            fcntl(fp, F_SETLK, &fl_h);
            close(fp);
        }

        unlock_customer_record(cfile, cust_offset);
        close(cfile);

        fl_loan.l_type = F_UNLCK;
        fcntl(file, F_SETLK, &fl_loan);
        close(file);

        char rem_str[64];
        format_paise(cs.balance, rem_str, sizeof(rem_str));
        snprintf(write_buf, sizeof(write_buf),
                 "Loan ID %d Approved successfully by %s (ID %d)! Rs. %d credited to Account %d. Customer Remaining Balance: %s^",
                 ld.loanID, approverRole, approverID, ld.loanAmount, cs.accountNumber, rem_str);
        socket_send_ack(connection_fd, write_buf);
    } else {
        /* Reject */
        ld.status = 3; /* Rejected */
        ld.empID = approverID;
        pwrite(file, &ld, sizeof(ld), loan_offset);
        fsync(file);

        fl_loan.l_type = F_UNLCK;
        fcntl(file, F_SETLK, &fl_loan);
        close(file);

        snprintf(write_buf, sizeof(write_buf), "Loan ID %d Rejected by %s (ID %d).^", ld.loanID, approverRole, approverID);
        socket_send_ack(connection_fd, write_buf);
    }
}

void viewAssignedLoan(int connection_fd, int empID) {
    char write_buf[4096];
    struct LoanDetails ld;

    int file = open(LOANPATH, O_RDONLY, 0644);
    if (file == -1) {
        socket_send_ack(connection_fd, "Error opening loan database!^");
        return;
    }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_RDLCK;
    fcntl(file, F_SETLKW, &fl);

    int count = 0;
    lseek(file, 0, SEEK_SET);
    while (read(file, &ld, sizeof(ld)) == (ssize_t)sizeof(ld)) {
        if (ld.empID == empID) {
            count++;
            const char *st_str = "Unknown";
            if (ld.status == 0) st_str = "Requested";
            else if (ld.status == 1) st_str = "Assigned";
            else if (ld.status == 2) st_str = "Approved";
            else if (ld.status == 3) st_str = "Rejected";

            snprintf(write_buf, sizeof(write_buf),
                     "Loan ID: %d | Account: %d | Amount: Rs. %d | Status: %s^",
                     ld.loanID, ld.accountNumber, ld.loanAmount, st_str);
            socket_send_ack(connection_fd, write_buf);
        }
    }

    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    if (count == 0) {
        socket_send_ack(connection_fd, "No loans currently assigned to you.^");
    }
}

int changeEMPPassword(int connection_fd, int empID) {
    char readBuffer[4096];
    char newPassword[64];
    struct Employee e;

    int file = open(EMPPATH, O_RDWR, 0644);
    if (file == -1) return 0;

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fcntl(file, F_SETLKW, &fl);

    off_t offset = -1;
    off_t cur = 0;
    while (read(file, &e, sizeof(e)) == (ssize_t)sizeof(e)) {
        if (e.empID == empID) {
            offset = cur;
            break;
        }
        cur += (off_t)sizeof(e);
    }

    if (offset == -1) {
        fl.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl);
        close(file);
        return 0;
    }

    if (socket_write_packet(connection_fd, "Enter new password: ") != 0) {
        fl.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl);
        close(file);
        return 0;
    }
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
        fl.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl);
        close(file);
        return 0;
    }

    copy_text(newPassword, readBuffer, sizeof(newPassword));
    newPassword[sizeof(newPassword) - 1] = '\0';

    char *h = crypt(newPassword, HASHKEY);
    if (h) {
        copy_text(e.password, h, sizeof(e.password));
    }

    pwrite(file, &e, sizeof(e), offset);
    fsync(file);

    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    printf("Employee %d changed password successfully\n", empID);
    return 1;
}
