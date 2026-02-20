#ifndef NET_H
#define NET_H

#include <stddef.h>

#define MAX_LINE 512

int  net_send_str(int sock, const char *s);
int  net_recv_into_buffer(int sock, char *buf, size_t *len, size_t cap);
int  net_pop_line(char *buf, size_t *len, char *line_out, size_t line_cap);

void send_all(int fd, const char *msg);
int  recv_line(int fd, char *buf, size_t bufsz);

#endif /* NET_H */