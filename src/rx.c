/* rx.c — the receiver. M1 (blocking loop) done; now Phase A:
 *
 * >>> YOURS TO IMPLEMENT: convert to a non-blocking epoll loop <<<
 *
 * Why (you have the evidence): the baseline strace shows one recvfrom
 * syscall per datagram, and under a 2000-packet unlimited-rate burst
 * the socket buffer overflowed — the kernel silently dropped most of
 * the flood before the app ever saw it. Readiness-based I/O is the
 * standard fix, and the same loop gives us --idle-timeout for free.
 *
 * The pieces, in order (existing socket/bind/sigaction stay as-is):
 *
 * 1. Make the socket non-blocking, AFTER bind succeeds:
 *        int flags = fcntl(sfd, F_GETFL);          — read current flags
 *        fcntl(sfd, F_SETFL, flags | O_NONBLOCK);  — add, don't replace
 *    Both calls return -1 on error. OR-ing preserves whatever flags the
 *    fd already has — F_SETFL with a bare O_NONBLOCK would wipe them.
 *    From here recvfrom NEVER sleeps: if the queue is empty it returns
 *    -1 with errno EAGAIN (a.k.a. EWOULDBLOCK — same value on Linux,
 *    but portable code checks both: EAGAIN || EWOULDBLOCK).
 *
 * 2. Create the epoll instance and register the socket:
 *        int epfd = epoll_create1(0);              — -1 on error
 *        struct epoll_event ev = {0};
 *        ev.events  = EPOLLIN;                     — "readable" interest
 *        ev.data.fd = sfd;                         — echoed back to you
 *        epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev); — -1 on error
 *    epfd is a real fd: it needs its own close(), on every exit path.
 *
 * 3. The outer loop — wait for readiness:
 *        struct epoll_event events[8];
 *        int nready = epoll_wait(epfd, events, 8, timeout_ms);
 *    timeout_ms: -1 = block forever; otherwise cfg->idle_timeout_s
 *    converted to ms (cli.c caps it so the int can't overflow).
 *    Three outcomes:
 *        nready > 0  → events[0..nready) are ready fds; with one fd
 *                      registered, nready is always 1 here.
 *        nready == 0 → TIMEOUT: idle_timeout_s passed with no traffic.
 *                      This is the clean-stop path — break, summary.
 *        nready == -1→ EINTR: same drill as M1's recvfrom (check stop,
 *                      else re-wait). Anything else: real error.
 *
 * 4. The inner loop — DRAIN the socket:
 *    One readiness event does NOT mean one datagram; a burst may have
 *    queued dozens. Loop recvfrom (same MSG_TRUNC call, same classify/
 *    count path you already wrote — move it, don't rewrite it) until
 *    recvfrom returns -1 with EAGAIN/EWOULDBLOCK → queue empty, go
 *    back to epoll_wait. Skipping the drain is THE classic epoll bug:
 *    with level-triggered epoll it just costs syscalls, with
 *    edge-triggered it loses data — be ready to explain why.
 *
 * 5. Your decision, before you write it: EPOLLIN alone (level-
 *    triggered, the default) or EPOLLIN | EPOLLET (edge-triggered)?
 *    LT: epoll_wait keeps reporting the fd as long as data remains.
 *    ET: it reports only on the empty→non-empty transition.
 *    Decide, write one sentence saying why next to the epoll_ctl call,
 *    and be able to defend it for 60 seconds (it becomes a drill).
 *
 * New headers: <sys/epoll.h>, <fcntl.h>.
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
#include <sys/epoll.h>
#include <fcntl.h>

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

    // make socket non-blocking
    int flags = fcntl(sfd, F_GETFL);
    if (flags == -1) {
        log_error("fcntl(F_GETFL): %s", strerror(errno));
        close(sfd);
        return -1;
    }

    if (fcntl(sfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        log_error("fcntl(F_SETFL): %s", strerror(errno));
        close(sfd);
        return -1;
    }

    // create epoll instance
    int epfd = epoll_create1(0);
    if (epfd == -1) {
        log_error("epoll_create1: %s", strerror(errno));
        close(sfd);
        return -1;
    }
    struct epoll_event ev = {0};
    ev.events  = EPOLLIN;
    ev.data.fd = sfd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev) == -1) {
        log_error("epoll_ctl(ADD): %s", strerror(errno));
        close(epfd);
        close(sfd);
        return -1;
    }


    // install handler here
    struct sigaction sa = {0};
    sa.sa_handler = signal_handler;
    if (sigemptyset(&sa.sa_mask) == -1) {
        log_error("sigemptyset: %s", strerror(errno));
        close(epfd);
        close(sfd);
        return -1;
    }
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        log_error("sigaction: %s", strerror(errno));
        close(epfd);
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
    // the outer loop - wait for readiness
    struct epoll_event events[8];
    // converted to ms (cli.c caps it so the int can't overflow)
    int timeout_ms = (cfg->idle_timeout_s > 0) ? (int)(cfg->idle_timeout_s * 1000) : -1;
    for (;;) {
        int nready = epoll_wait(epfd, events, 8, timeout_ms);
        if (nready == -1) {
            if (errno == EINTR) {
                if (stop == 1) {
                    goto done;
                }
                continue;
            } 
            log_error("epoll: %s", strerror(errno));
            rc = -1;
            goto done;
        }

        if (nready == 0) {
            goto done;
        }

        for (;;) {
            ssize_t n = recvfrom(sfd, buf, sizeof(buf), MSG_TRUNC, NULL, NULL);

            if (n == -1) {            
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }

                if (errno == EINTR) {
                    if (stop == 1) {
                        goto done;
                    }
                    continue;
                }

                log_error("recvfrom: %s", strerror(errno));
                rc = -1;
                goto done;
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
    }
    done:
    close(epfd);
    close(sfd);

    /* Summary. The four classes are mutually exclusive — every datagram
     * the kernel handed us is counted in exactly one, so they sum to
     * "observed" rather than being tallied separately. */
    uint64_t observed = received + ovs + malformed + checksum_mismatch;

    log_info("---- rx summary ----");
    /* Three exits: stop flag set → SIGINT; clean rc without the flag →
     * idle timeout; otherwise a syscall failure ended the loop. */
    log_info("stopped by         : %s",
             stop ? "SIGINT" : (rc == 0 ? "idle timeout" : "error"));
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
