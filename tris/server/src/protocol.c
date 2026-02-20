#include "protocol.h"
#include "net.h"
#include <stdio.h>
#include <stdarg.h>

/*
 * Invia un messaggio formattato (printf-style) su un singolo fd.
 * Esempio: proto_sendf(fd, PROTO_OK_LOGIN, name);
 */
void proto_sendf(int fd, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    send_all(fd, buf);
}