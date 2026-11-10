#ifndef AUDIT_HISTORY_H
#define AUDIT_HISTORY_H
#include <stdint.h>
int record_transaction_history(int acc_no, uint64_t txn_id, const char *action, long long amount_paise, int other_acc);
#endif
