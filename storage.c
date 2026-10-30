/*
 * storage.c – Implementation of the file-based storage layer.
 *
 * Storage format
 * ==============
 *
 * users.dat   – Plain text, one user per line:
 *                 username|password|email_address\n
 *
 * *.mail      – Plain text, one field per line (7 lines total):
 *                 Line 1: id          (int)
 *                 Line 2: from        (sender username)
 *                 Line 3: to          (recipient username)
 *                 Line 4: subject
 *                 Line 5: body
 *                 Line 6: timestamp   (ISO-ish string, ≤31 chars)
 *                 Line 7: read_status (0 = unread, 1 = read)
 *
 * Directory layout
 * ================
 *
 *   data/
 *     users.dat
 *     mail/
 *       <username>/
 *         inbox/<id>.mail
 *         sent/<id>.mail
 *
 * Thread safety
 * =============
 *
 * A single static mutex (storage_mutex) serialises every public function
 * so callers never need external synchronisation.
 */

#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

/* ── Paths ─────────────────────────────────────────────────────────────── */

/* DATA_DIR and USERS_FILE are inherited from protocol.h via storage.h.
 * We only need to define the mail subdirectory and a path buffer size.  */
#define MAIL_DIR       DATA_DIR "/mail"
#define PATH_MAX_LEN   512          /* generous buffer for path construction */

/* ── Internal mutex ────────────────────────────────────────────────────── */

static pthread_mutex_t storage_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Forward declarations for internal helpers ─────────────────────────── */

static int  make_dir(const char *path);
static int  read_emails_from_dir(const char *dir_path, Email *emails,
                                 int max_count);
static int  write_email_file(const char *path, const Email *email);
static int  read_email_file(const char *path, Email *email);
static void strip_newline(char *s);

/* ===================================================================== */
/*  Public API                                                            */
/* ===================================================================== */

/*
 * storage_init – Create the base directory tree and the user database file
 *                if they do not already exist.
 */
void storage_init(void)
{
    pthread_mutex_lock(&storage_mutex);

    /* Create top-level directories (ok if they already exist). */
    make_dir(DATA_DIR);
    make_dir(MAIL_DIR);

    /* Touch users.dat so later fopen("r") calls never fail on a fresh
     * installation. */
    FILE *fp = fopen(USERS_FILE, "a");
    if (fp) {
        fclose(fp);
    }

    pthread_mutex_unlock(&storage_mutex);
}

/* --------------------------------------------------------------------- */

/*
 * register_user – Append a new user record after checking for duplicates.
 *
 * Returns  0 on success,
 *         -1 if the username or email already exists,
 *         -2 on a file I/O error.
 */
int register_user(const char *username, const char *password,
                  const char *email_addr)
{
    if (!username || !password || !email_addr) {
        return -2;
    }

    pthread_mutex_lock(&storage_mutex);

    /* --- Check for duplicates ---------------------------------------- */
    FILE *fp = fopen(USERS_FILE, "r");
    if (!fp) {
        pthread_mutex_unlock(&storage_mutex);
        return -2;
    }

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        strip_newline(line);
        if (line[0] == '\0') {
            continue;  /* skip blank lines */
        }

        /* Parse: username|password|email */
        char file_user[MAX_USERNAME];
        char file_pass[256];
        char file_email[MAX_USERNAME];

        /* Use a temporary copy so we don't destroy the line. */
        char tmp[1024];
        strncpy(tmp, line, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';

        char *tok_user  = strtok(tmp,  "|");
        char *tok_pass  = strtok(NULL, "|");
        char *tok_email = strtok(NULL, "|");

        if (!tok_user || !tok_pass || !tok_email) {
            continue;  /* malformed line – skip */
        }

        strncpy(file_user,  tok_user,  sizeof(file_user)  - 1);
        file_user[sizeof(file_user) - 1]   = '\0';
        strncpy(file_pass,  tok_pass,  sizeof(file_pass)  - 1);
        file_pass[sizeof(file_pass) - 1]   = '\0';
        strncpy(file_email, tok_email, sizeof(file_email) - 1);
        file_email[sizeof(file_email) - 1] = '\0';

        if (strcmp(file_user, username) == 0 ||
            strcmp(file_email, email_addr) == 0) {
            fclose(fp);
            pthread_mutex_unlock(&storage_mutex);
            return -1;  /* duplicate */
        }
    }
    fclose(fp);

    /* --- Append the new user ----------------------------------------- */
    fp = fopen(USERS_FILE, "a");
    if (!fp) {
        pthread_mutex_unlock(&storage_mutex);
        return -2;
    }
    fprintf(fp, "%s|%s|%s\n", username, password, email_addr);
    fclose(fp);

    /* --- Create mailbox directories ---------------------------------- */
    char path[PATH_MAX_LEN];

    snprintf(path, sizeof(path), "%s/%s", MAIL_DIR, username);
    make_dir(path);

    snprintf(path, sizeof(path), "%s/%s/inbox", MAIL_DIR, username);
    make_dir(path);

    snprintf(path, sizeof(path), "%s/%s/sent", MAIL_DIR, username);
    make_dir(path);

    pthread_mutex_unlock(&storage_mutex);
    return 0;
}

/* --------------------------------------------------------------------- */

/*
 * authenticate_user – Look up a user and verify the password.
 *
 * Returns  0 on match,
 *         -1 if the password is wrong,
 *         -2 if the user was not found.
 */
int authenticate_user(const char *username, const char *password)
{
    if (!username || !password) {
        return -2;
    }

    pthread_mutex_lock(&storage_mutex);

    FILE *fp = fopen(USERS_FILE, "r");
    if (!fp) {
        pthread_mutex_unlock(&storage_mutex);
        return -2;
    }

    char line[1024];
    int  found = 0;
    int  result = -2;  /* default: user not found */

    while (fgets(line, sizeof(line), fp)) {
        strip_newline(line);
        if (line[0] == '\0') {
            continue;
        }

        char tmp[1024];
        strncpy(tmp, line, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';

        char *tok_user = strtok(tmp,  "|");
        char *tok_pass = strtok(NULL, "|");

        if (!tok_user || !tok_pass) {
            continue;
        }

        if (strcmp(tok_user, username) == 0) {
            found = 1;
            if (strcmp(tok_pass, password) == 0) {
                result = 0;   /* authenticated */
            } else {
                result = -1;  /* wrong password */
            }
            break;
        }
    }

    (void)found;  /* suppress unused-variable warning in release */
    fclose(fp);
    pthread_mutex_unlock(&storage_mutex);
    return result;
}

/* --------------------------------------------------------------------- */

/*
 * user_exists – Return 1 if the username appears in users.dat, 0 otherwise.
 */
int user_exists(const char *username)
{
    if (!username) {
        return 0;
    }

    pthread_mutex_lock(&storage_mutex);

    FILE *fp = fopen(USERS_FILE, "r");
    if (!fp) {
        pthread_mutex_unlock(&storage_mutex);
        return 0;
    }

    char line[1024];
    int  exists = 0;

    while (fgets(line, sizeof(line), fp)) {
        strip_newline(line);
        if (line[0] == '\0') {
            continue;
        }

        char tmp[1024];
        strncpy(tmp, line, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';

        char *tok_user = strtok(tmp, "|");
        if (tok_user && strcmp(tok_user, username) == 0) {
            exists = 1;
            break;
        }
    }

    fclose(fp);
    pthread_mutex_unlock(&storage_mutex);
    return exists;
}

/* --------------------------------------------------------------------- */

/*
 * store_email – Persist an email to the recipient's inbox and the sender's
 *               sent directory.
 *
 * The email ID is auto-generated as (max existing ID in recipient inbox) + 1.
 *
 * Returns the assigned ID on success, -1 on error.
 */
int store_email(const Email *email)
{
    if (!email) {
        return -1;
    }

    pthread_mutex_lock(&storage_mutex);

    /* Build the recipient's inbox path. */
    char inbox_dir[PATH_MAX_LEN];
    snprintf(inbox_dir, sizeof(inbox_dir), "%s/%s/inbox",
             MAIL_DIR, email->to);

    /* Auto-generate the email ID from the recipient's inbox. */
    int new_id = get_next_email_id(inbox_dir);
    if (new_id < 1) {
        pthread_mutex_unlock(&storage_mutex);
        return -1;
    }

    /* Make a mutable copy so we can set the ID. */
    Email copy = *email;
    copy.id = new_id;
    copy.read_status = 0;  /* new mail is always unread */

    /* --- Write to recipient's inbox ---------------------------------- */
    char path[PATH_MAX_LEN];
    snprintf(path, sizeof(path), "%s/%d.mail", inbox_dir, new_id);

    if (write_email_file(path, &copy) != 0) {
        pthread_mutex_unlock(&storage_mutex);
        return -1;
    }

    /* --- Write a copy to sender's sent directory --------------------- */
    char sent_dir[PATH_MAX_LEN];
    snprintf(sent_dir, sizeof(sent_dir), "%s/%s/sent",
             MAIL_DIR, email->from);

    /* Use an independent ID for the sent copy so multiple sends to
     * different recipients don't overwrite each other. */
    int sent_id = get_next_email_id(sent_dir);
    snprintf(path, sizeof(path), "%s/%d.mail", sent_dir, sent_id);
    /* Best effort – we don't fail the whole operation if sent copy fails. */
    (void)write_email_file(path, &copy);

    pthread_mutex_unlock(&storage_mutex);
    return new_id;
}

/* --------------------------------------------------------------------- */

/*
 * get_inbox – Read all .mail files from a user's inbox directory.
 *
 * Returns the number of emails read (0 … max_count), or -1 on error.
 */
int get_inbox(const char *username, Email *emails, int max_count)
{
    if (!username || !emails || max_count <= 0) {
        return -1;
    }

    char dir_path[PATH_MAX_LEN];
    snprintf(dir_path, sizeof(dir_path), "%s/%s/inbox", MAIL_DIR, username);

    pthread_mutex_lock(&storage_mutex);
    int count = read_emails_from_dir(dir_path, emails, max_count);
    pthread_mutex_unlock(&storage_mutex);

    return count;
}

/* --------------------------------------------------------------------- */

/*
 * get_sent – Read all .mail files from a user's sent directory.
 *
 * Returns the number of emails read (0 … max_count), or -1 on error.
 */
int get_sent(const char *username, Email *emails, int max_count)
{
    if (!username || !emails || max_count <= 0) {
        return -1;
    }

    char dir_path[PATH_MAX_LEN];
    snprintf(dir_path, sizeof(dir_path), "%s/%s/sent", MAIL_DIR, username);

    pthread_mutex_lock(&storage_mutex);
    int count = read_emails_from_dir(dir_path, emails, max_count);
    pthread_mutex_unlock(&storage_mutex);

    return count;
}

/* --------------------------------------------------------------------- */

/*
 * read_email – Fetch a single email from a user's inbox and mark it read.
 *
 * Returns 0 on success, -1 if the email file does not exist.
 */
int read_email(const char *username, int email_id, Email *email)
{
    if (!username || !email || email_id < 1) {
        return -1;
    }

    pthread_mutex_lock(&storage_mutex);

    char path[PATH_MAX_LEN];
    snprintf(path, sizeof(path), "%s/%s/inbox/%d.mail",
             MAIL_DIR, username, email_id);

    if (read_email_file(path, email) != 0) {
        pthread_mutex_unlock(&storage_mutex);
        return -1;
    }

    /* Mark as read if it was previously unread. */
    if (email->read_status == 0) {
        email->read_status = 1;
        /* Rewrite the file with updated read status. */
        (void)write_email_file(path, email);
    }

    pthread_mutex_unlock(&storage_mutex);
    return 0;
}

/* --------------------------------------------------------------------- */

/*
 * get_next_email_id – Scan `dir_path` for <N>.mail files and return
 *                     max(N) + 1.  Returns 1 when the directory is empty.
 *
 * NOTE: This function does NOT lock the mutex – callers that are already
 *       holding the lock invoke it directly.
 */
int get_next_email_id(const char *dir_path)
{
    if (!dir_path) {
        return 1;
    }

    DIR *dir = opendir(dir_path);
    if (!dir) {
        return 1;  /* directory doesn't exist yet – start at 1 */
    }

    int max_id = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        /* Only consider regular-looking .mail files. */
        const char *name = entry->d_name;
        int id = 0;

        /* Try to parse "<number>.mail" */
        if (sscanf(name, "%d.mail", &id) == 1 && id > max_id) {
            max_id = id;
        }
    }

    closedir(dir);
    return max_id + 1;
}

/* ===================================================================== */
/*  Internal helpers                                                      */
/* ===================================================================== */

/*
 * make_dir – Create a directory with mode 0755.
 *
 * Returns 0 on success or if the directory already exists, -1 on error.
 */
static int make_dir(const char *path)
{
    if (mkdir(path, 0755) == -1) {
        if (errno == EEXIST) {
            return 0;  /* already exists – that's fine */
        }
        perror(path);
        return -1;
    }
    return 0;
}

/*
 * write_email_file – Serialise an Email struct to a .mail file.
 *
 * File format (7 lines):
 *   id
 *   from
 *   to
 *   subject
 *   body
 *   timestamp
 *   read_status
 *
 * Returns 0 on success, -1 on error.
 */
static int write_email_file(const char *path, const Email *email)
{
    FILE *fp = fopen(path, "w");
    if (!fp) {
        perror(path);
        return -1;
    }

    fprintf(fp, "%d\n",  email->id);
    fprintf(fp, "%s\n",  email->from);
    fprintf(fp, "%s\n",  email->to);
    fprintf(fp, "%s\n",  email->subject);
    fprintf(fp, "%s\n",  email->body);
    fprintf(fp, "%s\n",  email->timestamp);
    fprintf(fp, "%d\n",  email->read_status);

    fclose(fp);
    return 0;
}

/*
 * read_email_file – Deserialise a .mail file into an Email struct.
 *
 * Returns 0 on success, -1 on error.
 */
static int read_email_file(const char *path, Email *email)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }

    char buf[MAX_BODY + 64];  /* body is the largest field */

    /* Line 1: id */
    if (!fgets(buf, sizeof(buf), fp)) { fclose(fp); return -1; }
    email->id = atoi(buf);

    /* Line 2: from */
    if (!fgets(buf, sizeof(buf), fp)) { fclose(fp); return -1; }
    strip_newline(buf);
    strncpy(email->from, buf, MAX_USERNAME - 1);
    email->from[MAX_USERNAME - 1] = '\0';

    /* Line 3: to */
    if (!fgets(buf, sizeof(buf), fp)) { fclose(fp); return -1; }
    strip_newline(buf);
    strncpy(email->to, buf, MAX_USERNAME - 1);
    email->to[MAX_USERNAME - 1] = '\0';

    /* Line 4: subject */
    if (!fgets(buf, sizeof(buf), fp)) { fclose(fp); return -1; }
    strip_newline(buf);
    strncpy(email->subject, buf, MAX_SUBJECT - 1);
    email->subject[MAX_SUBJECT - 1] = '\0';

    /* Line 5: body */
    if (!fgets(buf, sizeof(buf), fp)) { fclose(fp); return -1; }
    strip_newline(buf);
    strncpy(email->body, buf, MAX_BODY - 1);
    email->body[MAX_BODY - 1] = '\0';

    /* Line 6: timestamp */
    if (!fgets(buf, sizeof(buf), fp)) { fclose(fp); return -1; }
    strip_newline(buf);
    strncpy(email->timestamp, buf, sizeof(email->timestamp) - 1);
    email->timestamp[sizeof(email->timestamp) - 1] = '\0';

    /* Line 7: read_status */
    if (!fgets(buf, sizeof(buf), fp)) { fclose(fp); return -1; }
    email->read_status = atoi(buf);

    fclose(fp);
    return 0;
}

/*
 * read_emails_from_dir – Iterate over all .mail files in a directory and
 *                        populate the `emails` array.
 *
 * Returns the number of emails read (≥ 0), or -1 on error.
 */
static int read_emails_from_dir(const char *dir_path, Email *emails,
                                int max_count)
{
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return -1;
    }

    int count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL && count < max_count) {
        /* Skip entries that aren't .mail files. */
        const char *dot = strrchr(entry->d_name, '.');
        if (!dot || strcmp(dot, ".mail") != 0) {
            continue;
        }

        char path[PATH_MAX_LEN];
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

        if (read_email_file(path, &emails[count]) == 0) {
            count++;
        }
    }

    closedir(dir);
    return count;
}

/*
 * strip_newline – Remove trailing '\n' and '\r' from a string in-place.
 */
static void strip_newline(char *s)
{
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
}
