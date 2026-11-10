#include "financial_utils.h"
#include "app_config.h"
#include "txn_journal.h"

int journal_pread_full(int fd, void *buf, size_t len, off_t off) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = pread(fd, (char *)buf + done, len - done, off + (off_t)done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        done += (size_t)n;
    }
    return 0;
}

int journal_pwrite_full(int fd, const void *buf, size_t len, off_t off) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = pwrite(fd, (const char *)buf + done, len - done, off + (off_t)done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        done += (size_t)n;
    }
    return 0;
}

int journal_lock_range(int fd, off_t start, off_t len, short type) {
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = type;
    fl.l_whence = SEEK_SET;
    fl.l_start = start;
    fl.l_len = len;
    while (fcntl(fd, F_SETLKW, &fl) == -1) {
        if (errno == EINTR) continue;
        return -1;
    }
    return 0;
}

int journal_unlock_range(int fd, off_t start, off_t len) {
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = start;
    fl.l_len = len;
    while (fcntl(fd, F_SETLK, &fl) == -1) {
        if (errno == EINTR) continue;
        return -1;
    }
    return 0;
}

int journal_transaction_lock(void) {
    int fd = open(TXN_LOCK_PATH, O_RDWR | O_CREAT, 0600);
    if (fd < 0) return -1;
    if (journal_lock_range(fd, 0, 1, F_WRLCK) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int journal_transaction_unlock(int fd) {
    if (fd < 0) return -1;
    int rc = journal_unlock_range(fd, 0, 1);
    if (close(fd) != 0) rc = -1;
    return rc;
}

int journal_mark_recovery_required(void) {
    int fd = open(TXN_RECOVERY_FLAG, O_WRONLY | O_CREAT, 0600);
    if (fd < 0) return -1;
    int rc = fsync(fd);
    if (close(fd) != 0) rc = -1;
    return rc;
}

int journal_recovery_is_required(void) {
    return access(TXN_RECOVERY_FLAG, F_OK) == 0;
}

int journal_has_prepared_transactions(void) {
    int fd = open(JOURNAL_PATH, O_RDONLY);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size % (off_t)sizeof(struct JournalEntry) != 0) {
        close(fd); return -1;
    }
    for (off_t off = 0; off < st.st_size; off += (off_t)sizeof(struct JournalEntry)) {
        struct JournalEntry e;
        if (journal_pread_full(fd, &e, sizeof(e), off) != 0 || e.magic != JOURNAL_MAGIC) {
            close(fd); return -1;
        }
        if (e.state == TXN_STATE_PREPARED) { close(fd); return 1; }
        if (e.state != TXN_STATE_COMMITTED && e.state != TXN_STATE_ABORTED) { close(fd); return -1; }
    }
    if (close(fd) != 0) return -1;
    return 0;
}

uint64_t get_next_txn_id(void) {
    int fd = open(COUNTER_TXN_PATH, O_RDWR | O_CREAT, 0600);
    if (fd < 0) return 0;
    if (journal_lock_range(fd, 0, 0, F_WRLCK) != 0) {
        close(fd);
        return 0;
    }

    uint64_t current_id = 1000;
    struct stat st;
    if (fstat(fd, &st) != 0) goto fail;
    if (st.st_size != 0) {
        if (st.st_size != (off_t)sizeof(current_id) ||
            journal_pread_full(fd, &current_id, sizeof(current_id), 0) != 0) goto fail;
    }
    if (current_id == UINT64_MAX) goto fail;
    current_id++;
    if (journal_pwrite_full(fd, &current_id, sizeof(current_id), 0) != 0 ||
        ftruncate(fd, (off_t)sizeof(current_id)) != 0 || fsync(fd) != 0) goto fail;
    if (journal_unlock_range(fd, 0, 0) != 0) {
        close(fd);
        return 0;
    }
    if (close(fd) != 0) return 0;
    return current_id;

fail:
    (void)journal_unlock_range(fd, 0, 0);
    close(fd);
    return 0;
}

off_t journal_log_prepared(const struct JournalEntry *entry) {
    if (!entry || entry->txn_id == 0 || entry->amount_paise <= 0) return -1;
    int fd = open(JOURNAL_PATH, O_RDWR | O_CREAT, 0600);
    if (fd < 0) return -1;
    if (journal_lock_range(fd, 0, 0, F_WRLCK) != 0) { close(fd); return -1; }

    off_t offset = lseek(fd, 0, SEEK_END);
    if (offset == (off_t)-1) goto fail;
    struct JournalEntry e = *entry;
    e.magic = JOURNAL_MAGIC;
    e.state = TXN_STATE_PREPARED;
    if (e.timestamp == 0) e.timestamp = time(NULL);
    if (journal_pwrite_full(fd, &e, sizeof(e), offset) != 0 || fsync(fd) != 0) goto fail;
    if (journal_unlock_range(fd, 0, 0) != 0) { close(fd); return -1; }
    if (close(fd) != 0) return -1;
    return offset;

fail:
    (void)journal_unlock_range(fd, 0, 0);
    close(fd);
    return -1;
}

int journal_mark_state(off_t offset, uint64_t txn_id, int new_state) {
    if (offset < 0 || txn_id == 0 ||
        (new_state != TXN_STATE_COMMITTED && new_state != TXN_STATE_ABORTED)) return -1;
    int fd = open(JOURNAL_PATH, O_RDWR);
    if (fd < 0) return -1;
    if (journal_lock_range(fd, offset, (off_t)sizeof(struct JournalEntry), F_WRLCK) != 0) {
        close(fd); return -1;
    }
    struct JournalEntry e;
    int rc = -1;
    if (journal_pread_full(fd, &e, sizeof(e), offset) == 0 &&
        e.magic == JOURNAL_MAGIC && e.txn_id == txn_id &&
        (e.state == TXN_STATE_PREPARED || e.state == new_state)) {
        e.state = new_state;
        if (journal_pwrite_full(fd, &e, sizeof(e), offset) == 0 && fsync(fd) == 0) rc = 0;
    }
    if (journal_unlock_range(fd, offset, (off_t)sizeof(struct JournalEntry)) != 0) rc = -1;
    if (close(fd) != 0) rc = -1;
    return rc;
}

int journal_history_record_exists(int fd, int account, uint64_t txn_id) {
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size % (off_t)sizeof(struct trans_histroy) != 0) return -1;
    char token[64];
    snprintf(token, sizeof(token), "[Txn #%llu]", (unsigned long long)txn_id);
    for (off_t off = 0; off < st.st_size; off += (off_t)sizeof(struct trans_histroy)) {
        struct trans_histroy h;
        if (journal_pread_full(fd, &h, sizeof(h), off) != 0) return -1;
        if (h.acc_no == account && strstr(h.hist, token) != NULL) return 1;
    }
    return 0;
}

int journal_ensure_history_record(const struct JournalEntry *e, int account,
                                                const char *action, int other_account) {
    int fd = open(JOURNAL_HISTORY_PATH, O_RDWR | O_CREAT, 0600);
    if (fd < 0) return -1;
    if (journal_lock_range(fd, 0, 0, F_WRLCK) != 0) { close(fd); return -1; }
    int exists = journal_history_record_exists(fd, account, e->txn_id);
    if (exists < 0) { (void)journal_unlock_range(fd, 0, 0); close(fd); return -1; }
    if (exists == 1) {
        int rc = journal_unlock_range(fd, 0, 0);
        if (close(fd) != 0) rc = -1;
        return rc;
    }
    struct trans_histroy h;
    memset(&h, 0, sizeof(h));
    h.acc_no = account;
    char amount[64];
    format_paise_plain(e->amount_paise, amount, sizeof(amount));
    struct tm tm_value;
    time_t when = e->timestamp ? e->timestamp : time(NULL);
    if (localtime_r(&when, &tm_value) == NULL) {
        (void)journal_unlock_range(fd, 0, 0); close(fd); return -1;
    }
    if (other_account > 0) {
        snprintf(h.hist, sizeof(h.hist), "[Txn #%llu] %s %s to/from acc no %d at %02d:%02d:%02d %04d-%02d-%02d\n",
                 (unsigned long long)e->txn_id, amount, action, other_account,
                 tm_value.tm_hour, tm_value.tm_min, tm_value.tm_sec,
                 tm_value.tm_year + 1900, tm_value.tm_mon + 1, tm_value.tm_mday);
    } else {
        snprintf(h.hist, sizeof(h.hist), "[Txn #%llu] %s %s at %02d:%02d:%02d %04d-%02d-%02d\n",
                 (unsigned long long)e->txn_id, amount, action,
                 tm_value.tm_hour, tm_value.tm_min, tm_value.tm_sec,
                 tm_value.tm_year + 1900, tm_value.tm_mon + 1, tm_value.tm_mday);
    }
    off_t end = lseek(fd, 0, SEEK_END);
    int rc = (end >= 0 && journal_pwrite_full(fd, &h, sizeof(h), end) == 0 && fsync(fd) == 0) ? 0 : -1;
    if (journal_unlock_range(fd, 0, 0) != 0) rc = -1;
    if (close(fd) != 0) rc = -1;
    return rc;
}

int journal_reconcile_history(void) {
    int fd = open(JOURNAL_PATH, O_RDONLY);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size % (off_t)sizeof(struct JournalEntry) != 0) { close(fd); return -1; }
    for (off_t off = 0; off < st.st_size; off += (off_t)sizeof(struct JournalEntry)) {
        struct JournalEntry e;
        if (journal_pread_full(fd, &e, sizeof(e), off) != 0 || e.magic != JOURNAL_MAGIC) { close(fd); return -1; }
        if (e.state != TXN_STATE_COMMITTED) continue;
        int rc = 0;
        if (e.txn_type == TXN_TYPE_DEPOSIT)
            rc = journal_ensure_history_record(&e, e.dst_acc, "deposited", 0);
        else if (e.txn_type == TXN_TYPE_WITHDRAW)
            rc = journal_ensure_history_record(&e, e.src_acc, "withdraw", 0);
        else if (e.txn_type == TXN_TYPE_TRANSFER) {
            rc = journal_ensure_history_record(&e, e.src_acc, "transferred into", e.dst_acc);
            if (rc == 0) rc = journal_ensure_history_record(&e, e.dst_acc, "credited from", e.src_acc);
        } else if (e.txn_type == TXN_TYPE_LOAN_DISBURSE)
            rc = journal_ensure_history_record(&e, e.dst_acc, "loan disbursement", 0);
        else rc = -1;
        if (rc != 0) { close(fd); return -1; }
    }
    return close(fd) == 0 ? 0 : -1;
}

int journal_initialize_missing_account_history(const char *cust_path) {
    int cfd = open(cust_path, O_RDONLY);
    if (cfd < 0) return -1;

    struct stat cs;
    if (fstat(cfd, &cs) != 0 ||
        cs.st_size % (off_t)sizeof(struct Customer) != 0) {
        close(cfd);
        return -1;
    }

    if (journal_lock_range(cfd, 0, 0, F_RDLCK) != 0) {
        close(cfd);
        return -1;
    }

    int hfd = open(JOURNAL_HISTORY_PATH, O_RDWR | O_CREAT, 0600);
    if (hfd < 0) {
        (void)journal_unlock_range(cfd, 0, 0);
        close(cfd);
        return -1;
    }

    if (journal_lock_range(hfd, 0, 0, F_WRLCK) != 0) {
        close(hfd);
        (void)journal_unlock_range(cfd, 0, 0);
        close(cfd);
        return -1;
    }

    struct stat hs;
    int result = -1;
    if (fstat(hfd, &hs) != 0 ||
        hs.st_size % (off_t)sizeof(struct trans_histroy) != 0) {
        goto cleanup;
    }

    for (off_t coff = 0; coff < cs.st_size; coff += (off_t)sizeof(struct Customer)) {
        struct Customer customer;
        if (journal_pread_full(cfd, &customer, sizeof(customer), coff) != 0) goto cleanup;
        if (customer.accountNumber <= 0 || customer.balance < 0) goto cleanup;

        int found = 0;
        for (off_t hoff = 0; hoff < hs.st_size; hoff += (off_t)sizeof(struct trans_histroy)) {
            struct trans_histroy h;
            if (journal_pread_full(hfd, &h, sizeof(h), hoff) != 0) goto cleanup;
            if (h.acc_no == customer.accountNumber) {
                found = 1;
                break;
            }
        }
        if (found) continue;

        struct trans_histroy baseline;
        memset(&baseline, 0, sizeof(baseline));
        baseline.acc_no = customer.accountNumber;
        char amount[64];
        format_paise_plain(customer.balance, amount, sizeof(amount));
        time_t now = time(NULL);
        struct tm tm_value;
        if (localtime_r(&now, &tm_value) == NULL) goto cleanup;
        int n = snprintf(baseline.hist, sizeof(baseline.hist),
            "[BASELINE SNAPSHOT - prior transaction history unavailable] "
            "Balance recorded: Rs. %s at %04d-%02d-%02d %02d:%02d:%02d. "
            "This is not a historical deposit; earlier activity is unavailable.\n",
            amount, tm_value.tm_year + 1900, tm_value.tm_mon + 1,
            tm_value.tm_mday, tm_value.tm_hour, tm_value.tm_min, tm_value.tm_sec);
        if (n < 0 || (size_t)n >= sizeof(baseline.hist)) goto cleanup;

        off_t end = lseek(hfd, 0, SEEK_END);
        if (end < 0 || journal_pwrite_full(hfd, &baseline, sizeof(baseline), end) != 0 ||
            fsync(hfd) != 0) goto cleanup;
        hs.st_size += (off_t)sizeof(baseline);
    }
    result = 0;

cleanup:
    if (journal_unlock_range(hfd, 0, 0) != 0) result = -1;
    if (close(hfd) != 0) result = -1;
    if (journal_unlock_range(cfd, 0, 0) != 0) result = -1;
    if (close(cfd) != 0) result = -1;
    return result;
}

int journal_find_loan_record(int fd, int loan_id, off_t *out_offset, struct LoanDetails *out_loan) {
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size % (off_t)sizeof(struct LoanDetails) != 0) return -1;
    for (off_t off = 0; off < st.st_size; off += (off_t)sizeof(struct LoanDetails)) {
        struct LoanDetails ld;
        if (journal_pread_full(fd, &ld, sizeof(ld), off) != 0) return -1;
        if (ld.loanID == loan_id) {
            if (out_offset) *out_offset = off;
            if (out_loan) *out_loan = ld;
            return 0;
        }
    }
    return 1;
}

int journal_recover_at_startup(const char *cust_path) {
    int jfd = open(JOURNAL_PATH, O_RDWR);
    if (jfd < 0) {
        if (errno == ENOENT) {
            /* A recovery marker without its journal is data loss, not a clean state. */
            if (journal_recovery_is_required()) return -1;
            return 0;
        }
        return -1;
    }
    if (journal_lock_range(jfd, 0, 0, F_WRLCK) != 0) { close(jfd); return -1; }
    int cfd = open(cust_path, O_RDWR);
    if (cfd < 0) {
        (void)journal_unlock_range(jfd, 0, 0); close(jfd); return -1;
    }
    struct stat js;
    if (fstat(jfd, &js) != 0 || js.st_size % (off_t)sizeof(struct JournalEntry) != 0) {
        (void)journal_unlock_range(jfd, 0, 0); close(cfd); close(jfd); return -1;
    }

    int recovered_count = 0;
    for (off_t offset = 0; offset < js.st_size; offset += (off_t)sizeof(struct JournalEntry)) {
        struct JournalEntry e;
        if (journal_pread_full(jfd, &e, sizeof(e), offset) != 0 || e.magic != JOURNAL_MAGIC ||
            e.txn_id == 0 || e.amount_paise <= 0 ||
            (e.state != TXN_STATE_PREPARED && e.state != TXN_STATE_COMMITTED && e.state != TXN_STATE_ABORTED)) {
            (void)journal_unlock_range(jfd, 0, 0); close(cfd); close(jfd); return -1;
        }
        if (e.state != TXN_STATE_PREPARED) continue;

        if (e.txn_type == TXN_TYPE_TRANSFER) {
            off_t so = -1, doff = -1;
            struct Customer src, dst;
            if (find_customer_record(cfd, e.src_acc, &so, &src) != 0 ||
                find_customer_record(cfd, e.dst_acc, &doff, &dst) != 0 || so == doff ||
                lock_two_customer_records(cfd, so, doff, F_WRLCK) != 0) {
                (void)journal_unlock_range(jfd, 0, 0); close(cfd); close(jfd); return -1;
            }
            if (read_customer_record(cfd, so, &src) != 0 || read_customer_record(cfd, doff, &dst) != 0) {
                unlock_two_customer_records(cfd, so, doff); (void)journal_unlock_range(jfd, 0, 0); close(cfd); close(jfd); return -1;
            }
            if ((src.balance != e.src_before_bal && src.balance != e.src_after_bal) ||
                (dst.balance != e.dst_before_bal && dst.balance != e.dst_after_bal)) {
                unlock_two_customer_records(cfd, so, doff); (void)journal_unlock_range(jfd, 0, 0); close(cfd); close(jfd); return -1;
            }
            src.balance = e.src_after_bal;
            dst.balance = e.dst_after_bal;
            int ok = write_customer_record(cfd, so, &src) == 0 &&
                     write_customer_record(cfd, doff, &dst) == 0 && fsync(cfd) == 0;
            if (ok) { e.state = TXN_STATE_COMMITTED; ok = journal_pwrite_full(jfd, &e, sizeof(e), offset) == 0 && fsync(jfd) == 0; }
            unlock_two_customer_records(cfd, so, doff);
            if (!ok) { (void)journal_unlock_range(jfd, 0, 0); close(cfd); close(jfd); return -1; }
            recovered_count++;
        } else if (e.txn_type == TXN_TYPE_DEPOSIT) {
            off_t ro = -1; struct Customer dst;
            if (find_customer_record(cfd, e.dst_acc, &ro, &dst) != 0 || lock_customer_record(cfd, ro, F_WRLCK) != 0) {
                (void)journal_unlock_range(jfd, 0, 0); close(cfd); close(jfd); return -1;
            }
            if (read_customer_record(cfd, ro, &dst) != 0 ||
                (dst.balance != e.dst_before_bal && dst.balance != e.dst_after_bal)) {
                unlock_customer_record(cfd, ro); (void)journal_unlock_range(jfd, 0, 0); close(cfd); close(jfd); return -1;
            }
            dst.balance = e.dst_after_bal;
            int ok = write_customer_record(cfd, ro, &dst) == 0 && fsync(cfd) == 0;
            if (ok) { e.state = TXN_STATE_COMMITTED; ok = journal_pwrite_full(jfd, &e, sizeof(e), offset) == 0 && fsync(jfd) == 0; }
            unlock_customer_record(cfd, ro);
            if (!ok) { (void)journal_unlock_range(jfd, 0, 0); close(cfd); close(jfd); return -1; }
            recovered_count++;
        } else if (e.txn_type == TXN_TYPE_LOAN_DISBURSE) {
            int lfd = open(JOURNAL_LOAN_PATH, O_RDWR);
            off_t ro = -1, lo = -1;
            struct Customer dst;
            struct LoanDetails ld;
            if (lfd < 0 || find_customer_record(cfd, e.dst_acc, &ro, &dst) != 0 ||
                journal_find_loan_record(lfd, e.src_acc, &lo, &ld) != 0 ||
                lock_customer_record(cfd, ro, F_WRLCK) != 0 ||
                journal_lock_range(lfd, lo, (off_t)sizeof(struct LoanDetails), F_WRLCK) != 0) {
                if (lfd >= 0) close(lfd);
                (void)journal_unlock_range(jfd, 0, 0); close(cfd); close(jfd); return -1;
            }
            if (read_customer_record(cfd, ro, &dst) != 0 ||
                (dst.balance != e.dst_before_bal && dst.balance != e.dst_after_bal) ||
                (ld.status != (int)e.src_before_bal && ld.status != 2)) {
                (void)journal_unlock_range(lfd, lo, (off_t)sizeof(struct LoanDetails));
                unlock_customer_record(cfd, ro); close(lfd);
                (void)journal_unlock_range(jfd, 0, 0); close(cfd); close(jfd); return -1;
            }
            dst.balance = e.dst_after_bal;
            ld.status = 2;
            ld.empID = (int)e.src_after_bal;
            int ok = write_customer_record(cfd, ro, &dst) == 0 && fsync(cfd) == 0 &&
                     journal_pwrite_full(lfd, &ld, sizeof(ld), lo) == 0 && fsync(lfd) == 0;
            if (ok) { e.state = TXN_STATE_COMMITTED; ok = journal_pwrite_full(jfd, &e, sizeof(e), offset) == 0 && fsync(jfd) == 0; }
            (void)journal_unlock_range(lfd, lo, (off_t)sizeof(struct LoanDetails));
            unlock_customer_record(cfd, ro);
            if (close(lfd) != 0) ok = 0;
            if (!ok) { (void)journal_unlock_range(jfd, 0, 0); close(cfd); close(jfd); return -1; }
            recovered_count++;
        } else if (e.txn_type == TXN_TYPE_WITHDRAW) {
            off_t ro = -1; struct Customer src;
            if (find_customer_record(cfd, e.src_acc, &ro, &src) != 0 || lock_customer_record(cfd, ro, F_WRLCK) != 0) {
                (void)journal_unlock_range(jfd, 0, 0); close(cfd); close(jfd); return -1;
            }
            if (read_customer_record(cfd, ro, &src) != 0 ||
                (src.balance != e.src_before_bal && src.balance != e.src_after_bal)) {
                unlock_customer_record(cfd, ro); (void)journal_unlock_range(jfd, 0, 0); close(cfd); close(jfd); return -1;
            }
            src.balance = e.src_after_bal;
            int ok = write_customer_record(cfd, ro, &src) == 0 && fsync(cfd) == 0;
            if (ok) { e.state = TXN_STATE_COMMITTED; ok = journal_pwrite_full(jfd, &e, sizeof(e), offset) == 0 && fsync(jfd) == 0; }
            unlock_customer_record(cfd, ro);
            if (!ok) { (void)journal_unlock_range(jfd, 0, 0); close(cfd); close(jfd); return -1; }
            recovered_count++;
        } else {
            (void)journal_unlock_range(jfd, 0, 0); close(cfd); close(jfd); return -1;
        }
    }

    int rc = 0;
    if (journal_reconcile_history() != 0) rc = -1;
    if (fsync(jfd) != 0) rc = -1;
    if (journal_unlock_range(jfd, 0, 0) != 0) rc = -1;
    if (close(cfd) != 0 || close(jfd) != 0) rc = -1;
    if (rc == 0 && unlink(TXN_RECOVERY_FLAG) != 0 && errno != ENOENT) rc = -1;
    if (rc == 0 && recovered_count > 0)
        printf("[ACID Recovery] Recovered %d interrupted transaction(s).\n", recovered_count);
    return rc == 0 ? recovered_count : -1;
}
