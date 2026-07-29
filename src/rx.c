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
#include <sys/socket.h>   /* socket, sendto            */
#include <netinet/in.h>   /* sockaddr_in, htons        */
#include <unistd.h>       /* close                     */
#include <string.h>       /* strerror                  */
#include <errno.h>        /* errno                     */
#include <signal.h>
#include <assert.h>
#include <inttypes.h>     /* PRIu64                    */
#include "netval.h"
#include "wire.h"
#include "log.h"

static volatile sig_atomic_t stop;

static void signal_handler(int signo) {
    (void)signo; /* to avoid flagging the unused parameter */
    stop = 1;
}

int rx_run(const netval_cfg *cfg)
{
    assert(cfg != NULL);

    int sfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sfd == -1) {
        log_error("socket: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_in local = {0};
    local.sin_family = AF_INET;
    local.sin_port = htons(cfg->port);
    local.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sfd, (struct sockaddr *)&local, sizeof(local)) == -1) {
        log_error("bind: %s", strerror(errno));
        close(sfd);
        return -1;
    }

    // install handler here
    struct sigaction sa = {0};
    sa.sa_handler = signal_handler;
    if (sigemptyset(&sa.sa_mask) == -1) {
        log_error("sigemptyset: %s", strerror(errno));
        close(sfd);
        return -1;
    }
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        log_error("sigaction: %s", strerror(errno));
        close(sfd);
        return -1;
    }

    uint8_t buf[NETVAL_MAX_DGRAM];

    // counters
    uint64_t ovs = 0;
    uint64_t malformed = 0;
    uint64_t checksum_mismatch = 0;
    uint64_t received = 0;

    uint32_t expected_next = 0;
    /*With MSG_TRUNC, recvfrom returns the datagram's real length rather than the 
    copied length, so n > sizeof buf is the truncation signal. */
    int rc = 0;
    for (;;) {
        ssize_t n = recvfrom(sfd, buf, sizeof(buf), MSG_TRUNC, NULL, NULL);
        if (n == -1) {
            if (errno == EINTR) {
                if (stop == 1) {
                    break;
                }
                continue;
            }
            log_error("recvfrom: %s", strerror(errno));
            rc = -1;
            break;
        }

        if (n > (ssize_t)sizeof(buf)) {
            log_warn("Oversized packet: %zd > %d", n, NETVAL_MAX_DGRAM);
            ovs++;
            continue;
        }

        netval_hdr hdr = {0};
        if (wire_hdr_unpack(buf, n, &hdr) == -1) {
            malformed++;
            continue;
        }

        if (n != NETVAL_HDR_SIZE + hdr.payload_len) {
            log_warn("Number of read bytes don't match header size and payload length.");
            malformed++;
            continue;
        }

        uint32_t hash = wire_checksum(buf + NETVAL_HDR_SIZE, hdr.payload_len);
        if (hash != hdr.checksum) {
            log_warn("Checksum mismatch.    Expected: %u    Got: %u", hdr.checksum, hash);
            checksum_mismatch++;
            continue;
        }

        if (expected_next != hdr.seq) {
            log_warn("Expected seq:     %u      Actual seq:     %u", expected_next, hdr.seq);
        }

        received++;
        expected_next = hdr.seq + 1;
    }

    close(sfd);

    /* Summary. The four classes are mutually exclusive — every datagram
     * the kernel handed us is counted in exactly one, so they sum to
     * "observed" rather than being tallied separately. */
    uint64_t observed = received + ovs + malformed + checksum_mismatch;

    log_info("---- rx summary ----");
    log_info("stopped by         : %s", stop ? "SIGINT" : "error");
    log_info("datagrams observed : %" PRIu64, observed);
    log_info("  valid            : %" PRIu64, received);
    log_info("  oversized        : %" PRIu64, ovs);
    log_info("  malformed        : %" PRIu64, malformed);
    log_info("  checksum failed  : %" PRIu64, checksum_mismatch);
    if (received > 0) {
        log_info("highest seq + 1    : %" PRIu32, expected_next);
    }
    return rc;
}
