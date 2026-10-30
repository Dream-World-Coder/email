# Multi-Client Email Service — C + TCP Socket Programming

A miniature email service built with C and POSIX TCP socket programming. Supports user registration, authentication, composing/sending emails, inbox/sent viewing, and concurrent multi-client sessions via `pthreads`.

---

## Project Structure

```
.
├── protocol.h      # Shared protocol definitions (commands, responses, structs)
├── storage.h       # Storage module API (user/email persistence)
├── storage.c       # Storage implementation (file-based)
├── server.c        # Multi-threaded TCP email server
├── client.c        # Interactive TCP email client
├── Makefile        # Build automation
├── test.sh         # End-to-end test script
├── instructions.txt
├── README.md       # This file
└── data/           # Created at runtime
    ├── users.dat           # username|password|email (one per line)
    └── mail/
        └── <username>/
            ├── inbox/      # <id>.mail files (received)
            └── sent/       # <id>.mail files (copies of sent)
```

## Compilation

Requires: GCC, POSIX-compatible OS (macOS / Linux), pthreads.

```bash
# Build both server and client
make

# Or individually
make server
make client

# Manual compilation (equivalent)
gcc -Wall -Wextra -g -o server server.c storage.c -lpthread
gcc -Wall -Wextra -g -o client client.c
```

## Running

### 1. Start the Server

```bash
# Default port 8888
./server

# Custom port
./server 9000
```

The server prints a startup banner and logs all connections, logins, and mail sends to stdout.

### 2. Start a Client

Open a **separate terminal** for each client:

```bash
# Connect to server on localhost
./client 127.0.0.1

# Connect with a custom port
./client 127.0.0.1 9000

# Connect to a remote server
./client 192.168.1.100 8888
```

### 3. Client Usage

**Pre-login menu:**
```
========= EMAIL CLIENT =========
1. Register
2. Login
3. Quit
=================================
```

**Post-login menu:**
```
======== MAILBOX - alice ========
1. Compose & Send Email
2. Inbox
3. Read Email
4. Sent Mail
5. Logout
6. Quit
=======================================
```

## Port Number

Default: **8888** (defined in `protocol.h` as `SERVER_PORT`).

Override via command-line argument to both `server` and `client`.

## Configuration

- **Data directory**: `./data/` (created automatically on first server run)
- **User database**: `./data/users.dat`
- **Mail storage**: `./data/mail/<username>/inbox/` and `./data/mail/<username>/sent/`
- **Buffer size**: 4096 bytes max per message
- **Max concurrent clients**: 64

All constants are defined in `protocol.h` and can be adjusted before compilation.

## Application Protocol

```
Request format:   COMMAND|field1|field2|...\r\n
Response format:  RESPONSE_CODE|data...\r\n
Field separator:  | (pipe)
Terminator:       \r\n (CRLF)
Max message size: 4096 bytes
```

### Commands

| Command | Format | Description |
|---------|--------|-------------|
| REGISTER | `REGISTER\|username\|password\|email` | Register new user |
| LOGIN | `LOGIN\|username\|password` | Authenticate |
| SEND | `SEND\|to\|subject\|body` | Send email |
| INBOX | `INBOX` | List received emails |
| READ | `READ\|email_id` | Read specific email |
| SENT | `SENT` | List sent emails |
| LOGOUT | `LOGOUT` | End session |
| QUIT | `QUIT` | Disconnect |

### Responses

| Response | Meaning |
|----------|---------|
| `REGISTER_OK` | Registration successful |
| `REGISTER_FAIL\|reason` | Registration failed |
| `LOGIN_OK\|username` | Login successful |
| `LOGIN_FAIL\|reason` | Login failed |
| `SEND_OK\|id` | Email sent |
| `SEND_FAIL\|reason` | Send failed |
| `INBOX\|id\|from\|subject\|timestamp\|read_status` | Inbox entry |
| `READ\|id\|from\|to\|subject\|body\|timestamp` | Full email |
| `SENT\|id\|to\|subject\|timestamp` | Sent entry |
| `LOGOUT_OK` | Logged out |
| `END` | End of multi-line listing |
| `NOT_LOGGED_IN` | Auth required |
| `INVALID_CMD` | Unknown command |

## Example Session

```
Terminal 1:  ./server
Terminal 2:  ./client 127.0.0.1
Terminal 3:  ./client 127.0.0.1
Terminal 4:  ./client 127.0.0.1
```

## Testing

Run the automated test script (requires the server to be running):

```bash
# Terminal 1 — start server
./server

# Terminal 2 — run tests
bash test.sh
```

## Known Limitations

1. **No encryption**: Passwords are stored and transmitted in plaintext. This is an educational project — a real system would use TLS and hashed passwords.
2. **Pipe restriction**: The `|` character cannot appear in usernames, passwords, subjects, or message bodies (it is the field delimiter).
3. **Single-line messages**: The message body is a single line (no multi-line support in the current protocol).
4. **No email deletion**: There is no `DELETE` command; once stored, emails persist.
5. **No password change**: Users cannot update their credentials after registration.
6. **File-based storage**: Not suitable for high-throughput production use; intended for educational demonstration.
7. **recv() buffering**: The client uses a single `recv()` call per response. Very large multi-line responses may require multiple reads. For a production system, a proper line-buffered reader would be needed.

## Cleanup

```bash
# Remove binaries
make clean

# Remove stored data (users + emails)
rm -rf data/
```
