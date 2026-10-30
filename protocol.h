/*
 * protocol.h — Shared application-layer protocol definitions for the
 *               C email service (client ↔ server).
 *
 * ─── Wire Format ───────────────────────────────────────────────────
 *
 *   Request :  COMMAND|field1|field2|...\r\n
 *   Response:  RESPONSE_CODE|data...\r\n
 *
 *   • Field separator : pipe character '|'
 *   • Message terminator : "\r\n" (CRLF)
 *   • Maximum message size : BUFFER_SIZE (4096 bytes)
 *
 *   The pipe character '|' is RESERVED as the field delimiter and
 *   MUST NOT appear inside any user-supplied data (usernames,
 *   passwords, email addresses, subjects, or bodies).
 *
 * ─── Command Summary ──────────────────────────────────────────────
 *
 *   REGISTER|username|password|email_address
 *   LOGIN|username|password
 *   SEND|to_username|subject|body
 *   INBOX
 *   READ|email_id
 *   SENT
 *   LOGOUT
 *   QUIT
 *
 * ─── Response Summary ─────────────────────────────────────────────
 *
 *   OK|...              — generic success
 *   ERR|...             — generic error
 *   REGISTER_OK         — registration succeeded
 *   REGISTER_FAIL|reason— registration failed
 *   LOGIN_OK            — login succeeded
 *   LOGIN_FAIL|reason   — login failed
 *   SEND_OK             — email sent
 *   SEND_FAIL|reason    — send failed
 *   LOGOUT_OK           — logged out
 *   INVALID_CMD         — unrecognised command
 *   NOT_LOGGED_IN       — command requires authentication
 *   END                 — marks end of a multi-line listing
 *
 * ───────────────────────────────────────────────────────────────────
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ----------------------------------------------------------------
 * Size / capacity constants
 * ---------------------------------------------------------------- */
#define SERVER_PORT     8888
#define BUFFER_SIZE     4096
#define MAX_USERNAME    64
#define MAX_PASSWORD    64
#define MAX_EMAIL_ADDR  128
#define MAX_SUBJECT     256
#define MAX_BODY        2048
#define MAX_CLIENTS     64

/* ----------------------------------------------------------------
 * Framing constants
 * ---------------------------------------------------------------- */
#define FIELD_SEPARATOR  '|'
#define MSG_TERMINATOR   "\r\n"

/* ----------------------------------------------------------------
 * Filesystem paths
 * ---------------------------------------------------------------- */
#define DATA_DIR    "data"
#define USERS_FILE  "data/users.dat"

/* ----------------------------------------------------------------
 * Command strings (client → server)
 * ---------------------------------------------------------------- */
#define CMD_REGISTER  "REGISTER"
#define CMD_LOGIN     "LOGIN"
#define CMD_SEND      "SEND"
#define CMD_INBOX     "INBOX"
#define CMD_READ      "READ"
#define CMD_SENT      "SENT"
#define CMD_LOGOUT    "LOGOUT"
#define CMD_QUIT      "QUIT"

/* ----------------------------------------------------------------
 * Response strings (server → client)
 * ---------------------------------------------------------------- */
#define RESP_OK            "OK"
#define RESP_ERR           "ERR"
#define RESP_REGISTER_OK   "REGISTER_OK"
#define RESP_REGISTER_FAIL "REGISTER_FAIL"
#define RESP_LOGIN_OK      "LOGIN_OK"
#define RESP_LOGIN_FAIL    "LOGIN_FAIL"
#define RESP_SEND_OK       "SEND_OK"
#define RESP_SEND_FAIL     "SEND_FAIL"
#define RESP_LOGOUT_OK     "LOGOUT_OK"
#define RESP_INVALID_CMD   "INVALID_CMD"
#define RESP_NOT_LOGGED_IN "NOT_LOGGED_IN"
#define RESP_END           "END"

/* ----------------------------------------------------------------
 * Data structures
 * ---------------------------------------------------------------- */

/* Registered user record. */
typedef struct {
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    char email_address[MAX_EMAIL_ADDR];
} User;

/*
 * Email message record.
 *   read_status: 0 = unread, 1 = read.
 */
typedef struct {
    int  id;
    char from[MAX_USERNAME];        /* sender username   */
    char to[MAX_USERNAME];          /* recipient username */
    char subject[MAX_SUBJECT];
    char body[MAX_BODY];
    char timestamp[32];
    int  read_status;               /* 0 = unread, 1 = read */
} Email;

#endif /* PROTOCOL_H */
