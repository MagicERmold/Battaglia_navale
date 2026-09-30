#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>

#define SERVER_IP "127.0.0.1"
#define PORT 5587
#define BOARD_SIZE 10
#define NUM_SHIPS 5

void svuota_buffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

void stampa_griglia_personale(bool board[BOARD_SIZE][BOARD_SIZE]) {
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

void genera_griglia_random(bool board[BOARD_SIZE][BOARD_SIZE]) {
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

void inserisci_griglia_manuale(bool board[BOARD_SIZE][BOARD_SIZE]) {
    memset(board, false, BOARD_SIZE * BOARD_SIZE * sizeof(bool));
    stampa_griglia_personale(board);

    int posizionate = 0;
    while (posizionate < NUM_SHIPS) {
        int r, c;
        printf("Posiziona nave %d/%d (riga colonna [0-9]): ", posizionate + 1, NUM_SHIPS);
        if (scanf("%d %d", &r, &c) != 2) {
            svuota_buffer();
            printf("Input non valido.\n");
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

void configura_griglia(bool board[BOARD_SIZE][BOARD_SIZE]) {
    char scelta;
    while (1) {
        printf("\nConfigurazione Flotta:\n");
        printf("[1] Generazione casuale (%d navi)\n", NUM_SHIPS);
        printf("[2] Posizionamento manuale\n");
        printf("Scelta: ");
        if (scanf(" %c", &scelta) != 1) continue;
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

void unisciti_partita(int sockfd) {
    bool mia_griglia[BOARD_SIZE][BOARD_SIZE];
    configura_griglia(mia_griglia);

    if (send(sockfd, mia_griglia, sizeof(mia_griglia), 0) <= 0) {
        perror("Errore invio flotta");
        return;
    }
    printf("Flotta inviata! In attesa degli altri partecipanti...\n");

    char buffer[4096];
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_recv = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_recv <= 0) {
            printf("\nConnessione con il server interrotta.\n");
            break;
        }

        // Se nel messaggio c'è YOUR_TURN, stampiamo prima il testo (che contiene le tabelle)
        // e poi chiediamo l'input
        char* turn_pos = strstr(buffer, "YOUR_TURN");
        if (turn_pos != NULL) {
            *turn_pos = '\0'; // Separa e stampa tutto ciò che precede (le tabelle)
            if (strlen(buffer) > 0) {
                printf("%s", buffer);
            }

            int target_id, row, col;
            printf("\n👉 È IL TUO TURNO! Scegli chi attaccare.\n");
            while (1) {
                printf("Inserisci [ID_Giocatore] [Riga] [Colonna]: ");
                if (scanf("%d %d %d", &target_id, &row, &col) == 3) {
                    svuota_buffer();
                    if (row >= 0 && row < BOARD_SIZE && col >= 0 && col < BOARD_SIZE) {
                        break;
                    }
                    printf("Coordinate non valide (devono essere 0-%d).\n", BOARD_SIZE - 1);
                } else {
                    svuota_buffer();
                    printf("Formato errato! Esempio: 2 4 5\n");
                }
            }

            char shoot_cmd[64];
            snprintf(shoot_cmd, sizeof(shoot_cmd), "SHOOT %d %d %d\n", target_id, row, col);
            send(sockfd, shoot_cmd, strlen(shoot_cmd), 0);

        } else if (strncmp(buffer, "GAME_OVER", 9) == 0) {
            printf("\n--- PARTITA TERMINATA ---\n%s\n", buffer + 10);
            break;
        } else {
            // Messaggi generici di stato e broadcast
            printf("%s", buffer);
        }
    }
}

int main(int argc, char* argv[]) {
    srand(time(NULL));
    const char* target_ip = SERVER_IP;

    if (argc > 1) {
        target_ip = argv[1];
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Errore socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = inet_addr(target_ip)
    };

    printf("Tentativo di connessione a %s:%d...\n", target_ip, PORT);
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Errore di connessione");
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
        if (scanf(" %c", &scelta) != 1) continue;
        svuota_buffer();

        switch (scelta) {
            case 'a':
                unisciti_partita(sockfd);
                close(sockfd);
                return 0;
            case 'q':
                close(sockfd);
                printf("Arrivederci!\n");
                return 0;
            default:
                printf("Scelta non valida.\n");
        }
    }
}