#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 5587
#define BOARD_SIZE 10
#define NUM_SHIPS 5

// Imposta il numero esatto di giocatori attesi per la partita
#define TARGET_PLAYERS 3

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

Player* head_player = NULL;
Player* current_turn_player = NULL;
int total_players = 0;
int active_players = 0;
bool game_started = false;

pthread_mutex_t game_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t game_cond = PTHREAD_COND_INITIALIZER;

void broadcast_message(const char* msg) {
    if (!head_player) return;
    Player* curr = head_player;
    do {
        if (curr->is_alive && curr->socket_fd >= 0) {
            send(curr->socket_fd, msg, strlen(msg), 0);
        }
        curr = curr->next;
    } while (curr != head_player);
}

Player* add_player_circular(int socket_fd) {
    Player* new_p = (Player*)malloc(sizeof(Player));
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

Player* find_player_by_id(int id) {
    if (!head_player) return NULL;
    Player* curr = head_player;
    do {
        if (curr->id == id) return curr;
        curr = curr->next;
    } while (curr != head_player);
    return NULL;
}

void advance_turn() {
    if (active_players <= 1) return;
    Player* next_p = current_turn_player->next;
    while (!next_p->is_alive) {
        next_p = next_p->next;
    }
    current_turn_player = next_p;
}

// Invia le griglie formattate come testo per evitare mismatch di stream TCP
void invia_griglie_avversari(Player* self) {
    char packet[4096];
    packet[0] = '\0';

    strcat(packet, "\n================ STATO AVVERSARI ================\n");

    Player* curr = head_player;
    do {
        if (curr != self && curr->is_alive) {
            char title[128];
            snprintf(title, sizeof(title), "\n--- GIOCATORE %d (Navi rimaste: %d) ---\n", curr->id, curr->remaining_ships);
            strcat(packet, title);
            strcat(packet, "   |  0  1  2  3  4  5  6  7  8  9\n");
            strcat(packet, "---+------------------------------------\n");

            for (int i = 0; i < BOARD_SIZE; i++) {
                char row_line[128];
                snprintf(row_line, sizeof(row_line), "%2d | ", i);
                for (int j = 0; j < BOARD_SIZE; j++) {
                    if (curr->hits_board[i][j] == 2) {
                        strcat(row_line, "✅ ");
                    } else if (curr->hits_board[i][j] == 1) {
                        strcat(row_line, "❌ ");
                    } else {
                        strcat(row_line, " ~ ");
                    }
                }
                strcat(row_line, "\n");
                strcat(packet, row_line);
            }
            strcat(packet, "---+------------------------------------\n");
        }
        curr = curr->next;
    } while (curr != head_player);

    send(self->socket_fd, packet, strlen(packet), 0);
}

void* client_handler(void* arg) {
    int client_s = *(int*)arg;
    free(arg);

    pthread_mutex_lock(&game_mutex);
    if (game_started || total_players >= TARGET_PLAYERS) {
        char* err_msg = "PARTITA PIENA O GIA IN CORSO.\n";
        send(client_s, err_msg, strlen(err_msg), 0);
        close(client_s);
        pthread_mutex_unlock(&game_mutex);
        return NULL;
    }

    Player* self = add_player_circular(client_s);
    printf("[Server] Connesso Giocatore ID %d (%d/%d attesi)\n", self->id, total_players, TARGET_PLAYERS);

    char welcome[256];
    snprintf(welcome, sizeof(welcome), "[Server] Connesso! Sei il Giocatore ID %d. (%d/%d giocatori in lobby)\nConfigura la flotta...\n", 
             self->id, total_players, TARGET_PLAYERS);
    send(client_s, welcome, strlen(welcome), 0);
    pthread_mutex_unlock(&game_mutex);

    // Ricezione flotta
    bool temp_board[BOARD_SIZE][BOARD_SIZE];
    ssize_t bytes_recv = recv(client_s, temp_board, sizeof(temp_board), MSG_WAITALL);
    if (bytes_recv != sizeof(temp_board)) {
        printf("[Server] Errore flotta da Giocatore %d.\n", self->id);
        close(client_s);
        return NULL;
    }

    pthread_mutex_lock(&game_mutex);
    memcpy(self->board, temp_board, sizeof(temp_board));
    self->is_ready = true;

    int pronti = 0;
    Player* c = head_player;
    do {
        if (c->is_ready) pronti++;
        c = c->next;
    } while (c != head_player);

    printf("[Server] Giocatore %d pronto! (%d/%d pronti)\n", self->id, pronti, TARGET_PLAYERS);

    // La partita comincia solo se TUTTI i TARGET_PLAYERS sono connessi e pronti
    if (total_players == TARGET_PLAYERS && pronti == TARGET_PLAYERS && !game_started) {
        game_started = true;
        current_turn_player = head_player;
        printf("[Server] Tutti i %d giocatori sono pronti! Inizio partita.\n", TARGET_PLAYERS);
        broadcast_message("\n=== TUTTI PRONTI! LA BATTAGLIA HA INIZIO! ===\n");
        pthread_cond_broadcast(&game_cond);
    }

    while (!game_started) {
        pthread_cond_wait(&game_cond, &game_mutex);
    }
    pthread_mutex_unlock(&game_mutex);

    // Loop turni
    while (1) {
        pthread_mutex_lock(&game_mutex);

        if (active_players <= 1) {
            if (self->is_alive) {
                char win_msg[] = "GAME_OVER Hai vinto la battaglia navale!\n";
                send(self->socket_fd, win_msg, strlen(win_msg), 0);
            }
            pthread_mutex_unlock(&game_mutex);
            break;
        }

        while (current_turn_player != self && active_players > 1) {
            pthread_cond_wait(&game_cond, &game_mutex);
        }

        if (active_players <= 1 || !self->is_alive) {
            pthread_mutex_unlock(&game_mutex);
            break;
        }

        // Mostra le griglie nemiche al giocatore di turno
        invia_griglie_avversari(self);

        // Notifica il turno
        char turn_cmd[] = "YOUR_TURN\n";
        send(self->socket_fd, turn_cmd, strlen(turn_cmd), 0);
        pthread_mutex_unlock(&game_mutex);

        // Ricezione mossa
        char buffer[128];
        memset(buffer, 0, sizeof(buffer));
        ssize_t res = recv(self->socket_fd, buffer, sizeof(buffer) - 1, 0);
        if (res <= 0) {
            pthread_mutex_lock(&game_mutex);
            printf("[Server] Giocatore %d disconnesso.\n", self->id);
            self->is_alive = false;
            active_players--;
            advance_turn();
            pthread_cond_broadcast(&game_cond);
            pthread_mutex_unlock(&game_mutex);
            break;
        }

        int target_id, row, col;
        if (sscanf(buffer, "SHOOT %d %d %d", &target_id, &row, &col) == 3) {
            pthread_mutex_lock(&game_mutex);

            Player* target = find_player_by_id(target_id);
            char broadcast_buf[256];

            if (!target || !target->is_alive || target == self) {
                char err_turn[] = "[Server] Bersaglio non valido o già eliminato!\n";
                send(self->socket_fd, err_turn, strlen(err_turn), 0);
            } else if (target->hits_board[row][col] != 0) {
                char err_cell[] = "[Server] Cella già colpita in precedenza! Scegline un'altra.\n";
                send(self->socket_fd, err_cell, strlen(err_cell), 0);
            } else {
                if (target->board[row][col]) {
                    // Colpito
                    target->hits_board[row][col] = 2;
                    target->board[row][col] = false;
                    target->remaining_ships--;

                    snprintf(broadcast_buf, sizeof(broadcast_buf), 
                             "💥 COLPITO! Giocatore %d ha colpito il Giocatore %d in (%d, %d)!\n",
                             self->id, target->id, row, col);
                    broadcast_message(broadcast_buf);

                    if (target->remaining_ships == 0) {
                        target->is_alive = false;
                        active_players--;

                        snprintf(broadcast_buf, sizeof(broadcast_buf),
                                 "☠️ Giocatore %d ha perso tutte le navi ed è stato ELIMINATO!\n",
                                 target->id);
                        broadcast_message(broadcast_buf);

                        char elim_msg[] = "GAME_OVER Tutte le tue navi sono state affondate.\n";
                        send(target->socket_fd, elim_msg, strlen(elim_msg), 0);
                    }

                    if (active_players > 1) {
                        char bonus_msg[] = "🎉 Nave colpita! Hai diritto a un altro attacco!\n";
                        send(self->socket_fd, bonus_msg, strlen(bonus_msg), 0);
                    }
                } else {
                    // Acqua
                    target->hits_board[row][col] = 1;

                    snprintf(broadcast_buf, sizeof(broadcast_buf),
                             "💧 ACQUA! Giocatore %d ha sparato a vuoto su Giocatore %d in (%d, %d).\n",
                             self->id, target->id, row, col);
                    broadcast_message(broadcast_buf);

                    advance_turn();
                }
            }

            pthread_cond_broadcast(&game_cond);
            pthread_mutex_unlock(&game_mutex);
        }
    }

    close(self->socket_fd);
    return NULL;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    srand(time(NULL));

    int s = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = INADDR_ANY
    };

    bind(s, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(s, 30);

    printf("[Server] In ascolto sulla porta %d. Giocatori richiesti per iniziare: %d\n", PORT, TARGET_PLAYERS);

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_s = accept(s, (struct sockaddr*)&client_addr, &client_len);
        if (client_s < 0) continue;

        int* client_s_cpy = (int*)malloc(sizeof(int));
        *client_s_cpy = client_s;

        pthread_t thread;
        if (pthread_create(&thread, NULL, client_handler, (void*)client_s_cpy) < 0) {
            free(client_s_cpy);
            close(client_s);
            continue;
        }
        pthread_detach(thread);
    }

    close(s);
    return 0;
}