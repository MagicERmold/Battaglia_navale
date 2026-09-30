# Compiler e flag
CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11 -O2
LDFLAGS = -pthread

# Eseguibili target
SERVER_BIN = server
CLIENT_BIN = client

# Sorgenti
SERVER_SRC = server.c
CLIENT_SRC = client.c

# Oggetti intermedi
SERVER_OBJ = $(SERVER_SRC:.c=.o)
CLIENT_OBJ = $(CLIENT_SRC:.c=.o)

.PHONY: all clean help

# Target predefinito: compila sia client che server
all: $(SERVER_BIN) $(CLIENT_BIN)

# Compilazione del server (richiede supporto pthreads)
$(SERVER_BIN): $(SERVER_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Compilazione del client
$(CLIENT_BIN): $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Regola generica per generare i file oggetto .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Pulizia di binari e file intermedi
clean:
	rm -f $(SERVER_OBJ) $(CLIENT_OBJ) $(SERVER_BIN) $(CLIENT_BIN)

# Aiuto rapido
help:
	@echo "Comandi disponibili:"
	@echo "  make        - Compila sia client che server"
	@echo "  make server - Compila solo il server"
	@echo "  make client - Compila solo il client"
	@echo "  make clean  - Rimuove i file binari e oggetto (.o)"