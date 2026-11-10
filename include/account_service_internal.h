#ifndef ACCOUNT_SERVICE_INTERNAL_H
#define ACCOUNT_SERVICE_INTERNAL_H
#include "account_service.h"
/* Only the persistent request coordinator may supply a nonzero reserved ID.
 * Caller serializes retries and confirms this ID has no existing journal entry.
 */
BankStatus bank_transact_reserved(BankOperation op, int source, int destination,
    long long amount, uint64_t reserved_id, BankResult *result);
#endif
