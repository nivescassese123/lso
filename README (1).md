# Tris – Gioco del Tris Multiplayer via TCP

Progetto di Laboratorio Sistemi Operativi.  
Server multi-client in C per giocare a Tris (Tic-Tac-Toe) su rete TCP.

---

## Struttura del progetto

```
tris/
├── server/
│   ├── include/        # Header files
│   │   ├── match.h
│   │   ├── net.h
│   │   ├── protocol.h
│   │   └── state.h
│   ├── src/            # Sorgenti server
│   │   ├── main.c
│   │   ├── match.c
│   │   ├── net.c
│   │   ├── protocol.c
│   │   └── state.c
│   └── Makefile
├── client/
│   ├── src/            # Sorgente client
│   │   └── client.c
│   └── Makefile
└── README.md
```

---

## Requisiti

- Sistema operativo Linux (o WSL su Windows)
- GCC con supporto pthread
- make

---

## Compilazione

### Server

```bash
cd tris/server
make
```

Produce l'eseguibile `server` nella cartella `tris/server/`.

### Client

```bash
cd tris/client
make
```

Produce l'eseguibile `client` nella cartella `tris/client/`.

### Pulizia

```bash
make clean   # nella cartella server o client
```

---

## Esecuzione

### Avviare il server

```bash
cd tris/server
./server <porta>

# Esempio:
./server 12345
```

### Avviare un client (terminale separato)

```bash
cd tris/client
./client <ip_server> <porta>

# Esempio in locale:
./client 127.0.0.1 12345
```

Aprire più terminali per simulare più giocatori.

---

## Protocollo – Comandi disponibili

Dopo la connessione il client riceve un messaggio di benvenuto e deve fare il login.

### Login

| Comando | Descrizione |
|---------|-------------|
| `LOGIN <nome>` | Accede al server con il nome scelto |

### Lobby

| Comando | Descrizione |
|---------|-------------|
| `WHOAMI` | Mostra il proprio nome |
| `USERS` | Lista dei giocatori connessi |
| `CREATE` | Crea una nuova partita (si diventa owner, si gioca come X) |
| `LIST` | Lista delle partite disponibili |
| `JOIN <id>` | Richiede di unirsi alla partita con quell'ID |
| `QUIT` | Disconnette dal server |

### Gestione richieste (solo owner)

| Comando | Descrizione |
|---------|-------------|
| `ACCEPT <id>` | Accetta la richiesta di join per la partita |
| `REJECT <id>` | Rifiuta la richiesta di join |

### Partita in corso

| Comando | Descrizione |
|---------|-------------|
| `MOVE <riga> <colonna>` | Esegue una mossa (riga e colonna da 0 a 2) |
| `BOARD` | Mostra la board corrente |
| `RESIGN` | Abbandona la partita (si perde, l'avversario vince) |

### Fine partita

| Comando | Descrizione |
|---------|-------------|
| `REMATCH` | Richiede una nuova partita (solo vincitore, o entrambi in caso di pareggio) |

---

## Esempio di sessione completa

```
# Client A
> LOGIN alice
OK LOGIN alice
> CREATE
OK MATCH_CREATED 1

# Client B
> LOGIN bob
OK LOGIN bob
> LIST
MATCH 1 owner=alice status=WAITING
> JOIN 1
OK JOIN_REQUESTED

# Client A riceve:
EVENT JOIN_REQUEST 1 bob
> ACCEPT 1
OK MATCH_STARTED 1 vs bob (YOU=X)

# Client B riceve:
OK MATCH_STARTED 1 vs alice (YOU=O)

# Turno di alice (X)
> MOVE 0 0
OK MOVED

# Turno di bob (O)
> MOVE 1 1
OK MOVED

# ... partita continua ...

# Fine partita – alice vince
EVENT YOU_WIN
EVENT WINNER alice
EVENT GAME_OVER Hai vinto! Digita REMATCH per aprire una nuova partita

# alice può fare rematch
> REMATCH
OK REMATCH_CREATED 2 Sei owner (X), attendi un avversario
```

---

## Griglia di gioco

Le righe e colonne sono numerate da 0 a 2:

```
     col0  col1  col2
riga0  0,0 | 0,1 | 0,2
       ----+-----+----
riga1  1,0 | 1,1 | 1,2
       ----+-----+----
riga2  2,0 | 2,1 | 2,2
```

Esempio: `MOVE 0 0` posiziona il simbolo nell'angolo in alto a sinistra.

---

## Note

- Il server supporta fino a 128 client connessi contemporaneamente
- Ogni client viene gestito da un thread dedicato
- Un giocatore può giocare solo una partita alla volta
- In caso di disconnessione durante una partita, l'avversario vince automaticamente
- I broadcast informano tutti i giocatori connessi dei cambiamenti di stato delle partite
