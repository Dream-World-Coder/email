/*
 * storage.h – Persistent file-based storage for the email service.
 *
 * Manages a flat-file user database and per-user mailbox directories:
 *
 *   data/
 *     users.dat                 one line per user: username|password|email
 *     mail/
 *       <username>/
 *         inbox/<id>.mail       received messages
 *         sent/<id>.mail        copies of sent messages
 *
 * All public functions are thread-safe (guarded by an internal mutex).
 */

#ifndef STORAGE_H
#define STORAGE_H

#include "protocol.h"

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

/*
 * storage_init – Bootstrap the data directory tree.
 *
 * Creates data/, data/mail/, and data/users.dat if they do not already
 * exist.  Safe to call more than once.
 */
void storage_init(void);

/* ── User management ───────────────────────────────────────────────────── */

/*
 * register_user – Add a new user to users.dat.
 *
 * Also creates the user's inbox/ and sent/ directories under data/mail/.
 *
 * Returns:
 *    0  success
 *   -1  duplicate username or email address
 *   -2  file / I/O error
 */
int register_user(const char *username, const char *password,
                  const char *email_addr);

/*
 * authenticate_user – Verify a username/password pair.
 *
 * Returns:
 *    0  credentials match
 *   -1  wrong password
 *   -2  user not found
 */
int authenticate_user(const char *username, const char *password);

/*
 * user_exists – Check whether a username is registered.
 *
 * Returns 1 if found, 0 otherwise.
 */
int user_exists(const char *username);

/* ── Email storage ─────────────────────────────────────────────────────── */

/*
 * store_email – Persist an email to disk.
 *
 * The message is written to the recipient's inbox AND the sender's sent
 * directory.  An email ID is auto-assigned (max existing ID + 1 in the
 * recipient's inbox).
 *
 * Returns the assigned email ID on success, -1 on error.
 */
int store_email(const Email *email);

/*
 * get_inbox – Load all emails from a user's inbox.
 *
 * Populates `emails` with up to `max_count` messages.
 *
 * Returns the number of emails read, or -1 on error.
 */
int get_inbox(const char *username, Email *emails, int max_count);

/*
 * get_sent – Load all emails from a user's sent folder.
 *
 * Populates `emails` with up to `max_count` messages.
 *
 * Returns the number of emails read, or -1 on error.
 */
int get_sent(const char *username, Email *emails, int max_count);

/*
 * read_email – Fetch a single email from a user's inbox by ID.
 *
 * If the email is currently unread it is marked as read on disk.
 *
 * Returns 0 on success, -1 if the email was not found.
 */
int read_email(const char *username, int email_id, Email *email);

/* ── Helpers (also declared for unit-testing convenience) ───────────────── */

/*
 * get_next_email_id – Scan a directory for <N>.mail files and return
 *                     max(N) + 1.  Returns 1 when the directory is empty.
 */
int get_next_email_id(const char *dir_path);

#endif /* STORAGE_H */
