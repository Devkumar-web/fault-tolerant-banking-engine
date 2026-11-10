> Phase 1 compatibility-module reference. For the primary Phase 2 server, see
> PROTOCOL_V2.md and RETRY_DESIGN.md. The legacy socket/menu contracts below
> do not describe the new protocol.

# Interface and ownership contracts

## Financial service

`bank_transact(operation, source, destination, amount, result)` accepts integer
paise. It has no socket or terminal dependencies. The acting account is `source`,
even for deposits. Only transfers use `destination`. The caller must authenticate
and authorize the acting account first. The service validates identifiers, amount,
account status, funds, and balance limits under record locks.

The caller owns `BankResult`. It is reset on entry. Balance is meaningful on
BANK_OK. A nonzero transaction ID means an intent was persisted; it does not
alone establish a successful commit. BANK_RECOVERY indicates a blocked or
uncertain financial outcome requiring recovery, not permission to retry.
BANK_IO can include cleanup failure after a commit; inspect history before retry.
The API does not promise retry idempotency; that belongs to Phase 2.

The service owns every descriptor it opens and releases its transaction gate and
account locks on all exit paths. Transfers lock records in ascending offset order.
The transaction gate is acquired after account locks, matching the existing loan
workflow. Startup recovery must finish before clients are admitted.

## Storage and journal

Record functions borrow descriptors and never close caller-owned descriptors.
`find_customer_record`: 0 found, 1 absent, -1 I/O or invalid record.
Lock/unlock and read/write helpers: 0 success, -1 failure.
`journal_transaction_lock` returns an owned descriptor; release it with
`journal_transaction_unlock`, which also closes it. Low-level POSIX functions
retain their established errno/return conventions instead of pretending all
modules have identical failure semantics.

## Authentication and sessions

`authentication.h` documents named values for the existing login return codes.
A successful customer/employee/manager login acquires one session lock for the
process. The menu releases it on logout/disconnection; process exit also releases
kernel record locks. Admin authentication remains private to its role module.
Session APIs and financial APIs assume separate worker processes, not multiple
threads sharing a process: POSIX record locks do not provide thread exclusion.

## Protocol and UI

Socket helpers borrow descriptors. The owning server child or client closes them.
Helpers use 0 success / -1 error. `send_all` and `recv_all` return byte counts.
Current frames are fixed at 4096 bytes, and acknowledgements are lock-step.
This compatibility protocol has no receive deadline; do not claim bounded
response time or resource consumption until Phase 2.

`copy_text` copies into a caller-owned buffer and NUL-terminates when capacity
is nonzero. Input must be valid through its terminator or capacity-1 bytes.

## Portability boundaries

Current record persistence still uses native structure layout, and crypt uses
legacy hashing configuration. These are preserved compatibility choices, not
claims of architecture-independent storage or production authentication security.
