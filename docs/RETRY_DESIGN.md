# Persistent request identity and crash behavior

The deduplication key is `(authenticated_account, request_id)`. A canonical
financial payload comprises command, amount in paise, and destination (zero for
non-transfers). The full canonical payload is retained, not just a hash.

The request coordinator holds a process-shared record lock on requests_v2.bin.
It scans a bounded number of fixed slots, validates checksums, and either replays
an existing completed result, rejects a changed payload, resumes a pending
request, or reserves a new one. Lock acquisition has an absolute deadline.

## Durable ordering

1. Reserve a unique transaction ID using the existing synchronized counter.
2. Write and fsync an immutable request reservation with that transaction ID.
3. Check whether the reserved ID already exists in the financial journal.
4. If absent, invoke the financial service using that reserved ID. The service
   persists PREPARED intent before account changes, then accounts, COMMITTED,
   and transaction history as in Phase 1.
5. Persist and fsync the completed request outcome before sending the response.

Startup creates/synchronizes request, journal, counter, and history files and
fsyncs their directory before financial commands are admitted. A singleton flock
covers startup recovery and the entire server lifetime. Workers retain that open
lock descriptor, so a replacement parent cannot recover concurrently with an
orphaned worker after the original parent dies.

## Crash cases

| Last durable state | Retry behavior |
|---|---|
| No reservation | No transaction started; request can reserve an identity |
| Reservation, no journal entry | Execute with the same reserved transaction ID |
| PREPARED journal entry | Refuse execution pending startup roll-forward recovery |
| COMMITTED journal, missing result | Reconstruct original balance and ID from journal |
| Completed result | Return saved outcome without executing |

The coordinator serializes competing duplicates through execution and response
persistence. The service still uses ordered account locks and its transaction
gate. Different requests are conservatively serialized by the coordinator; this
phase favors inspectable correctness over transaction throughput.

A lost response cannot duplicate an operation: it is sent only after the result
is durable, and an interrupted result write cannot erase the immutable reservation
or its link to the authoritative journal entry. A corrupt/torn slot fails closed
instead of being silently discarded or mistaken for a new request.

If an uncommitted request had no journal intent and no durable result when its
worker died, retry may revalidate against current state. Deterministic rejections
are stable once their completed result is durable. I/O/recovery errors are not
stored as final outcomes, since the journal must determine what committed.

## Request file encoding

Each slot is 128 bytes: 64 immutable reservation bytes followed by 64 result
bytes. The result half is initially all zero. Integers are big-endian.

Reservation offsets: magic:u32 at 0 (0x52513250), version:u32 at 4 (=2), account:u32
at 8, command:u32 at 12, request_id:u64 at 16, reserved_txn_id:u64 at 24,
destination:u32 at 32, amount:u64 at 40, checksum:u32 at 60. Unused bytes are zero.

Result offsets relative to its half: magic:u32 at 0 (0x52513244), status:u32 at 4,
transaction_id:u64 at 8, balance:u64 at 16, checksum:u32 at 60. Unused bytes are zero.

Each checksum is FNV-1a-32 over the first 60 bytes of that half. It detects
accidental damage; it is not authentication against a malicious file editor.
A damaged file requires offline inspection/repair. Records are never automatically
removed. At 4096 slots (512 KiB), new requests return CAPACITY while completed
requests remain replayable. Manual deletion would invalidate the guarantee.

## Boundaries

This protocol assumes the filesystem/storage honors fsync and that cooperating
processes preserve the data files and locks. The account/journal formats remain
native Phase 1 layouts. Comprehensive torn-write, disk-error, and physical
power-loss validation belongs to Phase 3. Process-crash injection here covers six
specific boundaries, not every instruction or hardware failure.

Shutdown stops admission, permits a two-second drain, then terminates remaining
workers and reaps them. An interrupted mutation follows the same recovery/retry
rules. Ordinary session processing is also bounded by a lifetime alarm. Kernel
uninterruptible storage I/O cannot be given a hard application deadline.
