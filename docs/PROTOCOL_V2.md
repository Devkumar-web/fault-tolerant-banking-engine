# Version 2 wire protocol

All fields are unsigned big-endian integers. Headers are encoded field by field;
never send a native C structure. A TCP/UNIX stream may split or combine frames.
The same absolute monotonic deadline covers both header and payload of a frame.

| Offset | Bytes | Field |
|---|---:|---|
| 0 | 4 | Magic 0x42414e4b (`BANK`) |
| 4 | 2 | Version = 2 |
| 6 | 2 | Command |
| 8 | 4 | Payload length, at most 256 |
| 12 | 4 | Reserved flags = 0 |
| 16 | 8 | Nonzero request ID |

Invalid magic, version, flags, length, or zero ID closes the connection without
trusting remaining framing. Truncation/EOF or a deadline also closes it. A
well-framed unsupported command or invalid command payload receives INVALID.
Unauthenticated operations receive AUTH. The server does not send menus or wait
for acknowledgement packets.

| Command | Number | Request payload |
|---|---:|---|
| LOGIN | 1 | account:u32, account_type:u32, password:1..128 bytes |
| GET_BALANCE | 2 | empty |
| DEPOSIT | 3 | amount_paise:u64 |
| WITHDRAW | 4 | amount_paise:u64 |
| TRANSFER | 5 | amount_paise:u64, destination_account:u32 |
| LOGOUT | 6 | empty |

LOGIN password bytes have no trailing NUL and must not contain embedded NULs.
Normal/joint account types are 1/2. Identity is established by checking stored
credentials; financial payloads cannot choose a source account. Account IDs must
fit positive C int values. Amounts must be positive and no larger than the
inherited MAX_TRANSACTION_PAISE. Same-account transfers are invalid.

Only one successful LOGIN is allowed per connection. A failed login does not
establish identity. Multiple connections can authenticate the same account.
The frame-count and lifetime limits also bound failed-login sessions.

Every response repeats the request ID and uses `command | 0x8000`. Its payload
is exactly 20 bytes: status:u32, transaction_id:u64, balance_paise:u64.
The client checks command, request ID, and length before accepting a response.

| Status | Value | Meaning |
|---|---:|---|
| OK | 0 | Operation succeeded, or original successful result replayed |
| INVALID | 1 | Invalid command, payload or financial arguments |
| NOT_FOUND | 2 | Account absent |
| INACTIVE | 3 | Participating account inactive |
| INSUFFICIENT | 4 | Insufficient source funds |
| LIMIT | 5 | Balance limit exceeded |
| IO | 6 | Storage/cleanup failure; outcome may require inspection |
| RECOVERY | 7 | Interrupted/uncertain operation; recover before proceeding |
| AUTH | 8 | Login required or credentials invalid |
| CONFLICT | 9 | Existing financial request ID has different valid payload |
| CAPACITY | 10 | Request store full; new operation was not accepted |
| BUSY | 11 | Request-store serialization lock deadline expired |

Balance is meaningful for successful balance/financial responses. A replayed
financial balance is the balance immediately after that original transaction,
not necessarily today's balance; use GET_BALANCE for a fresh reading.
Nonzero transaction ID alone does not prove successful completion. IO, RECOVERY,
timeout, disconnect, or a client crash must never cause a retry with a new ID.
Retry the original ID/payload, or inspect and recover the data directory first.

Valid deterministic financial rejections are persisted once execution completes.
Malformed commands and invalid input rejected before reservation do not consume
an identity. GET_BALANCE, LOGIN, and LOGOUT are not persisted or deduplicated;
reusing an ID for those does not replay a financial result.

The server listens on loopback TCP or an explicitly named local UNIX socket.
Transport encryption and remote exposure are outside this protocol revision.
