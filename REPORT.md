# Project Report: Multi-Client Email Service

**B.Tech. Information Technology — Internet Technology Laboratory**
**Individual Assignment**

---

## 1. Introduction

Email remains one of the foundational pillars of Internet communication. Understanding how email works at the protocol and network level is essential for any IT professional. This project implements a miniature, self-contained email service using the C programming language and TCP/IP socket programming. The system consists of a central server and multiple interactive clients, demonstrating core networking concepts including client-server architecture, concurrent connection handling, custom application-layer protocol design, and persistent file-based storage.

The goal is not to reproduce a commercial email system but to build an educational tool that illuminates the fundamental principles of Internet technology.

---

## 2. Problem Statement

Design and implement a multi-client email service using C and network socket programming. The system must include:

- A **central email server** that listens on a configurable TCP port, accepts connections from multiple simultaneous clients, handles user registration and authentication, and stores/retrieves emails.
- **Multiple email clients** that connect to the server, register accounts, log in, compose and send emails, view inbox and sent mail, read individual emails, and disconnect gracefully.

The solution must demonstrate practical understanding of TCP/IP communication, socket APIs, concurrency, application-layer protocol design, file-based persistence, and error handling.

---

## 3. Objectives

1. Implement a working client-server architecture using TCP sockets in C.
2. Handle multiple concurrent clients using POSIX threads (pthreads).
3. Design and document a custom application-layer protocol.
4. Implement user registration, authentication, and session management.
5. Enable email composition, sending, inbox viewing, and reading.
6. Implement persistent file-based storage for users and emails.
7. Handle all common error conditions gracefully without server crashes.
8. Use an LLM as a programming assistant while understanding all generated code.

---

## 4. System Architecture

### 4.1 Client-Server Architecture

The system follows a classical client-server model:

```
 ┌──────────┐
 │ Client 1 │──┐
 └──────────┘  │
 ┌──────────┐  │    TCP/IP      ┌────────────┐     ┌──────────────┐
 │ Client 2 │──┼───────────────→│   Server   │────→│ File Storage │
 └──────────┘  │                │ (pthreads) │     │  (data/)     │
 ┌──────────┐  │                └────────────┘     └──────────────┘
 │ Client 3 │──┘                      │
 └──────────┘              ┌──────────┴──────────┐
                           │ Thread 1 │ Thread 2 │ ...
                           └──────────┴──────────┘
```

**Server**: A long-running daemon that creates a TCP socket, binds to `INADDR_ANY` on a configurable port (default 8888), and enters an accept loop. Each accepted connection is dispatched to a new detached pthread. The server maintains a session table (up to 64 concurrent clients) protected by a mutex.

**Client**: A single-threaded, interactive, menu-driven program. It connects to the server via TCP, presents pre-login and post-login menus, sends pipe-delimited commands, and displays formatted responses.

**Why pthreads?** The pthread model was chosen because:
- It provides a natural 1-thread-per-client model where each thread has its own stack and blocking `recv()`.
- It is simpler to reason about than `select()`/`poll()` for moderate client counts.
- pthreads scale well for the expected workload (≤64 simultaneous clients).
- Threads are detached (`pthread_detach`) so their resources are automatically reclaimed on exit.

### 4.2 Network Protocol Design

A custom application-layer protocol was designed for communication between client and server.

#### Wire Format

```
Request:    COMMAND|field1|field2|...\r\n
Response:   RESPONSE_CODE|data...\r\n
```

| Property | Value |
|----------|-------|
| Field separator | `\|` (pipe character) |
| Message terminator | `\r\n` (CRLF) |
| Maximum message size | 4096 bytes |
| Reserved characters | Pipe `\|` must not appear in user data |

#### Commands (Client → Server)

| Command | Wire Format | Description |
|---------|-------------|-------------|
| REGISTER | `REGISTER\|username\|password\|email_address` | Register a new user account |
| LOGIN | `LOGIN\|username\|password` | Authenticate with existing credentials |
| SEND | `SEND\|to\|subject\|body` | Send an email to a registered user |
| INBOX | `INBOX` | List all received emails |
| READ | `READ\|email_id` | Read a specific email by ID |
| SENT | `SENT` | List all sent emails |
| LOGOUT | `LOGOUT` | End the current session |
| QUIT | `QUIT` | Disconnect from the server |

#### Responses (Server → Client)

| Response | Wire Format | Meaning |
|----------|-------------|---------|
| Registration success | `REGISTER_OK` | Account created |
| Registration failure | `REGISTER_FAIL\|reason` | Duplicate or error |
| Login success | `LOGIN_OK\|username` | Session established |
| Login failure | `LOGIN_FAIL\|reason` | Bad credentials |
| Send success | `SEND_OK\|email_id` | Email stored |
| Send failure | `SEND_FAIL\|reason` | Recipient not found |
| Inbox entry | `INBOX\|id\|from\|subject\|timestamp\|read_status` | One per email |
| Email content | `READ\|id\|from\|to\|subject\|body\|timestamp` | Full email |
| Sent entry | `SENT\|id\|to\|subject\|timestamp` | One per email |
| Logged out | `LOGOUT_OK` | Session cleared |
| End of listing | `END` | Terminates multi-line response |
| Not authenticated | `NOT_LOGGED_IN` | Command requires login |
| Unknown command | `INVALID_CMD` | Unrecognised command |
| Generic error | `ERR\|reason` | Other errors |

#### Example Interaction

```
CLIENT → SERVER:   REGISTER|rahul|mypass123|rahul@iitlab.local\r\n
SERVER → CLIENT:   REGISTER_OK\r\n

CLIENT → SERVER:   LOGIN|rahul|mypass123\r\n
SERVER → CLIENT:   LOGIN_OK|rahul\r\n

CLIENT → SERVER:   SEND|alice|Lab Assignment|Please complete the socket experiment.\r\n
SERVER → CLIENT:   SEND_OK|1\r\n

CLIENT → SERVER:   INBOX\r\n
SERVER → CLIENT:   INBOX|1|bob|Hello|2024-10-30 10:00:00|0\r\n
SERVER → CLIENT:   INBOX|2|amit|Meeting|2024-10-30 11:00:00|1\r\n
SERVER → CLIENT:   END\r\n

CLIENT → SERVER:   LOGOUT\r\n
SERVER → CLIENT:   LOGOUT_OK\r\n
```

### 4.3 Data Storage Design

A file-based storage system persists all user and email data:

```
data/
├── users.dat                    # User database (plain text)
└── mail/
    ├── alice/
    │   ├── inbox/
    │   │   ├── 1.mail           # Received email #1
    │   │   └── 2.mail           # Received email #2
    │   └── sent/
    │       ├── 1.mail           # Sent email copy #1
    │       └── 2.mail           # Sent email copy #2
    ├── bob/
    │   ├── inbox/
    │   └── sent/
    └── amit/
        ├── inbox/
        └── sent/
```

**users.dat** format (one user per line):
```
username|password|email_address
```

**.mail file** format (7 lines):
```
1                           ← Email ID (int)
alice                       ← Sender username
bob                         ← Recipient username
Internet Technology Lab     ← Subject
Please complete the work.   ← Body
2024-10-30 10:00:00         ← Timestamp
0                           ← Read status (0=unread, 1=read)
```

**Email ID assignment**: Auto-generated as `max(existing_IDs_in_recipient_inbox) + 1`.

**Read status tracking**: When an email is read via the `READ` command, the `.mail` file is rewritten with `read_status` changed from `0` to `1`.

---

## 5. Implementation

### 5.1 Source Files

| File | Lines | Description |
|------|-------|-------------|
| `protocol.h` | 132 | Shared constants, command/response strings, data structures (`User`, `Email`) |
| `storage.h` | 111 | Storage module API declarations with full documentation |
| `storage.c` | 623 | File-based user and email persistence with mutex protection |
| `server.c` | 610 | Multi-threaded TCP server with session management |
| `client.c` | 604 | Interactive menu-driven TCP client |
| `Makefile` | 37 | Build automation with `make`, `make clean`, `make run-server` |
| `test.sh` | ~150 | Automated end-to-end test script |
| `README.md` | ~150 | Compilation, usage, and configuration documentation |

**Total**: ~2,400 lines of C code across 5 source files.

### 5.2 Key Networking Functions Used

| Function | Usage |
|----------|-------|
| `socket(AF_INET, SOCK_STREAM, 0)` | Creates a TCP socket for both server and client |
| `bind(fd, addr, len)` | Binds the server socket to an IP address and port |
| `listen(fd, backlog)` | Marks the server socket as passive, sets connection queue to 10 |
| `accept(fd, addr, len)` | Accepts an incoming client connection, returns a new socket fd |
| `connect(fd, addr, len)` | Client-side: connects to the server's IP and port |
| `send(fd, buf, len, 0)` | Sends data on a connected socket |
| `recv(fd, buf, len, 0)` | Receives data from a connected socket |
| `close(fd)` | Closes a socket file descriptor |
| `setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, ...)` | Allows immediate server restart without "Address already in use" |
| `inet_pton(AF_INET, ip_str, addr)` | Converts IP address string to binary form (client side) |
| `inet_ntop(AF_INET, addr, str, len)` | Converts binary IP to human-readable string (server logging) |

### 5.3 Multi-client Handling

The server uses POSIX threads (pthreads) for concurrent client handling:

```c
// Accept loop in main()
for (;;) {
    int client_fd = accept(server_fd, ...);
    int slot = find_free_slot();         // mutex-protected
    sessions[slot].sockfd = client_fd;

    int *idx = malloc(sizeof(int));
    *idx = slot;

    pthread_t tid;
    pthread_create(&tid, NULL, handle_client, idx);
    pthread_detach(tid);                 // auto-cleanup on exit
}
```

**Session management**: A `ClientSession` array of 64 slots tracks each client's socket fd, username, and login state, protected by `pthread_mutex_t sessions_mutex`.

**Storage mutex**: A separate `pthread_mutex_t storage_mutex` serializes all file operations (user registration, email storage, directory scanning) to prevent race conditions.

**Thread lifecycle**:
1. `accept()` → allocate session slot → `pthread_create()` → `pthread_detach()`
2. Thread runs `handle_client()`: recv loop → parse → dispatch → respond
3. On disconnect: clear session slot → close socket → thread exits

### 5.4 Error Handling

The application handles the following error conditions:

| Error Condition | Where | How |
|----------------|-------|-----|
| Server unavailable | Client `connect()` | Print error, exit |
| Invalid IP address/port | Client arg parsing | Print usage, exit |
| Connection failure | Client `connect()` | Print error, exit |
| Client disconnect | Server `recv()` returns 0/-1 | Log, clear session, close fd |
| Invalid username | Server LOGIN handler | Return `LOGIN_FAIL\|User not found` |
| Wrong password | Server LOGIN handler | Return `LOGIN_FAIL\|Wrong password` |
| Duplicate registration | Server REGISTER handler | Return `REGISTER_FAIL\|Username or email already exists` |
| Send to non-existent user | Server SEND handler | Return `SEND_FAIL\|Recipient not found` |
| Empty subject/message | Client-side validation | Print error, don't send |
| Invalid command | Server default case | Return `INVALID_CMD` |
| Malformed request | Server field count check | Return `RESP_FAIL\|Missing fields` |
| File access failure | Storage module | Return error code, `perror()` |
| Server full | Accept loop | Return `ERR\|Server full`, close fd |
| Command without login | Server handler preamble | Return `NOT_LOGGED_IN` |
| Pipe in user input | Client-side validation | Print error, don't send |
| Already logged in | Server LOGIN handler | Return `LOGIN_FAIL\|User already logged in` |

The server never crashes due to bad client input. All string operations use `snprintf()` (never `sprintf()`), and all socket call return values are checked.

---

## 6. Algorithms and Flowcharts

### 6.1 Server Main Loop

```
START
  │
  ├── Parse port (default 8888)
  ├── storage_init()
  ├── socket() → bind() → listen()
  │
  ▼
  ┌─── ACCEPT LOOP ──────────────────┐
  │ client_fd = accept()              │
  │ slot = find_free_slot()           │
  │ sessions[slot].sockfd = client_fd │
  │ pthread_create(handle_client)     │
  │ pthread_detach(tid)               │
  └───────────────────────────────────┘
```

### 6.2 Client Request Handling (per thread)

```
handle_client(slot):
  │
  ├── recv(buffer)
  ├── parse_command(buffer → fields[])
  │
  ├── REGISTER? → register_user() → REGISTER_OK / REGISTER_FAIL
  ├── LOGIN?    → authenticate_user() → check duplicate session → LOGIN_OK / LOGIN_FAIL
  ├── SEND?     → check logged_in → check recipient → store_email() → SEND_OK / SEND_FAIL
  ├── INBOX?    → check logged_in → get_inbox() → send each → END
  ├── READ?     → check logged_in → read_email() → send content
  ├── SENT?     → check logged_in → get_sent() → send each → END
  ├── LOGOUT?   → clear session → LOGOUT_OK
  ├── QUIT?     → clear session → close → return
  └── else?     → INVALID_CMD
```

---

## 7. Test Cases and Results

| # | Test Case | Input | Expected Output | Status |
|---|-----------|-------|-----------------|--------|
| 1 | Register new user (alice) | REGISTER alice password123 alice@iitlab.local | REGISTER_OK | ✓ Pass |
| 2 | Register new user (bob) | REGISTER bob pass456 bob@iitlab.local | REGISTER_OK | ✓ Pass |
| 3 | Register new user (amit) | REGISTER amit amit789 amit@iitlab.local | REGISTER_OK | ✓ Pass |
| 4 | Duplicate registration | REGISTER alice again | REGISTER_FAIL | ✓ Pass |
| 5 | Login valid credentials | LOGIN alice password123 | LOGIN_OK, "Login successful" | ✓ Pass |
| 6 | Login wrong password | LOGIN alice wrongpass | LOGIN_FAIL, "Login failed" | ✓ Pass |
| 7 | Login non-existent user | LOGIN nobody pass | LOGIN_FAIL, "Login failed" | ✓ Pass |
| 8 | Send email alice→bob | SEND bob "IT Lab" "Complete the work" | SEND_OK | ✓ Pass |
| 9 | Send email alice→amit | SEND amit Meeting "Lab meeting 3 PM" | SEND_OK | ✓ Pass |
| 10 | Send to non-existent user | SEND ghost Test body | SEND_FAIL | ✓ Pass |
| 11 | View inbox (bob) | INBOX | Shows email from alice, UNREAD | ✓ Pass |
| 12 | Read email (bob, ID=1) | READ 1 | Full email with From/To/Subject/Body | ✓ Pass |
| 13 | View sent mail (alice) | SENT | Shows emails to bob and amit | ✓ Pass |
| 14 | Empty sent mail (bob) | SENT | "No sent emails" | ✓ Pass |
| 15 | Logout and re-login | LOGOUT then LOGIN | "Logged out", then "Login successful" | ✓ Pass |
| 16 | Invalid command | FOOBAR | INVALID_CMD | ✓ Pass |
| 17 | Command without login | INBOX (no login) | NOT_LOGGED_IN | ✓ Pass |
| 18 | Multiple simultaneous clients | 3 client terminals | All work independently | ✓ Pass |
| 19 | Data persistence | Check data/ after operations | users.dat and .mail files exist | ✓ Pass |
| 20 | Server survives bad client | Client sends garbage, disconnects | Server continues running | ✓ Pass |

The automated test script (`test.sh`) covers tests 1–15 and 19 programmatically.

---

## 8. AI/LLM Usage

Claude (Anthropic) was used as a programming assistant for:

1. **Code structure**: Initial scaffolding of the source file organization and module boundaries.
2. **Protocol design**: Consultation on command/response format and field separator choices.
3. **Socket programming**: Boilerplate for socket creation, binding, listening, and accept loops.
4. **Threading**: pthread creation, detachment, and mutex usage patterns.
5. **Error handling**: Comprehensive enumeration of edge cases and appropriate responses.
6. **Code review**: Identifying potential buffer overflows, race conditions, and resource leaks.

**Important**: All generated code was reviewed, understood, and adapted. Every function and design decision can be explained during the viva. The LLM served as a pair-programming assistant, not as a black-box code generator.

---

## 9. Limitations

1. **No encryption**: Passwords are stored in plaintext in `users.dat` and transmitted unencrypted over TCP. A production system would use TLS and bcrypt/scrypt password hashing.
2. **Pipe character restriction**: The pipe `|` is the field delimiter and cannot appear in usernames, passwords, email addresses, subjects, or message bodies.
3. **Single-line message body**: The message body is limited to a single line due to the line-oriented protocol.
4. **No email deletion**: There is no `DELETE` command; once stored, emails persist until manually removed.
5. **No password change**: Users cannot update credentials after registration.
6. **File-based storage**: Not suitable for high-throughput or large-scale use. A production system would use a database (e.g., SQLite, PostgreSQL).
7. **Simple recv buffering**: The client uses a single `recv()` call per response. Very large multi-line responses may not be fully received in one call.
8. **No attachments**: Binary file attachments are not supported.

---

## 10. Security Considerations

| Aspect | Current State | Production Recommendation |
|--------|---------------|---------------------------|
| Password storage | Plaintext in `users.dat` | bcrypt/scrypt hashing |
| Transport | Unencrypted TCP | TLS 1.3 (OpenSSL) |
| Input validation | Pipe character rejected | Full input sanitization |
| Buffer safety | `snprintf()` everywhere | Same — never use `sprintf()` |
| Race conditions | Mutex-protected sessions and storage | Same |
| Denial of service | Max 64 clients | Rate limiting, connection timeouts |
| Session management | Per-connection sessions | Token-based with expiry |

Key security measures implemented:
- **Buffer overflow protection**: All string operations use `snprintf()`, never `sprintf()`.
- **Mutex protection**: Both the session table and storage module are protected by separate mutexes to prevent race conditions.
- **Input validation**: The client rejects empty fields and fields containing the pipe delimiter before sending.
- **Server resilience**: The server never crashes on malformed input — it returns appropriate error responses and continues.
- **Clean disconnection**: On client disconnect, the server properly clears the session slot and closes the file descriptor.

---

## 11. Future Enhancements

1. **TLS encryption** using OpenSSL for secure communication.
2. **Password hashing** with bcrypt/scrypt instead of plaintext storage.
3. **Multi-line message body** support with a proper message framing protocol.
4. **Email deletion** and reply functionality.
5. **Email attachments** via Base64 encoding.
6. **Multiple recipients** (CC/BCC) per email.
7. **Full-text search** across emails.
8. **Web-based client** interface using HTTP/HTML.
9. **Database backend** (SQLite) replacing flat files.
10. **Email forwarding** and auto-reply rules.
11. **Connection timeouts** and rate limiting for DoS protection.

---

## 12. Conclusion

This project successfully implements a functional multi-client email service that demonstrates the core principles of Internet technology:

- **Client-server architecture** with TCP/IP socket programming
- **Concurrent client handling** using POSIX threads
- **Custom application-layer protocol** with clear request/response semantics
- **Persistent file-based storage** for users and emails
- **Comprehensive error handling** for all identified failure modes

The system supports all required operations: user registration, authentication, email composition and sending, inbox and sent-mail viewing, individual email reading with read-status tracking, and clean session management. Multiple clients (3+) can operate simultaneously without interference.

The project serves as a practical exploration of how Internet communication services work at the protocol and systems programming level.

---

## 13. References

1. W. Richard Stevens, *UNIX Network Programming, Volume 1: The Sockets Networking API*, 3rd Edition, Addison-Wesley, 2003.
2. Beej's Guide to Network Programming — https://beej.us/guide/bgnet/
3. POSIX Threads Programming — https://computing.llnl.gov/tutorials/pthreads/
4. RFC 5321 — Simple Mail Transfer Protocol (SMTP), J. Klensin, October 2008.
5. GNU C Library Manual — Socket Interface, https://www.gnu.org/software/libc/manual/
6. Linux man pages: `socket(2)`, `bind(2)`, `listen(2)`, `accept(2)`, `connect(2)`, `send(2)`, `recv(2)`, `close(2)`, `pthread_create(3)`, `pthread_mutex_lock(3)`.

