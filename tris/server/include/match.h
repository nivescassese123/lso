#ifndef MATCH_H
#define MATCH_H

#include <pthread.h>
#include "state.h"

#define MAX_MATCHES 128

typedef enum {
    MATCH_WAITING  = 0,
    MATCH_PENDING  = 1,
    MATCH_PLAYING  = 2,
    MATCH_FINISHED = 3,
    MATCH_REMATCH  = 4    /* fine partita, slot ancora vivo per tracciare risultato */
} match_status_t;

typedef struct {
    int            id;
    match_status_t status;

    int owner_fd;
    int joiner_fd;
    int pending_fd;

    char board[3][3];
    int  turn;         /* 0=X(owner), 1=O(joiner) */

    /*
     * Risultato — valorizzati quando si entra in MATCH_REMATCH.
     * winner_fd = fd vincitore  (-1 se pareggio)
     * loser_fd  = fd perdente   (-1 se pareggio)
     * draw      = 1 se pareggio
     */
    int winner_fd;
    int loser_fd;
    int draw;
} match_t;

typedef struct {
    pthread_mutex_t mtx;
    int             next_id;
    match_t         matches[MAX_MATCHES];
} match_store_t;

/* Init */
void matches_init(match_store_t *ms);

/* Lobby */
int  matches_create(match_store_t *ms, int owner_fd);
void matches_list(match_store_t *ms, server_state_t *st, char *out, int outsz);

/* JOIN flow */
int matches_request_join(match_store_t *ms, int match_id, int joiner_fd,
                         int *owner_fd_out);
int matches_accept(match_store_t *ms, int match_id, int owner_fd,
                   int *joiner_fd_out);
int matches_reject(match_store_t *ms, int match_id, int owner_fd,
                   int *rejected_fd_out);

/* Gioco */
int matches_move(match_store_t *ms, server_state_t *st,
                 int match_id, int player_fd, int r, int c,
                 int *opponent_fd_out,
                 char *board_out, int board_outsz,
                 char *winner_name_out, int winner_name_sz);

int matches_board(match_store_t *ms, int match_id, char *out, int outsz);

int matches_resign(match_store_t *ms, server_state_t *st,
                   int match_id, int player_fd,
                   int *opponent_fd_out,
                   char *board_out, int board_outsz,
                   char *winner_name_out, int winner_name_sz);

/*
 * REMATCH — logica conforme alla traccia:
 *
 *  Il vincitore (o chiunque in caso di pareggio) fa REMATCH:
 *  → viene creata una NUOVA partita in WAITING con lui come owner (X)
 *  → broadcast a tutti: chiunque può fare JOIN
 *  → il vecchio slot MATCH_REMATCH viene liberato
 *
 *  Il perdente non può fare REMATCH.
 *
 * Ritorna:
 *   new_id >= 1  : nuova partita creata, ritorna il nuovo match_id
 *   -1           : match non trovato / non in MATCH_REMATCH
 *   -2           : non sei un giocatore di questa partita
 *   -3           : sei il perdente, non puoi fare rematch
 *   -4           : nessuno slot libero
 */
int matches_rematch(match_store_t *ms, int match_id, int player_fd);

/*
 * Cerca la partita in stato MATCH_REMATCH in cui player_fd è coinvolto.
 * Ritorna il match_id oppure -1.
 */
int matches_find_rematch(match_store_t *ms, int player_fd);

/* Disconnect */
void matches_on_disconnect(match_store_t *ms, server_state_t *st, int fd);

#endif /* MATCH_H */