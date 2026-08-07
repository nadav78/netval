/* tx.c — the sender. M1 (single-threaded) done; now Phase B:
 *
 * >>> YOURS TO IMPLEMENT: --threads N worker senders <<<
 *
 * Model: one worker = one FLOW = one socket = one seq space from 0 =
 * one stats struct. Workers share nothing they write. (The rejected
 * alternative — splitting one global seq range across workers — would
 * make normal thread interleaving look like loss/reorder to rx.)
 *
 * The pieces:
 *
 * 1. A per-worker args/results struct (workers can't take parameters
 *    any other way — pthread start routines get ONE void*):
 *        typedef struct {
 *            const netval_cfg   *cfg;    read-only, shared
 *            struct sockaddr_in  dest;   or a shared const pointer
 *            int                 id;     0..N-1, for logs
 *            uint64_t            sent;   result: filled by the worker
 *            int                 rc;     result: 0 / -1
 *        } tx_worker;
 *    One array of these in tx_run, one element per worker — the
 *    element is OWNED by its worker until join.
 *
 * 2. The worker function:
 *        static void *worker(void *arg)
 *    First line: cast arg back — tx_worker *w = arg;
 *    Body = your existing M1 send loop, MOVED here (socket, send
 *    loop, rate pacing, close). Each worker creates ITS OWN socket —
 *    the OS gives each a distinct source port, which is what makes it
 *    a distinct flow at rx. Results go in w->sent / w->rc; return
 *    NULL (or return w — unused either way).
 *    NOTE inside the loop: errors set w->rc and break — never exit()
 *    (D6: it would kill all workers mid-send) and never return -1
 *    (that's a void* — returning an int through it is a bug).
 *
 * 3. tx_run becomes spawn + join:
 *        pthread_t tids[cfg->threads];
 *        for i: fill workers[i]; pthread_create(&tids[i], NULL,
 *                                               worker, &workers[i]);
 *        for i: pthread_join(tids[i], NULL);
 *    pthread_create/join return 0 or an ERROR NUMBER directly (they
 *    do NOT set errno — use strerror(ret), a classic trap).
 *    Join in order 0..N-1 — join order doesn't matter, you need ALL.
 *    After the joins: aggregate workers[].sent / .rc for the summary
 *    and the return code. N=1 goes through the same path — no
 *    special case.
 *
 * 4. Early stop (Ctrl-C) — one shared flag, done RIGHT this time:
 *        #include <stdatomic.h>
 *        static atomic_bool stop;              file scope
 *        handler: atomic_store(&stop, true);   still the only line
 *        worker loop condition: && !atomic_load(&stop)
 *    Why not volatile sig_atomic_t like rx? That type is guaranteed
 *    only against SIGNALS (same-thread interruption). For visibility
 *    ACROSS THREADS C11 requires an atomic; a lock-free atomic_bool
 *    is safe for both the handler write and cross-thread reads —
 *    one flag covering both hazards. (Install SIGINT via sigaction,
 *    same as rx. TSan will validate this choice under load.)
 *
 * Ownership table (write it in PROGRESS.md when done):
 *    cfg, dest      shared, READ-ONLY after spawn — safe, say why
 *    workers[i]     worker i writes, main reads AFTER join — the
 *                   join IS the synchronization point
 *    stop           handler writes, workers read — atomic
 *
 * New headers: <pthread.h>, <stdatomic.h>, <signal.h>.
 * Link: -lpthread (Makefile already does).
 */
#include <sys/socket.h>   /* socket, sendto            */
#include <netinet/in.h>   /* sockaddr_in, htons        */
#include <arpa/inet.h>    /* inet_pton                 */
#include <unistd.h>       /* close                     */
#include <string.h>       /* strerror                  */
#include <errno.h>        /* errno                     */
#include <time.h>         /* clock_gettime, nanosleep  */
#include <stdio.h>        /* perror                    */
#include <assert.h>
#include "netval.h"
#include "wire.h"
#include "log.h"


int tx_run(const netval_cfg *cfg)
{
    assert(cfg != NULL);

    int sfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sfd == -1) {
        log_error("socket: %s", strerror(errno));
        return -1;
    }

    // build destination address
    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(cfg->port);

    int s = inet_pton(AF_INET, cfg->dest_ip, &dest.sin_addr);
    if (s == -1) {
        perror("inet_pton");
        close(sfd);
        return -1;
    }
    if (s == 0) {
        log_error("invalid destination address: %s", cfg->dest_ip);
        close(sfd);
        return -1;
    }

    // send loop
    for (uint32_t seq = 0; seq < cfg->count; seq++) {
        netval_hdr header = {0};
        header.magic = NETVAL_MAGIC;
        header.seq = seq;
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        header.ts_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
        header.payload_len = cfg->payload_len;

        // the bytes all go in a buffer: [24-byte-header][payload]
        uint8_t buf[NETVAL_MAX_DGRAM];
        for (uint16_t i = 0; i < cfg->payload_len; i++) {
            buf[NETVAL_HDR_SIZE + i] = (uint8_t)(seq + i);
        }

        header.checksum = wire_checksum(buf + NETVAL_HDR_SIZE, cfg->payload_len);

        if (wire_hdr_pack(&header, buf, NETVAL_MAX_DGRAM) == -1) {
            log_error("failed to wire header");
            close(sfd);
            return -1;
        }

        if (sendto(sfd, buf, NETVAL_HDR_SIZE + cfg->payload_len, 0, 
            (struct sockaddr *)&dest, sizeof(dest)) == -1) {
                log_error("error sending to server: %s", strerror(errno));
                close(sfd);
                return -1;
            }
        
        if (cfg->rate_pps > 0) {
            struct timespec gap;
            gap.tv_nsec = 1000000000ULL / cfg->rate_pps;
            gap.tv_sec = 0;
            if (nanosleep(&gap, NULL) == -1) {
                log_error("sleep failed: %s", strerror(errno));
            }
        }
    }
    close(sfd);
    log_info("Sent %u packets", cfg->count);
    return 0;

}
