#include "text_utils.h"
#include "app_config.h"
#include "manager_functions.h"
#include "customer_functions.h"
#include "employee_functions.h"
#include "manager_functions.h"
#include "admin_functions.h"

void managerMenu(int connection_fd) {
    int mngID;
    char password[64];
    int choice;
    char readBuffer[PACKET_SIZE];

start:
    if (socket_write_packet(connection_fd, "\nEnter Manager ID: ") != 0) return;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return;
    if (strcasecmp(readBuffer, "back") == 0 || strcmp(readBuffer, "0") == 0) {
        socket_send_ack(connection_fd, "Returning to the main menu...^");
        return;
    }
    mngID = atoi(readBuffer);

    if (socket_write_packet(connection_fd, "Enter password (or type back to return to main menu): ") != 0) return;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return;
    if (strcasecmp(readBuffer, "back") == 0 || strcmp(readBuffer, "0") == 0) {
        socket_send_ack(connection_fd, "Returning to the main menu...^");
        return;
    }
    copy_text(password, readBuffer, sizeof(password));
    password[sizeof(password) - 1] = '\0';

    int loginResult = loginManager(connection_fd, mngID, password);
    if (loginResult == 1) {
        printf("Manager with ID %d logged in\n", mngID);
        if (socket_send_ack(connection_fd, "Login Successfully\n^") != 0) {
            release_session_semaphore();
            return;
        }

        while (1) {
            if (socket_write_packet(connection_fd, MNGMENU) != 0) {
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
            printf("Manager %d entered choice: %d\n", mngID, choice);

            switch (choice) {
                case 1:
                    /* Enable/Disable Customer Accounts */
                    changeStatus(connection_fd);
                    break;
                case 2:
                    /* Facility of Loan Request Approved by Manager */
                    approveRejectLoan(connection_fd, mngID, "Manager");
                    break;
                case 3:
                    /* Assign Loan Application Processes to Employees */
                    assignLoanApplication(connection_fd);
                    break;
                case 4:
                    /* Printing All Money in Bank Currently for Manager */
                    viewTotalBankMoney(connection_fd);
                    break;
                case 5:
                    /* Review Customer Feedback */
                    readFeedBack(connection_fd);
                    break;
                case 6:
                    /* Print Transaction History for Particular Account with Timestamp */
                    {
                        if (socket_write_packet(connection_fd, "Enter Account Number to View History: ") != 0) {
                            release_session_semaphore();
                            return;
                        }
                        if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
                            release_session_semaphore();
                            return;
                        }
                        int accNo = atoi(readBuffer);
                        if (accNo <= 0) {
                            socket_send_ack(connection_fd, "Error: Invalid account number format!^");
                        } else {
                            transactionHistory(connection_fd, accNo);
                        }
                    }
                    break;
                case 7:
                    /* Update Password */
                    if (changeMNGPassword(connection_fd, mngID)) {
                        socket_send_ack(connection_fd, "Password changed successfully! Login with new password...\n^");
                    } else {
                        socket_send_ack(connection_fd, "Unable to change password\n^");
                    }
                    logout(connection_fd, mngID);
                    goto start;
                case 8:
                    /* Logout */
                    printf("Manager %d logged out\n", mngID);
                    logout(connection_fd, mngID);
                    return;
                case 9:
                    /* Exit Program */
                    printf("Manager %d exited\n", mngID);
                    exitClient(connection_fd, mngID);
                    return;
                default:
                    printf("Invalid input for manager menu: %d\n", choice);
                    socket_send_ack(connection_fd, "Invalid choice for Manager menu!^");
                    break;
            }
        }
    } else if (loginResult == -2) {
        socket_send_ack(connection_fd, "\nManager ID is already logged in on another terminal!^");
        goto start;
    } else if (loginResult == -4) {
        socket_send_ack(connection_fd, "\nIncorrect password for Manager ID! Please try again.^");
        goto start;
    } else if (loginResult == -3) {
        socket_send_ack(connection_fd, "\nThis ID does not belong to a Bank Manager!^");
        goto start;
    } else {
        socket_send_ack(connection_fd, "\nManager ID not found! Please check and try again.^");
        goto start;
    }
}



void changeStatus(int connection_fd) {
    char write_buf[PACKET_SIZE], readBuffer[PACKET_SIZE];
    struct Customer cs;
    off_t offset = -1;

    if (socket_write_packet(connection_fd, "Enter Account Number: ") != 0) return;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return;
    int accNo = atoi(readBuffer);
    if (accNo <= 0) {
        socket_send_ack(connection_fd, "Error: Invalid account number format!^");
        return;
    }

    int file = open(CUSPATH, O_RDWR, 0644);
    if (file == -1) {
        socket_send_ack(connection_fd, "Error opening customer database!^");
        return;
    }

    if (find_customer_record(file, accNo, &offset, &cs) != 0) {
        close(file);
        printf("Manager entered invalid account number: %d\n", accNo);
        socket_send_ack(connection_fd, "Invalid account number!^");
        return;
    }

    /* Acquire exclusive write lock */
    if (lock_customer_record(file, offset, F_WRLCK) != 0) {
        close(file);
        socket_send_ack(connection_fd, "Account record is currently locked by another transaction. Try again.^");
        return;
    }

    /* Re-read under lock */
    if (read_customer_record(file, offset, &cs) != 0) {
        unlock_customer_record(file, offset);
        close(file);
        socket_send_ack(connection_fd, "Error reading locked customer record!^");
        return;
    }

    if (socket_write_packet(connection_fd, "Enter 1 to Deactivate\nEnter 2 to Activate\nEnter your choice: ") != 0) {
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
        cs.activeStatus = 0;
        printf("Manager deactivated customer %d\n", accNo);
    } else if (choice == 2) {
        cs.activeStatus = 1;
        printf("Manager activated customer %d\n", accNo);
    } else {
        unlock_customer_record(file, offset);
        close(file);
        socket_send_ack(connection_fd, "Invalid status choice! Operation cancelled.^");
        return;
    }

    /* Persist change */
    write_customer_record(file, offset, &cs);
    unlock_customer_record(file, offset);
    close(file);

    char bal_str[64];
    format_paise(cs.balance, bal_str, sizeof(bal_str));

    snprintf(write_buf, sizeof(write_buf),
             "Account %d status changed to %s successfully! Remaining balance: %s^",
             accNo, cs.activeStatus == 1 ? "Active" : "Deactivated", bal_str);
    socket_send_ack(connection_fd, write_buf);
}

void readFeedBack(int connection_fd) {
    struct FeedBack fb;
    char chunk[PACKET_SIZE];
    int fd = open(FEEDPATH, O_RDONLY);
    if (fd < 0) { socket_send_ack(connection_fd, "No feedback recorded yet.^"); return; }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_RDLCK; fl.l_whence = SEEK_SET; fl.l_start = 0; fl.l_len = 0;
    if (fcntl(fd, F_SETLKW, &fl) != 0) { close(fd); socket_send_ack(connection_fd, "Unable to read feedback.^"); return; }

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size % (off_t)sizeof(struct FeedBack) != 0) {
        fl.l_type = F_UNLCK; fcntl(fd, F_SETLK, &fl); close(fd);
        socket_send_ack(connection_fd, "Feedback file has an unsupported older format; migrate it before review.^");
        return;
    }

    snprintf(chunk, sizeof(chunk), "\n=== Customer Feedback Review ===\n");
    int count = 0;
    while (read(fd, &fb, sizeof(fb)) == (ssize_t)sizeof(fb)) {
        char line[1200];
        count++;
        snprintf(line, sizeof(line), "\n[%d] Account Number: %d\nFeedback: %s\n",
                 count, fb.accountNumber, fb.feedback);
        if (strlen(chunk) + strlen(line) + 8 >= sizeof(chunk)) {
            strncat(chunk, "^", sizeof(chunk) - strlen(chunk) - 1);
            if (socket_send_ack(connection_fd, chunk) != 0) {
                fl.l_type = F_UNLCK; fcntl(fd, F_SETLK, &fl); close(fd); return;
            }
            snprintf(chunk, sizeof(chunk), "=== Customer Feedback Review (continued) ===\n");
        }
        strncat(chunk, line, sizeof(chunk) - strlen(chunk) - 1);
    }
    fl.l_type = F_UNLCK; fcntl(fd, F_SETLK, &fl); close(fd);

    if (count == 0) socket_send_ack(connection_fd, "No feedback recorded yet.^");
    else {
        strncat(chunk, "^", sizeof(chunk) - strlen(chunk) - 1);
        socket_send_ack(connection_fd, chunk);
    }
    printf("Manager reviewed %d customer feedback entries\n", count);
}

void assignLoanApplication(int connection_fd) {
    char write_buf[PACKET_SIZE], readBuffer[PACKET_SIZE];
    struct LoanDetails ld;

    int file = open(LOANPATH, O_CREAT | O_RDWR, 0644);
    if (file == -1) {
        socket_send_ack(connection_fd, "Error opening loan details file!^");
        return;
    }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_RDLCK;
    fcntl(file, F_SETLKW, &fl);

    /* Print unassigned loan applications */
    int unassignedCount = 0;
    while (read(file, &ld, sizeof(ld)) == (ssize_t)sizeof(ld)) {
        if (ld.empID == -1 && ld.status == 0) {
            unassignedCount++;
            char loan_msg[256];
            snprintf(loan_msg, sizeof(loan_msg),
                     "Unassigned Loan ID: %d | Account Number: %d | Amount: Rs. %d^",
                     ld.loanID, ld.accountNumber, ld.loanAmount);
            socket_send_ack(connection_fd, loan_msg);
        }
    }

    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);

    if (unassignedCount == 0) {
        close(file);
        socket_send_ack(connection_fd, "No unassigned loan applications found.^");
        return;
    }

    if (socket_write_packet(connection_fd, "Enter Loan ID to Assign: ") != 0) {
        close(file);
        return;
    }
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
        close(file);
        return;
    }
    int lID = atoi(readBuffer);

    off_t srcOffset = -1;
    lseek(file, 0, SEEK_SET);
    while (read(file, &ld, sizeof(ld)) == (ssize_t)sizeof(ld)) {
        if (ld.loanID == lID) {
            srcOffset = lseek(file, -((off_t)sizeof(struct LoanDetails)), SEEK_CUR);
            break;
        }
    }

    if (srcOffset == -1) {
        close(file);
        socket_send_ack(connection_fd, "Invalid loan ID!^");
        return;
    }

    /* Lock the loan record exclusively */
    struct flock fl1;
    memset(&fl1, 0, sizeof(fl1));
    fl1.l_type = F_WRLCK;
    fl1.l_whence = SEEK_SET;
    fl1.l_start = srcOffset;
    fl1.l_len = sizeof(struct LoanDetails);
    fl1.l_pid = getpid();

    if (fcntl(file, F_SETLK, &fl1) == -1) {
        close(file);
        socket_send_ack(connection_fd, "Loan application is currently locked by another process!^");
        return;
    }

    if (socket_write_packet(connection_fd, "Enter Employee ID to Assign: ") != 0) {
        fl1.l_type = F_UNLCK;
        fcntl(file, F_SETLK, &fl1);
        close(file);
        return;
    }
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
        fl1.l_type = F_UNLCK;
        fcntl(file, F_SETLK, &fl1);
        close(file);
        return;
    }
    int eID = atoi(readBuffer);

    /* Verify employee exists */
    int empFile = open(EMPPATH, O_RDONLY);
    int empExists = 0;
    if (empFile != -1) {
        struct Employee e;
        while (read(empFile, &e, sizeof(e)) == (ssize_t)sizeof(e)) {
            if (e.empID == eID) {
                empExists = 1;
                break;
            }
        }
        close(empFile);
    }

    if (!empExists) {
        fl1.l_type = F_UNLCK;
        fcntl(file, F_SETLK, &fl1);
        close(file);
        socket_send_ack(connection_fd, "Error: Employee ID does not exist!^");
        return;
    }

    lseek(file, srcOffset, SEEK_SET);
    read(file, &ld, sizeof(ld));

    ld.empID = eID;
    ld.status = 1; /* Pending review by assigned employee */
    printf("Manager assigned employee %d to loan application %d\n", eID, lID);

    lseek(file, srcOffset, SEEK_SET);
    write(file, &ld, sizeof(ld));

    fl1.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl1);
    close(file);

    snprintf(write_buf, sizeof(write_buf),
             "Loan application %d successfully assigned to employee %d!^", lID, eID);
    socket_send_ack(connection_fd, write_buf);
}

int changeMNGPassword(int connection_fd, int mngID) {
    char readBuffer[PACKET_SIZE];
    char newPassword[64];
    struct Employee m;

    int file = open(EMPPATH, O_CREAT | O_RDWR, 0644);
    if (file == -1) return 0;

    lseek(file, 0, SEEK_SET);
    off_t srcOffset = -1;
    int sourceFound = 0;

    while (read(file, &m, sizeof(m)) == (ssize_t)sizeof(m)) {
        if (m.empID == mngID) {
            srcOffset = lseek(file, -((off_t)sizeof(struct Employee)), SEEK_CUR);
            sourceFound = 1;
            break;
        }
    }

    if (!sourceFound) {
        close(file);
        return 0;
    }

    struct flock fl1;
    memset(&fl1, 0, sizeof(fl1));
    fl1.l_type = F_WRLCK;
    fl1.l_whence = SEEK_SET;
    fl1.l_start = srcOffset;
    fl1.l_len = sizeof(struct Employee);
    fl1.l_pid = getpid();
    fcntl(file, F_SETLKW, &fl1);

    if (socket_write_packet(connection_fd, "Enter new password: ") != 0) {
        fl1.l_type = F_UNLCK;
        fcntl(file, F_SETLK, &fl1);
        close(file);
        return 0;
    }
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
        fl1.l_type = F_UNLCK;
        fcntl(file, F_SETLK, &fl1);
        close(file);
        return 0;
    }

    copy_text(newPassword, readBuffer, sizeof(newPassword));
    newPassword[sizeof(newPassword) - 1] = '\0';

    lseek(file, srcOffset, SEEK_SET);
    read(file, &m, sizeof(m));
    copy_text(m.password, crypt(newPassword, HASHKEY), sizeof(m.password));

    lseek(file, srcOffset, SEEK_SET);
    write(file, &m, sizeof(m));

    fl1.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl1);
    close(file);

    printf("Manager %d changed password successfully\n", mngID);
    return 1;
}
