#include "text_utils.h"
#include "app_config.h"
#include "admin_functions.h"
#include "customer_functions.h"
#include "employee_functions.h"
#include "manager_functions.h"
#include "admin_functions.h"

static int get_admin_credentials(struct AdminCred *cred) {
    int fd = open(ADMINPATH, O_RDONLY);
    if (fd == -1) {
        /* Initialize default admin credentials */
        fd = open(ADMINPATH, O_CREAT | O_RDWR, 0600);
        if (fd == -1) return -1;
        memset(cred, 0, sizeof(*cred));
        copy_text(cred->username, ADMINNAME, sizeof(cred->username));
        char *hashed = crypt(PASSWORD, HASHKEY);
        if (hashed) copy_text(cred->password, hashed, sizeof(cred->password));
        write(fd, cred, sizeof(*cred));
        fsync(fd);
        close(fd);
        return 0;
    }
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_RDLCK;
    fcntl(fd, F_SETLKW, &fl);
    ssize_t r = read(fd, cred, sizeof(*cred));
    fl.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &fl);
    close(fd);
    if (r != sizeof(*cred)) return -1;
    return 0;
}

static int verify_admin_password(const char *password) {
    struct AdminCred cred;
    if (get_admin_credentials(&cred) != 0) return 0;
    char *hashed = crypt(password, HASHKEY);
    if (!hashed) return 0;
    return (strcmp(cred.password, hashed) == 0);
}

int changeAdminPassword(int connection_fd) {
    char readBuffer[PACKET_SIZE];
    char oldPassword[64], newPassword[64];

    if (socket_write_packet(connection_fd, "Enter Current Admin Password: ") != 0) return 0;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return 0;
    copy_text(oldPassword, readBuffer, sizeof(oldPassword));
    oldPassword[sizeof(oldPassword) - 1] = '\0';

    if (!verify_admin_password(oldPassword)) {
        socket_send_ack(connection_fd, "Error: Current admin password incorrect!^");
        return 0;
    }

    if (socket_write_packet(connection_fd, "Enter New Admin Password: ") != 0) return 0;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return 0;
    if (strlen(readBuffer) == 0) {
        socket_send_ack(connection_fd, "Error: Password cannot be empty!^");
        return 0;
    }
    copy_text(newPassword, readBuffer, sizeof(newPassword));
    newPassword[sizeof(newPassword) - 1] = '\0';

    int fd = open(ADMINPATH, O_CREAT | O_RDWR, 0600);
    if (fd == -1) {
        socket_send_ack(connection_fd, "Error opening admin credential store!^");
        return 0;
    }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fcntl(fd, F_SETLKW, &fl);

    struct AdminCred cred;
    memset(&cred, 0, sizeof(cred));
    copy_text(cred.username, ADMINNAME, sizeof(cred.username));
    char *hashed = crypt(newPassword, HASHKEY);
    if (hashed) copy_text(cred.password, hashed, sizeof(cred.password));

    lseek(fd, 0, SEEK_SET);
    write(fd, &cred, sizeof(cred));
    fsync(fd);

    fl.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &fl);
    close(fd);

    printf("Admin password changed successfully\n");
    return 1;
}

void adminMenu(int connection_fd) {
    char password[64];
    char readBuffer[PACKET_SIZE];

start:
    if (socket_write_packet(connection_fd, "Enter password (or type back to return to main menu): ") != 0) return;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return;
    if (strcasecmp(readBuffer, "back") == 0 || strcmp(readBuffer, "0") == 0) {
        socket_send_ack(connection_fd, "Returning to the main menu...^");
        return;
    }
    copy_text(password, readBuffer, sizeof(password));
    password[sizeof(password) - 1] = '\0';

    if (verify_admin_password(password)) {
        /* One administrator session across all server ports. */
        if (acquire_session_lock("admin", 1) != 0) {
            if (errno == EACCES || errno == EAGAIN) {
                socket_send_ack(connection_fd, "\nAdministrator is already logged in on another terminal!^");
                goto start;
            }
            socket_send_ack(connection_fd, "\nSession lock error during admin login!^");
            goto start;
        }

        printf("Administrator authenticated successfully (session locked)\n");
        if (socket_send_ack(connection_fd, "\nLogin Successfully^") != 0) {
            release_session_semaphore();
            return;
        }
    } else {
        printf("Admin login failed: invalid password\n");
        socket_send_ack(connection_fd, "\nInvalid admin credentials! Please try again.^");
        goto start;
    }

    while (1) {
        int choice;
        if (socket_write_packet(connection_fd, ADMINMENU) != 0) {
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
        printf("Admin entered choice: %d\n", choice);

        switch (choice) {
            case 1:
                /* Add New Customer / Joint Account */
                addAccountAdmin(connection_fd);
                break;
            case 2:
                /* Delete Account */
                deleteAccountAdmin(connection_fd);
                break;
            case 3:
                /* Modify Account */
                modifyAccountAdmin(connection_fd);
                break;
            case 4:
                /* Search Account Details */
                searchAccountAdmin(connection_fd);
                break;
            case 5:
                /* View Total Bank Money */
                viewTotalBankMoney(connection_fd);
                break;
            case 6:
                /* Print Transaction History for Account with Timestamp */
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
                /* Add New Bank Employee */
                if (addEmployee(connection_fd)) {
                    socket_send_ack(connection_fd, "Employee successfully added!\n^");
                }
                break;
            case 8:
                /* Modify Employee Info */
                modifyEmployeeAdmin(connection_fd);
                break;
            case 9:
                /* Manage Roles & Permissions */
                manageRole(connection_fd);
                break;
            case 10:
                /* Change Password */
                if (changeAdminPassword(connection_fd)) {
                    socket_send_ack(connection_fd, "Password changed successfully! Please login again with new password.^");
                    release_session_semaphore();
                    goto start;
                }
                break;
            case 11:
                /* Logout */
                printf("Admin logged out\n");
                release_session_semaphore();
                return;
            case 12:
                /* Exit Program */
                printf("Admin exited program\n");
                release_session_semaphore();
                exitClient(connection_fd, 0);
                return;
            default:
                printf("Invalid choice for admin menu: %d\n", choice);
                socket_send_ack(connection_fd, "Invalid choice! Please try again.^");
                break;
        }
    }
}

int addAccountAdmin(int connection_fd) {
    char write_buf[PACKET_SIZE], readBuffer[PACKET_SIZE];
    struct Customer newCust, checkCust;
    memset(&newCust, 0, sizeof(newCust));

    /* Account Number */
    if (socket_write_packet(connection_fd, "Enter New Account Number: ") != 0) return 0;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return 0;
    int accNo = atoi(readBuffer);
    if (accNo <= 0) {
        socket_send_ack(connection_fd, "Error: Account number must be a positive integer!^");
        return 0;
    }
    newCust.accountNumber = accNo;

    /* Check uniqueness */
    int fileCheck = open(CUSPATH, O_RDONLY | O_CREAT, 0644);
    if (fileCheck != -1) {
        off_t off;
        if (find_customer_record(fileCheck, newCust.accountNumber, &off, &checkCust) == 0) {
            close(fileCheck);
            socket_send_ack(connection_fd, "Error: Account number already exists!^");
            return 0;
        }
        close(fileCheck);
    }

    /* Customer Name */
    if (socket_write_packet(connection_fd, "Enter Account Holder Name: ") != 0) return 0;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return 0;
    if (strlen(readBuffer) == 0) {
        socket_send_ack(connection_fd, "Error: Account holder name cannot be empty!^");
        return 0;
    }
    copy_text(newCust.customerName, readBuffer, sizeof(newCust.customerName));

    /* Password */
    if (socket_write_packet(connection_fd, "Enter Password: ") != 0) return 0;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return 0;
    if (strlen(readBuffer) == 0) {
        socket_send_ack(connection_fd, "Error: Password cannot be empty!^");
        return 0;
    }
    copy_text(newCust.password, crypt(readBuffer, HASHKEY), sizeof(newCust.password));

    /* Account Type */
    if (socket_write_packet(connection_fd, "Select Account Type:\n1. Normal Account\n2. Joint Account\nEnter choice: ") != 0) return 0;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return 0;
    int typeChoice = atoi(readBuffer);
    newCust.accountType = (typeChoice == 2) ? 2 : 1;

    /* Opening Balance */
    if (socket_write_packet(connection_fd, "Enter Opening Balance (maximum initial deposit: Rs. 10,000,000.00; maximum account balance: Rs. 1,000,000,000.00): ") != 0) return 0;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return 0;

    long long opening_paise = 0;
    char err_msg[128];
    if (parse_monetary_amount(readBuffer, &opening_paise, err_msg, sizeof(err_msg)) != 0) {
        snprintf(write_buf, sizeof(write_buf), "Error: Invalid opening balance (%s)!^", err_msg);
        socket_send_ack(connection_fd, write_buf);
        return 0;
    }
    newCust.balance = opening_paise;
    newCust.activeStatus = 1;

    /* Append to customer database under write lock */
    int file = open(CUSPATH, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (file == -1) {
        socket_send_ack(connection_fd, "Error opening customer database!^");
        return 0;
    }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_END;
    fl.l_start = 0;
    fl.l_len = sizeof(struct Customer);
    fcntl(file, F_SETLKW, &fl);

    write(file, &newCust, sizeof(newCust));

    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    /* Record initial transaction with timestamp in paise formatted plain */
    int fp = open(HISTORYPATH, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fp != -1) {
        time_t s = time(NULL);
        struct tm *ct = localtime(&s);
        struct trans_histroy th;
        memset(&th, 0, sizeof(th));
        th.acc_no = newCust.accountNumber;

        char amt_plain[64];
        format_paise_plain(newCust.balance, amt_plain, sizeof(amt_plain));

        snprintf(th.hist, sizeof(th.hist), "%s Opening Balance at %02d:%02d:%02d %04d-%02d-%02d\n",
                 amt_plain, ct->tm_hour, ct->tm_min, ct->tm_sec,
                 ct->tm_year + 1900, ct->tm_mon + 1, ct->tm_mday);
        write(fp, &th, sizeof(th));
        close(fp);
    }

    char bal_str[64];
    format_paise(newCust.balance, bal_str, sizeof(bal_str));

    snprintf(write_buf, sizeof(write_buf),
             "Account %d successfully created as %s! Remaining balance: %s^",
             newCust.accountNumber,
             newCust.accountType == 2 ? "Joint Account" : "Normal Account",
             bal_str);
    socket_send_ack(connection_fd, write_buf);
    return 1;
}

int deleteAccountAdmin(int connection_fd) {
    char write_buf[PACKET_SIZE], readBuffer[PACKET_SIZE];
    struct Customer c;
    off_t offset = -1;

    if (socket_write_packet(connection_fd, "Enter Account Number to Delete/Deactivate: ") != 0) return 0;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return 0;
    int accNo = atoi(readBuffer);
    if (accNo <= 0) {
        socket_send_ack(connection_fd, "Error: Invalid account number format!^");
        return 0;
    }

    int file = open(CUSPATH, O_RDWR, 0644);
    if (file == -1) {
        socket_send_ack(connection_fd, "Error opening customer database!^");
        return 0;
    }

    if (find_customer_record(file, accNo, &offset, &c) != 0) {
        close(file);
        socket_send_ack(connection_fd, "Account not found!^");
        return 0;
    }

    if (lock_customer_record(file, offset, F_WRLCK) != 0) {
        close(file);
        socket_send_ack(connection_fd, "Account is currently locked by another transaction!^");
        return 0;
    }

    read_customer_record(file, offset, &c);
    c.activeStatus = 0; /* Deactivated */
    write_customer_record(file, offset, &c);

    unlock_customer_record(file, offset);
    close(file);

    char bal_str[64];
    format_paise(c.balance, bal_str, sizeof(bal_str));

    snprintf(write_buf, sizeof(write_buf),
             "Account %d successfully deactivated/deleted! Remaining balance: %s^",
             accNo, bal_str);
    socket_send_ack(connection_fd, write_buf);
    return 1;
}

int modifyAccountAdmin(int connection_fd) {
    char write_buf[PACKET_SIZE], readBuffer[PACKET_SIZE];
    struct Customer c;
    off_t offset = -1;

    if (socket_write_packet(connection_fd, "Enter Account Number to Modify: ") != 0) return 0;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return 0;
    int accNo = atoi(readBuffer);
    if (accNo <= 0) {
        socket_send_ack(connection_fd, "Error: Invalid account number format!^");
        return 0;
    }

    int file = open(CUSPATH, O_RDWR, 0644);
    if (file == -1) {
        socket_send_ack(connection_fd, "Error opening database!^");
        return 0;
    }

    if (find_customer_record(file, accNo, &offset, &c) != 0) {
        close(file);
        socket_send_ack(connection_fd, "Account not found!^");
        return 0;
    }

    if (lock_customer_record(file, offset, F_WRLCK) != 0) {
        close(file);
        socket_send_ack(connection_fd, "Account is currently locked by another process!^");
        return 0;
    }

    read_customer_record(file, offset, &c);

    if (socket_write_packet(connection_fd,
                            "What would you like to modify?\n"
                            "1. Customer Name\n"
                            "2. Password\n"
                            "3. Account Type (Normal/Joint)\n"
                            "4. Activate/Deactivate Status\n"
                            "Enter choice: ") != 0) {
        unlock_customer_record(file, offset);
        close(file);
        return 0;
    }
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
        unlock_customer_record(file, offset);
        close(file);
        return 0;
    }

    int modChoice = atoi(readBuffer);
    if (modChoice == 1) {
        if (socket_write_packet(connection_fd, "Enter New Customer Name: ") != 0) {
            unlock_customer_record(file, offset);
            close(file);
            return 0;
        }
        if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
            unlock_customer_record(file, offset);
            close(file);
            return 0;
        }
        copy_text(c.customerName, readBuffer, sizeof(c.customerName));
    } else if (modChoice == 2) {
        if (socket_write_packet(connection_fd, "Enter New Password: ") != 0) {
            unlock_customer_record(file, offset);
            close(file);
            return 0;
        }
        if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
            unlock_customer_record(file, offset);
            close(file);
            return 0;
        }
        copy_text(c.password, crypt(readBuffer, HASHKEY), sizeof(c.password));
    } else if (modChoice == 3) {
        if (socket_write_packet(connection_fd, "Enter 1 for Normal Account, 2 for Joint Account: ") != 0) {
            unlock_customer_record(file, offset);
            close(file);
            return 0;
        }
        if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
            unlock_customer_record(file, offset);
            close(file);
            return 0;
        }
        int t = atoi(readBuffer);
        c.accountType = (t == 2) ? 2 : 1;
    } else if (modChoice == 4) {
        if (socket_write_packet(connection_fd, "Enter 1 to Activate, 0 to Deactivate: ") != 0) {
            unlock_customer_record(file, offset);
            close(file);
            return 0;
        }
        if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
            unlock_customer_record(file, offset);
            close(file);
            return 0;
        }
        c.activeStatus = atoi(readBuffer);
    } else {
        unlock_customer_record(file, offset);
        close(file);
        socket_send_ack(connection_fd, "Invalid modification option!^");
        return 0;
    }

    write_customer_record(file, offset, &c);
    unlock_customer_record(file, offset);
    close(file);

    char bal_str[64];
    format_paise(c.balance, bal_str, sizeof(bal_str));

    snprintf(write_buf, sizeof(write_buf),
             "Account %d updated successfully! Remaining balance: %s^", accNo, bal_str);
    socket_send_ack(connection_fd, write_buf);
    return 1;
}

int searchAccountAdmin(int connection_fd) {
    char write_buf[PACKET_SIZE], readBuffer[PACKET_SIZE];
    struct Customer c;
    off_t offset = -1;

    if (socket_write_packet(connection_fd, "Enter Account Number to Search: ") != 0) return 0;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return 0;
    int accNo = atoi(readBuffer);
    if (accNo <= 0) {
        socket_send_ack(connection_fd, "Error: Invalid account number format!^");
        return 0;
    }

    int file = open(CUSPATH, O_RDONLY, 0644);
    if (file == -1) {
        socket_send_ack(connection_fd, "Error opening customer database!^");
        return 0;
    }

    if (find_customer_record(file, accNo, &offset, &c) != 0) {
        close(file);
        socket_send_ack(connection_fd, "Account not found!^");
        return 0;
    }

    if (lock_customer_record(file, offset, F_RDLCK) != 0) {
        close(file);
        socket_send_ack(connection_fd, "Error locking customer record for search!^");
        return 0;
    }

    read_customer_record(file, offset, &c);
    unlock_customer_record(file, offset);
    close(file);

    char bal_str[64];
    format_paise(c.balance, bal_str, sizeof(bal_str));

    snprintf(write_buf, sizeof(write_buf),
             "\n=== Account Search Result ===\n"
             "Account Number   : %d\n"
             "Customer Name    : %s\n"
             "Account Type     : %s\n"
             "Remaining Balance: %s\n"
             "Account Status   : %s\n^",
             c.accountNumber,
             c.customerName,
             c.accountType == 2 ? "Joint Account" : "Normal Account",
             bal_str,
             c.activeStatus == 1 ? "Active" : "Deactivated");
    socket_send_ack(connection_fd, write_buf);
    return 1;
}

int addEmployee(int connection_fd) {
    char readBuffer[PACKET_SIZE];
    struct Employee emp, checkEmp;
    memset(&emp, 0, sizeof(emp));

    if (socket_write_packet(connection_fd, "Enter Employee ID: ") != 0) return 0;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return 0;
    int empID = atoi(readBuffer);
    if (empID <= 0) {
        socket_send_ack(connection_fd, "Error: Employee ID must be a positive integer!^");
        return 0;
    }
    emp.empID = empID;

    /* Check duplicate employee ID */
    int empCheck = open(EMPPATH, O_RDONLY | O_CREAT, 0644);
    if (empCheck != -1) {
        while (read(empCheck, &checkEmp, sizeof(checkEmp)) == (ssize_t)sizeof(checkEmp)) {
            if (checkEmp.empID == emp.empID) {
                close(empCheck);
                socket_send_ack(connection_fd, "Error: Employee ID already exists!^");
                return 0;
            }
        }
        close(empCheck);
    }

    if (socket_write_packet(connection_fd, "Enter FirstName: ") != 0) return 0;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return 0;
    copy_text(emp.firstName, readBuffer, sizeof(emp.firstName));

    if (socket_write_packet(connection_fd, "Enter LastName: ") != 0) return 0;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return 0;
    copy_text(emp.lastName, readBuffer, sizeof(emp.lastName));

    if (socket_write_packet(connection_fd, "Enter Password: ") != 0) return 0;
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) return 0;
    copy_text(emp.password, crypt(readBuffer, HASHKEY), sizeof(emp.password));

    emp.role = 1; /* Default: Employee */

    int file = open(EMPPATH, O_CREAT | O_RDWR, 0644);
    if (file == -1) {
        socket_send_ack(connection_fd, "Error opening employee file!^");
        return 0;
    }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_END;
    fl.l_start = 0;
    fl.l_len = sizeof(struct Employee);
    fcntl(file, F_SETLKW, &fl);

    lseek(file, 0, SEEK_END);
    ssize_t ret = write(file, &emp, sizeof(emp));

    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    return (ret > 0);
}

void modifyEmployeeAdmin(int connection_fd) {
    char write_buf[PACKET_SIZE], readBuffer[PACKET_SIZE];
    struct Employee emp;

    int file = open(EMPPATH, O_CREAT | O_RDWR, 0644);
    if (file == -1) {
        socket_send_ack(connection_fd, "Error opening employee file!^");
        return;
    }

    if (socket_write_packet(connection_fd, "Enter Employee ID: ") != 0) {
        close(file);
        return;
    }
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
        close(file);
        return;
    }
    int id = atoi(readBuffer);

    lseek(file, 0, SEEK_SET);
    off_t srcOffset = -1;
    int sourceFound = 0;

    while (read(file, &emp, sizeof(emp)) == (ssize_t)sizeof(emp)) {
        if (emp.empID == id) {
            srcOffset = lseek(file, -((off_t)sizeof(struct Employee)), SEEK_CUR);
            sourceFound = 1;
            break;
        }
    }

    if (sourceFound) {
        struct flock fl1;
        memset(&fl1, 0, sizeof(fl1));
        fl1.l_type = F_WRLCK;
        fl1.l_whence = SEEK_SET;
        fl1.l_start = srcOffset;
        fl1.l_len = sizeof(struct Employee);
        fl1.l_pid = getpid();
        fcntl(file, F_SETLKW, &fl1);

        if (socket_write_packet(connection_fd,
                                "What would you like to modify?\n"
                                "1. First Name\n"
                                "2. Last Name\n"
                                "3. Password\n"
                                "Enter choice: ") != 0) {
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

        int empChoice = atoi(readBuffer);
        if (empChoice == 1) {
            if (socket_write_packet(connection_fd, "Enter New First Name: ") != 0) {
                fl1.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl1); close(file); return;
            }
            if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
                fl1.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl1); close(file); return;
            }
            copy_text(emp.firstName, readBuffer, sizeof(emp.firstName));
            emp.firstName[sizeof(emp.firstName) - 1] = '\0';
        } else if (empChoice == 2) {
            if (socket_write_packet(connection_fd, "Enter New Last Name: ") != 0) {
                fl1.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl1); close(file); return;
            }
            if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
                fl1.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl1); close(file); return;
            }
            copy_text(emp.lastName, readBuffer, sizeof(emp.lastName));
            emp.lastName[sizeof(emp.lastName) - 1] = '\0';
        } else if (empChoice == 3) {
            if (socket_write_packet(connection_fd, "Enter New Password: ") != 0) {
                fl1.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl1); close(file); return;
            }
            if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
                fl1.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl1); close(file); return;
            }
            copy_text(emp.password, crypt(readBuffer, HASHKEY), sizeof(emp.password));
            emp.password[sizeof(emp.password) - 1] = '\0';
        } else {
            fl1.l_type = F_UNLCK;
            fcntl(file, F_SETLK, &fl1);
            close(file);
            socket_send_ack(connection_fd, "Invalid modification choice!^");
            return;
        }

        lseek(file, srcOffset, SEEK_SET);
        write(file, &emp, sizeof(emp));

        fl1.l_type = F_UNLCK;
        fcntl(file, F_SETLK, &fl1);
        close(file);

        snprintf(write_buf, sizeof(write_buf), "Employee %d details updated successfully!^", id);
        socket_send_ack(connection_fd, write_buf);
    } else {
        close(file);
        socket_send_ack(connection_fd, "Invalid Employee ID!^");
    }
}

void manageRole(int connection_fd) {
    char write_buf[PACKET_SIZE], readBuffer[PACKET_SIZE];
    int file = open(EMPPATH, O_CREAT | O_RDWR, 0644);
    if (file == -1) {
        socket_send_ack(connection_fd, "Error opening employee file!^");
        return;
    }

    if (socket_write_packet(connection_fd, "Enter Employee ID: ") != 0) {
        close(file);
        return;
    }
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
        close(file);
        return;
    }
    int id = atoi(readBuffer);

    struct Employee emp;
    lseek(file, 0, SEEK_SET);
    off_t offset = -1;
    int found = 0;

    while (read(file, &emp, sizeof(emp)) == (ssize_t)sizeof(emp)) {
        if (emp.empID == id) {
            offset = lseek(file, -((off_t)sizeof(struct Employee)), SEEK_CUR);
            found = 1;
            break;
        }
    }

    if (!found) {
        close(file);
        socket_send_ack(connection_fd, "Invalid Employee ID!^");
        return;
    }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = offset;
    fl.l_len = sizeof(struct Employee);
    fl.l_pid = getpid();
    fcntl(file, F_SETLKW, &fl);

    if (socket_write_packet(connection_fd,
                            "Enter 1 to promote to Manager\n"
                            "Enter 2 to set as Employee\n"
                            "Enter choice: ") != 0) {
        fl.l_type = F_UNLCK;
        fcntl(file, F_SETLK, &fl);
        close(file);
        return;
    }
    if (socket_read_packet(connection_fd, readBuffer, sizeof(readBuffer)) != 0) {
        fl.l_type = F_UNLCK;
        fcntl(file, F_SETLK, &fl);
        close(file);
        return;
    }

    int choice = atoi(readBuffer);
    if (choice == 1) {
        emp.role = 0; /* Manager */
        printf("Admin promoted employee %d to Manager\n", id);
    } else if (choice == 2) {
        emp.role = 1; /* Employee */
        printf("Admin assigned employee %d as Employee\n", id);
    } else {
        fl.l_type = F_UNLCK;
        fcntl(file, F_SETLK, &fl);
        close(file);
        socket_send_ack(connection_fd, "Invalid role choice!^");
        return;
    }

    lseek(file, offset, SEEK_SET);
    write(file, &emp, sizeof(emp));

    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    snprintf(write_buf, sizeof(write_buf), "Role updated successfully for ID %d!^", id);
    socket_send_ack(connection_fd, write_buf);
}
