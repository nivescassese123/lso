#ifndef MATCH_H
#define MATCH_H

#include <pthread.h>
#include "state.h"

#define MAX_MATCHES 128

typedef enum {
    MATCH_WAITING  = 0,   /* creata, in attesa di un avversario    */
    MATCH_PENDING  = 1,   /* richiesta di join pendente            */
    MATCH_PLAYING  = 2,   /* partita in corso                      */
    MATCH_FINISHED = 3,   /* terminata (slot liberato → id=0)      */
    MATCH_REMATCH  = 4    /* fine partita, in attesa di rematch    */
} match_status_t;

typedef struct {
    int            id;
    match_status_t status;

    int owner_fd;      /* creatore (X)                   */
    int joiner_fd;     /* avversario (O)                 */
    int pending_fd;    /* richiesta join in attesa       */

    char board[3][3];  /* 'X', 'O', ' '                 */
    int  turn;         /* 0=X(owner), 1=O(joiner)        */

    /* ---- REMATCH ---- */
    /*
     * Quando una partita finisce entra in stato MATCH_REMATCH.
     * Chi vuole rigiocare invia REMATCH: diventa il nuovo owner
     * della prossima partita e attende che l'avversario accetti.
     *
     * rematch_requester_fd : chi ha chiesto per primo il rematch (-1 se nessuno)
     * rematch_other_fd     : l'altro giocatore (quello che deve rispondere)
     */
    int rematch_requester_fd;
    int rematch_other_fd;
} match_t;

typedef struct {
    pthread_mutex_t mtx;
    int             next_id;
    match_t         matches[MAX_MATCHES];
} match_store_t;

/* ------------------------------------------------------------------ */
/*  Init                                                                */
/* ------------------------------------------------------------------ */
void matches_init(match_store_t *ms);

/* ------------------------------------------------------------------ */
/*  Lobby                                                               */
/* ------------------------------------------------------------------ */
int  matches_create(match_store_t *ms, int owner_fd);
void matches_list(match_store_t *ms, server_state_t *st, char *out, int outsz);

/* ------------------------------------------------------------------ */
/*  JOIN flow                                                           */
/* ------------------------------------------------------------------ */
int matches_request_join(match_store_t *ms, int match_id, int joiner_fd,
                         int *owner_fd_out);
int matches_accept(match_store_t *ms, int match_id, int owner_fd,
                   int *joiner_fd_out);
int matches_reject(match_store_t *ms, int match_id, int owner_fd,
                   int *rejected_fd_out);

/* ------------------------------------------------------------------ */
/*  Gioco                                                               */
/* ------------------------------------------------------------------ */
/*
 * matches_move → ritorna:
 *   0  mossa ok, partita continua
 *   1  win
 *   2  draw
 *  -1  match non trovato
 *  -2  match non in PLAYING
 *  -3  non sei un giocatore
 *  -4  non è il tuo turno
 *  -5  mossa non valida
 */
int matches_move(match_store_t *ms, server_state_t *st,
                 int match_id, int player_fd, int r, int c,
                 int *opponent_fd_out,
                 char *board_out, int board_outsz,
                 char *winner_name_out, int winner_name_sz);

int matches_board(match_store_t *ms, int match_id, char *out, int outsz);

/*
 * matches_resign → ritorna:
 *   0  ok
 *  -1  match non trovato
 *  -2  match non in PLAYING
 *  -3  non sei un giocatore
 *  -4  nessun avversario
 */
int matches_resign(match_store_t *ms, server_state_t *st,
                   int match_id, int player_fd,
                   int *opponent_fd_out,
                   char *board_out, int board_outsz,
                   char *winner_name_out, int winner_name_sz);

/* ------------------------------------------------------------------ */
/*  REMATCH                                                             */
/* ------------------------------------------------------------------ */
/*
 * matches_rematch → chiamato quando un giocatore digita REMATCH.
 *
 * Ritorna:
 *   0  ok, richiesta registrata — aspetta l'avversario
 *         (*other_fd_out = fd dell'avversario da notificare)
 *   1  entrambi hanno accettato — nuova partita creata
 *         (*new_match_id_out = id della nuova partita)
 *         (*new_owner_fd_out = chi diventa X, cioè chi ha chiesto per primo)
 *         (*new_joiner_fd_out = l'altro)
 *  -1  match non trovato / non in stato REMATCH
 *  -2  non sei uno dei due giocatori
 *  -3  hai già chiesto il rematch
 */
int matches_rematch(match_store_t *ms, int match_id, int player_fd,
                    int *other_fd_out,
                    int *new_match_id_out,
                    int *new_owner_fd_out,
                    int *new_joiner_fd_out);

/*
 * Ritorna il match_id dell'eventuale partita REMATCH in cui
 * player_fd è coinvolto (-1 se non ce n'è).
 * Usato da main.c per trovare il match dopo fine partita.
 */
int matches_find_rematch(match_store_t *ms, int player_fd);

/* ------------------------------------------------------------------ */
/*  Disconnect                                                          */
/* ------------------------------------------------------------------ */
void matches_on_disconnect(match_store_t *ms, server_state_t *st, int fd);

#endif 