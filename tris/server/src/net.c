#include "net.h"
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int net_send_str(int sock, const char *s) {
    if (!s) return 0;
    size_t total = strlen(s);
    size_t sent  = 0;
    while (sent < total) {
        int n = (int)send(sock, s + sent, total - sent, 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return (int)sent;
}

int net_recv_into_buffer(int sock, char *buf, size_t *len, size_t cap) {
    char tmp[256];
    int n = (int)recv(sock, tmp, sizeof(tmp), 0);
    if (n <= 0) return -1;
    if (*len + (size_t)n >= cap) *len = 0;
    memcpy(buf + *len, tmp, (size_t)n);
    *len += (size_t)n;
    buf[*len] = '\0';
    return 0;
}

int net_pop_line(char *buf, size_t *len, char *line_out, size_t line_cap) {
    char *nl = memchr(buf, '\n', *len);
    if (!nl) return 0;
    size_t l = (size_t)(nl - buf);
    if (l >= line_cap) l = line_cap - 1;
    memcpy(line_out, buf, l);
    line_out[l] = '\0';
    if (l > 0 && line_out[l - 1] == '\r') line_out[--l] = '\0';
    size_t remaining = *len - ((size_t)(nl - buf) + 1);
    memmove(buf, nl + 1, remaining);
    *len = remaining;
    buf[*len] = '\0';
    return 1;
}

void send_all(int fd, const char *msg) {
    net_send_str(fd, msg);
}

int recv_line(int fd, char *buf, size_t bufsz) {
    size_t i = 0;
    while (i < bufsz - 1) {
        char c;
        int n = (int)recv(fd, &c, 1, 0);
        if (n == 0) return 0;
        if (n < 0)  return -1;
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return (int)i;
}