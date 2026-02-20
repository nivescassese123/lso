#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#define MAX_LINE 1024

/*
 * Thread ricevitore: legge CONTINUAMENTE dal socket e stampa
 * tutto quello che arriva. Questo permette di ricevere eventi
 * asincroni (broadcast, mosse avversario, rematch, ecc.)
 * mentre l'utente sta digitando sul terminale.
 */
static volatile int g_running = 1;  /* flag per terminare il thread */

static void *receiver_thread(void *arg) {
    int fd = *(int *)arg;
    char buf[MAX_LINE];

    while (g_running) {
        ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            /* Connessione chiusa dal server */
            if (g_running) {
                printf("\n[SERVER] Connessione chiusa.\n");
                g_running = 0;
            }
            break;
        }
        buf[n] = '\0';

        /* Stampa il messaggio ricevuto, poi ripristina il prompt */
        printf("\r%s> ", buf);   /* \r sovrascrive la riga del prompt */
        fflush(stdout);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <ip_server> <porta>\n", argv[0]);
        return 1;
    }

    const char *ip   = argv[1];
    int         port = atoi(argv[2]);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &srv.sin_addr) != 1) {
        fprintf(stderr, "IP non valido: %s\n", ip);
        close(fd);
        return 1;
    }

    if (connect(fd, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        perror("connect");
        close(fd);
        return 1;
    }

    /* Avvia il thread ricevitore */
    pthread_t tid;
    int fd_copy = fd;   /* copia stabile per il thread */
    if (pthread_create(&tid, NULL, receiver_thread, &fd_copy) != 0) {
        perror("pthread_create");
        close(fd);
        return 1;
    }
    pthread_detach(tid);

    /* Loop principale: legge da stdin e manda al server */
    char buf[MAX_LINE];
    while (g_running) {
        printf("> ");
        fflush(stdout);

        if (!fgets(buf, sizeof(buf), stdin)) {
            /* EOF (es. Ctrl+D) */
            break;
        }

        if (!g_running) break;  /* server si è disconnesso nel frattempo */

        /* Invia la riga al server (include già il '\n') */
        size_t len = strlen(buf);
        if (send(fd, buf, len, 0) < 0) {
            perror("send");
            break;
        }

        /* Controlla QUIT localmente per uscire pulito */
        char trimmed[MAX_LINE];
        strncpy(trimmed, buf, sizeof(trimmed) - 1);
        trimmed[sizeof(trimmed) - 1] = '\0';
        trimmed[strcspn(trimmed, "\r\n")] = '\0';
        if (strcmp(trimmed, "QUIT") == 0 || strcmp(trimmed, "quit") == 0) {
            /* Aspetta un attimo la risposta BYE dal server, poi esci */
            usleep(200000);
            break;
        }
    }

    g_running = 0;
    close(fd);
    printf("Disconnesso dal server.\n");
    return 0;
}