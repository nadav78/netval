/* log.c — timestamped stdout log formatter (boilerplate, Claude-generated). */
#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static void log_vmsg(const char *level, const char *fmt, va_list ap)
{
    struct timespec ts;
    struct tm tm;

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);

    printf("[%02d:%02d:%02d.%03ld] [%s] ",
           tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000L, level);
    vprintf(fmt, ap);
    putchar('\n');
    fflush(stdout);
}

void log_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_vmsg("INFO", fmt, ap);
    va_end(ap);
}

void log_warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_vmsg("WARN", fmt, ap);
    va_end(ap);
}

void log_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_vmsg("ERROR", fmt, ap);
    va_end(ap);
}
