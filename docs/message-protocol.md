# BOMX message protocol version 1

The protocol carries task metadata and deterministic result checksums. It never
serializes C++ objects or matrix storage. Every multi-byte integer is unsigned and
big-endian.

## Framing

Every message begins with exactly 12 bytes:

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 4 | ASCII magic `BOMX` |
| 4 | 2 | protocol version, currently `1` |
| 6 | 2 | message type |
| 8 | 4 | payload length |

Payloads larger than 1024 bytes are rejected before payload allocation. Known
message types require their exact payload length; trailing and truncated data are
invalid.

| ID | Message | Payload bytes | Payload |
|---:|---|---:|---|
| 1 | HELLO | 0 | none |
| 2 | TASK | 20 | task ID u64, dimension u32, seed u64 |
| 3 | RESULT | 42 | task ID u64, status u16, checksum u64, preparation ns u64, computation ns u64, checksum ns u64 |
| 4 | ERROR | 10 | task ID u64, error code u16 |
| 5 | SHUTDOWN | 0 | none |

RESULT status is zero. A task failure is represented by ERROR, never by a success
RESULT with a nonzero status.

## Error codes

| Code | Name | Meaning |
|---:|---|---|
| 1 | malformed_message | Invalid framing, version, length, or payload |
| 2 | handshake_required | A message other than HELLO arrived before handshake |
| 3 | invalid_task | TASK fields failed validation |
| 4 | task_failed | Workload execution raised an error |
| 5 | unexpected_message | Message is invalid in the current session state |

When framing fails before a task ID can be decoded, ERROR uses sentinel task ID
`0xffffffffffffffff`.

## Session state

1. The client opens TCP and sends HELLO.
2. The worker replies with HELLO.
3. The client sends zero or more TASK messages and receives one correlated RESULT
   or ERROR for each.
4. SHUTDOWN closes only that client session. It does not terminate the listening
   worker process.

The worker processes one task at a time and returns to `accept` after disconnect,
timeout, or session shutdown. A normal disconnect does not terminate the listener.
The worker process is terminated separately with terminal interruption or operating-
system process control.

## Transport safety

Complete sends and receives loop over partial operations and retry interrupted calls.
Connection, send, and receive operations use overall `steady_clock` deadlines.
Payload length is validated from the header before allocating the payload buffer.
Linux uses `MSG_NOSIGNAL`; macOS configures `SO_NOSIGPIPE`; Winsock behavior stays
inside the socket abstraction.

The worker binds to `127.0.0.1` by default. V1 provides no authentication or
encryption and must be used only on trusted networks. Do not expose it to the public
Internet.
