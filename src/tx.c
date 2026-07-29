/* tx.c — the sender.
 *
 * >>> YOURS TO IMPLEMENT <<<
 *
 * Blocking UDP send loop. The system calls you need, in order:
 *
 * 1. socket(AF_INET, SOCK_DGRAM, 0)
 *      Creates a UDP socket. AF_INET = IPv4, SOCK_DGRAM = UDP
 *      (datagrams). Returns a file descriptor (small int) or -1; on
 *      any syscall failure, log strerror(errno) and bail out.
 *
 * 2. Build the destination address — a struct sockaddr_in:
 *        .sin_family = AF_INET
 *        .sin_port   = htons(cfg->port)        <- 16-bit, network order!
 *        .sin_addr   = via inet_pton(AF_INET, cfg->dest_ip, &addr.sin_addr)
 *      inet_pton converts "127.0.0.1" text to the binary address;
 *      returns 1 on success (0 = malformed address — check it).
 *
 * 3. The loop, for seq = 0 .. cfg->count-1:
 *      a. Fill a netval_hdr: magic, seq, ts_ns from
 *         clock_gettime(CLOCK_REALTIME) (ts.tv_sec * 1000000000ull +
 *         ts.tv_nsec), payload_len = cfg->payload_len.
 *      b. Fill the payload with a deterministic pattern the receiver
 *         can re-derive, e.g. payload[i] = (uint8_t)(seq + i).
 *      c. checksum = wire_checksum(payload, payload_len); then
 *         wire_hdr_pack into buf, memcpy payload after the header.
 *      d. sendto(fd, buf, NETVAL_HDR_SIZE + payload_len, 0,
 *                (struct sockaddr *)&dest, sizeof(dest))
 *         One datagram per call. Check for -1.
 *      e. Rate limiting: if cfg->rate_pps > 0, sleep between sends —
 *         nanosleep() with interval 1e9 / rate_pps nanoseconds is fine
 *         for Milestone 1.
 *
 * 4. close(fd), log a summary ("sent N packets"), return 0.
 *
 * A single stack buffer uint8_t buf[NETVAL_MAX_DGRAM] is enough — no
 * malloc needed here.
 *
 * Headers you'll need: <sys/socket.h>, <netinet/in.h>, <arpa/inet.h>,
 * <unistd.h>, <string.h>, <errno.h>, <time.h>.
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
