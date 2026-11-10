#include "text_utils.h"
#include "audit_history.h"
#include "account_service.h"
#include "app_config.h"
#include "customer_functions.h"
#include "customer_functions.h"
#include "employee_functions.h"
#include "manager_functions.h"
#include "admin_functions.h"



static void present_transaction(int fd, BankOperation op, int source, int dest, const char *input) {
    long long amount;
    char error[128], message[256], balance[64];
    if (parse_monetary_amount(input, &amount, error, sizeof(error)) != 0) {
        snprintf(message, sizeof(message), "Error: %s^", error);
        (void)socket_send_ack(fd, message); return;
    }
    BankResult result;
    BankStatus status = bank_transact(op, source, dest, amount, &result);
    const char *errors[] = {"", "Invalid transaction", "Account not found", "Account is inactive",
        "Insufficient balance", "Maximum account balance exceeded", "Storage or locking error; inspect outcome before retrying",
        "Transaction outcome requires recovery; restart the server before retrying"};
    if (status != BANK_OK) { (void)socket_send_ack(fd, errors[status]); return; }
    format_paise(result.balance, balance, sizeof(balance));
    snprintf(message, sizeof(message), "%s successful! Remaining balance: %s^",
        op == BANK_DEPOSIT ? "Deposit" : op == BANK_WITHDRAW ? "Withdrawal" : "Transfer", balance);
    (void)socket_send_ack(fd, message);
}

void customerMenu(int connection_fd, int requestedType) {
    int accountNumber = 0;
    int destAcc = 0;
    int response = 0;
    char password[64];
    char readBuffer[4096];

start:
    if (socket_write_packet(connection_fd, "\nEnter account number (or type back to return to main menu): ") != 0) return;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return;
    if (strcasecmp(readBuffer, "back") == 0 || strcmp(readBuffer, "0") == 0) {
        socket_send_ack(connection_fd, "Returning to the main menu...^");
        return;
    }
    accountNumber = atoi(readBuffer);

    if (socket_write_packet(connection_fd, "Enter password (or type back to return to main menu): ") != 0) return;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return;
    if (strcasecmp(readBuffer, "back") == 0 || strcmp(readBuffer, "0") == 0) {
        socket_send_ack(connection_fd, "Returning to the main menu...^");
        return;
    }
    copy_text(password, readBuffer, sizeof(password));
    password[sizeof(password) - 1] = '\0';

    int loginResult = loginCustomer(connection_fd, accountNumber, password, requestedType);
    if (loginResult == 1) {
        if (socket_send_ack(connection_fd, "\nLogin Successfully^") != 0) {
            release_session_semaphore();
            return;
        }

        while (1) {
            if (socket_write_packet(connection_fd, CUSMENU) != 0) {
                release_session_semaphore();
                return;
            }
            if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
                release_session_semaphore();
                return;
            }

            int choice = atoi(readBuffer);
            if (choice == 0) {
                release_session_semaphore();
                socket_send_ack(connection_fd, "Returned to the main menu.^");
                return;
            }
            printf("Customer (%d) selected choice: %d\n", accountNumber, choice);

            switch (choice) {
                case 1:
                    depositMoney(connection_fd, accountNumber);
                    break;
                case 2:
                    withdrawMoney(connection_fd, accountNumber);
                    break;
                case 3:
                    customerBal(connection_fd, accountNumber);
                    break;
                case 4:
                    viewCustomerDetails(connection_fd, accountNumber);
                    break;
                case 5:
                    response = changePassword(connection_fd, accountNumber);
                    if (!response) {
                        socket_send_ack(connection_fd, "Unable to change password^");
                    } else {
                        socket_send_ack(connection_fd, "Password changed successfully! Please login again with new password.^");
                    }
                    logout(connection_fd, accountNumber);
                    goto start;
                case 6: {
                    char dest_buf[64], amt_buf[64];
                    if (socket_write_packet(connection_fd, "Enter dest account number: ") != 0) {
                        release_session_semaphore();
                        return;
                    }
                    if (socket_read_packet(connection_fd, dest_buf, sizeof(dest_buf)) != 0) {
                        release_session_semaphore();
                        return;
                    }
                    destAcc = atoi(dest_buf);

                    if (socket_write_packet(connection_fd, "Enter transfer amount (maximum per transaction: Rs. 10,000,000.00): ") != 0) {
                        release_session_semaphore();
                        return;
                    }
                    if (socket_read_packet(connection_fd, amt_buf, sizeof(amt_buf)) != 0) {
                        release_session_semaphore();
                        return;
                    }

                    transferFunds(connection_fd, accountNumber, destAcc, amt_buf);
                    break;
                }
                case 7:
                    applyLoan(connection_fd, accountNumber);
                    break;
                case 8:
                    transactionHistory(connection_fd, accountNumber);
                    break;
                case 9:
                    addFeedback(connection_fd, accountNumber);
                    break;
                case 10:
                    printf("Account %d logged out!\n", accountNumber);
                    logout(connection_fd, accountNumber);
                    return;
                case 11:
                    printf("Account %d exited program!\n", accountNumber);
                    exitClient(connection_fd, accountNumber);
                    return;
                default:
                    socket_send_ack(connection_fd, "Invalid Choice for customer menu^");
                    break;
            }
        }
    } else if (loginResult == -2) {
        socket_send_ack(connection_fd, "\nAccount is already logged in from another terminal!^");
        goto start;
    } else if (loginResult == -3) {
        if (requestedType == 1)
            socket_send_ack(connection_fd, "\nThis account is a Joint Account! Please login using option 2 (Joint Account User Login).^");
        else
            socket_send_ack(connection_fd, "\nThis account is a Normal Account! Please login using option 1 (Normal User Login).^");
        goto start;
    } else if (loginResult == -4) {
        socket_send_ack(connection_fd, "\nIncorrect password! Please try again.^");
        goto start;
    } else if (loginResult == -5) {
        socket_send_ack(connection_fd, "\nAccount is deactivated! Please contact administration.^");
        goto start;
    } else {
        socket_send_ack(connection_fd, "\nAccount number not found! Please check and try again.^");
        goto start;
    }
}



void viewCustomerDetails(int connection_fd, int accountNumber) {
    char write_buf[4096];
    int file = open(CUSPATH, O_RDONLY, 0644);
    if (file == -1) {
        socket_send_ack(connection_fd, "Error opening database!^");
        return;
    }

    off_t offset = -1;
    struct Customer customer;
    if (find_customer_record(file, accountNumber, &offset, &customer) != 0) {
        close(file);
        socket_send_ack(connection_fd, "Account not found!^");
        return;
    }

    /* Acquire shared read lock */
    lock_customer_record(file, offset, F_RDLCK);
    read_customer_record(file, offset, &customer);
    unlock_customer_record(file, offset);
    close(file);

    char bal_str[64];
    format_paise(customer.balance, bal_str, sizeof(bal_str));

    snprintf(write_buf, sizeof(write_buf),
             "\n=== Account Details ===\n"
             "Account Number   : %d\n"
             "Account Holder   : %s\n"
             "Account Type     : %s\n"
             "Remaining Balance: %s\n"
             "Account Status   : %s\n^",
             customer.accountNumber,
             customer.customerName,
             customer.accountType == 2 ? "Joint Account" : "Normal Account",
             bal_str,
             customer.activeStatus == 1 ? "Active" : "Deactivated");

    socket_send_ack(connection_fd, write_buf);
}

void customerBal(int connection_fd, int accountNumber) {
    char write_buf[4096];
    int file = open(CUSPATH, O_RDONLY, 0644);
    if (file == -1) {
        socket_send_ack(connection_fd, "Error opening database!^");
        return;
    }

    off_t offset = -1;
    struct Customer customer;
    if (find_customer_record(file, accountNumber, &offset, &customer) != 0) {
        close(file);
        socket_send_ack(connection_fd, "Account not found!^");
        return;
    }

    lock_customer_record(file, offset, F_RDLCK);
    read_customer_record(file, offset, &customer);
    unlock_customer_record(file, offset);
    close(file);

    char bal_str[64];
    format_paise(customer.balance, bal_str, sizeof(bal_str));

    snprintf(write_buf, sizeof(write_buf), "The remaining balance of account %d is: %s^", accountNumber, bal_str);
    socket_send_ack(connection_fd, write_buf);
}

void depositMoney(int connection_fd, int accountNumber) {
    char input[4096];
    if (socket_write_packet(connection_fd, "Enter deposit amount (maximum per transaction: Rs. 10,000,000.00): ") != 0) return;
    if (socket_read_packet(connection_fd, input, sizeof(input)) != 0) return;
    present_transaction(connection_fd, BANK_DEPOSIT, accountNumber, 0, input);
}

void withdrawMoney(int connection_fd, int accountNumber) {
    char input[4096];
    if (socket_write_packet(connection_fd, "Enter withdrawal amount (maximum per transaction: Rs. 10,000,000.00): ") != 0) return;
    if (socket_read_packet(connection_fd, input, sizeof(input)) != 0) return;
    present_transaction(connection_fd, BANK_WITHDRAW, accountNumber, 0, input);
}

void transferFunds(int connection_fd, int sourceAccount, int destAccount, const char *amt_str) {
    present_transaction(connection_fd, BANK_TRANSFER, sourceAccount, destAccount, amt_str);
}

void applyLoan(int connection_fd, int accountNumber) {
    char readBuffer[4096], write_buf[4096];
    char err_msg[128];
    long long loan_paise = 0;

    if (socket_write_packet(connection_fd, "Enter loan amount in whole rupees (maximum: Rs. 10,000,000.00): ") != 0) return;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return;

    if (parse_monetary_amount(readBuffer, &loan_paise, err_msg, sizeof(err_msg)) != 0) {
        snprintf(write_buf, sizeof(write_buf), "Error: %s^", err_msg);
        socket_send_ack(connection_fd, write_buf);
        return;
    }

    if ((loan_paise % 100) != 0 || loan_paise > MAX_TRANSACTION_PAISE) {
        socket_send_ack(connection_fd,
                        "Invalid loan amount. Loan must be in whole rupees and cannot exceed Rs. 10,000,000.00.^");
        return;
    }
    int loan_rupees = (int)(loan_paise / 100);

    /* Atomic monotonic loan counter update */
    int file = open(COUNTERPATH, O_RDWR | O_CREAT, 0644);
    int lc = 0;
    if (file != -1) {
        struct flock fl;
        memset(&fl, 0, sizeof(fl));
        fl.l_type = F_WRLCK;
        fcntl(file, F_SETLKW, &fl);

        struct Counter ct;
        if (read(file, &ct, sizeof(ct)) > 0) {
            lc = ct.count;
        }
        lc++;
        ct.count = lc;
        lseek(file, 0, SEEK_SET);
        write(file, &ct, sizeof(ct));
        fsync(file);

        fl.l_type = F_UNLCK;
        fcntl(file, F_SETLK, &fl);
        close(file);
    } else {
        lc = 1;
    }

    struct LoanDetails ld;
    memset(&ld, 0, sizeof(ld));
    ld.empID = -1;
    ld.accountNumber = accountNumber;
    ld.loanAmount = loan_rupees;
    ld.status = 0; /* Requested */
    ld.loanID = lc;

    int file1 = open(LOANPATH, O_RDWR | O_CREAT | O_APPEND, 0644);
    if (file1 != -1) {
        struct flock fl;
        memset(&fl, 0, sizeof(fl));
        fl.l_type = F_WRLCK;
        fcntl(file1, F_SETLKW, &fl);

        write(file1, &ld, sizeof(ld));
        fsync(file1);

        fl.l_type = F_UNLCK;
        fcntl(file1, F_SETLK, &fl);
        close(file1);
    }

    snprintf(write_buf, sizeof(write_buf),
             "Loan application submitted successfully! Loan ID: %d (Pending Manager/Employee approval)^",
             ld.loanID);
    socket_send_ack(connection_fd, write_buf);
}

void transactionHistory(int connection_fd, int accountNumber) {
    struct trans_histroy th;
    char chunk[PACKET_SIZE];
    int count = 0;
    int fd = open(HISTORYPATH, O_RDONLY | O_CREAT, 0600);
    if (fd < 0) { socket_send_ack(connection_fd, "Unable to open transaction history file.^"); return; }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_RDLCK; fl.l_whence = SEEK_SET; fl.l_start = 0; fl.l_len = 0;
    if (fcntl(fd, F_SETLKW, &fl) != 0) { close(fd); socket_send_ack(connection_fd, "Unable to lock transaction history.^"); return; }

    snprintf(chunk, sizeof(chunk), "\n--- Complete Transaction History for Account %d ---\n", accountNumber);
    while (read(fd, &th, sizeof(th)) == (ssize_t)sizeof(th)) {
        if (th.acc_no != accountNumber) continue;
        count++;
        /* Repair legacy records that stored the two characters \ and n
           instead of a real newline. This keeps existing history readable. */
        for (size_t i = 0; i + 1 < sizeof(th.hist) && th.hist[i] != '\0'; i++) {
            if (th.hist[i] == '\\' && th.hist[i + 1] == 'n') {
                th.hist[i] = '\n';
                memmove(&th.hist[i + 1], &th.hist[i + 2],
                        sizeof(th.hist) - i - 2);
            }
        }
        char line[sizeof(th.hist) + 64];
        snprintf(line, sizeof(line), "[%d] %s", count, th.hist);
        if (strlen(chunk) + strlen(line) + 32 >= sizeof(chunk)) {
            strncat(chunk, "\n(continued)^", sizeof(chunk) - strlen(chunk) - 1);
            if (socket_send_ack(connection_fd, chunk) != 0) {
                fl.l_type = F_UNLCK; fcntl(fd, F_SETLK, &fl); close(fd); return;
            }
            snprintf(chunk, sizeof(chunk),
                     "--- Transaction History for Account %d (continued) ---\n", accountNumber);
        }
        strncat(chunk, line, sizeof(chunk) - strlen(chunk) - 1);
    }
    fl.l_type = F_UNLCK; fcntl(fd, F_SETLK, &fl); close(fd);

    if (count == 0) {
        socket_send_ack(connection_fd, "No transactions found for this account. History is stored persistently.^");
        return;
    }
    strncat(chunk, "\n--- End of Transaction History ---^",
            sizeof(chunk) - strlen(chunk) - 1);
    socket_send_ack(connection_fd, chunk);
}

void viewTotalBankMoney(int connection_fd) {
    char write_buf[4096];
    struct Customer c;
    int file = open(CUSPATH, O_RDONLY, 0644);
    if (file == -1) {
        socket_send_ack(connection_fd, "Error reading customer database!^");
        return;
    }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_RDLCK;
    fcntl(file, F_SETLKW, &fl);

    long long totalMoney_paise = 0;
    int totalAccounts = 0;
    int activeAccounts = 0;
    int deactiveAccounts = 0;
    int normalAccounts = 0;
    int jointAccounts = 0;

    lseek(file, 0, SEEK_SET);
    while (read(file, &c, sizeof(c)) == (ssize_t)sizeof(c)) {
        totalAccounts++;
        totalMoney_paise += c.balance;
        if (c.activeStatus == 1) {
            activeAccounts++;
        } else {
            deactiveAccounts++;
        }
        if (c.accountType == 2) {
            jointAccounts++;
        } else {
            normalAccounts++;
        }
    }

    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    char total_str[64];
    format_paise(totalMoney_paise, total_str, sizeof(total_str));

    snprintf(write_buf, sizeof(write_buf),
             "\n========================================\n"
             "        TOTAL BANK MONEY REPORT         \n"
             "========================================\n"
             "Total Money in Bank     : %s\n"
             "Total Accounts          : %d\n"
             "  - Active Accounts     : %d\n"
             "  - Deactivated Accounts: %d\n"
             "  - Normal Accounts     : %d\n"
             "  - Joint Accounts      : %d\n"
             "========================================\n^",
             total_str, totalAccounts,
             activeAccounts, deactiveAccounts,
             normalAccounts, jointAccounts);

    socket_send_ack(connection_fd, write_buf);
}

void addFeedback(int connection_fd, int accountNumber) {
    struct FeedBack fb;
    char input[PACKET_SIZE];
    int fd = open(FEEDPATH, O_CREAT | O_RDWR, 0600);
    if (fd < 0) { socket_send_ack(connection_fd, "Unable to open feedback storage.^"); return; }

    if (socket_write_packet(connection_fd, "Write your feedback (maximum 1023 characters): ") != 0 ||
        socket_read_packet(connection_fd, input, sizeof(input)) != 0) { close(fd); return; }

    size_t len = strnlen(input, sizeof(input));
    while (len > 0 && (input[len-1] == '\n' || input[len-1] == '\r')) input[--len] = '\0';
    if (len == 0 || len >= sizeof(fb.feedback)) {
        close(fd);
        socket_send_ack(connection_fd, len == 0 ? "Feedback cannot be empty.^" :
                        "Feedback too long. Maximum is 1023 characters.^");
        return;
    }

    memset(&fb, 0, sizeof(fb));
    fb.accountNumber = accountNumber;
    memcpy(fb.feedback, input, len);

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK; fl.l_whence = SEEK_SET; fl.l_start = 0; fl.l_len = 0;
    if (fcntl(fd, F_SETLKW, &fl) != 0) { close(fd); socket_send_ack(connection_fd, "Unable to lock feedback file.^"); return; }
    off_t end = lseek(fd, 0, SEEK_END);
    int ok = end >= 0 && write(fd, &fb, sizeof(fb)) == (ssize_t)sizeof(fb) && fsync(fd) == 0;
    fl.l_type = F_UNLCK; fcntl(fd, F_SETLK, &fl); close(fd);
    socket_send_ack(connection_fd, ok ? "Thank you! Your feedback has been saved.^" :
                    "Unable to save feedback. Please try again.^");
}

int changePassword(int connection_fd, int accountNumber) {
    char readBuffer[4096];
    char newPassword[64];
    struct Customer c;

    int file = open(CUSPATH, O_RDWR, 0644);
    if (file == -1) return 0;

    off_t offset = -1;
    if (find_customer_record(file, accountNumber, &offset, &c) != 0) {
        close(file);
        return 0;
    }

    if (lock_customer_record(file, offset, F_WRLCK) != 0) {
        close(file);
        return 0;
    }

    if (socket_write_packet(connection_fd, "Enter new password: ") != 0) {
        unlock_customer_record(file, offset);
        close(file);
        return 0;
    }

    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
        unlock_customer_record(file, offset);
        close(file);
        return 0;
    }

    copy_text(newPassword, readBuffer, sizeof(newPassword));
    newPassword[sizeof(newPassword) - 1] = '\0';

    read_customer_record(file, offset, &c);
    char *h = crypt(newPassword, HASHKEY);
    if (h) {
        copy_text(c.password, h, sizeof(c.password));
    }
    write_customer_record(file, offset, &c);
    fsync(file);

    unlock_customer_record(file, offset);
    close(file);

    printf("Customer %d changed password successfully\n", accountNumber);
    return 1;
}

void logout(int connection_fd, int id) {
    (void)id;
    release_session_semaphore();
    socket_send_ack(connection_fd, "^");
}
