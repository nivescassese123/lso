#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#include "net.h"
#include "state.h"
#include "match.h"
#include "protocol.h"

#define BACKLOG 16

server_state_t g_state;
match_store_t  g_matches;

/* ------------------------------------------------------------------ */
/*  Helper: invia board iniziale e messaggi di avvio a entrambi        */
/* ------------------------------------------------------------------ */
static void start_match_notify(int match_id,
                                int owner_fd, const char *owner_name,
                                int joiner_fd, const char *joiner_name,
                                const char *ok_fmt_x, const char *ok_fmt_o) {
    char msg[256];

    snprintf(msg, sizeof(msg), ok_fmt_x, match_id, joiner_name);
    send_all(owner_fd, msg);

    snprintf(msg, sizeof(msg), ok_fmt_o, match_id, owner_name);
    send_all(joiner_fd, msg);

    char bbuf[512];
    if (matches_board(&g_matches, match_id, bbuf, sizeof(bbuf)) == 0) {
        send_all(owner_fd,  bbuf);
        send_all(joiner_fd, bbuf);
    }
}

/* ------------------------------------------------------------------ */
/*  Thread per ogni client                                              */
/* ------------------------------------------------------------------ */
static void *client_handler(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);

    send_all(client_fd, PROTO_WELCOME);
    send_all(client_fd, PROTO_HINT_LOGIN);
    send_all(client_fd, PROTO_HINT_CMDS);

    char line[MAX_LINE];
    char me[MAX_NAME] = {0};

    while (1) {
        int r = recv_line(client_fd, line, sizeof(line));
        if (r == 0) break;
        if (r < 0) { perror("recv_line"); break; }

        /* Strip \r\n prima di qualsiasi confronto */
        line[strcspn(line, "\r\n")] = '\0';

        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') continue;

        if (strcmp(p, "QUIT") == 0 || strcmp(p, "quit") == 0) {
            send_all(client_fd, PROTO_BYE);
            break;
        }

        /* Aggiorna nome locale ad ogni iterazione (thread-safe) */
        me[0] = '\0';
        int logged_in = state_get_name_copy(&g_state, client_fd, me, sizeof(me));

        /* ---------------------------------------------------------- */
        /*  Non loggato: solo LOGIN                                    */
        /* ---------------------------------------------------------- */
        if (!logged_in) {
            if (strncmp(p, "LOGIN ", 6) == 0) {
                const char *name = p + 6;
                int ok = state_login(&g_state, client_fd, name);
                if (ok == 0) {
                    proto_sendf(client_fd, PROTO_OK_LOGIN, name);
                } else if (ok == -1) {
                    send_all(client_fd, PROTO_ERR_NAME_TAKEN);
                } else {
                    send_all(client_fd, PROTO_ERR_BAD_NAME);
                }
            } else {
                send_all(client_fd, PROTO_ERR_PLEASE_LOGIN);
            }
            continue;
        }

        /* ---------------------------------------------------------- */
        /*  Comandi disponibili dopo il login                          */
        /* ---------------------------------------------------------- */

        if (strcmp(p, "WHOAMI") == 0) {
            proto_sendf(client_fd, PROTO_OK_WHOAMI, me);

        } else if (strcmp(p, "USERS") == 0) {
            char buf[512];
            state_users(&g_state, buf, sizeof(buf));
            send_all(client_fd, buf);

        } else if (strcmp(p, "CREATE") == 0) {
            int id = matches_create(&g_matches, client_fd);
            if (id < 0) {
                send_all(client_fd, PROTO_ERR_MATCHES_FULL);
            } else {
                proto_sendf(client_fd, PROTO_OK_MATCH_CREATED, id);
                /* Broadcast: nuova partita disponibile */
                char bcast[128];
                snprintf(bcast, sizeof(bcast), PROTO_EVENT_MATCH_AVAILABLE, id, me);
                state_broadcast(&g_state, bcast, client_fd);
            }

        } else if (strcmp(p, "LIST") == 0) {
            char buf[1024];
            matches_list(&g_matches, &g_state, buf, sizeof(buf));
            send_all(client_fd, buf);

        } else if (strncmp(p, "JOIN", 4) == 0 && (p[4] == ' ' || p[4] == '\0')) {
            int id;
            if (sscanf(p, "JOIN %d", &id) != 1) {
                send_all(client_fd, PROTO_ERR_BAD_USAGE);
                continue;
            }
            if (state_get_playing_match(&g_state, client_fd) != -1) {
                send_all(client_fd, PROTO_ERR_ALREADY_PLAYING);
                continue;
            }
            int owner_fd = -1;
            int rc = matches_request_join(&g_matches, id, client_fd, &owner_fd);
            if (rc == 0) {
                proto_sendf(owner_fd, PROTO_EVENT_JOIN_REQUEST, id, me);
                send_all(client_fd, PROTO_OK_JOIN_REQUESTED);
            } else if (rc == -1) {
                send_all(client_fd, PROTO_ERR_MATCH_NOT_FOUND);
            } else if (rc == -2) {
                send_all(client_fd, PROTO_ERR_MATCH_NOT_JOINABLE);
            } else if (rc == -3) {
                send_all(client_fd, PROTO_ERR_CANNOT_JOIN_OWN);
            } else {
                send_all(client_fd, PROTO_ERR_JOIN_FAILED);
            }

        } else if (strncmp(p, "ACCEPT", 6) == 0 && (p[6] == ' ' || p[6] == '\0')) {
            int id;
            if (sscanf(p, "ACCEPT %d", &id) != 1) {
                send_all(client_fd, PROTO_ERR_BAD_USAGE);
                continue;
            }
            int joiner_fd = -1;
            int rc = matches_accept(&g_matches, id, client_fd, &joiner_fd);
            if (rc == 0) {
                state_set_playing_match(&g_state, client_fd, id);
                state_set_playing_match(&g_state, joiner_fd, id);

                char joiner_name[MAX_NAME] = "??";
                state_get_name_copy(&g_state, joiner_fd, joiner_name, sizeof(joiner_name));

                start_match_notify(id,
                    client_fd, me,
                    joiner_fd, joiner_name,
                    PROTO_OK_MATCH_STARTED_X,
                    PROTO_OK_MATCH_STARTED_O);

                char bcast[128];
                snprintf(bcast, sizeof(bcast), PROTO_EVENT_MATCH_STARTED_ALL, id);
                state_broadcast(&g_state, bcast, -1);

            } else if (rc == -1) {
                send_all(client_fd, PROTO_ERR_MATCH_NOT_FOUND);
            } else if (rc == -2) {
                send_all(client_fd, PROTO_ERR_NOT_OWNER);
            } else if (rc == -3) {
                send_all(client_fd, PROTO_ERR_NO_PENDING);
            } else {
                send_all(client_fd, PROTO_ERR_ACCEPT_FAILED);
            }

        } else if (strncmp(p, "REJECT", 6) == 0 && (p[6] == ' ' || p[6] == '\0')) {
            int id;
            if (sscanf(p, "REJECT %d", &id) != 1) {
                send_all(client_fd, PROTO_ERR_BAD_USAGE);
                continue;
            }
            int rejected_fd = -1;
            int rc = matches_reject(&g_matches, id, client_fd, &rejected_fd);
            if (rc == 0) {
                send_all(client_fd, PROTO_OK_REJECTED);
                if (rejected_fd != -1) send_all(rejected_fd, PROTO_ERR_JOIN_REJECTED);
            } else if (rc == -1) {
                send_all(client_fd, PROTO_ERR_MATCH_NOT_FOUND);
            } else if (rc == -2) {
                send_all(client_fd, PROTO_ERR_NOT_OWNER);
            } else if (rc == -3) {
                send_all(client_fd, PROTO_ERR_NO_PENDING);
            } else {
                send_all(client_fd, PROTO_ERR_REJECT_FAILED);
            }

        } else if (strncmp(p, "MOVE", 4) == 0 && (p[4] == ' ' || p[4] == '\0')) {
            int rr, cc;
            if (sscanf(p, "MOVE %d %d", &rr, &cc) != 2) {
                send_all(client_fd, PROTO_ERR_BAD_USAGE);
                continue;
            }
            int mid = state_get_playing_match(&g_state, client_fd);
            if (mid == -1) {
                send_all(client_fd, PROTO_ERR_NOT_IN_MATCH);
                continue;
            }
            int  opp_fd = -1;
            char boardbuf[512];
            char winner[MAX_NAME] = {0};
            int mrc = matches_move(&g_matches, &g_state, mid, client_fd, rr, cc,
                                   &opp_fd, boardbuf, sizeof(boardbuf),
                                   winner, sizeof(winner));
            if (mrc == 0) {
                send_all(client_fd, PROTO_OK_MOVED);
                send_all(client_fd, boardbuf);
                if (opp_fd != -1) {
                    proto_sendf(opp_fd, PROTO_EVENT_OPPONENT_MOVED, rr, cc);
                    send_all(opp_fd, boardbuf);
                }
            } else if (mrc == 1 || mrc == 2) {
                /* Fine partita: win o draw */
                state_clear_playing_match(&g_state, client_fd);
                if (opp_fd != -1) state_clear_playing_match(&g_state, opp_fd);

                if (mrc == 1) {
                    send_all(client_fd, PROTO_EVENT_YOU_WIN);
                    proto_sendf(client_fd, PROTO_EVENT_WINNER, winner);
                    send_all(client_fd, boardbuf);
                    if (opp_fd != -1) {
                        send_all(opp_fd, PROTO_EVENT_YOU_LOSE);
                        proto_sendf(opp_fd, PROTO_EVENT_WINNER, winner);
                        send_all(opp_fd, boardbuf);
                    }
                } else {
                    send_all(client_fd, PROTO_EVENT_DRAW);
                    send_all(client_fd, boardbuf);
                    if (opp_fd != -1) {
                        send_all(opp_fd, PROTO_EVENT_DRAW);
                        send_all(opp_fd, boardbuf);
                    }
                }
                /* Invita al rematch */
                send_all(client_fd, PROTO_EVENT_GAME_OVER);
                if (opp_fd != -1) send_all(opp_fd, PROTO_EVENT_GAME_OVER);

                /* Broadcast partita terminata */
                char bcast[64];
                snprintf(bcast, sizeof(bcast), PROTO_EVENT_MATCH_FINISHED, mid);
                state_broadcast(&g_state, bcast, -1);

            } else if (mrc == -4) {
                send_all(client_fd, PROTO_ERR_NOT_YOUR_TURN);
            } else if (mrc == -5) {
                send_all(client_fd, PROTO_ERR_BAD_MOVE);
            } else if (mrc == -2) {
                send_all(client_fd, PROTO_ERR_MATCH_NOT_PLAYING);
            } else {
                send_all(client_fd, PROTO_ERR_MOVE_FAILED);
            }

        } else if (strcmp(p, "BOARD") == 0) {
            int mid = state_get_playing_match(&g_state, client_fd);
            if (mid == -1) {
                send_all(client_fd, PROTO_ERR_NOT_IN_MATCH);
                continue;
            }
            char bbuf[512];
            if (matches_board(&g_matches, mid, bbuf, sizeof(bbuf)) == 0)
                send_all(client_fd, bbuf);
            else
                send_all(client_fd, PROTO_ERR_BOARD_NOT_FOUND);

        } else if (strcmp(p, "RESIGN") == 0) {
            int mid = state_get_playing_match(&g_state, client_fd);
            if (mid == -1) {
                send_all(client_fd, PROTO_ERR_NOT_IN_MATCH);
                continue;
            }
            int  opp_fd = -1;
            char boardbuf[512];
            char winner[MAX_NAME] = {0};
            int rrc = matches_resign(&g_matches, &g_state, mid, client_fd,
                                     &opp_fd, boardbuf, sizeof(boardbuf),
                                     winner, sizeof(winner));
            if (rrc == 0) {
                state_clear_playing_match(&g_state, client_fd);
                if (opp_fd != -1) state_clear_playing_match(&g_state, opp_fd);

                send_all(client_fd, PROTO_EVENT_YOU_LOSE);
                proto_sendf(client_fd, PROTO_EVENT_WINNER, winner);
                send_all(client_fd, boardbuf);

                if (opp_fd != -1) {
                    send_all(opp_fd, PROTO_EVENT_YOU_WIN);
                    proto_sendf(opp_fd, PROTO_EVENT_WINNER, winner);
                    send_all(opp_fd, boardbuf);
                }
                /* Invita al rematch */
                send_all(client_fd, PROTO_EVENT_GAME_OVER);
                if (opp_fd != -1) send_all(opp_fd, PROTO_EVENT_GAME_OVER);

                char bcast[64];
                snprintf(bcast, sizeof(bcast), PROTO_EVENT_MATCH_FINISHED, mid);
                state_broadcast(&g_state, bcast, -1);

            } else if (rrc == -2) {
                send_all(client_fd, PROTO_ERR_MATCH_NOT_PLAYING);
            } else if (rrc == -4) {
                send_all(client_fd, PROTO_ERR_NO_OPPONENT);
            } else {
                send_all(client_fd, PROTO_ERR_RESIGN_FAILED);
            }

        } else if (strcmp(p, "REMATCH") == 0) {
            /* --------------------------------------------------------
             * REMATCH: cerca la partita terminata in cui questo client
             * era coinvolto (stato MATCH_REMATCH).
             * -------------------------------------------------------- */
            int mid = matches_find_rematch(&g_matches, client_fd);
            if (mid == -1) {
                send_all(client_fd, PROTO_ERR_REMATCH_NOT_AVAIL);
                continue;
            }

            int other_fd      = -1;
            int new_mid       = -1;
            int new_owner_fd  = -1;
            int new_joiner_fd = -1;

            int rc = matches_rematch(&g_matches, mid, client_fd,
                                     &other_fd,
                                     &new_mid, &new_owner_fd, &new_joiner_fd);

            if (rc == 0) {
                /* Primo a chiedere: aspetta l'avversario */
                send_all(client_fd, PROTO_OK_REMATCH_WAITING);
                if (other_fd != -1)
                    proto_sendf(other_fd, PROTO_EVENT_REMATCH_OFFERED, me);

            } else if (rc == 1) {
                /* Entrambi pronti: nuova partita avviata */
                state_set_playing_match(&g_state, new_owner_fd,  new_mid);
                state_set_playing_match(&g_state, new_joiner_fd, new_mid);

                char owner_name[MAX_NAME]  = "??";
                char joiner_name[MAX_NAME] = "??";
                state_get_name_copy(&g_state, new_owner_fd,  owner_name,  sizeof(owner_name));
                state_get_name_copy(&g_state, new_joiner_fd, joiner_name, sizeof(joiner_name));

                start_match_notify(new_mid,
                    new_owner_fd,  owner_name,
                    new_joiner_fd, joiner_name,
                    PROTO_OK_REMATCH_STARTED_X,
                    PROTO_OK_REMATCH_STARTED_O);

                /* Broadcast: nuova partita disponibile */
                char bcast[128];
                snprintf(bcast, sizeof(bcast), PROTO_EVENT_MATCH_STARTED_ALL, new_mid);
                state_broadcast(&g_state, bcast, -1);

            } else if (rc == -3) {
                send_all(client_fd, PROTO_OK_REMATCH_WAITING); /* già in attesa */
            } else {
                send_all(client_fd, PROTO_ERR_REMATCH_FAILED);
            }

        } else {
            send_all(client_fd, PROTO_ERR_UNKNOWN_CMD);
        }
    }

    /* ---------------------------------------------------------------- */
    /*  Cleanup disconnessione                                           */
    /* ---------------------------------------------------------------- */
    printf("Client disconnesso: %s (fd=%d)\n",
           me[0] ? me : "<not logged in>", client_fd);

    matches_on_disconnect(&g_matches, &g_state, client_fd);
    state_remove_client(&g_state, client_fd);
    close(client_fd);
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <porta>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Porta non valida.\n");
        return 1;
    }

    state_init(&g_state);
    matches_init(&g_matches);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons((uint16_t)port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(listen_fd); return 1;
    }
    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen"); close(listen_fd); return 1;
    }
    printf("Server in ascolto sulla porta %d...\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t clen = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &clen);
        if (client_fd < 0) { perror("accept"); continue; }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        printf("Client connesso: %s:%d (fd=%d)\n",
               ip, ntohs(client_addr.sin_port), client_fd);

        state_add_client(&g_state, client_fd);

        int *pfd = malloc(sizeof(int));
        if (!pfd) {
            perror("malloc");
            close(client_fd);
            state_remove_client(&g_state, client_fd);
            continue;
        }
        *pfd = client_fd;

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler, pfd) != 0) {
            perror("pthread_create");
            close(client_fd);
            state_remove_client(&g_state, client_fd);
            free(pfd);
            continue;
        }
        pthread_detach(tid);
    }

    close(listen_fd);
    return 0;
}