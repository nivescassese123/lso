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

/* Trova uno slot libero (id==0). Chiama sotto lock. */
static match_t *find_free_slot(match_store_t *ms) {
    for (int i = 0; i < MAX_MATCHES; i++)
        if (ms->matches[i].id == 0) return &ms->matches[i];
    return NULL;
}

/* Trova match per id. Chiama sotto lock. */
static match_t *find_match(match_store_t *ms, int match_id) {
    for (int i = 0; i < MAX_MATCHES; i++)
        if (ms->matches[i].id == match_id) return &ms->matches[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Inizializzazione                                                    */
/* ------------------------------------------------------------------ */

void matches_init(match_store_t *ms) {
    pthread_mutex_init(&ms->mtx, NULL);
    ms->next_id = 1;
    memset(ms->matches, 0, sizeof(ms->matches));
    for (int i = 0; i < MAX_MATCHES; i++) {
        ms->matches[i].id                   = 0;
        ms->matches[i].owner_fd             = -1;
        ms->matches[i].joiner_fd            = -1;
        ms->matches[i].pending_fd           = -1;
        ms->matches[i].rematch_requester_fd = -1;
        ms->matches[i].rematch_other_fd     = -1;
    }
}

/* ------------------------------------------------------------------ */
/*  CREATE                                                              */
/* ------------------------------------------------------------------ */

int matches_create(match_store_t *ms, int owner_fd) {
    pthread_mutex_lock(&ms->mtx);
    match_t *m = find_free_slot(ms);
    if (!m) {
        pthread_mutex_unlock(&ms->mtx);
        return -1;
    }
    m->id                   = ms->next_id++;
    m->status               = MATCH_WAITING;
    m->owner_fd             = owner_fd;
    m->joiner_fd            = -1;
    m->pending_fd           = -1;
    m->turn                 = 0;
    m->rematch_requester_fd = -1;
    m->rematch_other_fd     = -1;
    board_clear(m->board);
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
        /* Ordine lock: ms->mtx → st->mtx (rispettato ovunque) */
        state_get_name_copy(st, m->owner_fd, owner_name, sizeof(owner_name));

        const char *ss;
        switch (m->status) {
            case MATCH_WAITING:  ss = "WAITING";  break;
            case MATCH_PENDING:  ss = "PENDING";  break;
            case MATCH_PLAYING:  ss = "PLAYING";  break;
            case MATCH_FINISHED: ss = "FINISHED"; break;
            case MATCH_REMATCH:  ss = "REMATCH";  break;
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

    char mark = is_owner ? 'X' : 'O';
    m->board[r][c]   = mark;
    *opponent_fd_out = is_owner ? m->joiner_fd : m->owner_fd;

    int result = 0;
    if (check_winner(m->board, mark)) {
        /* Passa a REMATCH invece di liberare lo slot subito:
           i giocatori possono ancora chiedere il rematch.          */
        m->status               = MATCH_REMATCH;
        m->rematch_requester_fd = -1;
        m->rematch_other_fd     = -1;
        if (winner_name_out)
            state_get_name_copy(st, player_fd, winner_name_out, winner_name_sz);
        result = 1;
    } else if (board_full(m->board)) {
        m->status               = MATCH_REMATCH;
        m->rematch_requester_fd = -1;
        m->rematch_other_fd     = -1;
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

    *opponent_fd_out        = opp_fd;
    m->status               = MATCH_REMATCH;
    m->rematch_requester_fd = -1;
    m->rematch_other_fd     = -1;

    if (winner_name_out)
        state_get_name_copy(st, opp_fd, winner_name_out, winner_name_sz);

    render_board(m, board_out, board_outsz);
    pthread_mutex_unlock(&ms->mtx);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  REMATCH                                                             */
/* ------------------------------------------------------------------ */

int matches_find_rematch(match_store_t *ms, int player_fd) {
    pthread_mutex_lock(&ms->mtx);
    int found_id = -1;
    for (int i = 0; i < MAX_MATCHES; i++) {
        match_t *m = &ms->matches[i];
        if (m->id == 0) continue;
        if (m->status != MATCH_REMATCH) continue;
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
 *
 *   Ritorna 0: richiesta registrata, aspetta l'avversario.
 *              *other_fd_out = fd avversario da notificare.
 *   Ritorna 1: entrambi pronti → nuova partita creata.
 *              *new_match_id_out, *new_owner_fd_out, *new_joiner_fd_out valorizzati.
 *   Ritorna -1: match non trovato o non in REMATCH.
 *   Ritorna -2: non sei un giocatore.
 *   Ritorna -3: hai già chiesto il rematch.
 */
int matches_rematch(match_store_t *ms, int match_id, int player_fd,
                    int *other_fd_out,
                    int *new_match_id_out,
                    int *new_owner_fd_out,
                    int *new_joiner_fd_out) {
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

    /* Ha già chiesto? */
    if (m->rematch_requester_fd == player_fd) {
        pthread_mutex_unlock(&ms->mtx);
        return -3;
    }

    if (m->rematch_requester_fd == -1) {
        /* Primo a chiedere: registra e notifica l'avversario */
        m->rematch_requester_fd = player_fd;
        m->rematch_other_fd     = is_owner ? m->joiner_fd : m->owner_fd;
        *other_fd_out           = m->rematch_other_fd;
        pthread_mutex_unlock(&ms->mtx);
        return 0;
    }

    /* Secondo a chiedere: entrambi pronti → crea nuova partita */
    int first_fd  = m->rematch_requester_fd;  /* chi ha chiesto per primo → X */
    int second_fd = player_fd;                 /* chi ha appena accettato  → O */

    /* Trova slot libero per la nuova partita */
    match_t *nm = find_free_slot(ms);
    if (!nm) {
        pthread_mutex_unlock(&ms->mtx);
        return -1;  /* nessuno slot libero */
    }

    int new_id               = ms->next_id++;
    nm->id                   = new_id;
    nm->status               = MATCH_PLAYING;
    nm->owner_fd             = first_fd;   /* chi ha chiesto per primo è X */
    nm->joiner_fd            = second_fd;
    nm->pending_fd           = -1;
    nm->turn                 = 0;
    nm->rematch_requester_fd = -1;
    nm->rematch_other_fd     = -1;
    board_clear(nm->board);

    /* Libera il vecchio slot */
    m->status = MATCH_FINISHED;
    m->id     = 0;

    *new_match_id_out  = new_id;
    *new_owner_fd_out  = first_fd;
    *new_joiner_fd_out = second_fd;

    pthread_mutex_unlock(&ms->mtx);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  DISCONNECT                                                          */
/* ------------------------------------------------------------------ */

void matches_on_disconnect(match_store_t *ms, server_state_t *st, int fd) {
    int notify_opp_fd  = -1;   /* avversario da notificare (YOU_WIN) */
    int notify_pend_fd = -1;   /* pending joiner da notificare       */
    int notify_rematch_fd = -1; /* aspettava rematch, avversario sparito */

    pthread_mutex_lock(&ms->mtx);

    for (int i = 0; i < MAX_MATCHES; i++) {
        match_t *m = &ms->matches[i];
        if (m->id == 0) continue;

        /* --- In partita in corso --- */
        if (m->status == MATCH_PLAYING &&
            (m->owner_fd == fd || m->joiner_fd == fd)) {
            notify_opp_fd = (m->owner_fd == fd) ? m->joiner_fd : m->owner_fd;
            m->status     = MATCH_FINISHED;
            m->id         = 0;
            continue;
        }

        /* --- In fase REMATCH --- */
        if (m->status == MATCH_REMATCH &&
            (m->owner_fd == fd || m->joiner_fd == fd)) {
            notify_rematch_fd = (m->owner_fd == fd) ? m->joiner_fd : m->owner_fd;
            m->status         = MATCH_FINISHED;
            m->id             = 0;
            continue;
        }

        /* --- Owner di partita non iniziata --- */
        if ((m->status == MATCH_WAITING || m->status == MATCH_PENDING)
             && m->owner_fd == fd) {
            if (m->status == MATCH_PENDING && m->pending_fd != -1)
                notify_pend_fd = m->pending_fd;
            m->status     = MATCH_FINISHED;
            m->pending_fd = -1;
            m->id         = 0;
            continue;
        }

        /* --- Pending joiner che si disconnette --- */
        if (m->status == MATCH_PENDING && m->pending_fd == fd) {
            m->pending_fd = -1;
            m->status     = MATCH_WAITING;
        }
    }

    pthread_mutex_unlock(&ms->mtx);

    /* --- Azioni fuori dal lock --- */
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
        state_clear_playing_match(st, notify_opp_fd);
    }

    /* Se l'avversario era in attesa di rematch lo notifichiamo */
    if (notify_rematch_fd != -1)
        send_all(notify_rematch_fd, PROTO_EVENT_REMATCH_DECLINED);

    state_clear_playing_match(st, fd);

    if (notify_pend_fd != -1)
        send_all(notify_pend_fd, PROTO_ERR_MATCH_CLOSED);
}