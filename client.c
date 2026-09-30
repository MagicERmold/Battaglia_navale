#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <time.h>

#define SERVER_IP "127.0.0.1"
#define PORT 5587
#define BOARD_SIZE 10
#define NUM_SHIPS 5

static void svuota_buffer(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

// Wrapper per send robusta a interruzioni da segnale
static ssize_t send_all(int sockfd, const void *buf, size_t len) {
    size_t total_sent = 0;
    const char *ptr = (const char *)buf;

    while (total_sent < len) {
        ssize_t sent = send(sockfd, ptr + total_sent, len - total_sent, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (sent == 0) {
            return 0;
        }
        total_sent += (size_t)sent;
    }
    return (ssize_t)total_sent;
}

// Wrapper per recv robusta a interruzioni da segnale
static ssize_t recv_interruptible(int sockfd, void *buf, size_t len, int flags) {
    while (1) {
        ssize_t res = recv(sockfd, buf, len, flags);
        if (res < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        return res;
    }
}

static void stampa_griglia_personale(bool board[BOARD_SIZE][BOARD_SIZE]) {
    printf("\n--- LA TUA FLOTTA ---\n");
    printf("   | ");
    for (int j = 0; j < BOARD_SIZE; j++) printf("%2d ", j);
    printf("\n---+------------------------------------\n");
    for (int i = 0; i < BOARD_SIZE; i++) {
        printf("%2d | ", i);
        for (int j = 0; j < BOARD_SIZE; j++) {
            if (board[i][j]) {
                printf("🚢 ");
            } else {
                printf(" . ");
            }
        }
        printf("\n");
    }
    printf("---+------------------------------------\n\n");
}

static void genera_griglia_random(bool board[BOARD_SIZE][BOARD_SIZE]) {
    memset(board, false, BOARD_SIZE * BOARD_SIZE * sizeof(bool));
    int posizionate = 0;
    while (posizionate < NUM_SHIPS) {
        int r = rand() % BOARD_SIZE;
        int c = rand() % BOARD_SIZE;
        if (!board[r][c]) {
            board[r][c] = true;
            posizionate++;
        }
    }
}

static void inserisci_griglia_manuale(bool board[BOARD_SIZE][BOARD_SIZE]) {
    memset(board, false, BOARD_SIZE * BOARD_SIZE * sizeof(bool));
    stampa_griglia_personale(board);

    int posizionate = 0;
    while (posizionate < NUM_SHIPS) {
        int r = -1, c = -1;
        printf("Posiziona nave %d/%d (riga colonna [0-9]): ", posizionate + 1, NUM_SHIPS);
        int scanned = scanf("%d %d", &r, &c);
        if (scanned != 2) {
            svuota_buffer();
            printf("Formato input non valido. Inserire due interi.\n");
            continue;
        }
        svuota_buffer();

        if (r < 0 || r >= BOARD_SIZE || c < 0 || c >= BOARD_SIZE) {
            printf("Coordinate fuori scala (0-%d)!\n", BOARD_SIZE - 1);
            continue;
        }
        if (board[r][c]) {
            printf("Cella già occupata!\n");
            continue;
        }

        board[r][c] = true;
        posizionate++;
        stampa_griglia_personale(board);
    }
}

static void configura_griglia(bool board[BOARD_SIZE][BOARD_SIZE]) {
    char scelta;
    while (1) {
        printf("\nConfigurazione Flotta:\n");
        printf("[1] Generazione casuale (%d navi)\n", NUM_SHIPS);
        printf("[2] Posizionamento manuale\n");
        printf("Scelta: ");
        if (scanf(" %c", &scelta) != 1) {
            svuota_buffer();
            continue;
        }
        svuota_buffer();

        if (scelta == '1') {
            genera_griglia_random(board);
            printf("Griglia confermata:\n");
            stampa_griglia_personale(board);
            break;
        } else if (scelta == '2') {
            inserisci_griglia_manuale(board);
            break;
        } else {
            printf("Scelta non valida.\n");
        }
    }
}

static void unisciti_partita(int sockfd) {
    bool mia_griglia[BOARD_SIZE][BOARD_SIZE];
    configura_griglia(mia_griglia);

    if (send_all(sockfd, mia_griglia, sizeof(mia_griglia)) <= 0) {
        perror("[Client] Errore durante l'invio della flotta");
        return;
    }
    printf("Flotta inviata! In attesa degli altri partecipanti...\n");

    char buffer[4096];
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_recv = recv_interruptible(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_recv < 0) {
            perror("[Client] Errore ricezione dati dal server");
            break;
        }
        if (bytes_recv == 0) {
            printf("\nIl server ha chiuso la connessione.\n");
            break;
        }

        char* turn_pos = strstr(buffer, "YOUR_TURN");
        if (turn_pos != NULL) {
            *turn_pos = '\0';
            if (strlen(buffer) > 0) {
                printf("%s", buffer);
            }

            int target_id = -1, row = -1, col = -1;
            printf("\n👉 È IL TUO TURNO! Scegli chi attaccare.\n");
            while (1) {
                printf("Inserisci [ID_Giocatore] [Riga] [Colonna]: ");
                int read_count = scanf("%d %d %d", &target_id, &row, &col);
                if (read_count == 3) {
                    svuota_buffer();
                    if (row >= 0 && row < BOARD_SIZE && col >= 0 && col < BOARD_SIZE && target_id > 0) {
                        break;
                    }
                    printf("Dati non validi: coordinate 0-%d e ID > 0.\n", BOARD_SIZE - 1);
                } else {
                    svuota_buffer();
                    printf("Formato errato! Esempio valido: 2 4 5\n");
                }
            }

            char shoot_cmd[64];
            int cmd_len = snprintf(shoot_cmd, sizeof(shoot_cmd), "SHOOT %d %d %d\n", target_id, row, col);
            if (cmd_len > 0) {
                if (send_all(sockfd, shoot_cmd, (size_t)cmd_len) < 0) {
                    perror("[Client] Errore invio mossa di attacco");
                    break;
                }
            }

        } else if (strncmp(buffer, "GAME_OVER", 9) == 0) {
            printf("\n--- PARTITA TERMINATA ---\n%s\n", buffer + 10);
            break;
        } else {
            printf("%s", buffer);
        }
    }
}

int main(int argc, char* argv[]) {
    srand((unsigned int)time(NULL));
    const char* target_ip = SERVER_IP;

    if (argc > 1) {
        target_ip = argv[1];
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("[Client] Creazione socket fallita");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, target_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "[Client] Indirizzo IP '%s' non valido o non supportato\n", target_ip);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Tentativo di connessione a %s:%d...\n", target_ip, PORT);
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("[Client] Connessione al server fallita");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    printf("Connessione stabilita con successo!\n");

    char scelta;
    while (1) {
        printf("\n=== MENU BATTAGLIA NAVALE ===\n");
        printf("[a] Gioca\n");
        printf("[q] Esci 👋\n");
        printf("Scegli un'opzione: ");
        if (scanf(" %c", &scelta) != 1) {
            svuota_buffer();
            continue;
        }
        svuota_buffer();

        switch (scelta) {
            case 'a':
                unisciti_partita(sockfd);
                if (close(sockfd) < 0) {
                    perror("[Client] Chiusura socket fallita");
                }
                return 0;
            case 'q':
                if (close(sockfd) < 0) {
                    perror("[Client] Chiusura socket fallita");
                }
                printf("Arrivederci!\n");
                return 0;
            default:
                printf("Scelta non valida.\n");
        }
    }
}
