#include "account_service_internal.h"
#include "account_service.h"
#include "app_config.h"
#include "account_storage.h"
#include "financial_utils.h"
#include "txn_journal.h"

#ifdef BANK_TESTING
static void service_crash(const char *point) {
    const char *s=getenv("BANK_TEST_CRASH");
    if(s&&strcmp(s,point)==0)_exit(90);
}
#else
#define service_crash(point) ((void)0)
#endif

BankStatus bank_transact_reserved(BankOperation op, int source, int destination,
                         long long amount, uint64_t reserved_id, BankResult *result) {
    if (!result) return BANK_INVALID;
    memset(result, 0, sizeof(*result));
    if (op < BANK_DEPOSIT || op > BANK_TRANSFER || source <= 0 ||
        amount <= 0 || amount > MAX_TRANSACTION_PAISE ||
        (op == BANK_TRANSFER && (destination <= 0 || source == destination)))
        return BANK_INVALID;
    int fd = open(CUSPATH, O_RDWR);
    if (fd < 0) return BANK_IO;
    BankStatus status = BANK_IO;
    off_t aoff = -1, boff = -1;
    struct Customer a, b;
    int locked = 0, gate = -1;
    int lookup = find_customer_record(fd, source, &aoff, &a);
    if (lookup != 0) { status = lookup == 1 ? BANK_NOT_FOUND : BANK_IO; goto done; }
    if (op == BANK_TRANSFER) {
        lookup = find_customer_record(fd, destination, &boff, &b);
        if (lookup != 0) { status = lookup == 1 ? BANK_NOT_FOUND : BANK_IO; goto done; }
    }
    if ((op == BANK_TRANSFER ? lock_two_customer_records(fd, aoff, boff, F_WRLCK)
                            : lock_customer_record(fd, aoff, F_WRLCK)) != 0) goto done;
    locked = 1;
    if (read_customer_record(fd, aoff, &a) != 0 ||
        (op == BANK_TRANSFER && read_customer_record(fd, boff, &b) != 0)) goto done;
    if (a.activeStatus != 1 || (op == BANK_TRANSFER && b.activeStatus != 1)) {
        status = BANK_INACTIVE; goto done;
    }
    long long anew = 0, bnew = 0;
    if (op != BANK_DEPOSIT && a.balance < amount) { status = BANK_INSUFFICIENT; goto done; }
    if ((op == BANK_DEPOSIT ? safe_add_paise(a.balance, amount, &anew)
                           : safe_sub_paise(a.balance, amount, &anew)) != 0 ||
        (op == BANK_TRANSFER && safe_add_paise(b.balance, amount, &bnew) != 0)) {
        status = BANK_LIMIT; goto done;
    }
    gate = journal_transaction_lock();
    if (gate < 0) goto done;
    if (journal_recovery_is_required() || journal_has_prepared_transactions() != 0) {
        status = BANK_RECOVERY; goto done;
    }
    struct JournalEntry e = {0};
    e.txn_id = reserved_id ? reserved_id : get_next_txn_id();
    if (!e.txn_id) goto done;
    e.txn_type = op == BANK_DEPOSIT ? TXN_TYPE_DEPOSIT :
                 op == BANK_WITHDRAW ? TXN_TYPE_WITHDRAW : TXN_TYPE_TRANSFER;
    e.amount_paise = amount; e.timestamp = time(NULL);
    if (op == BANK_DEPOSIT) {
        e.dst_acc = source; e.dst_before_bal = a.balance; e.dst_after_bal = anew;
    } else {
        e.src_acc = source; e.src_before_bal = a.balance; e.src_after_bal = anew;
        if (op == BANK_TRANSFER) {
            e.dst_acc = destination; e.dst_before_bal = b.balance; e.dst_after_bal = bnew;
        }
    }
    off_t joff = journal_log_prepared(&e);
    if (joff < 0) goto done;
    service_crash("intent");
    result->transaction_id = e.txn_id;
    a.balance = anew;
    if (op == BANK_TRANSFER) b.balance = bnew;
    if (write_customer_record(fd, aoff, &a) != 0) goto recovery;
    service_crash("account_written");
    if ((op == BANK_TRANSFER && write_customer_record(fd, boff, &b) != 0) ||
        fsync(fd) != 0 || journal_mark_state(joff, e.txn_id, TXN_STATE_COMMITTED) != 0)
        goto recovery;
    service_crash("journal_commit");
    if (journal_ensure_history_record(&e, source,
            op == BANK_DEPOSIT ? "deposited" : op == BANK_WITHDRAW ? "withdrawn" : "transferred into",
            op == BANK_TRANSFER ? destination : 0) != 0 ||
        (op == BANK_TRANSFER && journal_ensure_history_record(&e, destination, "credited from", source) != 0))
        goto recovery;
    result->balance = anew; status = BANK_OK; goto done;
recovery:
    (void)journal_mark_recovery_required(); status = BANK_RECOVERY;
done:
    if (gate >= 0 && journal_transaction_unlock(gate) != 0 && status == BANK_OK) status = BANK_IO;
    if (locked) {
        int rc = op == BANK_TRANSFER ? unlock_two_customer_records(fd, aoff, boff) : unlock_customer_record(fd, aoff);
        if (rc != 0 && status == BANK_OK) status = BANK_IO;
    }
    if (close(fd) != 0 && status == BANK_OK) status = BANK_IO;
    return status;
}

BankStatus bank_transact(BankOperation op, int source, int destination, long long amount, BankResult *result) {
    return bank_transact_reserved(op, source, destination, amount, 0, result);
}
