#include "match.h"
#include "state.h"
#include "net.h"
#include "protocol.h"
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  Helpers interni                                                     */
/* ------------------------------------------------------------------ */

static void board_clear(char b[3][3]) {
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            b[r][c] = ' ';
}

static int check_winner(char b[3][3], char ch) {
    for (int i = 0; i < 3; i++) {
        if (b[i][0]==ch && b[i][1]==ch && b[i][2]==ch) return 1;
        if (b[0][i]==ch && b[1][i]==ch && b[2][i]==ch) return 1;
    }
    if (b[0][0]==ch && b[1][1]==ch && b[2][2]==ch) return 1;
    if (b[0][2]==ch && b[1][1]==ch && b[2][0]==ch) return 1;
    return 0;
}

static int board_full(char b[3][3]) {
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            if (b[r][c] == ' ') return 0;
    return 1;
}

static void render_board(const match_t *m, char *out, int outsz) {
    snprintf(out, outsz,
        "Board (match %d):\n"
        " %c | %c | %c \n"
        "-----------\n"
        " %c | %c | %c \n"
        "-----------\n"
        " %c | %c | %c \n"
        "Turno: %s\n",
        m->id,
        m->board[0][0], m->board[0][1], m->board[0][2],
        m->board[1][0], m->board[1][1], m->board[1][2],
        m->board[2][0], m->board[2][1], m->board[2][2],
        (m->status == MATCH_PLAYING)
            ? (m->turn == 0 ? "X (owner)" : "O (joiner)")
            : "-"
    );
}

static match_t *find_free_slot(match_store_t *ms) {
    for (int i = 0; i < MAX_MATCHES; i++)
        if (ms->matches[i].id == 0) return &ms->matches[i];
    return NULL;
}

static match_t *find_match(match_store_t *ms, int match_id) {
    for (int i = 0; i < MAX_MATCHES; i++)
        if (ms->matches[i].id == match_id) return &ms->matches[i];
    return NULL;
}

static void match_reset(match_t *m) {
    m->id        = 0;
    m->status    = MATCH_FINISHED;
    m->owner_fd  = -1;
    m->joiner_fd = -1;
    m->pending_fd= -1;
    m->winner_fd = -1;
    m->loser_fd  = -1;
    m->draw      = 0;
    m->turn      = 0;
    board_clear(m->board);
}

/* ------------------------------------------------------------------ */
/*  Inizializzazione                                                    */
/* ------------------------------------------------------------------ */

void matches_init(match_store_t *ms) {
    pthread_mutex_init(&ms->mtx, NULL);
    ms->next_id = 1;
    for (int i = 0; i < MAX_MATCHES; i++)
        match_reset(&ms->matches[i]);
}

/* ------------------------------------------------------------------ */
/*  CREATE                                                              */
/* ------------------------------------------------------------------ */

int matches_create(match_store_t *ms, int owner_fd) {
    pthread_mutex_lock(&ms->mtx);
    match_t *m = find_free_slot(ms);
    if (!m) { pthread_mutex_unlock(&ms->mtx); return -1; }

    match_reset(m);
    m->id       = ms->next_id++;
    m->status   = MATCH_WAITING;
    m->owner_fd = owner_fd;

    int id = m->id;
    pthread_mutex_unlock(&ms->mtx);
    return id;
}

/* ------------------------------------------------------------------ */
/*  LIST                                                                */
/* ------------------------------------------------------------------ */

void matches_list(match_store_t *ms, server_state_t *st, char *out, int outsz) {
    pthread_mutex_lock(&ms->mtx);

    char *p    = out;
    int   left = outsz;
    int   found = 0;

    for (int i = 0; i < MAX_MATCHES; i++) {
        match_t *m = &ms->matches[i];
        if (m->id == 0) continue;

        char owner_name[MAX_NAME] = "??";
        state_get_name_copy(st, m->owner_fd, owner_name, sizeof(owner_name));

        const char *ss;
        switch (m->status) {
            case MATCH_WAITING:  ss = "WAITING";  break;
            case MATCH_PENDING:  ss = "PENDING";  break;
            case MATCH_PLAYING:  ss = "PLAYING";  break;
            case MATCH_FINISHED: ss = "FINISHED"; break;
            case MATCH_REMATCH:  ss = "FINISHED"; break; /* dall'esterno è finita */
            default:             ss = "UNKNOWN";  break;
        }

        int n = snprintf(p, left, "MATCH %d owner=%s status=%s\n",
                         m->id, owner_name, ss);
        if (n > 0 && n < left) { p += n; left -= n; }
        found = 1;
    }

    if (!found) snprintf(out, outsz, PROTO_NO_MATCHES);
    pthread_mutex_unlock(&ms->mtx);
}

/* ------------------------------------------------------------------ */
/*  JOIN flow                                                           */
/* ------------------------------------------------------------------ */

int matches_request_join(match_store_t *ms, int match_id,
                         int joiner_fd, int *owner_fd_out) {
    pthread_mutex_lock(&ms->mtx);
    match_t *m = find_match(ms, match_id);
    if (!m) { pthread_mutex_unlock(&ms->mtx); return -1; }
    if (m->owner_fd == joiner_fd) { pthread_mutex_unlock(&ms->mtx); return -3; }
    if (m->status != MATCH_WAITING) { pthread_mutex_unlock(&ms->mtx); return -2; }

    m->status     = MATCH_PENDING;
    m->pending_fd = joiner_fd;
    *owner_fd_out = m->owner_fd;
    pthread_mutex_unlock(&ms->mtx);
    return 0;
}

int matches_accept(match_store_t *ms, int match_id,
                   int owner_fd, int *joiner_fd_out) {
    pthread_mutex_lock(&ms->mtx);
    match_t *m = find_match(ms, match_id);
    if (!m) { pthread_mutex_unlock(&ms->mtx); return -1; }
    if (m->owner_fd != owner_fd) { pthread_mutex_unlock(&ms->mtx); return -2; }
    if (m->status != MATCH_PENDING || m->pending_fd == -1) {
        pthread_mutex_unlock(&ms->mtx); return -3;
    }

    *joiner_fd_out = m->pending_fd;
    m->joiner_fd   = m->pending_fd;
    m->pending_fd  = -1;
    m->status      = MATCH_PLAYING;
    m->turn        = 0;
    board_clear(m->board);
    pthread_mutex_unlock(&ms->mtx);
    return 0;
}

int matches_reject(match_store_t *ms, int match_id,
                   int owner_fd, int *rejected_fd_out) {
    pthread_mutex_lock(&ms->mtx);
    match_t *m = find_match(ms, match_id);
    if (!m) { pthread_mutex_unlock(&ms->mtx); return -1; }
    if (m->owner_fd != owner_fd) { pthread_mutex_unlock(&ms->mtx); return -2; }
    if (m->status != MATCH_PENDING || m->pending_fd == -1) {
        pthread_mutex_unlock(&ms->mtx); return -3;
    }

    *rejected_fd_out = m->pending_fd;
    m->pending_fd    = -1;
    m->status        = MATCH_WAITING;
    pthread_mutex_unlock(&ms->mtx);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  MOVE                                                                */
/* ------------------------------------------------------------------ */

int matches_move(match_store_t *ms, server_state_t *st,
                 int match_id, int player_fd, int r, int c,
                 int *opponent_fd_out,
                 char *board_out, int board_outsz,
                 char *winner_name_out, int winner_name_sz) {
    pthread_mutex_lock(&ms->mtx);
    match_t *m = find_match(ms, match_id);
    if (!m) { pthread_mutex_unlock(&ms->mtx); return -1; }
    if (m->status != MATCH_PLAYING) { pthread_mutex_unlock(&ms->mtx); return -2; }

    int is_owner  = (m->owner_fd  == player_fd);
    int is_joiner = (m->joiner_fd == player_fd);
    if (!is_owner && !is_joiner) { pthread_mutex_unlock(&ms->mtx); return -3; }
    if (m->turn != (is_owner ? 0 : 1)) { pthread_mutex_unlock(&ms->mtx); return -4; }
    if (r < 0 || r > 2 || c < 0 || c > 2) { pthread_mutex_unlock(&ms->mtx); return -5; }
    if (m->board[r][c] != ' ') { pthread_mutex_unlock(&ms->mtx); return -5; }

    char mark        = is_owner ? 'X' : 'O';
    m->board[r][c]   = mark;
    *opponent_fd_out = is_owner ? m->joiner_fd : m->owner_fd;

    int result = 0;
    if (check_winner(m->board, mark)) {
        m->winner_fd = player_fd;
        m->loser_fd  = *opponent_fd_out;
        m->draw      = 0;
        m->status    = MATCH_REMATCH;
        if (winner_name_out)
            state_get_name_copy(st, player_fd, winner_name_out, winner_name_sz);
        result = 1;
    } else if (board_full(m->board)) {
        m->winner_fd = -1;
        m->loser_fd  = -1;
        m->draw      = 1;
        m->status    = MATCH_REMATCH;
        result = 2;
    } else {
        m->turn = 1 - m->turn;
    }

    render_board(m, board_out, board_outsz);
    pthread_mutex_unlock(&ms->mtx);
    return result;
}

/* ------------------------------------------------------------------ */
/*  BOARD                                                               */
/* ------------------------------------------------------------------ */

int matches_board(match_store_t *ms, int match_id, char *out, int outsz) {
    pthread_mutex_lock(&ms->mtx);
    match_t *m = find_match(ms, match_id);
    if (!m) { pthread_mutex_unlock(&ms->mtx); return -1; }
    render_board(m, out, outsz);
    pthread_mutex_unlock(&ms->mtx);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  RESIGN                                                              */
/* ------------------------------------------------------------------ */

int matches_resign(match_store_t *ms, server_state_t *st,
                   int match_id, int player_fd,
                   int *opponent_fd_out,
                   char *board_out, int board_outsz,
                   char *winner_name_out, int winner_name_sz) {
    pthread_mutex_lock(&ms->mtx);
    match_t *m = find_match(ms, match_id);
    if (!m) { pthread_mutex_unlock(&ms->mtx); return -1; }
    if (m->status != MATCH_PLAYING) { pthread_mutex_unlock(&ms->mtx); return -2; }

    int is_owner  = (m->owner_fd  == player_fd);
    int is_joiner = (m->joiner_fd == player_fd);
    if (!is_owner && !is_joiner) { pthread_mutex_unlock(&ms->mtx); return -3; }

    int opp_fd = is_owner ? m->joiner_fd : m->owner_fd;
    if (opp_fd == -1) { pthread_mutex_unlock(&ms->mtx); return -4; }

    /* Chi fa resign perde */
    m->winner_fd     = opp_fd;
    m->loser_fd      = player_fd;
    m->draw          = 0;
    *opponent_fd_out = opp_fd;
    m->status        = MATCH_REMATCH;

    if (winner_name_out)
        state_get_name_copy(st, opp_fd, winner_name_out, winner_name_sz);

    render_board(m, board_out, board_outsz);
    pthread_mutex_unlock(&ms->mtx);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  REMATCH                                                             */
/*                                                                      */
/*  Logica:                                                             */
/*   - Solo il vincitore (o entrambi in caso di pareggio) può fare     */
/*     REMATCH.                                                         */
/*   - Il REMATCH crea una NUOVA partita in WAITING con il richiedente  */
/*     come owner (X). Il vecchio slot viene liberato.                  */
/*   - Broadcast a tutti: chiunque può joinare la nuova partita.        */
/* ------------------------------------------------------------------ */

int matches_find_rematch(match_store_t *ms, int player_fd) {
    pthread_mutex_lock(&ms->mtx);
    int found_id = -1;
    for (int i = 0; i < MAX_MATCHES; i++) {
        match_t *m = &ms->matches[i];
        if (m->id == 0 || m->status != MATCH_REMATCH) continue;
        if (m->owner_fd == player_fd || m->joiner_fd == player_fd) {
            found_id = m->id;
            break;
        }
    }
    pthread_mutex_unlock(&ms->mtx);
    return found_id;
}

/*
 * matches_rematch:
 *  Ritorna new_match_id (>= 1) se la nuova partita è stata creata.
 *  Ritorna -1 match non trovato / non in REMATCH
 *  Ritorna -2 non sei un giocatore
 *  Ritorna -3 sei il perdente
 *  Ritorna -4 nessuno slot libero
 */
int matches_rematch(match_store_t *ms, int match_id, int player_fd) {
    pthread_mutex_lock(&ms->mtx);

    match_t *m = find_match(ms, match_id);
    if (!m || m->status != MATCH_REMATCH) {
        pthread_mutex_unlock(&ms->mtx);
        return -1;
    }

    int is_owner  = (m->owner_fd  == player_fd);
    int is_joiner = (m->joiner_fd == player_fd);
    if (!is_owner && !is_joiner) {
        pthread_mutex_unlock(&ms->mtx);
        return -2;
    }

    
    if (!m->draw && m->loser_fd == player_fd) {
        pthread_mutex_unlock(&ms->mtx);
        return -3;
    }

   
    match_t *nm = find_free_slot(ms);
    if (!nm) {
        pthread_mutex_unlock(&ms->mtx);
        return -4;
    }

    
    int new_id  = ms->next_id++;
    match_reset(nm);
    nm->id       = new_id;
    nm->status   = MATCH_WAITING;
    nm->owner_fd = player_fd;   

    
    match_reset(m);

    pthread_mutex_unlock(&ms->mtx);
    return new_id;
}

/* ------------------------------------------------------------------ */
/*  DISCONNECT                                                          */
/* ------------------------------------------------------------------ */

void matches_on_disconnect(match_store_t *ms, server_state_t *st, int fd) {
    int notify_opp_fd  = -1;
    int notify_pend_fd = -1;

    pthread_mutex_lock(&ms->mtx);

    for (int i = 0; i < MAX_MATCHES; i++) {
        match_t *m = &ms->matches[i];
        if (m->id == 0) continue;

        
        if (m->status == MATCH_PLAYING &&
            (m->owner_fd == fd || m->joiner_fd == fd)) {
            notify_opp_fd = (m->owner_fd == fd) ? m->joiner_fd : m->owner_fd;
            match_reset(m);
            continue;
        }

        
        if (m->status == MATCH_REMATCH &&
            (m->owner_fd == fd || m->joiner_fd == fd)) {
            match_reset(m);
            continue;
        }

        
        if ((m->status == MATCH_WAITING || m->status == MATCH_PENDING)
             && m->owner_fd == fd) {
            if (m->status == MATCH_PENDING && m->pending_fd != -1)
                notify_pend_fd = m->pending_fd;
            match_reset(m);
            continue;
        }

       
        if (m->status == MATCH_PENDING && m->pending_fd == fd) {
            m->pending_fd = -1;
            m->status     = MATCH_WAITING;
        }
    }

    pthread_mutex_unlock(&ms->mtx);

    if (notify_opp_fd != -1) {
        char winner_name[MAX_NAME] = "??";
        state_get_name_copy(st, notify_opp_fd, winner_name, sizeof(winner_name));
        char msg[256];
        snprintf(msg, sizeof(msg),
                 PROTO_EVENT_OPP_DISCONNECTED
                 PROTO_EVENT_YOU_WIN
                 PROTO_EVENT_WINNER,
                 winner_name);
        send_all(notify_opp_fd, msg);
        send_all(notify_opp_fd, PROTO_EVENT_GAME_OVER_WIN);
        state_clear_playing_match(st, notify_opp_fd);
    }

    state_clear_playing_match(st, fd);

    if (notify_pend_fd != -1)
        send_all(notify_pend_fd, PROTO_ERR_MATCH_CLOSED);
}