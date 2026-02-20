#include "protocol.h"
#include "net.h"
#include <stdio.h>
#include <stdarg.h>

void proto_sendf(int fd, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    send_all(fd, buf);
}