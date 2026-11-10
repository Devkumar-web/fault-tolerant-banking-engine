# Phase 2 — Bounded command protocol and persistent retries

This standalone folder builds on Phase 1. The original submission and
`phase1_embedded_foundation/` are preserved. Primary `server` and `client` now use
version 2 of the binary command protocol, not interactive menu packets.

## Build and verify

Requirements: Linux, Make, GCC-compatible C11 compiler, libcrypt development
files, and Python 3. No third-party Python packages are needed.

```sh
make check
make release
make sanitize
```

In environments where LeakSanitizer cannot initialize under tracing:

```sh
ASAN_OPTIONS=detect_leaks=0 make sanitize
```

The latter verifies AddressSanitizer and UndefinedBehaviorSanitizer behavior,
not memory leaks. Tests use disposable directories, synthetic accounts, and
process-only crash injection compiled into test objects. Production binaries do
not recognize the test crash environment variable.

`make check` includes the Phase 1 regression suite and Phase 2 tests. Listener
smoke tests explicitly skip only if the environment prohibits creating/binding
sockets. Connected socket-pair protocol and worker-supervisor tests still run.

## Run

From this folder:

```sh
make
python3 tools/init_demo.py
cd runtime
../build/debug/server 8080
```

The TCP server binds to **127.0.0.1**. In another terminal, from this folder:

```sh
./build/debug/client 8080 101 1 balance 10001
./build/debug/client 8080 101 1 deposit 10002 10.50
./build/debug/client 8080 101 1 transfer 10003 5.00 103
```

The client prompts for the account password. Amounts on the CLI use rupees;
wire amounts and displayed `balance_paise` use integer paise. Account type is
`1` for normal or `2` for joint. Fixtures retain the original project's snapshot
and credentials; tests use generated credentials and do not depend on fixtures.

If no confirmed response arrives, reconnect and repeat the **same request ID,
command, amount, and destination**. A completed request returns its original
transaction ID and resulting balance without changing balances again. Use a new
request ID for each new intended operation. IDs are scoped to authenticated
account identity and must remain unique across clients using that account.

Local UNIX stream sockets are also supported:

```sh
# In runtime:
../build/debug/server --unix /tmp/bank-demo.sock
# From project folder, in another terminal:
./build/debug/client unix:/tmp/bank-demo.sock 101 1 balance 10004
```

Use a socket path you own. Normal shutdown removes it; after an abnormal exit,
verify no server still uses it before removing a stale endpoint. The server
refuses to overwrite an existing path. Signal SIGINT/SIGTERM for controlled
shutdown. Run from a working data directory, never fixtures or a prior phase.

## Implemented limits

| Resource | Limit |
|---|---:|
| Header | 24 bytes |
| Payload | 256 bytes |
| Simultaneous workers | 16 |
| Whole-frame receive/send deadline | 5 seconds each |
| Total connection lifetime | 60 seconds |
| Commands per connection, including login | 128 |
| Persistent financial request identities | 4096 |
| Request/result file | 512 KiB |

Excess accepted connections close immediately. Existing clients still have their
own deadlines. Request-store capacity refuses new financial identities but
continues replaying existing ones. Records are never automatically evicted.
Do not delete the request store to regain capacity: doing so destroys retry
protection. Retention/compaction is deliberately left for a later migration.

Worker memory is fixed-size application storage with at most 16 processes; no
payload-driven allocation occurs. OS process overhead and inherited legacy
storage growth are not claimed to be capped by the request-store limit.
Filesystem/kernel I/O can block beyond application deadlines; this is not a
hard-real-time implementation. The worker lifetime alarm bounds ordinary
interruptible processing, and termination can leave journal work for recovery.

## Architecture and compatibility

- `protocol.c`: explicit network byte order, bounded frames, deadline-based I/O.
- `command_service.c`: connection authentication and typed command dispatch.
- `requests.c`: durable request reservation, payload matching, result replay.
- `account_service.c`: Phase 1 financial service with reserved journal IDs.
- `server_runtime.c`: limited process supervisor, child reaping and shutdown.
- `client.c`: version 2 CLI with explicit request IDs and uncertain-outcome guidance.

Version 2 exposes customer/joint-account balance, deposit, withdrawal, and transfer
commands. Administrative and loan menus remain in the legacy source modules;
they are not version 2 commands. Concurrent authenticated connections for an
account are allowed, with persistent deduplication shared across those connections.
The original one-session-per-account behavior remains in legacy menu code only.

`legacy_server` is built for regression testing, not as the primary service.
Both servers in this folder honor the same data-directory singleton lock. Older
phase executables do not know that lock: never point them at Phase 2 working data.
The default `all` target builds only the version 2 server/client and database tool.

The account/journal format and authentication hashing remain inherited. The new
wire format and request store have explicit encodings; portable account storage,
full storage fault injection, and TLS remain separate work. Keep the current
plaintext demo on loopback or a protected local UNIX socket.

See `docs/PROTOCOL_V2.md`, `docs/RETRY_DESIGN.md`, and `docs/VALIDATION.md`.
