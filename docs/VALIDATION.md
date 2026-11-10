# Phase 2 validation — 2026-11-10

After `make clean`, each of these completed successfully with warnings treated
as errors:

```sh
make MODE=debug check
make MODE=release check
ASAN_OPTIONS=detect_leaks=0 make MODE=sanitize check
```

Each configuration produced **20 PASS groups and 2 explicit environment skips**.
Release tests retain assertions. The sanitizer run enabled AddressSanitizer and
UndefinedBehaviorSanitizer and reported no findings in exercised paths.
Leak detection was disabled because the execution environment prevents
LeakSanitizer from operating; no leak-verification claim is made.

## Phase 2 coverage

- Exact wire encoding and byte-by-byte fragmented delivery.
- Invalid magic, version, flags, oversized length, and zero request ID rejection.
- EOF/truncated frames, partial-header deadline, blocked-send deadline, and
  disconnected-peer writes without SIGPIPE termination.
- Authentication required before transactions; wrong-password rejection and
  unsupported-command response.
- Lost response: close the connection after sending a complete financial request,
  reconnect, authenticate, retry twice, and verify only one balance change.
- Persistent result replay, changed-payload conflict, and account-scoped IDs.
- Stable cached insufficient-funds rejection after a later deposit.
- Four concurrent workers submitting one identical request; exactly one effect.
- Six process-crash boundaries: reservation, PREPARED intent, first account write,
  journal COMMITTED, service completion before result, and durable result.
  Restart recovery plus repeated retry produces one transfer at every boundary.
- Session command limit and absolute session deadline.
- Same worker-supervisor implementation with injected connected descriptors:
  16 admitted workers, rejection of an extra connection, controlled shutdown,
  worker termination, and child reaping.
- Request coordinator lock timeout.
- Truncated request-store tail and checksum corruption fail closed with no balance
  mutation.
- Full 4096-slot request store refuses new identities while replaying old results.

## Retained Phase 1 coverage

Standalone public headers, monetary parsing/overflow, deposits/withdrawals,
overdraft, transfer conservation, concurrent cross-transfers, inactive accounts,
customer login, legacy session cleanup, recovery, legacy menu socket integration,
database inspection, invalid legacy server arguments, and fixture integrity.

## Environment skips and evidence limits

1. The inherited TCP listener smoke test cannot create an IPv4 socket here.
2. The real Phase 2 listener/C-client smoke test cannot bind a local socket here.

The actual listener/client test remains in `tests/run_phase2.py` and runs on a
normal Linux environment where binding is permitted. It exercises startup,
command exchange, C-client replay, restart persistence, singleton ownership,
connection capacity, idle-frame timeout, and shutdown. Application failures are
not skipped; the skip is triggered only by the preliminary environment probe
raising PermissionError.

Protocol/session tests use real connected UNIX socket pairs. The supervisor test
injects connected descriptors into a test-only accept adapter; its admission,
forking, timeout setup, shutdown, and wait/reaping code are the production code.
This does not claim successful testing of bind/listen/accept or client connect in
this environment.

Crash hooks are compiled only with BANK_TESTING into dedicated test objects.
Production binaries do not include the BANK_TEST_CRASH environment-variable hook.
Process interruption tests do not prove behavior for physical power loss or every
storage error. The account/journal native format and legacy workflows retain their
previous limitations; complete storage hardening remains Phase 3.

## Preservation

SHA-256 checks matched all 49 recorded Phase 1 non-build files and all 28 original
root files. Tests generate mutable data in temporary directories. Existing
fixtures remain unchanged. `phase1_baseline_sha256.json` records the Phase 1
baseline; `baseline_sha256.json` retains the original submission's manifest.
