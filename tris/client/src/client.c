#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#define MAX_LINE 1024


static volatile int g_running = 1;  

static void *receiver_thread(void *arg) {
    int fd = *(int *)arg;
    char buf[MAX_LINE];

    while (g_running) {
        ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            
            if (g_running) {
                printf("\n[SERVER] Connessione chiusa.\n");
                g_running = 0;
            }
            break;
        }
        buf[n] = '\0';

        
        printf("\r%s> ", buf);   
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

    
    pthread_t tid;
    int fd_copy = fd;   
    if (pthread_create(&tid, NULL, receiver_thread, &fd_copy) != 0) {
        perror("pthread_create");
        close(fd);
        return 1;
    }
    pthread_detach(tid);

   
    char buf[MAX_LINE];
    while (g_running) {
        printf("> ");
        fflush(stdout);

        if (!fgets(buf, sizeof(buf), stdin)) {
            
            break;
        }

        if (!g_running) break;  

        
        size_t len = strlen(buf);
        if (send(fd, buf, len, 0) < 0) {
            perror("send");
            break;
        }

        char trimmed[MAX_LINE];
        strncpy(trimmed, buf, sizeof(trimmed) - 1);
        trimmed[sizeof(trimmed) - 1] = '\0';
        trimmed[strcspn(trimmed, "\r\n")] = '\0';
        if (strcmp(trimmed, "QUIT") == 0 || strcmp(trimmed, "quit") == 0) {
           
            usleep(200000);
            break;
        }
    }

    g_running = 0;
    close(fd);
    printf("Disconnesso dal server.\n");
    return 0;
}