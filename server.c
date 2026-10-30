/*
 * server.c — Multi-client TCP email server.
 *
 * Architecture:
 *   - Main thread: socket → bind → listen → accept loop.
 *   - Each accepted connection spawns a detached pthread.
 *   - SIGINT shuts down gracefully (closes listen socket, exits).
 *
 * Compile:
 *   cc -Wall -Wextra -o server server.c storage.c -lpthread
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "protocol.h"
#include "storage.h"

/* ----------------------------------------------------------------
 * Per-client session record
 * ---------------------------------------------------------------- */
typedef struct {
  int sockfd;                  /* -1 when slot is empty           */
  char username[MAX_USERNAME]; /* empty string when not logged in */
  int logged_in;               /* 0 or 1                          */
  pthread_t thread_id;
} ClientSession;

/* ----------------------------------------------------------------
 * Global state
 * ---------------------------------------------------------------- */
static ClientSession sessions[MAX_CLIENTS];
static pthread_mutex_t sessions_mutex = PTHREAD_MUTEX_INITIALIZER;
static int server_fd = -1; /* listen socket */

/* ----------------------------------------------------------------
 * Forward declarations
 * ---------------------------------------------------------------- */
static void *handle_client(void *arg);
static void send_response(int sockfd, const char *msg);
static int parse_command(const char *buffer, char fields[][BUFFER_SIZE],
                         int max_fields);
static int find_free_slot(void);
static void clear_session(int index);
static void sigint_handler(int sig);

/* ================================================================
 * main – Set up the listening socket and accept loop.
 * ================================================================ */
int main(int argc, char *argv[]) {
  int port = SERVER_PORT;

  /* Optional command-line port override. */
  if (argc >= 2) {
    port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
      fprintf(stderr, "Invalid port: %s\n", argv[1]);
      return EXIT_FAILURE;
    }
  }

  /* Initialise persistent storage (creates data dirs / files). */
  storage_init();

  /* Initialise session table (all slots empty). */
  for (int i = 0; i < MAX_CLIENTS; i++) {
    sessions[i].sockfd = -1;
    sessions[i].logged_in = 0;
    sessions[i].username[0] = '\0';
  }

  /* Install SIGINT handler for graceful shutdown. */
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigint_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  if (sigaction(SIGINT, &sa, NULL) == -1) {
    perror("sigaction");
    return EXIT_FAILURE;
  }

  /* Create TCP socket. */
  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("socket");
    return EXIT_FAILURE;
  }

  /* Allow quick restart (SO_REUSEADDR). */
  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("setsockopt SO_REUSEADDR");
    close(server_fd);
    return EXIT_FAILURE;
  }

  /* Bind to INADDR_ANY:<port>. */
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons((uint16_t)port);

  if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(server_fd);
    return EXIT_FAILURE;
  }

  /* Start listening. */
  if (listen(server_fd, 10) < 0) {
    perror("listen");
    close(server_fd);
    return EXIT_FAILURE;
  }

  printf("Server started on port %d\n", port);

  /* ---- Accept loop ------------------------------------------------- */
  for (;;) {
    struct sockaddr_in cli_addr;
    socklen_t cli_len = sizeof(cli_addr);

    int client_fd = accept(server_fd, (struct sockaddr *)&cli_addr, &cli_len);
    if (client_fd < 0) {
      /* accept() fails after close(server_fd) in signal handler. */
      perror("accept");
      break;
    }

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cli_addr.sin_addr, ip_str, sizeof(ip_str));
    printf("[+] New connection from %s:%d (fd %d)\n", ip_str,
           ntohs(cli_addr.sin_port), client_fd);

    /* Find a free session slot. */
    pthread_mutex_lock(&sessions_mutex);
    int slot = find_free_slot();
    if (slot < 0) {
      pthread_mutex_unlock(&sessions_mutex);
      fprintf(stderr, "[!] Max clients reached, rejecting fd %d\n", client_fd);
      send_response(client_fd, "ERR|Server full");
      close(client_fd);
      continue;
    }
    sessions[slot].sockfd = client_fd;
    sessions[slot].logged_in = 0;
    sessions[slot].username[0] = '\0';
    pthread_mutex_unlock(&sessions_mutex);

    /* Spawn a detached thread for this client.
     * We pass the slot index (cast to intptr_t → void *).
     * Stack size is set to 2 MB because the Email struct is large
     * (~2.5 KB each) and handlers allocate arrays of them. */
    int *idx = malloc(sizeof(int));
    if (!idx) {
      perror("malloc");
      pthread_mutex_lock(&sessions_mutex);
      clear_session(slot);
      pthread_mutex_unlock(&sessions_mutex);
      close(client_fd);
      continue;
    }
    *idx = slot;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 2 * 1024 * 1024); /* 2 MB */
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    pthread_t tid;
    if (pthread_create(&tid, &attr, handle_client, idx) != 0) {
      perror("pthread_create");
      free(idx);
      pthread_attr_destroy(&attr);
      pthread_mutex_lock(&sessions_mutex);
      clear_session(slot);
      pthread_mutex_unlock(&sessions_mutex);
      close(client_fd);
      continue;
    }
    pthread_attr_destroy(&attr);

    pthread_mutex_lock(&sessions_mutex);
    sessions[slot].thread_id = tid;
    pthread_mutex_unlock(&sessions_mutex);
  }

  printf("Server shutting down\n");
  return EXIT_SUCCESS;
}

/* ================================================================
 * SIGINT handler – close the listen socket so accept() returns.
 * ================================================================ */
static void sigint_handler(int sig) {
  (void)sig;
  if (server_fd >= 0) {
    close(server_fd);
    server_fd = -1;
  }
  /* write() is async-signal-safe, printf is not. */
  const char msg[] = "\nServer shutting down\n";
  (void)write(STDOUT_FILENO, msg, sizeof(msg) - 1);
  _exit(EXIT_SUCCESS);
}

/* ================================================================
 * send_response – Send a string with MSG_TERMINATOR appended.
 * ================================================================ */
static void send_response(int sockfd, const char *msg) {
  char buf[BUFFER_SIZE];
  int n = snprintf(buf, sizeof(buf), "%s%s", msg, MSG_TERMINATOR);
  if (n < 0 || (size_t)n >= sizeof(buf)) {
    /* Message too long – truncate and still terminate. */
    memcpy(buf + sizeof(buf) - 3, "\r\n", 3); /* includes '\0' */
    n = (int)sizeof(buf) - 1;
  }
  /* Best-effort send (ignore partial writes for simplicity). */
  send(sockfd, buf, (size_t)n, 0);
}

/* ================================================================
 * parse_command – Split `buffer` on FIELD_SEPARATOR into fields.
 *
 * Strips any trailing \r or \n before splitting.
 * Returns the number of fields parsed (≥ 1 for non-empty input).
 * ================================================================ */
static int parse_command(const char *buffer, char fields[][BUFFER_SIZE],
                         int max_fields) {
  char tmp[BUFFER_SIZE];
  snprintf(tmp, sizeof(tmp), "%s", buffer);

  /* Strip trailing \r and \n. */
  size_t len = strlen(tmp);
  while (len > 0 && (tmp[len - 1] == '\r' || tmp[len - 1] == '\n')) {
    tmp[--len] = '\0';
  }

  if (len == 0) {
    return 0;
  }

  int count = 0;
  char *saveptr = NULL;
  char sep_str[2] = {FIELD_SEPARATOR, '\0'};
  char *token = strtok_r(tmp, sep_str, &saveptr);

  while (token && count < max_fields) {
    snprintf(fields[count], BUFFER_SIZE, "%s", token);
    count++;
    token = strtok_r(NULL, sep_str, &saveptr);
  }

  return count;
}

/* ================================================================
 * find_free_slot – Return index of first empty session, or -1.
 *
 * Caller MUST hold sessions_mutex.
 * ================================================================ */
static int find_free_slot(void) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (sessions[i].sockfd == -1) {
      return i;
    }
  }
  return -1;
}

/* ================================================================
 * clear_session – Reset session slot to empty.
 *
 * Caller MUST hold sessions_mutex.
 * ================================================================ */
static void clear_session(int index) {
  if (index < 0 || index >= MAX_CLIENTS)
    return;
  sessions[index].sockfd = -1;
  sessions[index].logged_in = 0;
  sessions[index].username[0] = '\0';
}

/* ================================================================
 * handle_client – Per-client thread entry point.
 *
 * Reads commands, dispatches to the appropriate handler, and
 * cleans up on disconnect.
 * ================================================================ */
static void *handle_client(void *arg) {
  int slot = *(int *)arg;
  free(arg);

  pthread_mutex_lock(&sessions_mutex);
  int sockfd = sessions[slot].sockfd;
  pthread_mutex_unlock(&sessions_mutex);

  char recv_buf[BUFFER_SIZE];
  char response[BUFFER_SIZE];

  /* Allocate field array on the heap – 8 * 4096 = 32 KB is too much
   * for the default pthread stack (512 KB) when combined with the
   * large Email struct used in INBOX/SENT handlers. */
  char(*fields)[BUFFER_SIZE] = malloc(8 * BUFFER_SIZE);
  if (!fields) {
    perror("malloc fields");
    pthread_mutex_lock(&sessions_mutex);
    clear_session(slot);
    pthread_mutex_unlock(&sessions_mutex);
    close(sockfd);
    return NULL;
  }

  for (;;) {
    memset(recv_buf, 0, sizeof(recv_buf));
    ssize_t nbytes = recv(sockfd, recv_buf, sizeof(recv_buf) - 1, 0);

    if (nbytes <= 0) {
      /* Client disconnected or error. */
      if (nbytes == 0) {
        printf("[-] Client fd %d disconnected\n", sockfd);
      } else {
        perror("recv");
      }
      break;
    }
    recv_buf[nbytes] = '\0';

    /* Parse the command into pipe-separated fields. */
    int nfields = parse_command(recv_buf, fields, 8);
    if (nfields == 0) {
      send_response(sockfd, RESP_INVALID_CMD);
      continue;
    }

    /* ----------------------------------------------------------
     * REGISTER|username|password|email_addr
     * ---------------------------------------------------------- */
    if (strcmp(fields[0], CMD_REGISTER) == 0) {
      if (nfields < 4) {
        send_response(sockfd, RESP_REGISTER_FAIL "|Missing fields");
        continue;
      }
      int rc = register_user(fields[1], fields[2], fields[3]);
      if (rc == 0) {
        printf("[*] User registered: %s\n", fields[1]);
        send_response(sockfd, RESP_REGISTER_OK);
      } else if (rc == -1) {
        send_response(sockfd,
                      RESP_REGISTER_FAIL "|Username or email already exists");
      } else {
        send_response(sockfd, RESP_REGISTER_FAIL "|Storage error");
      }
    }

    /* ----------------------------------------------------------
     * LOGIN|username|password
     * ---------------------------------------------------------- */
    else if (strcmp(fields[0], CMD_LOGIN) == 0) {
      if (nfields < 3) {
        send_response(sockfd, RESP_LOGIN_FAIL "|Missing fields");
        continue;
      }

      int rc = authenticate_user(fields[1], fields[2]);
      if (rc != 0) {
        const char *reason = (rc == -2) ? "User not found" : "Wrong password";
        snprintf(response, sizeof(response), "%s|%s", RESP_LOGIN_FAIL, reason);
        send_response(sockfd, response);
        continue;
      }

      /* Check if this user is already logged in on another session. */
      pthread_mutex_lock(&sessions_mutex);
      int already = 0;
      for (int i = 0; i < MAX_CLIENTS; i++) {
        if (i != slot && sessions[i].logged_in &&
            strcmp(sessions[i].username, fields[1]) == 0) {
          already = 1;
          break;
        }
      }
      if (already) {
        pthread_mutex_unlock(&sessions_mutex);
        send_response(sockfd, RESP_LOGIN_FAIL "|User already logged in");
        continue;
      }

      /* Mark session as logged in. */
      sessions[slot].logged_in = 1;
      snprintf(sessions[slot].username, sizeof(sessions[slot].username), "%s",
               fields[1]);
      pthread_mutex_unlock(&sessions_mutex);

      printf("[*] User logged in: %s (fd %d)\n", fields[1], sockfd);
      snprintf(response, sizeof(response), "%s|%s", RESP_LOGIN_OK, fields[1]);
      send_response(sockfd, response);
    }

    /* ----------------------------------------------------------
     * SEND|to|subject|body
     * ---------------------------------------------------------- */
    else if (strcmp(fields[0], CMD_SEND) == 0) {
      pthread_mutex_lock(&sessions_mutex);
      int li = sessions[slot].logged_in;
      char from[MAX_USERNAME];
      snprintf(from, sizeof(from), "%s", sessions[slot].username);
      pthread_mutex_unlock(&sessions_mutex);

      if (!li) {
        send_response(sockfd, RESP_NOT_LOGGED_IN);
        continue;
      }
      if (nfields < 4) {
        send_response(sockfd, RESP_SEND_FAIL "|Missing fields");
        continue;
      }

      /* Validate recipient exists. */
      if (!user_exists(fields[1])) {
        send_response(sockfd, RESP_SEND_FAIL "|Recipient not found");
        continue;
      }

      /* Build the Email struct. */
      Email email;
      memset(&email, 0, sizeof(email));
      snprintf(email.from, sizeof(email.from), "%s", from);
      snprintf(email.to, sizeof(email.to), "%s", fields[1]);
      snprintf(email.subject, sizeof(email.subject), "%s", fields[2]);
      snprintf(email.body, sizeof(email.body), "%s", fields[3]);

      /* Timestamp. */
      time_t now = time(NULL);
      struct tm tm_buf;
      localtime_r(&now, &tm_buf);
      strftime(email.timestamp, sizeof(email.timestamp), "%Y-%m-%d %H:%M:%S",
               &tm_buf);

      int eid = store_email(&email);
      if (eid < 0) {
        send_response(sockfd, RESP_SEND_FAIL "|Storage error");
      } else {
        printf("[*] Mail sent: %s -> %s (id %d)\n", from, fields[1], eid);
        snprintf(response, sizeof(response), "%s|%d", RESP_SEND_OK, eid);
        send_response(sockfd, response);
      }
    }

    /* ----------------------------------------------------------
     * INBOX
     * ---------------------------------------------------------- */
    else if (strcmp(fields[0], CMD_INBOX) == 0) {
      pthread_mutex_lock(&sessions_mutex);
      int li = sessions[slot].logged_in;
      char user[MAX_USERNAME];
      snprintf(user, sizeof(user), "%s", sessions[slot].username);
      pthread_mutex_unlock(&sessions_mutex);

      if (!li) {
        send_response(sockfd, RESP_NOT_LOGGED_IN);
        continue;
      }

      Email *emails = malloc(128 * sizeof(Email));
      if (!emails) {
        send_response(sockfd, RESP_ERR "|Server memory error");
        continue;
      }
      int count = get_inbox(user, emails, 128);
      if (count < 0)
        count = 0;

      for (int i = 0; i < count; i++) {
        snprintf(response, sizeof(response), "INBOX|%d|%s|%s|%s|%d",
                 emails[i].id, emails[i].from, emails[i].subject,
                 emails[i].timestamp, emails[i].read_status);
        send_response(sockfd, response);
      }
      send_response(sockfd, RESP_END);
      free(emails);
    }

    /* ----------------------------------------------------------
     * READ|email_id
     * ---------------------------------------------------------- */
    else if (strcmp(fields[0], CMD_READ) == 0) {
      pthread_mutex_lock(&sessions_mutex);
      int li = sessions[slot].logged_in;
      char user[MAX_USERNAME];
      snprintf(user, sizeof(user), "%s", sessions[slot].username);
      pthread_mutex_unlock(&sessions_mutex);

      if (!li) {
        send_response(sockfd, RESP_NOT_LOGGED_IN);
        continue;
      }
      if (nfields < 2) {
        send_response(sockfd, RESP_ERR "|Missing email ID");
        continue;
      }

      int eid = atoi(fields[1]);
      Email email;
      memset(&email, 0, sizeof(email));

      if (read_email(user, eid, &email) == 0) {
        snprintf(response, sizeof(response), "READ|%d|%s|%s|%s|%s|%s", email.id,
                 email.from, email.to, email.subject, email.body,
                 email.timestamp);
        send_response(sockfd, response);
      } else {
        send_response(sockfd, RESP_ERR "|Email not found");
      }
    }

    /* ----------------------------------------------------------
     * SENT
     * ---------------------------------------------------------- */
    else if (strcmp(fields[0], CMD_SENT) == 0) {
      pthread_mutex_lock(&sessions_mutex);
      int li = sessions[slot].logged_in;
      char user[MAX_USERNAME];
      snprintf(user, sizeof(user), "%s", sessions[slot].username);
      pthread_mutex_unlock(&sessions_mutex);

      if (!li) {
        send_response(sockfd, RESP_NOT_LOGGED_IN);
        continue;
      }

      Email *emails = malloc(128 * sizeof(Email));
      if (!emails) {
        send_response(sockfd, RESP_ERR "|Server memory error");
        continue;
      }
      int count = get_sent(user, emails, 128);
      if (count < 0)
        count = 0;

      for (int i = 0; i < count; i++) {
        snprintf(response, sizeof(response), "SENT|%d|%s|%s|%s", emails[i].id,
                 emails[i].to, emails[i].subject, emails[i].timestamp);
        send_response(sockfd, response);
      }
      send_response(sockfd, RESP_END);
      free(emails);
    }

    /* ----------------------------------------------------------
     * LOGOUT
     * ---------------------------------------------------------- */
    else if (strcmp(fields[0], CMD_LOGOUT) == 0) {
      pthread_mutex_lock(&sessions_mutex);
      if (sessions[slot].logged_in) {
        printf("[*] User logged out: %s (fd %d)\n", sessions[slot].username,
               sockfd);
      }
      sessions[slot].logged_in = 0;
      sessions[slot].username[0] = '\0';
      pthread_mutex_unlock(&sessions_mutex);

      send_response(sockfd, RESP_LOGOUT_OK);
    }

    /* ----------------------------------------------------------
     * QUIT
     * ---------------------------------------------------------- */
    else if (strcmp(fields[0], CMD_QUIT) == 0) {
      printf("[-] Client fd %d quit\n", sockfd);
      pthread_mutex_lock(&sessions_mutex);
      clear_session(slot);
      pthread_mutex_unlock(&sessions_mutex);
      close(sockfd);
      free(fields);
      return NULL;
    }

    /* ----------------------------------------------------------
     * Unknown command
     * ---------------------------------------------------------- */
    else {
      send_response(sockfd, RESP_INVALID_CMD);
    }
  }

  /* Client disconnected – clean up. */
  pthread_mutex_lock(&sessions_mutex);
  if (sessions[slot].logged_in) {
    printf("[*] User disconnected: %s (fd %d)\n", sessions[slot].username,
           sockfd);
  }
  clear_session(slot);
  pthread_mutex_unlock(&sessions_mutex);
  close(sockfd);
  free(fields);

  return NULL;
}
