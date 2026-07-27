/* log.h — timestamped stdout log formatter (boilerplate, Claude-generated).
 *
 * Output format (stable — the Python harness will parse these lines):
 *      [HH:MM:SS.mmm] [LEVEL] message
 */
#ifndef NETVAL_LOG_H
#define NETVAL_LOG_H

void log_info(const char *fmt, ...)  __attribute__((format(printf, 1, 2)));
void log_warn(const char *fmt, ...)  __attribute__((format(printf, 1, 2)));
void log_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* NETVAL_LOG_H */
