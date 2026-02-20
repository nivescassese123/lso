#ifndef STATE_H
#define STATE_H

#include <pthread.h>

#define MAX_NAME    32
#define MAX_CLIENTS 128

typedef struct {
    int  fd;
    int  logged_in;
    char name[MAX_NAME];
    int  playing_match_id;   /* -1 se non sta giocando */
} client_t;

typedef struct {
    pthread_mutex_t mtx;
    client_t clients[MAX_CLIENTS];
} server_state_t;

void        state_init(server_state_t *st);
void        state_add_client(server_state_t *st, int fd);
void        state_remove_client(server_state_t *st, int fd);

int         state_login(server_state_t *st, int fd, const char *name);
const char *state_get_name(server_state_t *st, int fd);
int         state_get_name_copy(server_state_t *st, int fd, char *buf, int bufsz);

void        state_users(server_state_t *st, char *out, int outsz);

int         state_get_playing_match(server_state_t *st, int fd);
int         state_set_playing_match(server_state_t *st, int fd, int mid);
int         state_clear_playing_match(server_state_t *st, int fd);

/*
 * Invia msg a tutti i client loggati, escludendo exclude_fd.
 * Passa -1 come exclude_fd per non escludere nessuno.
 */
void        state_broadcast(server_state_t *st, const char *msg, int exclude_fd);

#endif /* STATE_H */