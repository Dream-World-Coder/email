/*
 * client.c — Interactive TCP email client.
 *
 * Usage:  ./client <server_ip> [port]
 *
 * Connects to the email server, presents a menu-driven interface for
 * registering, logging in, composing/reading/listing emails, and
 * logging out.  All commands use the pipe-delimited wire protocol
 * defined in protocol.h.
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "protocol.h"

/* ----------------------------------------------------------------
 * Helper function prototypes
 * ---------------------------------------------------------------- */
static void send_command(int sockfd, const char *cmd);
static int receive_response(int sockfd, char *buffer, int size);
static void clear_input_buffer(void);
static void read_line(const char *prompt, char *buffer, int size);

/* Menu handlers */
static void handle_register(int sockfd);
static void handle_login(int sockfd, char *username, int *logged_in);
static void handle_compose(int sockfd);
static void handle_inbox(int sockfd);
static void handle_read_email(int sockfd);
static void handle_sent_mail(int sockfd);
static void handle_logout(int sockfd, int *logged_in);
static void handle_quit(int sockfd);

/* Input validation */
static int contains_pipe(const char *s);
static int is_empty(const char *s);

/* ----------------------------------------------------------------
 * send_command — send a command string with MSG_TERMINATOR appended.
 * ---------------------------------------------------------------- */
static void send_command(int sockfd, const char *cmd) {
  char buf[BUFFER_SIZE];
  int len;

  len = snprintf(buf, sizeof(buf), "%s%s", cmd, MSG_TERMINATOR);
  if (len < 0 || (size_t)len >= sizeof(buf)) {
    fprintf(stderr, "Error: command too long.\n");
    return;
  }

  if (send(sockfd, buf, (size_t)len, 0) < 0) {
    perror("send");
  }
}

/* ----------------------------------------------------------------
 * receive_response — Line-buffered receiver.
 *
 * TCP is a byte stream: a single recv() may contain multiple
 * protocol messages (each terminated by \r\n), or a partial one.
 * This function maintains a static internal buffer and returns
 * exactly one \r\n-delimited line per call, stripping the
 * terminator.
 *
 * Returns the line length (≥ 0) on success, -1 on disconnect.
 * ---------------------------------------------------------------- */
static int receive_response(int sockfd, char *buffer, int size) {
  /* Persistent read-ahead buffer across calls.  Fine for a
   * single-threaded, single-connection client. */
  static char rbuf[BUFFER_SIZE * 4];
  static int rlen = 0;

  for (;;) {
    /* Scan for the first \n in the buffered data. */
    for (int i = 0; i < rlen; i++) {
      if (rbuf[i] == '\n') {
        /* Found end of line.  Copy everything before it
         * (minus a possible trailing \r) into the caller's
         * buffer. */
        int line_len = i;
        if (line_len > 0 && rbuf[line_len - 1] == '\r') {
          line_len--; /* strip \r */
        }
        if (line_len >= size) {
          line_len = size - 1;
        }
        memcpy(buffer, rbuf, (size_t)line_len);
        buffer[line_len] = '\0';

        /* Consume the line + \n from the internal buffer. */
        int consumed = i + 1;
        rlen -= consumed;
        if (rlen > 0) {
          memmove(rbuf, rbuf + consumed, (size_t)rlen);
        }
        rbuf[rlen] = '\0';

        return line_len;
      }
    }

    /* No complete line yet — read more data from the socket. */
    if (rlen >= (int)sizeof(rbuf) - 1) {
      /* Safety: buffer full with no newline.  Return what we have. */
      int out = (rlen >= size) ? size - 1 : rlen;
      memcpy(buffer, rbuf, (size_t)out);
      buffer[out] = '\0';
      rlen = 0;
      return out;
    }

    ssize_t n = recv(sockfd, rbuf + rlen, (size_t)(sizeof(rbuf) - 1 - rlen), 0);
    if (n <= 0) {
      /* Disconnected.  If we have leftover data, return it. */
      if (rlen > 0) {
        int out = (rlen >= size) ? size - 1 : rlen;
        memcpy(buffer, rbuf, (size_t)out);
        buffer[out] = '\0';
        rlen = 0;
        return out;
      }
      return -1;
    }

    rlen += (int)n;
    rbuf[rlen] = '\0';
  }
}

/* ----------------------------------------------------------------
 * clear_input_buffer — drain any remaining characters on stdin.
 * ---------------------------------------------------------------- */
static void clear_input_buffer(void) __attribute__((unused));
static void clear_input_buffer(void) {
  int c;
  while ((c = getchar()) != '\n' && c != EOF)
    ; /* discard */
}

/* ----------------------------------------------------------------
 * read_line — prompt the user, read a line with fgets, strip the
 *             trailing newline.
 * ---------------------------------------------------------------- */
static void read_line(const char *prompt, char *buffer, int size) {
  printf("%s", prompt);
  fflush(stdout);

  if (fgets(buffer, size, stdin) == NULL) {
    buffer[0] = '\0';
    return;
  }

  /* Strip trailing newline / carriage return */
  size_t len = strlen(buffer);
  while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
    buffer[--len] = '\0';
  }
}

/* ----------------------------------------------------------------
 * Validation helpers
 * ---------------------------------------------------------------- */
static int contains_pipe(const char *s) {
  return strchr(s, FIELD_SEPARATOR) != NULL;
}

static int is_empty(const char *s) { return s[0] == '\0'; }

/* ----------------------------------------------------------------
 * handle_register — Register a new user account.
 * ---------------------------------------------------------------- */
static void handle_register(int sockfd) {
  char username[MAX_USERNAME];
  char password[MAX_PASSWORD];
  char email[MAX_EMAIL_ADDR];

  read_line("Username: ", username, sizeof(username));
  read_line("Password: ", password, sizeof(password));
  read_line("Email Address: ", email, sizeof(email));

  if (is_empty(username) || is_empty(password) || is_empty(email)) {
    printf("Error: All fields are required.\n");
    return;
  }
  if (contains_pipe(username) || contains_pipe(password) ||
      contains_pipe(email)) {
    printf("Error: Fields must not contain the '|' character.\n");
    return;
  }

  char cmd[BUFFER_SIZE];
  snprintf(cmd, sizeof(cmd), "%s%c%s%c%s%c%s", CMD_REGISTER, FIELD_SEPARATOR,
           username, FIELD_SEPARATOR, password, FIELD_SEPARATOR, email);

  send_command(sockfd, cmd);

  char resp[BUFFER_SIZE];
  if (receive_response(sockfd, resp, sizeof(resp)) < 0) {
    printf("Server disconnected.\n");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  printf("Server: %s\n\n", resp);
}

/* ----------------------------------------------------------------
 * handle_login — Authenticate with the server.
 * ---------------------------------------------------------------- */
static void handle_login(int sockfd, char *username, int *logged_in) {
  char user[MAX_USERNAME];
  char pass[MAX_PASSWORD];

  read_line("Username: ", user, sizeof(user));
  read_line("Password: ", pass, sizeof(pass));

  if (is_empty(user) || is_empty(pass)) {
    printf("Error: Username and password are required.\n");
    return;
  }

  char cmd[BUFFER_SIZE];
  snprintf(cmd, sizeof(cmd), "%s%c%s%c%s", CMD_LOGIN, FIELD_SEPARATOR, user,
           FIELD_SEPARATOR, pass);

  send_command(sockfd, cmd);

  char resp[BUFFER_SIZE];
  if (receive_response(sockfd, resp, sizeof(resp)) < 0) {
    printf("Server disconnected.\n");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  if (strncmp(resp, RESP_LOGIN_OK, strlen(RESP_LOGIN_OK)) == 0) {
    *logged_in = 1;
    strncpy(username, user, MAX_USERNAME - 1);
    username[MAX_USERNAME - 1] = '\0';
    printf("Login successful! Welcome, %s.\n\n", username);
  } else {
    /* LOGIN_FAIL|reason  — show reason if present */
    char *reason = strchr(resp, FIELD_SEPARATOR);
    if (reason) {
      printf("Login failed: %s\n\n", reason + 1);
    } else {
      printf("Login failed: %s\n\n", resp);
    }
  }
}

/* ----------------------------------------------------------------
 * handle_compose — Compose and send an email.
 * ---------------------------------------------------------------- */
static void handle_compose(int sockfd) {
  char to[MAX_USERNAME];
  char subject[MAX_SUBJECT];
  char body[MAX_BODY];

  read_line("To: ", to, sizeof(to));
  read_line("Subject: ", subject, sizeof(subject));
  read_line("Message: ", body, sizeof(body));

  if (is_empty(to) || is_empty(subject) || is_empty(body)) {
    printf("Error: All fields are required.\n");
    return;
  }
  if (contains_pipe(to) || contains_pipe(subject) || contains_pipe(body)) {
    printf("Error: Fields must not contain the '|' character.\n");
    return;
  }

  char cmd[BUFFER_SIZE];
  snprintf(cmd, sizeof(cmd), "%s%c%s%c%s%c%s", CMD_SEND, FIELD_SEPARATOR, to,
           FIELD_SEPARATOR, subject, FIELD_SEPARATOR, body);

  send_command(sockfd, cmd);

  char resp[BUFFER_SIZE];
  if (receive_response(sockfd, resp, sizeof(resp)) < 0) {
    printf("Server disconnected.\n");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  printf("Server: %s\n\n", resp);
}

/* ----------------------------------------------------------------
 * handle_inbox — List inbox emails in a formatted table.
 *
 * Each line from the server:  INBOX|id|from|subject|timestamp|read_status
 * Final line:                 END
 * ---------------------------------------------------------------- */
static void handle_inbox(int sockfd) {
  send_command(sockfd, CMD_INBOX);

  printf("\n--------- INBOX ---------\n");
  printf("%-4s %-12s %-20s %-20s %s\n", "ID", "FROM", "SUBJECT", "DATE",
         "STATUS");

  int count = 0;
  char resp[BUFFER_SIZE];

  while (1) {
    if (receive_response(sockfd, resp, sizeof(resp)) < 0) {
      printf("Server disconnected.\n");
      close(sockfd);
      exit(EXIT_FAILURE);
    }

    /* End of listing marker */
    if (strcmp(resp, RESP_END) == 0) {
      break;
    }

    /* Parse: INBOX|id|from|subject|timestamp|read_status */
    char *token;
    char *saveptr;

    /* Skip the "INBOX" prefix */
    token = strtok_r(resp, "|", &saveptr);
    if (!token)
      continue;

    char *id_str = strtok_r(NULL, "|", &saveptr);
    char *from = strtok_r(NULL, "|", &saveptr);
    char *subject = strtok_r(NULL, "|", &saveptr);
    char *date = strtok_r(NULL, "|", &saveptr);
    char *status_s = strtok_r(NULL, "|", &saveptr);

    if (!id_str || !from || !subject || !date || !status_s)
      continue;

    const char *status_label = (atoi(status_s) == 0) ? "[UNREAD]" : "[READ]";
    printf("%-4s %-12s %-20s %-20s %s\n", id_str, from, subject, date,
           status_label);
    count++;
  }

  if (count == 0) {
    printf("Inbox is empty.\n");
  }
  printf("---------------------------\n\n");
}

/* ----------------------------------------------------------------
 * handle_read_email — Read a single email by ID.
 *
 * Response: READ|id|from|to|subject|body|timestamp
 * ---------------------------------------------------------------- */
static void handle_read_email(int sockfd) {
  char id_str[16];
  read_line("Email ID: ", id_str, sizeof(id_str));

  if (is_empty(id_str)) {
    printf("Error: Email ID is required.\n");
    return;
  }

  int email_id = atoi(id_str);
  if (email_id <= 0) {
    printf("Error: Invalid email ID.\n");
    return;
  }

  char cmd[BUFFER_SIZE];
  snprintf(cmd, sizeof(cmd), "%s%c%d", CMD_READ, FIELD_SEPARATOR, email_id);
  send_command(sockfd, cmd);

  char resp[BUFFER_SIZE];
  if (receive_response(sockfd, resp, sizeof(resp)) < 0) {
    printf("Server disconnected.\n");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  /* Check for error responses */
  if (strncmp(resp, RESP_ERR, strlen(RESP_ERR)) == 0) {
    char *reason = strchr(resp, FIELD_SEPARATOR);
    if (reason) {
      printf("Error: %s\n\n", reason + 1);
    } else {
      printf("Error: %s\n\n", resp);
    }
    return;
  }

  /* Parse: READ|id|from|to|subject|body|timestamp */
  char *saveptr;
  char *token;

  token = strtok_r(resp, "|", &saveptr); /* "READ" prefix */
  if (!token) {
    printf("Error: Malformed response.\n");
    return;
  }

  char *r_id = strtok_r(NULL, "|", &saveptr);
  char *r_from = strtok_r(NULL, "|", &saveptr);
  char *r_to = strtok_r(NULL, "|", &saveptr);
  char *r_subject = strtok_r(NULL, "|", &saveptr);
  char *r_body = strtok_r(NULL, "|", &saveptr);
  char *r_date = strtok_r(NULL, "|", &saveptr);

  if (!r_id || !r_from || !r_to || !r_subject || !r_body || !r_date) {
    printf("Error: Malformed response.\n");
    return;
  }

  printf("\n-------- EMAIL --------\n");
  printf("From:    %s\n", r_from);
  printf("To:      %s\n", r_to);
  printf("Subject: %s\n", r_subject);
  printf("Date:    %s\n", r_date);
  printf("\nMessage:\n%s\n", r_body);
  printf("-----------------------\n\n");
}

/* ----------------------------------------------------------------
 * handle_sent_mail — List sent emails in a formatted table.
 *
 * Each line from the server:  SENT|id|to|subject|timestamp
 * Final line:                 END
 * ---------------------------------------------------------------- */
static void handle_sent_mail(int sockfd) {
  send_command(sockfd, CMD_SENT);

  printf("\n-------- SENT MAIL --------\n");
  printf("%-4s %-12s %-20s %s\n", "ID", "TO", "SUBJECT", "DATE");

  int count = 0;
  char resp[BUFFER_SIZE];

  while (1) {
    if (receive_response(sockfd, resp, sizeof(resp)) < 0) {
      printf("Server disconnected.\n");
      close(sockfd);
      exit(EXIT_FAILURE);
    }

    if (strcmp(resp, RESP_END) == 0) {
      break;
    }

    /* Parse: SENT|id|to|subject|timestamp */
    char *saveptr;
    char *token;

    token = strtok_r(resp, "|", &saveptr); /* "SENT" prefix */
    if (!token)
      continue;

    char *id_str = strtok_r(NULL, "|", &saveptr);
    char *to = strtok_r(NULL, "|", &saveptr);
    char *subject = strtok_r(NULL, "|", &saveptr);
    char *date = strtok_r(NULL, "|", &saveptr);

    if (!id_str || !to || !subject || !date)
      continue;

    printf("%-4s %-12s %-20s %s\n", id_str, to, subject, date);
    count++;
  }

  if (count == 0) {
    printf("No sent emails.\n");
  }
  printf("----------------------------\n\n");
}

/* ----------------------------------------------------------------
 * handle_logout — Log out and return to pre-login menu.
 * ---------------------------------------------------------------- */
static void handle_logout(int sockfd, int *logged_in) {
  send_command(sockfd, CMD_LOGOUT);

  char resp[BUFFER_SIZE];
  if (receive_response(sockfd, resp, sizeof(resp)) < 0) {
    printf("Server disconnected.\n");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  if (strncmp(resp, RESP_LOGOUT_OK, strlen(RESP_LOGOUT_OK)) == 0) {
    *logged_in = 0;
    printf("Logged out successfully.\n\n");
  } else {
    printf("Server: %s\n\n", resp);
  }
}

/* ----------------------------------------------------------------
 * handle_quit — Notify server and disconnect.
 * ---------------------------------------------------------------- */
static void handle_quit(int sockfd) {
  send_command(sockfd, CMD_QUIT);
  close(sockfd);
  printf("Disconnected from server. Goodbye!\n");
  exit(EXIT_SUCCESS);
}

/* ================================================================
 * main — entry point.
 * ================================================================ */
int main(int argc, char *argv[]) {
  /* ----------------------------------------------------------
   * 1. Parse command-line arguments
   * ---------------------------------------------------------- */
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "Usage: %s <server_ip> [port]\n", argv[0]);
    return EXIT_FAILURE;
  }

  const char *server_ip = argv[1];
  int port = SERVER_PORT;

  if (argc == 3) {
    port = atoi(argv[2]);
    if (port <= 0 || port > 65535) {
      fprintf(stderr, "Error: Invalid port number '%s'.\n", argv[2]);
      return EXIT_FAILURE;
    }
  }

  /* ----------------------------------------------------------
   * 2. Create TCP socket
   * ---------------------------------------------------------- */
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("socket");
    return EXIT_FAILURE;
  }

  /* ----------------------------------------------------------
   * 3. Connect to server
   * ---------------------------------------------------------- */
  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons((uint16_t)port);

  if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
    fprintf(stderr, "Error: Invalid server IP address '%s'.\n", server_ip);
    close(sockfd);
    return EXIT_FAILURE;
  }

  if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    fprintf(stderr, "Failed to connect to server.\n");
    close(sockfd);
    return EXIT_FAILURE;
  }

  printf("Connected to server at %s:%d\n\n", server_ip, port);

  /* ----------------------------------------------------------
   * 4. Main interactive loop
   * ---------------------------------------------------------- */
  int logged_in = 0;
  char username[MAX_USERNAME] = {0};
  char choice_buf[16];

  while (1) {
    if (!logged_in) {
      /* ---------- Pre-login menu ---------- */
      printf("========= EMAIL CLIENT =========\n");
      printf("1. Register\n");
      printf("2. Login\n");
      printf("3. Quit\n");
      printf("=================================\n");
      read_line("Choice: ", choice_buf, sizeof(choice_buf));

      int choice = atoi(choice_buf);
      printf("\n");

      switch (choice) {
      case 1:
        handle_register(sockfd);
        break;
      case 2:
        handle_login(sockfd, username, &logged_in);
        break;
      case 3:
        handle_quit(sockfd);
        break; /* not reached */
      default:
        printf("Invalid choice. Please enter 1, 2, or 3.\n\n");
        break;
      }
    } else {
      /* ---------- Post-login menu ---------- */
      printf("======== MAILBOX - %s ========\n", username);
      printf("1. Compose & Send Email\n");
      printf("2. Inbox\n");
      printf("3. Read Email\n");
      printf("4. Sent Mail\n");
      printf("5. Logout\n");
      printf("6. Quit\n");
      printf("=======================================\n");
      read_line("Choice: ", choice_buf, sizeof(choice_buf));

      int choice = atoi(choice_buf);
      printf("\n");

      switch (choice) {
      case 1:
        handle_compose(sockfd);
        break;
      case 2:
        handle_inbox(sockfd);
        break;
      case 3:
        handle_read_email(sockfd);
        break;
      case 4:
        handle_sent_mail(sockfd);
        break;
      case 5:
        handle_logout(sockfd, &logged_in);
        break;
      case 6:
        handle_quit(sockfd);
        break; /* not reached */
      default:
        printf("Invalid choice. Please enter 1-6.\n\n");
        break;
      }
    }
  }

  /* Not reached; handle_quit exits the process. */
  close(sockfd);
  return EXIT_SUCCESS;
}
