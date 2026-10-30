# ============================================================
# Makefile — Multi-Client Email Service (C + TCP Sockets)
# ============================================================
# Usage:
#   make            — build both server and client
#   make server     — build server only
#   make client     — build client only
#   make clean      — remove binaries and object files
#   make run-server — build and start the server (port 8888)
#   make run-client — build and start a client (localhost)
# ============================================================

CC       = gcc
CFLAGS   = -Wall -Wextra -Wno-unused-parameter -g
LDFLAGS  = -lpthread

# Source files
SERVER_SRC = server.c storage.c
CLIENT_SRC = client.c
HEADERS    = protocol.h storage.h

# Targets
SERVER = server
CLIENT = client

.PHONY: all clean run-server run-client

all: $(SERVER) $(CLIENT)

$(SERVER): $(SERVER_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $(SERVER) $(SERVER_SRC) $(LDFLAGS)

$(CLIENT): $(CLIENT_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $(CLIENT) $(CLIENT_SRC)

clean:
	rm -f $(SERVER) $(CLIENT)

distclean: clean
	rm -rf data/

run-server: $(SERVER)
	./$(SERVER)

run-client: $(CLIENT)
	./$(CLIENT) 127.0.0.1
