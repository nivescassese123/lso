#include "state.h"
#include "net.h"
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  Helpers interni (chiamare solo con mutex già acquisito)             */
/* ------------------------------------------------------------------ */

static client_t *find_client(server_state_t *st, int fd) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (st->clients[i].fd == fd && st->clients[i].fd != 0)
            return &st->clients[i];
    return NULL;
}

static client_t *find_free_slot(server_state_t *st) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (st->clients[i].fd == 0)
            return &st->clients[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Inizializzazione                                                    */
/* ------------------------------------------------------------------ */

void state_init(server_state_t *st) {
    pthread_mutex_init(&st->mtx, NULL);
    memset(st->clients, 0, sizeof(st->clients));
    for (int i = 0; i < MAX_CLIENTS; i++)
        st->clients[i].playing_match_id = -1;
}

/* ------------------------------------------------------------------ */
/*  Aggiunta / rimozione client                                         */
/* ------------------------------------------------------------------ */

void state_add_client(server_state_t *st, int fd) {
    pthread_mutex_lock(&st->mtx);
    client_t *c = find_free_slot(st);
    if (c) {
        memset(c, 0, sizeof(*c));
        c->fd               = fd;
        c->logged_in        = 0;
        c->playing_match_id = -1;
    }
    pthread_mutex_unlock(&st->mtx);
}

void state_remove_client(server_state_t *st, int fd) {
    pthread_mutex_lock(&st->mtx);
    client_t *c = find_client(st, fd);
    if (c) memset(c, 0, sizeof(*c));
    pthread_mutex_unlock(&st->mtx);
}

/* ------------------------------------------------------------------ */
/*  Login                                                               */
/* ------------------------------------------------------------------ */

int state_login(server_state_t *st, int fd, const char *name) {
    if (!name || name[0] == '\0' || strlen(name) >= MAX_NAME)
        return -2;

    pthread_mutex_lock(&st->mtx);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (st->clients[i].fd != 0 &&
            st->clients[i].logged_in &&
            strcmp(st->clients[i].name, name) == 0) {
            pthread_mutex_unlock(&st->mtx);
            return -1;
        }
    }

    client_t *c = find_client(st, fd);
    if (!c) { pthread_mutex_unlock(&st->mtx); return -3; }

    strncpy(c->name, name, MAX_NAME - 1);
    c->name[MAX_NAME - 1] = '\0';
    c->logged_in = 1;

    pthread_mutex_unlock(&st->mtx);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Lettura nome                                                        */
/* ------------------------------------------------------------------ */

const char *state_get_name(server_state_t *st, int fd) {
    pthread_mutex_lock(&st->mtx);
    client_t *c = find_client(st, fd);
    const char *name = (c && c->logged_in) ? c->name : NULL;
    pthread_mutex_unlock(&st->mtx);
    return name;
}

int state_get_name_copy(server_state_t *st, int fd, char *buf, int bufsz) {
    pthread_mutex_lock(&st->mtx);
    client_t *c = find_client(st, fd);
    int found = 0;
    if (c && c->logged_in) {
        strncpy(buf, c->name, bufsz - 1);
        buf[bufsz - 1] = '\0';
        found = 1;
    }
    pthread_mutex_unlock(&st->mtx);
    return found;
}

/* ------------------------------------------------------------------ */
/*  Lista utenti                                                        */
/* ------------------------------------------------------------------ */

void state_users(server_state_t *st, char *out, int outsz) {
    pthread_mutex_lock(&st->mtx);
    char *p    = out;
    int   left = outsz;
    int   found = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_t *c = &st->clients[i];
        if (c->fd == 0 || !c->logged_in) continue;
        int n = snprintf(p, left, "USER %s\n", c->name);
        if (n > 0 && n < left) { p += n; left -= n; }
        found = 1;
    }
    if (!found) snprintf(out, outsz, "NO_USERS\n");
    pthread_mutex_unlock(&st->mtx);
}

/* ------------------------------------------------------------------ */
/*  Gestione partita in corso                                           */
/* ------------------------------------------------------------------ */

int state_get_playing_match(server_state_t *st, int fd) {
    pthread_mutex_lock(&st->mtx);
    client_t *c = find_client(st, fd);
    int mid = c ? c->playing_match_id : -1;
    pthread_mutex_unlock(&st->mtx);
    return mid;
}

int state_set_playing_match(server_state_t *st, int fd, int mid) {
    pthread_mutex_lock(&st->mtx);
    client_t *c = find_client(st, fd);
    if (!c) { pthread_mutex_unlock(&st->mtx); return -1; }
    c->playing_match_id = mid;
    pthread_mutex_unlock(&st->mtx);
    return 0;
}

int state_clear_playing_match(server_state_t *st, int fd) {
    return state_set_playing_match(st, fd, -1);
}

/* ------------------------------------------------------------------ */
/*  Broadcast                                                           */
/* ------------------------------------------------------------------ */

void state_broadcast(server_state_t *st, const char *msg, int exclude_fd) {
    int fds[MAX_CLIENTS];
    int count = 0;

    pthread_mutex_lock(&st->mtx);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_t *c = &st->clients[i];
        if (c->fd == 0 || !c->logged_in) continue;
        if (c->fd == exclude_fd) continue;
        fds[count++] = c->fd;
    }
    pthread_mutex_unlock(&st->mtx);

    for (int i = 0; i < count; i++)
        send_all(fds[i], msg);
}