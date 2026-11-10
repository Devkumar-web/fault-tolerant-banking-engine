#ifndef ACCOUNT_SERVICE_H
#define ACCOUNT_SERVICE_H
#include <stdint.h>
typedef enum { BANK_OK, BANK_INVALID, BANK_NOT_FOUND, BANK_INACTIVE,
    BANK_INSUFFICIENT, BANK_LIMIT, BANK_IO, BANK_RECOVERY } BankStatus;
typedef enum { BANK_DEPOSIT, BANK_WITHDRAW, BANK_TRANSFER } BankOperation;
typedef struct { uint64_t transaction_id; long long balance; } BankResult;
/* Caller owns result; service owns and closes all acquired descriptors/locks.
 * Amount is positive integer paise. Source is the acting account; destination
 * is used only for transfers. Caller must authorize the acting account.
 * BANK_RECOVERY means outcome may be committed: never blindly retry.
 * Process-based API: POSIX record locks do not synchronize threads.
 */
BankStatus bank_transact(BankOperation operation, int source, int destination,
                         long long amount, BankResult *result);
#endif
