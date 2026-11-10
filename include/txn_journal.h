#ifndef TXN_JOURNAL_H
#define TXN_JOURNAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include "bank_fun.h"
#include "account_storage.h"

#define JOURNAL_PATH "trans_journal.bin"
#define COUNTER_TXN_PATH "trans_counter.bin"
#define JOURNAL_MAGIC 0x54584E4AU /* "TXNJ" */

enum TxnType {
    TXN_TYPE_DEPOSIT = 1,
    TXN_TYPE_WITHDRAW = 2,
    TXN_TYPE_TRANSFER = 3,
    TXN_TYPE_LOAN_DISBURSE = 4
};

enum TxnState {
    TXN_STATE_PREPARED = 1, /* Intent written & fsynced; accounts pending write */
    TXN_STATE_COMMITTED = 2, /* Accounts written & fsynced; transaction durable */
    TXN_STATE_ABORTED = 3    /* Operation cancelled or rolled back */
};

struct JournalEntry {
    uint32_t magic;           /* JOURNAL_MAGIC */
    uint64_t txn_id;          /* Monotonic unique transaction ID */
    int txn_type;             /* enum TxnType */
    int src_acc;              /* Source account (0 if deposit/loan) */
    int dst_acc;              /* Destination account (0 if withdraw) */
    long long amount_paise;   /* Amount in integer paise */
    long long src_before_bal; /* Source balance before in paise */
    long long src_after_bal;  /* Source balance after in paise */
    long long dst_before_bal; /* Destination balance before in paise */
    long long dst_after_bal;  /* Destination balance after in paise */
    int state;                /* enum TxnState */
    time_t timestamp;         /* Transaction timestamp */
};

/* Full positioned I/O helpers: handle EINTR and short operations. */
int journal_pread_full(int fd, void *buf, size_t len, off_t off);

int journal_pwrite_full(int fd, const void *buf, size_t len, off_t off);

int journal_lock_range(int fd, off_t start, off_t len, short type);

int journal_unlock_range(int fd, off_t start, off_t len);

/* A process-shared transaction gate prevents another financial update from
   running after an I/O failure leaves a PREPARED transaction unresolved. */
#define TXN_LOCK_PATH "txn_global.lock"
#define TXN_RECOVERY_FLAG "txn_recovery_required.flag"
#define JOURNAL_LOAN_PATH "loanDetails.txt"
#define JOURNAL_HISTORY_PATH "trans_hist.txt"

int journal_transaction_lock(void);

int journal_transaction_unlock(int fd);

int journal_mark_recovery_required(void);

int journal_recovery_is_required(void);

/* Return 1 if a PREPARED entry exists, 0 if the journal is clean, -1 on error.
   Call while holding the transaction gate so a crashed child cannot leave an
   unresolved intent while another financial operation proceeds. */
int journal_has_prepared_transactions(void);

/* Allocate a persistent, monotonically increasing ID. Zero means failure. */
uint64_t get_next_txn_id(void);

/* Append and durably persist a PREPARED journal entry. Returns -1 on failure. */
off_t journal_log_prepared(const struct JournalEntry *entry);

/* Update only a matching record and report every I/O/locking failure. */
int journal_mark_state(off_t offset, uint64_t txn_id, int new_state);

int journal_history_record_exists(int fd, int account, uint64_t txn_id);

int journal_ensure_history_record(const struct JournalEntry *e, int account,
                                                const char *action, int other_account);

int journal_reconcile_history(void);



/*
 * Add a clearly labelled baseline snapshot for legacy accounts that have no
 * history records. This does NOT invent historical deposits/withdrawals:
 * it records the balance observed when history initialization is performed.
 * It is idempotent per account and runs after journal recovery/reconciliation.
 */
int journal_initialize_missing_account_history(const char *cust_path);

int journal_find_loan_record(int fd, int loan_id, off_t *out_offset, struct LoanDetails *out_loan);

/*
 * Startup recovery runs before the server accepts clients. PREPARED intents
 * are rolled forward in journal order. Any unexpected balance, corrupt record,
 * truncated journal, lock failure, or I/O failure aborts startup recovery rather
 * than guessing or silently accepting inconsistent financial state.
 */
int journal_recover_at_startup(const char *cust_path);

#endif /* TXN_JOURNAL_H */
