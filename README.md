# Fault-Tolerant Concurrent Banking Engine & Wire Protocol

[![Language: C11](https://img.shields.io/badge/Language-C11-00599C.svg)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![Platform: Linux/POSIX](https://img.shields.io/badge/Platform-Linux%20%2F%20POSIX-FCC624.svg)](https://pubs.opengroup.org/onlinepubs/9699919799/)
[![Sanitizers: ASan & UBSan](https://img.shields.io/badge/Sanitizers-ASan%20%26%20UBSan%20Clean-brightgreen.svg)]()
[![Tests: 20 PASS](https://img.shields.io/badge/Tests-20%20PASS%20Groups-success.svg)]()

A crash-resilient, transactional banking core and bounded binary wire protocol implemented from scratch in C11. Built with Linux/POSIX systems programming primitives, featuring a multi-process supervisor pool, event-driven non-blocking I/O, deadlock-free hierarchical record locking, Write-Ahead Logging (WAL) with roll-forward crash recovery, and a persistent 4096-slot request deduplication coordinator with FNV-1a checksums for end-to-end idempotency.

---

## Architecture Overview

```
                      +---------------------------------------+
                      |       Client (CLI / Automated)        |
                      +---------------------------------------+
                                          |
                                          | [Binary Wire Protocol]
                                          | 24-byte big-endian framing
                                          | Monotonic deadline (poll)
                                          v
                      +---------------------------------------+
                      |   Process Supervisor (server_runtime) |
                      |   - poll() listener                   |
                      |   - Bounded 16 worker pool            |
                      |   - Asynchronous reaping (waitpid)    |
                      +---------------------------------------+
                                          | fork() worker
                                          v
                      +---------------------------------------+
                      |  Worker Process (command_service.c)   |
                      |  - 60s lifetime interrupt alarm       |
                      |  - Stack-allocated zero-heap framing  |
                      +---------------------------------------+
                                          |
                      +-------------------+-------------------+
                      |                                       |
                      v                                       v
+---------------------------------------+ +---------------------------------------+
|  Request Coordinator (requests.c)     | |   Write-Ahead Journal (txn_journal.c) |
|  - requests_v2.bin (4096 slots)       | |   - trans_journal.bin                 |
|  - 32-bit FNV-1a checksum validation  | |   - PREPARED -> COMMITTED state       |
|  - Idempotent replay & conflict check | |   - Startup roll-forward recovery     |
+---------------------------------------+ +---------------------------------------+
                      |                                       |
                      +-------------------+-------------------+
                                          |
                                          v
                      +---------------------------------------+
                      |    Account Storage (account_storage)  |
                      |    - customers.txt                    |
                      |    - Atomic positioned I/O (pread/pwrite)
                      |    - Deadlock-free fcntl lock order   |
                      +---------------------------------------+
```

---

## Core Systems Highlights

- **Bounded Binary Wire Protocol:** Uses 24-byte big-endian headers and a strictly bounded maximum 256-byte payload over TCP (`AF_INET`) and local UNIX stream sockets (`AF_UNIX`). Enforces absolute monotonic deadlines (`CLOCK_MONOTONIC`) with `poll()` on both header and payload reads, eliminating slowloris vulnerabilities with **zero dynamic heap allocation on the wire path**.
- **Multi-Process Supervisor Model:** Dedicated supervisor process accepts connections and spawns isolated worker processes for up to 16 concurrent clients. Reaps exited child processes asynchronously via `waitpid(..., WNOHANG)` and performs graceful termination on `SIGTERM`/`SIGINT` with a 2-second drain barrier.
- **Deadlock-Free Two-Phase Locking:** Transfers update two accounts simultaneously. To eliminate the Coffman circular-wait condition, record locks (`fcntl F_SETLK`) are strictly sorted by byte offset (`min(off1, off2)` followed by `max(off1, off2)`), with immediate unwind on partial failure.
- **ACID Write-Ahead Logging (WAL) & Crash Recovery:** Financial transactions append a `PREPARED` intent entry into `trans_journal.bin` with explicit file and directory `fsync` barriers before mutating balances in `customers.txt`. On startup, the recovery engine detects incomplete `PREPARED` entries and rolls them forward deterministically.
- **Persistent Request Idempotency:** Implements a durable 4096-slot binary coordinator (`requests_v2.bin`, 512 KiB) keyed by `(account_id, request_id)` protected by 32-bit FNV-1a checksums. Safely replays committed outcomes under network drops and prevents duplicate executions.
- **Crash Injection Validation:** Verified against 6 distinct process failure boundaries (`reserved`, `intent`, `account_written`, `journal_commit`, `committed`, `completed`) with clean runs under AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan).

---

## Implemented Bounds & Resource Limits

| Resource | Limit | Rationale |
|---|---:|---|
| Header Size | 24 bytes | Fixed-size bitwise wire deserialization |
| Wire Payload | 256 bytes | Guaranteed stack-bound execution; no heap allocation |
| Simultaneous Workers | 16 processes | Bounds OS process table and system memory footprint |
| Frame Read/Write Deadline | 5 seconds each | Mitigates half-open connections and slowloris attacks |
| Connection Lifetime | 60 seconds | Enforced via worker `alarm()` signal |
| Commands per Connection | 128 commands | Caps retained per-session resources |
| Persistent Request Identities | 4,096 slots | Fixed 512 KiB file prevents unbounded disk growth |
| Currency Precision | 64-bit integer paise | Prevents IEEE-754 floating-point rounding errors |

---

## Build and Verify

**Requirements:** Linux, GNU Make, GCC-compatible C11 compiler, `libcrypt` development headers, and Python 3 (standard library only).

```sh
# Run full regression and test suite in debug mode
make check

# Build optimized release binaries
make release

# Run AddressSanitizer and UndefinedBehaviorSanitizer suite
make sanitize
```

*Note: In environments where container security policies restrict `ptrace`, execute sanitizer checks with:*
```sh
ASAN_OPTIONS=detect_leaks=0 make sanitize
```

---

## Quickstart & Execution

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

### 3. Run Client Commands (In another terminal)
```sh
# Query account balance (Account 101, Normal Account Type 1, Request ID 10001)
./build/debug/client 8080 101 1 balance 10001

# Deposit 10.50 rupees (Request ID 10002)
./build/debug/client 8080 101 1 deposit 10002 10.50

# Transfer 5.00 rupees from 101 to Account 103 (Request ID 10003)
./build/debug/client 8080 101 1 transfer 10003 5.00 103
```

### 4. Local UNIX Domain Stream Sockets
```sh
# In runtime:
../build/debug/server --unix /tmp/bank-demo.sock

# From project folder:
./build/debug/client unix:/tmp/bank-demo.sock 101 1 balance 10004
```

---

## Verification & Test Matrix

The test suite exercises:
- Byte-by-byte fragmented stream delivery and wire-framing validation.
- Malformed magic, unknown protocol versions, reserved flag violations, and oversized payloads.
- EOF/truncation detection, partial-frame deadlines, and write deadline expiration.
- Signal safety: writing to disconnected sockets without `SIGPIPE` termination.
- Deadlock-free concurrent cross-transfers and concurrent duplicate request filtering.
- Fault injection across 6 transaction crash points with startup journal roll-forward recovery.
- Full 4096-slot store capacity enforcement and checksum corruption failing closed.

---

## Documentation Index

- [`docs/PROTOCOL_V2.md`](file:///home/eager/sys_software_lab/BANKING%20PROJECT/Bank_Management_System_Terminal_Output_Audit_Fix/Bank_Management_System_Final_Submission/phase2_bounded_protocol/docs/PROTOCOL_V2.md) — Detailed wire protocol layout, bit encodings, command IDs, and status codes.
- [`docs/RETRY_DESIGN.md`](file:///home/eager/sys_software_lab/BANKING%20PROJECT/Bank_Management_System_Terminal_Output_Audit_Fix/Bank_Management_System_Final_Submission/phase2_bounded_protocol/docs/RETRY_DESIGN.md) — Persistent deduplication store design, lock acquisition ordering, and crash states.
- [`docs/INTERFACES.md`](file:///home/eager/sys_software_lab/BANKING%20PROJECT/Bank_Management_System_Terminal_Output_Audit_Fix/Bank_Management_System_Final_Submission/phase2_bounded_protocol/docs/INTERFACES.md) — Core financial service ownership and API contracts.
- [`docs/VALIDATION.md`](file:///home/eager/sys_software_lab/BANKING%20PROJECT/Bank_Management_System_Terminal_Output_Audit_Fix/Bank_Management_System_Final_Submission/phase2_bounded_protocol/docs/VALIDATION.md) — Test validation logs and environment verification criteria.
