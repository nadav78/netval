/* rx.c — the receiver.
 *
 * >>> YOURS TO IMPLEMENT <<<
 *
 * Blocking UDP receive loop. The system calls you need, in order:
 *
 * 1. socket(AF_INET, SOCK_DGRAM, 0) — same as tx.
 *
 * 2. bind(fd, ...) — claim the port so the OS delivers packets for it
 *    to this socket. Build a struct sockaddr_in with:
 *        .sin_family      = AF_INET
 *        .sin_port        = htons(cfg->port)
 *        .sin_addr.s_addr = htonl(INADDR_ANY)   <- "any local address"
 *    then bind(fd, (struct sockaddr *)&addr, sizeof(addr)).
 *    (tx doesn't bind — the OS picks a random source port for it.)
 *
 * 3. The loop:
 *      a. n = recvfrom(fd, buf, sizeof(buf), 0, NULL, NULL)
 *         Blocks (sleeps) until one datagram arrives; returns its
 *         length. One datagram per call — UDP never merges or splits
 *         them. buf is uint8_t buf[NETVAL_MAX_DGRAM].
 *      b. wire_hdr_unpack(buf, n, &hdr) — on -1, count it as a bad
 *         packet and continue.
 *      c. Verify n == NETVAL_HDR_SIZE + hdr.payload_len, and that
 *         wire_checksum over the payload (buf + NETVAL_HDR_SIZE)
 *         matches hdr.checksum → else count as corrupt.
 *      d. Track stats: received count; expected next seq vs hdr.seq
 *         (a jump forward = gap/loss, backward = reorder — for
 *         Milestone 1 just log a WARN when hdr.seq != expected and
 *         resync expected = hdr.seq + 1).
 *
 * 4. Stopping: recvfrom blocks forever, so Ctrl-C is the natural stop
 *    for now. To print a summary on Ctrl-C, install a SIGINT handler
 *    with sigaction() that sets a `volatile sig_atomic_t stop = 1`
 *    flag (the ONLY thing a signal handler should do), and check the
 *    flag after recvfrom returns -1 with errno == EINTR.
 *    (A proper idle-timeout stop comes with epoll in Milestone 2.)
 *
 * Headers: <sys/socket.h>, <netinet/in.h>, <unistd.h>, <string.h>,
 * <errno.h>, <signal.h>.
 */
#include "netval.h"
#include "log.h"
#include "wire.h"

int rx_run(const netval_cfg *cfg)
{
    /* TODO(you): implement per the plan above. */
    (void)cfg;
    log_error("rx_run: not implemented yet");
    return -1;
}
