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
#include "netval.h"
#include "log.h"
#include "wire.h"

int tx_run(const netval_cfg *cfg)
{
    /* TODO(you): implement per the plan above. */
    (void)cfg;
    log_error("tx_run: not implemented yet");
    return -1;
}
