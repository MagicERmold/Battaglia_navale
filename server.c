#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 5587
#define BOARD_SIZE 10
#define NUM_SHIPS 5
#define TARGET_PLAYERS 2

typedef struct Player {
    int id;
    int socket_fd;
    bool board[BOARD_SIZE][BOARD_SIZE];
    int hits_board[BOARD_SIZE][BOARD_SIZE]; // 0: ignoto, 1: acqua, 2: colpito
    int remaining_ships;
    bool is_ready;
    bool is_alive;
    struct Player* next;
} Player;

static Player* head_player = NULL;
static Player* current_turn_player = NULL;
static int total_players = 0;
static int active_players = 0;
static bool game_started = false;

static pthread_mutex_t game_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t game_cond = PTHREAD_COND_INITIALIZER;

// Wrapper per send robusta a interruzioni da segnale (EINTR) e frammentazione TCP
static ssize_t send_all(int sockfd, const void *buf, size_t len) {
    size_t total_sent = 0;
    const char *ptr = (const char *)buf;

    while (total_sent < len) {
        ssize_t sent = send(sockfd, ptr + total_sent, len - total_sent, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue; // Chiamata interrotta da segnale, si ritenta
            }
            return -1; // Errore irreversibile
        }
        if (sent == 0) {
            return 0; // Connessione chiusa
        }
        total_sent += (size_t)sent;
    }
    return (ssize_t)total_sent;
}

// Wrapper per recv robusta a interruzioni da segnale (EINTR)
static ssize_t recv_interruptible(int sockfd, void *buf, size_t len, int flags) {
    while (1) {
        ssize_t res = recv(sockfd, buf, len, flags);
        if (res < 0) {
            if (errno == EINTR) {
                continue; // Ritenta se interrotta da segnale
            }
            return -1;
        }
        return res;
    }
}

// Ricezione esatta di len byte (simile a MSG_WAITALL ma sicuro rispetto a EINTR)
static ssize_t recv_exact(int sockfd, void *buf, size_t len) {
    size_t total_recv = 0;
    char *ptr = (char *)buf;

    while (total_recv < len) {
        ssize_t res = recv_interruptible(sockfd, ptr + total_recv, len - total_recv, 0);
        if (res <= 0) {
            return res; // 0 per disconnessione o -1 per errore
        }
        total_recv += (size_t)res;
    }
    return (ssize_t)total_recv;
}

static void broadcast_message(const char* msg) {
    if (!head_player || !msg) return;
    size_t len = strlen(msg);
    Player* curr = head_player;
    do {
        if (curr->is_alive && curr->socket_fd >= 0) {
            if (send_all(curr->socket_fd, msg, len) < 0) {
                perror("[Server] Errore invio broadcast");
            }
        }
        curr = curr->next;
    } while (curr != head_player);
}

static Player* add_player_circular(int socket_fd) {
    Player* new_p = (Player*)malloc(sizeof(Player));
    if (!new_p) {
        perror("[Server] Impossibile allocare memoria per il giocatore");
        return NULL;
    }

    new_p->socket_fd = socket_fd;
    new_p->is_ready = false;
    new_p->is_alive = true;
    new_p->remaining_ships = NUM_SHIPS;
    memset(new_p->board, false, sizeof(new_p->board));
    memset(new_p->hits_board, 0, sizeof(new_p->hits_board));

    if (head_player == NULL) {
        head_player = new_p;
        new_p->next = head_player;
        new_p->id = 1;
    } else {
        Player* last = head_player;
        while (last->next != head_player) {
            last = last->next;
        }
        new_p->id = last->id + 1;
        last->next = new_p;
        new_p->next = head_player;
    }

    total_players++;
    active_players++;
    return new_p;
}

static Player* find_player_by_id(int id) {
    if (!head_player) return NULL;
    Player* curr = head_player;
    do {
        if (curr->id == id) return curr;
        curr = curr->next;
    } while (curr != head_player);
    return NULL;
}

static void advance_turn(void) {
    if (active_players <= 1) return;
    Player* next_p = current_turn_player->next;
    while (!next_p->is_alive) {
        next_p = next_p->next;
    }
    current_turn_player = next_p;
}

static void invia_griglie_avversari(Player* self) {
    char packet[4096];
    packet[0] = '\0';

    strncat(packet, "\n================ STATO AVVERSARI ================\n", sizeof(packet) - strlen(packet) - 1);

    Player* curr = head_player;
    do {
        if (curr != self && curr->is_alive) {
            char title[128];
            int ret = snprintf(title, sizeof(title), "\n--- GIOCATORE %d (Navi rimaste: %d) ---\n", curr->id, curr->remaining_ships);
            if (ret > 0) {
                strncat(packet, title, sizeof(packet) - strlen(packet) - 1);
            }
            strncat(packet, "   |  0  1  2  3  4  5  6  7  8  9\n", sizeof(packet) - strlen(packet) - 1);
            strncat(packet, "---+------------------------------------\n", sizeof(packet) - strlen(packet) - 1);

            for (int i = 0; i < BOARD_SIZE; i++) {
                char row_line[128];
                int len_row = snprintf(row_line, sizeof(row_line), "%2d | ", i);
                if (len_row < 0) continue;

                for (int j = 0; j < BOARD_SIZE; j++) {
                    if (curr->hits_board[i][j] == 2) {
                        strncat(row_line, "✅ ", sizeof(row_line) - strlen(row_line) - 1);
                    } else if (curr->hits_board[i][j] == 1) {
                        strncat(row_line, "❌ ", sizeof(row_line) - strlen(row_line) - 1);
                    } else {
                        strncat(row_line, " ~ ", sizeof(row_line) - strlen(row_line) - 1);
                    }
                }
                strncat(row_line, "\n", sizeof(row_line) - strlen(row_line) - 1);
                strncat(packet, row_line, sizeof(packet) - strlen(packet) - 1);
            }
            strncat(packet, "---+------------------------------------\n", sizeof(packet) - strlen(packet) - 1);
        }
        curr = curr->next;
    } while (curr != head_player);

    if (send_all(self->socket_fd, packet, strlen(packet)) < 0) {
        perror("[Server] Errore durante l'invio delle griglie avversarie");
    }
}

void* client_handler(void* arg) {
    if (!arg) return NULL;
    int client_s = *(int*)arg;
    free(arg);

    if (pthread_mutex_lock(&game_mutex) != 0) {
        perror("[Server] Mutex lock fallito");
        close(client_s);
        return NULL;
    }

    if (game_started || total_players >= TARGET_PLAYERS) {
        const char* err_msg = "PARTITA PIENA O GIA IN CORSO.\n";
        send_all(client_s, err_msg, strlen(err_msg));
        if (close(client_s) < 0) perror("[Server] Close socket fallita");
        pthread_mutex_unlock(&game_mutex);
        return NULL;
    }

    Player* self = add_player_circular(client_s);
    if (!self) {
        const char* mem_err = "ERRORE INTERNO SERVER (MEMORIA).\n";
        send_all(client_s, mem_err, strlen(mem_err));
        close(client_s);
        pthread_mutex_unlock(&game_mutex);
        return NULL;
    }

    printf("[Server] Connesso Giocatore ID %d (%d/%d attesi)\n", self->id, total_players, TARGET_PLAYERS);

    char welcome[256];
    int w_ret = snprintf(welcome, sizeof(welcome), "[Server] Connesso! Sei il Giocatore ID %d. (%d/%d giocatori in lobby)\nConfigura la flotta...\n", 
                         self->id, total_players, TARGET_PLAYERS);
    if (w_ret > 0) {
        send_all(client_s, welcome, strlen(welcome));
    }
    pthread_mutex_unlock(&game_mutex);

    // Ricezione flotta dal client
    bool temp_board[BOARD_SIZE][BOARD_SIZE];
    ssize_t bytes_recv = recv_exact(client_s, temp_board, sizeof(temp_board));
    if (bytes_recv != (ssize_t)sizeof(temp_board)) {
        fprintf(stderr, "[Server] Ricezione incompleta o socket chiuso per Giocatore %d.\n", self->id);
        if (pthread_mutex_lock(&game_mutex) == 0) {
            self->is_alive = false;
            active_players--;
            close(client_s);
            pthread_mutex_unlock(&game_mutex);
        }
        return NULL;
    }

    if (pthread_mutex_lock(&game_mutex) != 0) {
        perror("[Server] Mutex lock fallito");
        close(client_s);
        return NULL;
    }

    memcpy(self->board, temp_board, sizeof(temp_board));
    self->is_ready = true;

    int pronti = 0;
    Player* c = head_player;
    do {
        if (c->is_ready) pronti++;
        c = c->next;
    } while (c != head_player);

    printf("[Server] Giocatore %d pronto! (%d/%d pronti)\n", self->id, pronti, TARGET_PLAYERS);

    if (total_players == TARGET_PLAYERS && pronti == TARGET_PLAYERS && !game_started) {
        game_started = true;
        current_turn_player = head_player;
        printf("[Server] Tutti i %d giocatori sono pronti! Inizio della battaglia.\n", TARGET_PLAYERS);
        broadcast_message("\n=== TUTTI PRONTI! LA BATTAGLIA HA INIZIO! ===\n");
        if (pthread_cond_broadcast(&game_cond) != 0) {
            perror("[Server] Errore cond broadcast");
        }
    }

    while (!game_started) {
        if (pthread_cond_wait(&game_cond, &game_mutex) != 0) {
            perror("[Server] Errore cond wait");
            pthread_mutex_unlock(&game_mutex);
            close(client_s);
            return NULL;
        }
    }
    pthread_mutex_unlock(&game_mutex);

    // Ciclo dei turni
    while (1) {
        if (pthread_mutex_lock(&game_mutex) != 0) {
            perror("[Server] Mutex lock fallito");
            break;
        }

        if (active_players <= 1) {
            if (self->is_alive) {
                const char* win_msg = "GAME_OVER Hai vinto la battaglia navale!\n";
                send_all(self->socket_fd, win_msg, strlen(win_msg));
            }
            pthread_mutex_unlock(&game_mutex);
            break;
        }

        while (current_turn_player != self && active_players > 1) {
            if (pthread_cond_wait(&game_cond, &game_mutex) != 0) {
                perror("[Server] Errore cond wait");
                pthread_mutex_unlock(&game_mutex);
                goto cleanup;
            }
        }

        if (active_players <= 1 || !self->is_alive) {
            pthread_mutex_unlock(&game_mutex);
            break;
        }

        invia_griglie_avversari(self);

        const char* turn_cmd = "YOUR_TURN\n";
        if (send_all(self->socket_fd, turn_cmd, strlen(turn_cmd)) < 0) {
            perror("[Server] Errore notifica turno");
        }
        pthread_mutex_unlock(&game_mutex);

        // Ricezione mossa
        char buffer[128];
        memset(buffer, 0, sizeof(buffer));
        ssize_t res = recv_interruptible(self->socket_fd, buffer, sizeof(buffer) - 1, 0);
        if (res <= 0) {
            if (pthread_mutex_lock(&game_mutex) == 0) {
                printf("[Server] Disconnessione rilevata per il Giocatore %d.\n", self->id);
                self->is_alive = false;
                active_players--;
                advance_turn();
                pthread_cond_broadcast(&game_cond);
                pthread_mutex_unlock(&game_mutex);
            }
            break;
        }

        int target_id, row, col;
        if (sscanf(buffer, "SHOOT %d %d %d", &target_id, &row, &col) == 3) {
            if (pthread_mutex_lock(&game_mutex) != 0) {
                perror("[Server] Mutex lock fallito");
                break;
            }

            Player* target = find_player_by_id(target_id);
            char broadcast_buf[256];

            if (!target || !target->is_alive || target == self) {
                const char* err_turn = "[Server] Bersaglio non valido o già eliminato!\n";
                send_all(self->socket_fd, err_turn, strlen(err_turn));
            } else if (row < 0 || row >= BOARD_SIZE || col < 0 || col >= BOARD_SIZE) {
                const char* err_coord = "[Server] Coordinate fuori scala!\n";
                send_all(self->socket_fd, err_coord, strlen(err_coord));
            } else if (target->hits_board[row][col] != 0) {
                const char* err_cell = "[Server] Cella già colpita in precedenza! Scegline un'altra.\n";
                send_all(self->socket_fd, err_cell, strlen(err_cell));
            } else {
                if (target->board[row][col]) {
                    target->hits_board[row][col] = 2;
                    target->board[row][col] = false;
                    target->remaining_ships--;

                    int b_len = snprintf(broadcast_buf, sizeof(broadcast_buf), 
                                         "💥 COLPITO! Giocatore %d ha centrato una nave del Giocatore %d in (%d, %d)!\n",
                                         self->id, target->id, row, col);
                    if (b_len > 0) broadcast_message(broadcast_buf);

                    if (target->remaining_ships == 0) {
                        target->is_alive = false;
                        active_players--;

                        b_len = snprintf(broadcast_buf, sizeof(broadcast_buf),
                                         "☠️ Giocatore %d ha perso tutte le navi ed è stato ELIMINATO!\n",
                                         target->id);
                        if (b_len > 0) broadcast_message(broadcast_buf);

                        const char* elim_msg = "GAME_OVER Tutte le tue navi sono state affondate.\n";
                        send_all(target->socket_fd, elim_msg, strlen(elim_msg));
                    }

                    if (active_players > 1) {
                        const char* bonus_msg = "🎉 Nave colpita! Hai diritto a un altro attacco!\n";
                        send_all(self->socket_fd, bonus_msg, strlen(bonus_msg));
                    }
                } else {
                    target->hits_board[row][col] = 1;

                    int b_len = snprintf(broadcast_buf, sizeof(broadcast_buf),
                                         "💧 ACQUA! Giocatore %d ha sparato a vuoto su Giocatore %d in (%d, %d).\n",
                                         self->id, target->id, row, col);
                    if (b_len > 0) broadcast_message(broadcast_buf);

                    advance_turn();
                }
            }

            pthread_cond_broadcast(&game_cond);
            pthread_mutex_unlock(&game_mutex);
        }
    }

cleanup:
    if (close(self->socket_fd) < 0) {
        perror("[Server] Errore in chiusura socket");
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    (void)argc; 
    (void)argv;
    srand((unsigned int)time(NULL));

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("[Server] Errore creazione socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("[Server] setsockopt SO_REUSEADDR fallita");
        close(s);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(s, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("[Server] Errore durante il binding del socket");
        close(s);
        exit(EXIT_FAILURE);
    }

    if (listen(s, 30) < 0) {
        perror("[Server] Errore durante il listening");
        close(s);
        exit(EXIT_FAILURE);
    }

    printf("[Server] In ascolto sulla porta %d. Giocatori richiesti per iniziare: %d\n", PORT, TARGET_PLAYERS);

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_s = accept(s, (struct sockaddr*)&client_addr, &client_len);
        if (client_s < 0) {
            if (errno == EINTR) {
                continue; // Ritenta la accept se interrotta da segnale
            }
            perror("[Server] Errore durante accept");
            continue;
        }

        int* client_s_cpy = (int*)malloc(sizeof(int));
        if (!client_s_cpy) {
            perror("[Server] Impossibile allocare memoria per il descrittore client");
            close(client_s);
            continue;
        }
        *client_s_cpy = client_s;

        pthread_t thread;
        int err = pthread_create(&thread, NULL, client_handler, (void*)client_s_cpy);
        if (err != 0) {
            fprintf(stderr, "[Server] pthread_create fallita con codice: %d\n", err);
            free(client_s_cpy);
            close(client_s);
            continue;
        }

        err = pthread_detach(thread);
        if (err != 0) {
            fprintf(stderr, "[Server] pthread_detach fallita con codice: %d\n", err);
        }
    }

    if (close(s) < 0) {
        perror("[Server] Errore chiusura socket principale");
    }
    return 0;
}
