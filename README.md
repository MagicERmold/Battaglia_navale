# Battaglia Navale Multiutente (Client-Server in C)

Progetto per il corso di **Sistemi Operativi**: implementazione di una versione elettronica multiutente del gioco della **Battaglia Navale** basata su architettura Client-Server mediante socket stream TCP e thread POSIX (`pthread`).

---

## 📋 Descrizione del Progetto

Il sistema supporta un numero arbitrario e configurabile di giocatori distribuiti su macchine distinte o sulla stessa macchina locale.

### Caratteristiche Principali
* **Architettura Concorrente Multi-thread**: il server alloca un thread POSIX dedicato alla gestione di ciascun client connesso, sincronizzando l'accesso alle risorse condivise con mutex (`pthread_mutex_t`) e condition variables (`pthread_cond_t`).
* **Gestione dei Turni con Lista Circolare**: i partecipanti sono memorizzati in una lista collegata circolare; al termine di ciascuna mossa valida il turno viene inoltrato in sequenza al giocatore successivo ancora attivo.
* **Setup della Flotta**: ciascun client può scegliere se generare la posizione delle proprie navi in modo casuale o immetterle manualmente tramite coordinate da terminale.
* **Consultazione Griglie Avversarie**: all'inizio di ogni turno il giocatore visualizza lo stato noto dei tabelloni di tutti gli altri concorrenti:
  * `~` : Cella non ancora attaccata (inesplorata)
  * `❌` : Colpo terminato in acqua
  * `✅` : Nave avversaria colpita
* **Mantenimento del Turno**: se un attacco va a segno (nave colpita), il giocatore ottiene un tiro bonus consecutivo senza passare il turno.
* **Notifiche Broadcast ed Eliminazioni**: ogni evento di gioco (colpi a segno, acqua, eliminazione per affondamento totale o vittoria finale) viene trasmesso in tempo reale a tutti i partecipanti collegati.

---

## 🛠️ Requisiti di Sistema

* Sistema Operativo: Linux / macOS / ambiente POSIX
* Compilatore: `gcc`
* Libreria: POSIX Threads (`-pthread`)
* Strumento di build: GNU `make`

---

## 📁 Struttura della Repository

```text
.
├── client.c      # Logica del client, interfaccia utente CLI e input
├── server.c      # Server multi-thread, gestione turni, sincronizzazione e stato di gioco
├── Makefile      # Regole di compilazione e pulizia dei file binari
└── README.md     # Documentazione del progetto