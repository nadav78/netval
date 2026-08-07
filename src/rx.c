/* rx.c — the receiver. M1 (blocking) and Phase A (epoll) done; Phase B:
 *
 * >>> YOURS TO IMPLEMENT: a malloc'd per-flow table <<<
 *
 * Why (you watched this happen in GDB): a restarted tx produced
 * "Expected seq: 50  Actual seq: 0" — the single global expected_next
 * can't tell two flows apart, so N tx workers would look like a storm
 * of fake loss/reorder. A flow = one sender socket = one (source ip,
 * source port) pair; each gets its own seq tracking.
 *
 * The pieces:
 *
 * 1. Capture WHO sent each datagram — fill recvfrom's last two args:
 *        struct sockaddr_in src;
 *        socklen_t srclen = sizeof(src);           <- EVERY call
 *        recvfrom(..., (struct sockaddr *)&src, &srclen);
 *    srclen is value-result: the kernel overwrites it, so it MUST be
 *    re-set before each call (declare both INSIDE the drain loop and
 *    it's automatic). The classic bug is hoisting srclen out.
 *
 * 2. The flow entry — this is the "explicit memory management" piece:
 *        typedef struct {
 *            uint32_t addr;         src.sin_addr.s_addr, kept as-is
 *            uint16_t port;         src.sin_port, kept as-is
 *            uint32_t expected_next;
 *            uint64_t received;     valid datagrams from this flow
 *            uint64_t gaps;         forward jumps (suspected loss)
 *            uint64_t backward;     seq < expected: dup OR reorder —
 *                                   we can't tell which yet (M3 can);
 *                                   count honestly under one name
 *        } flow_t;
 *    Keep addr/port in network order as opaque key bytes — they're
 *    only compared, never done arithmetic on, so no ntohl needed.
 *
 * 3. The table: a fixed array of POINTERS, entries malloc'd on first
 *    sight of a new key:
 *        #define MAX_FLOWS 64
 *        flow_t *flows[MAX_FLOWS];  int nflows = 0;
 *    Lookup = linear scan matching (addr, port) — 64 entries, fine.
 *    Miss → malloc(sizeof(flow_t)), CHECK FOR NULL (malloc returns
 *    NULL on failure — unchecked NULL deref is the classic), zero or
 *    fill every field (malloc memory is GARBAGE, not zeroed —
 *    expected_next must start at 0 deliberately; calloc is the
 *    alternative that zeroes), store the key, expected_next = 0.
 *    Table full → count it in a global flows_dropped and skip seq
 *    tracking for that datagram (still count it received) — honest
 *    degradation, no crash.
 *
 * 4. Move the seq logic (lines ~231-236) onto the flow entry:
 *    equal → in order; hdr.seq > expected → gaps++, WARN, resync;
 *    hdr.seq < expected → backward++, WARN, do NOT resync (a stale
 *    late packet shouldn't drag expected_next backwards).
 *    Everything else (magic/len/checksum rejects) stays global —
 *    those failures aren't attributable to a trusted flow identity.
 *
 * 5. Teardown, at done: for each flow: free(flows[i]). Every malloc
 *    has exactly one owner and one free site; ASan's leak check is
 *    the gate. (Free-and-forget: NULLing slots after free is optional
 *    here since the program exits — but say why if asked: dangling
 *    PONTERS are only dangerous if used.)
 *
 * 6. Summary: per-flow lines + the existing global tallies. Print the
 *    flow key with inet_ntop + ntohs — the ONE place byte order gets
 *    converted (for display). Formatter is Claude's once your table
 *    lands.
 *
 * New headers: <stdlib.h> (malloc/free), <arpa/inet.h> (inet_ntop).
 */
#include <sys/socket.h>   /* socket, sendto            */
#include <netinet/in.h>   /* sockaddr_in, htons        */
#include <unistd.h>       /* close                     */
#include <string.h>       /* strerror                  */
#include <errno.h>        /* errno                     */
#include <signal.h>
#include <assert.h>
#include <inttypes.h>     /* PRIu64                    */
#include <stdio.h>        /* snprintf                  */
#include <stdlib.h>
#include <arpa/inet.h>
#include "netval.h"
#include "wire.h"
#include "log.h"
#include <sys/epoll.h>
#include <fcntl.h>

#define MAX_FLOWS 64

typedef struct {
    uint32_t addr;         /* src.sin_addr.s_addr, kept as-is */
    uint16_t port;         /* src.sin_port, kept as-is */
    uint32_t expected_next;
    uint64_t received;     /* valid datagrams from this flow */
    uint64_t gaps;         /* forward jumps (suspected loss) */
    uint64_t backward;     /* seq < expected: dup OR reorder —
                            we can't tell which yet;
                            count honestly under one name */
} flow_t;

static flow_t *flows[MAX_FLOWS];  
static int nflows = 0;

static volatile sig_atomic_t stop;

static void signal_handler(int signo) {
    (void)signo; /* to avoid flagging the unused parameter */
    stop = 1;
}

/* Find the flow for (addr, port), creating it on first sight.
 * Returns NULL if the table is full or allocation failed. */
static flow_t *flow_get(uint32_t addr, uint16_t port) {
    for (int i = 0; i < nflows; i++) {
        if (addr == flows[i]->addr && port == flows[i]->port) {
            return flows[i];
        }
    }        

    if (nflows == MAX_FLOWS) {
        return NULL;
    }    
    
    flow_t *f = calloc(1, sizeof(flow_t));
    if (f == NULL) {
        return NULL;
    }

    f->addr = addr;
    f->port = port;
    flows[nflows] = f;
    nflows += 1;
    return f;
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
    uint64_t flows_dropped = 0;
   
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
        
        // drain loop
        for (;;) {
            struct sockaddr_in src;
            socklen_t srclen = sizeof(src);

            ssize_t n = recvfrom(sfd, buf, sizeof(buf), MSG_TRUNC, (struct sockaddr *)&src, &srclen);

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

            flow_t *f = flow_get(src.sin_addr.s_addr, src.sin_port);
            if (f == NULL) {
                flows_dropped++;
                continue;
            }

            if (hdr.seq == f->expected_next) {
                f->expected_next = hdr.seq + 1;

            } else if (hdr.seq > f->expected_next) {
                f->gaps++;
                f->expected_next = hdr.seq + 1;

            } else {
                f->backward++;
            }

            f->received++;
        }
    }


    done:
    close(epfd);
    close(sfd);

    /* Summary. The reject classes are mutually exclusive — every datagram
     * the kernel handed us is counted in exactly one, so they sum to
     * "observed" rather than being tallied separately. "valid" is now the
     * sum of the per-flow tallies rather than its own counter, so a
     * bookkeeping slip in the flow table shows up as an inconsistency
     * instead of hiding. */
    uint64_t valid = 0, gaps = 0, backward = 0;
    for (int i = 0; i < nflows; i++) {
        valid    += flows[i]->received;
        gaps     += flows[i]->gaps;
        backward += flows[i]->backward;
    }
    uint64_t observed = valid + ovs + malformed + checksum_mismatch + flows_dropped;

    log_info("---- rx summary ----");
    /* Three exits: stop flag set → SIGINT; clean rc without the flag →
     * idle timeout; otherwise a syscall failure ended the loop. */
    log_info("stopped by         : %s",
             stop ? "SIGINT" : (rc == 0 ? "idle timeout" : "error"));
    log_info("datagrams observed : %" PRIu64, observed);
    log_info("  valid            : %" PRIu64, valid);
    log_info("  oversized        : %" PRIu64, ovs);
    log_info("  malformed        : %" PRIu64, malformed);
    log_info("  checksum failed  : %" PRIu64, checksum_mismatch);
    log_info("  untracked        : %" PRIu64 "  (table full / alloc failed)",
             flows_dropped);

    log_info("flows seen         : %d / %d", nflows, MAX_FLOWS);
    for (int i = 0; i < nflows; i++) {
        /* The one place the key gets byte-order converted: it is stored
         * network-order because it is only ever compared, but humans read
         * dotted-quad and decimal. */
        char ip[INET_ADDRSTRLEN];
        struct in_addr a = { .s_addr = flows[i]->addr };
        if (inet_ntop(AF_INET, &a, ip, sizeof(ip)) == NULL)
            snprintf(ip, sizeof(ip), "?");

        log_info("  %s:%-5u  valid %" PRIu64 "  gaps %" PRIu64
                 "  backward %" PRIu64 "  next %" PRIu32,
                 ip, ntohs(flows[i]->port), flows[i]->received,
                 flows[i]->gaps, flows[i]->backward,
                 flows[i]->expected_next);
    }
    if (gaps || backward)
        log_info("totals             : gaps %" PRIu64 "  backward %" PRIu64
                 "  (backward = duplicate OR late reorder — not separable"
                 " without per-seq history)", gaps, backward);

    /* Teardown LAST: the summary above reads these records. */
    for (int i = 0; i < nflows; i++) {
        free(flows[i]);
    }

    return rc;
}
