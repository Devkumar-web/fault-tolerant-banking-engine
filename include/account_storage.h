#ifndef ACCOUNT_STORAGE_H
#define ACCOUNT_STORAGE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <math.h>
#include "bank_fun.h"
#include "financial_utils.h"

/* Legacy structure for deliberate, safe migration of pre-existing data files */
struct CustomerLegacy {
    int accountNumber;
    float balance;
    char customerName[50];
    char password[256];
    int accountType;
    int activeStatus;
};

/*
 * migrate_customers_if_legacy:
 * Inspects customers.txt on server startup. If it detects legacy 324-byte records
 * with float balance, it safely converts each record to the 64-bit integer paise
 * format, saves a backup, and updates the file atomically.
 */
int migrate_customers_if_legacy(const char *path);

/*
 * find_customer_record:
 * Searches persistent storage for account_number.
 * Returns:
 *   0 on success (populating out_offset and out_customer)
 *   1 if account not found (EOF)
 *  -1 on I/O error
 */
int find_customer_record(int fd, int account_number, off_t *out_offset, struct Customer *out_customer);

/*
 * read_customer_record:
 * Reads customer record at exact byte offset using pread (atomic seek + read).
 */
int read_customer_record(int fd, off_t offset, struct Customer *out_customer);

/*
 * write_customer_record:
 * Writes customer record at exact byte offset using pwrite (atomic seek + write).
 */
int write_customer_record(int fd, off_t offset, const struct Customer *customer);

/*
 * lock_customer_record:
 * Acquires a POSIX record lock (F_RDLCK or F_WRLCK) on the specific customer record.
 * Handles EINTR gracefully.
 */
int lock_customer_record(int fd, off_t offset, int lock_type);

/*
 * unlock_customer_record:
 * Releases the POSIX record lock on the specific customer record.
 */
int unlock_customer_record(int fd, off_t offset);

/*
 * lock_two_customer_records:
 * Locks two records in strictly ascending offset order to guarantee deadlock-free locking.
 * If second lock fails, first lock is automatically released.
 */
int lock_two_customer_records(int fd, off_t offset1, off_t offset2, int lock_type);

/*
 * unlock_two_customer_records:
 * Releases locks on both records.
 */
int unlock_two_customer_records(int fd, off_t offset1, off_t offset2);

#endif /* ACCOUNT_STORAGE_H */
