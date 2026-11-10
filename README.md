# Fault-Tolerant Concurrent Banking Engine & Bounded Wire Protocol

[![Language: C11](https://img.shields.io/badge/Language-C11-00599C.svg)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![Platform: Linux/POSIX](https://img.shields.io/badge/Platform-Linux%20%2F%20POSIX-FCC624.svg)](https://pubs.opengroup.org/onlinepubs/9699919799/)
[![Sanitizers: ASan & UBSan Clean](https://img.shields.io/badge/Sanitizers-ASan%20%26%20UBSan%20Clean-brightgreen.svg)]()
[![Tests: 20 PASS](https://img.shields.io/badge/Tests-20%20PASS%20Groups-success.svg)]()
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A high-concurrency, crash-resilient transactional banking core and bounded binary wire protocol implemented from scratch in pure C11. Built entirely with low-level Linux/POSIX system programming primitives, the engine features an event-driven multi-process supervisor pool, fine-grained deadlock-free record locking, Write-Ahead Logging (WAL) with automated startup roll-forward recovery, and a persistent 4096-slot request deduplication store with FNV-1a checksums for end-to-end idempotency.

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Key Features & System Design](#key-features--system-design)
3. [Binary Wire Protocol Specification](#binary-wire-protocol-specification)
4. [Storage Engine & Concurrency Control](#storage-engine--concurrency-control)
5. [Write-Ahead Logging (WAL) & Crash Recovery](#write-ahead-logging-wal--crash-recovery)
6. [Persistent Request Idempotency & Deduplication](#persistent-request-idempotency--deduplication)
7. [Process Supervisor & Lifecycle Dynamics](#process-supervisor--lifecycle-dynamics)
8. [Codebase Organization](#codebase-organization)
9. [Resource Limits & Operational Bounds](#resource-limits--operational-bounds)
10. [Build, Test & Verification](#build-test--verification)
11. [Quickstart & Demo Execution](#quickstart--demo-execution)

---

## Architecture Overview

```
                      +---------------------------------------+
                      |       Client Process (CLI / SDK)      |
                      +---------------------------------------+
                                          |
                                          | [Binary Wire Protocol V2]
                                          | - 24-byte big-endian header
                                          | - Monotonic deadline (poll)
                                          | - TCP (AF_INET) or UNIX (AF_UNIX)
                                          v
                      +---------------------------------------+
                      |   Supervisor Process (server_runtime) |
                      |   - Non-blocking poll() listener      |
                      |   - Max 16 worker pool admission      |
                      |   - Asynchronous reaping (waitpid)    |
                      +---------------------------------------+
                                          | fork() worker process
                                          v
                      +---------------------------------------+
                      |  Worker Process (command_service.c)   |
                      |  - 60s lifetime interrupt alarm       |
                      |  - Zero-heap stack allocation         |
                      |  - crypt() credential verification    |
                      +---------------------------------------+
                                          |
                      +-------------------+-------------------+
                      |                                       |
                      v                                       v
+---------------------------------------+ +---------------------------------------+
|  Request Coordinator (requests.c)     | |   Write-Ahead Journal (txn_journal.c) |
|  - requests_v2.bin (4096 slots)       | |   - trans_journal.bin (WAL)           |
|  - 32-bit FNV-1a checksum validation  | |   - PREPARED -> COMMITTED transitions |
|  - Deduplication & conflict detection | |   - Startup roll-forward recovery     |
|  - Atomic slot commit via pwrite()    | |   - trans_counter.bin monotonic IDs   |
+---------------------------------------+ +---------------------------------------+
                      |                                       |
                      +-------------------+-------------------+
                                          |
                                          v
                      +---------------------------------------+
                      |    Account Storage (account_storage)  |
                      |    - customers.txt (Fixed records)    |
                      |    - Atomic pread() / pwrite() I/O    |
                      |    - Deadlock-free ascending fcntl    |
                      |    - Integer paise exact arithmetic   |
                      +---------------------------------------+
```

---

## Key Features & System Design

- **Zero Dynamic Heap Allocations on Wire Path:** Network parsing reads strictly into stack-allocated fixed buffers. Malformed payloads or oversized packets are rejected in constant time without memory fragmentation or allocation overhead.
- **Hierarchical Deadlock Elimination:** When updating multiple accounts in transfers, record locks (`fcntl F_SETLK`) are acquired in strictly ascending byte-offset order (`min(off1, off2)` then `max(off1, off2)`), mathematically eliminating circular-wait deadlocks.
- **Two-Phase Write-Ahead Logging (WAL):** Every balance mutation logs a durable `PREPARED` intent entry with file and directory `fsync` barriers before modifying balances, followed by an atomic transition to `COMMITTED`.
- **Automated Roll-Forward Crash Recovery:** On startup, the server inspects the journal and automatically recovers incomplete `PREPARED` transactions without human intervention.
- **End-to-End Request Idempotency:** The persistent request store caches transaction outcomes keyed by `(account_id, request_id)`. Network timeouts and retries replay cached results without double-debiting.
- **Fault-Injection Validated:** Resiliency verified across 6 discrete process-crash boundaries with 100% test pass under AddressSanitizer and UndefinedBehaviorSanitizer.

---

## Binary Wire Protocol Specification

The protocol uses network byte order (big-endian) for all multi-byte fields. Native C structures are never transmitted over the wire directly.

### 1. Frame Header (Fixed 24 Bytes)

| Offset (Bytes) | Size (Bytes) | Field Name | Description |
|:---:|:---:|:---|:---|
| `0..3` | 4 | `Magic` | Must equal `0x42414e4b` (`BANK` in ASCII) |
| `4..5` | 2 | `Version` | Protocol version; must equal `2` |
| `6..7` | 2 | `Command` | Command identifier (e.g., `CMD_LOGIN = 1`) |
| `8..11` | 4 | `Payload Length` | Length of attached payload; bounded to $\le 256$ bytes |
| `12..15` | 4 | `Reserved` | Reserved flags; must be `0` |
| `16..23` | 8 | `Request ID` | Monotonic, non-zero client request identifier |

### 2. Supported Commands

| Command | ID | Payload Structure |
|:---|:---:|:---|
| `CMD_LOGIN` | `1` | `account_id: u32`, `account_type: u32`, `password: 1..128 bytes` |
| `CMD_BALANCE` | `2` | *Empty* (authenticated identity inferred from session) |
| `CMD_DEPOSIT` | `3` | `amount_paise: u64` |
| `CMD_WITHDRAW` | `4` | `amount_paise: u64` |
| `CMD_TRANSFER` | `5` | `amount_paise: u64`, `destination_account: u32` |
| `CMD_LOGOUT` | `6` | *Empty* |

### 3. Server Reply Frame

Every response repeats the original client's `Request ID` and sets `command | 0x8000`. The response payload is exactly 20 bytes:

$$\text{Reply Payload} = [\text{status: u32 (4B)}] + [\text{transaction\_id: u64 (8B)}] + [\text{balance\_paise: u64 (8B)}]$$

| Status Code | Value | Description |
|:---|:---:|:---|
| `WIRE_OK` | `0` | Operation succeeded or durable outcome replayed |
| `WIRE_INVALID` | `1` | Malformed framing or invalid command arguments |
| `WIRE_NOT_FOUND` | `2` | Specified account does not exist |
| `WIRE_INACTIVE` | `3` | Account is marked inactive |
| `WIRE_INSUFFICIENT` | `4` | Insufficient funds for withdrawal or transfer |
| `WIRE_LIMIT` | `5` | Balance would exceed `MAX_BALANCE_PAISE` |
| `WIRE_IO` | `6` | Storage I/O or file descriptor error |
| `WIRE_RECOVERY` | `7` | Unresolved crash recovery in progress; retry blocked |
| `WIRE_AUTH` | `8` | Authentication required or invalid credentials |
| `WIRE_CONFLICT` | `9` | Existing Request ID submitted with mismatched payload |
| `WIRE_CAPACITY` | `10` | 4096-slot request store full; new requests refused |
| `WIRE_BUSY` | `11` | Request coordinator lock acquisition timed out |

---

## Storage Engine & Concurrency Control

### Deadlock-Free Two-Phase Record Locking
When transferring funds between account $A$ and account $B$, concurrent reverse transfers ($A \to B$ and $B \to A$) can trigger classic deadlocks under naive locking.

The engine prevents this in `lock_two_customer_records()` ([`src/account_storage.c`](src/account_storage.c)):
1. Determines byte offsets for both accounts: $\text{off}_1$ and $\text{off}_2$.
2. Acquires exclusive POSIX record write lock (`F_WRLCK`) on $\min(\text{off}_1, \text{off}_2)$.
3. Acquires exclusive write lock on $\max(\text{off}_1, \text{off}_2)$.
4. If the second lock fails, the first lock is immediately released.
5. Imposing a global acquisition order eliminates Coffman's circular-wait condition.

### Positioned Atomic I/O (`pread` / `pwrite`)
All disk reads and writes use `pread()` and `pwrite()` rather than `lseek()` + `read()`/`write()`. This guarantees atomic positioned operations without race conditions on the kernel file offset pointer across cooperating worker processes.

### Exact Integer Currency Arithmetic
All monetary balances and wire values use 64-bit integer paise (1 Rupee = 100 paise). This prevents IEEE-754 binary floating-point rounding errors. Arithmetic additions and subtractions use boundary-checked helpers (`safe_add_paise`, `safe_sub_paise`) that check against `MAX_BALANCE_PAISE` and detect overflow before applying mutations.

---

## Write-Ahead Logging (WAL) & Crash Recovery

The engine uses Write-Ahead Logging to guarantee Atomicity and Durability across power cuts or process deaths:

```
[Client Request]
       |
       v
1. Lock customer records (Ascending offset order via fcntl)
       |
       v
2. Acquire global transaction lock (journal_transaction_lock)
       |
       v
3. Append Journal Entry (state = TXN_STATE_PREPARED) ---> fsync(journal_fd)
       |
       v
4. Mutate account balance in customers.txt (pwrite) -----> fsync(customers_fd)
       |
       v
5. Update Journal Entry (state = TXN_STATE_COMMITTED) --> fsync(journal_fd)
       |
       v
6. Append audit history to trans_hist.txt
       |
       v
7. Release all locks & send wire reply
```

### Startup Recovery Engine
When [`server`](src/server.c) launches, [`journal_recover_at_startup`](src/txn_journal.c) runs **before** accepting any client connections:
- Scans `trans_journal.bin` sequentially.
- If a record is found in `TXN_STATE_PREPARED`, it indicates a crash occurred between Step 3 and Step 5.
- The recovery engine verifies the customer record on disk. If the mutation was partially applied or pending, it rolls the transaction forward to completion and marks it `COMMITTED`.
- Reconciles missing audit history records in `trans_hist.txt`.

---

## Persistent Request Idempotency & Deduplication

To prevent duplicate debiting when clients retry after dropped network connections, [`requests.c`](src/requests.c) maintains a durable file: `requests_v2.bin`.

### Slot Structure (128 Bytes Fixed)

```
+-------------------------------------------------------------------+
|               Immutable Reservation Half (Bytes 0..63)            |
| Magic (0x52513250) | Version (2) | Account ID | Command           |
| Request ID         | Reserved Txn ID | Destination | Amount       |
| [Unused zero padding]                | FNV-1a Checksum (Offset 60)|
+-------------------------------------------------------------------+
|                  Durable Result Half (Bytes 64..127)              |
| Magic (0x52513244) | Status Code | Transaction ID | Result Balance|
| [Unused zero padding]                | FNV-1a Checksum (Offset 124|
+-------------------------------------------------------------------+
```

- **Reservation First:** Before calling the financial service, an immutable reservation is written and `fsync`'d with a reserved monotonic transaction ID.
- **Result Replay:** If the same `(account_id, request_id)` arrives again with identical command arguments, the coordinator replays the saved status, transaction ID, and balance from disk without re-executing business logic.
- **Conflict Rejection:** If the same Request ID is sent with different parameters (e.g., changed amount or recipient), the server immediately rejects it with `WIRE_CONFLICT`.
- **Integrity Checksums:** Each 64-byte half is verified with 32-bit FNV-1a hashing over the first 60 bytes. Torn writes or damaged slots fail closed (`WIRE_IO`), preventing data corruption.

---

## Process Supervisor & Lifecycle Dynamics

1. **Supervisor Loop:** The parent process listens on the socket and uses `poll()` with a 50ms timeout.
2. **Admission Cap:** Maintains a strict pool of at most 16 concurrent worker processes (`WIRE_MAX_CLIENTS`). Any incoming connection beyond 16 is immediately closed to protect system resources.
3. **Worker Isolation:** Forked workers inherit the connection descriptor and close the listening socket. The worker sets an operating system interrupt alarm (`alarm(60)`) bounding maximum session lifetime.
4. **Signal-Driven Graceful Shutdown:**
   - `SIGPIPE` is ignored globally (`SIG_IGN`), preventing aborted socket writes from terminating the server.
   - On `SIGTERM` or `SIGINT`, the supervisor stops accepting connections, enters a 2-second drain window to allow active transactions to complete, terminates remaining workers with `SIGTERM`, and reaps them synchronously.
5. **Non-Blocking Reaping:** The supervisor calls `waitpid(pid, NULL, WNOHANG)` on each loop tick, preventing zombie process buildup.

---

## Codebase Organization

| File | Purpose |
|:---|:---|
| [`src/server.c`](src/server.c) | Server entry point; CLI argument parsing (`PORT` or `--unix SOCKET`), data-directory singleton lock (`.server_v2.lock`), startup recovery orchestration. |
| [`src/server_runtime.c`](src/server_runtime.c) | Process supervisor; worker forking, connection admission limit (16), non-blocking child reaping (`waitpid`), signal handling, drain and termination sequence. |
| [`src/command_service.c`](src/command_service.c) | Per-connection worker dispatcher; command loop, credential authentication via `crypt(3)`, command limits, session timeouts. |
| [`src/protocol.c`](src/protocol.c) | Binary wire protocol serializer/deserializer; big-endian integer encoding, `CLOCK_MONOTONIC` deadline-based non-blocking I/O via `poll()`. |
| [`src/requests.c`](src/requests.c) | 4096-slot request deduplication store; reservation writing, result replay, conflict validation, FNV-1a checksums, crash boundary hooks. |
| [`src/account_service.c`](src/account_service.c) | Financial business logic; deposits, withdrawals, transfers, account status validation, balance limits, crash injection points. |
| [`src/account_storage.c`](src/account_storage.c) | Low-level storage access; record locking (`fcntl`), ascending-order deadlock avoidance, atomic `pread`/`pwrite` on customer records. |
| [`src/txn_journal.c`](src/txn_journal.c) | Write-Ahead Log implementation; `PREPARED` and `COMMITTED` states, monotonic transaction counter, startup roll-forward recovery engine. |
| [`src/client.c`](src/client.c) | Production CLI client; connects over TCP or UNIX sockets, executes commands with explicit request IDs, provides retry advice. |
| [`tests/test_phase2.c`](tests/test_phase2.c) | Comprehensive integration test suite; protocol fragmentation, deadlines, duplicate requests, supervisor limits, and the 6 crash boundaries. |

---

## Resource Limits & Operational Bounds

| Resource | Enforced Limit | Systems Rationale |
|:---|---:|:---|
| Header Size | 24 bytes | Fixed-size bitwise parsing; no variable header allocations |
| Wire Payload | 256 bytes | Guaranteed stack-bound execution; no dynamic allocation |
| Simultaneous Workers | 16 processes | Bounds OS process table consumption and context-switching overhead |
| Frame Read/Write Deadline | 5 seconds each | Mitigates half-open connections and slowloris attacks |
| Total Session Lifetime | 60 seconds | Enforced via kernel `alarm()` signal |
| Commands per Session | 128 commands | Caps retained per-session state |
| Persistent Request Slots | 4,096 records | Fixed 512 KiB table prevents unbounded disk consumption |
| Currency Precision | 64-bit integer paise | Prevents floating-point rounding errors |
| Singleton File Guard | `.server_v2.lock` | Uses `flock(LOCK_EX \| LOCK_NB)` to prevent dual server instances |

---

## Build, Test & Verification

### Build Targets

```sh
# Build debug binaries and run full automated verification suite
make check

# Build optimized release binaries
make release

# Build with AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan)
make sanitize
```

*Note on sandboxed environments: If `ptrace` or leak tracing is restricted by security policies, run:*
```sh
ASAN_OPTIONS=detect_leaks=0 make sanitize
```

### Automated Test Coverage

The test suite exercises **20 distinct PASS groups**:
1. Standalone header compilation (`-Wall -Werror -fsyntax-only`).
2. Exact wire encoding, byte-by-byte fragmented delivery, and endianness.
3. Malformed magic, invalid version, flag violations, oversized length, and zero request ID rejection.
4. Monotonic read/write deadlines and EOF detection.
5. Disconnected peer writes without `SIGPIPE` termination.
6. Deadlock-free concurrent cross-account transfers.
7. Concurrent duplicate request deduplication (4 parallel workers, 1 commit).
8. **6 Process-Crash Boundaries:** Tested with synthetic failure points (`BANK_TEST_CRASH`):
   - `reserved`: Process crashes after writing reservation, before journal intent.
   - `intent`: Process crashes after writing `PREPARED` journal entry.
   - `account_written`: Process crashes after modifying source account.
   - `journal_commit`: Process crashes at the moment of journal commit.
   - `committed`: Process crashes after journal commit, before result is durable.
   - `completed`: Process crashes after result write, before sending wire response.
   *Result: Every boundary safely recovered and committed exactly once upon retry.*
9. Supervisor admission cap (16 workers max; 17th connection rejected).
10. Checksum corruption and truncated store tails fail closed with zero balance mutations.

---

## Quickstart & Demo Execution

### 1. Build and Initialize Demo Data

```sh
make
python3 tools/init_demo.py
cd runtime
```

### 2. Launch the TCP Server

```sh
../build/debug/server 8080
```
*(The server binds to `127.0.0.1` and establishes an exclusive `.server_v2.lock` file guard).*

### 3. Run Client Operations (In another terminal)

```sh
# Balance query: Account 101, Normal Account Type (1), Request ID (10001)
./build/debug/client 8080 101 1 balance 10001

# Deposit: 10.50 Rupees into Account 101 (Request ID 10002)
./build/debug/client 8080 101 1 deposit 10002 10.50

# Transfer: 5.00 Rupees from Account 101 to Account 103 (Request ID 10003)
./build/debug/client 8080 101 1 transfer 10003 5.00 103
```

### 4. Local UNIX Domain Stream Sockets

```sh
# Start server with UNIX socket
../build/debug/server --unix /tmp/bank-demo.sock

# Connect client via UNIX socket
./build/debug/client unix:/tmp/bank-demo.sock 101 1 balance 10004
```

---

## License

This project is licensed under the [MIT License](LICENSE).
