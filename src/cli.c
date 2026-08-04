/* cli.c — command-line parsing via getopt_long (boilerplate, Claude-generated). */
#include "cli.h"
#include "wire.h"

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s --mode tx|rx [options]\n"
        "\n"
        "  -m, --mode tx|rx     role: sender (tx) or receiver (rx)  [required]\n"
        "  -d, --dest IP        tx: destination IPv4 address        [127.0.0.1]\n"
        "  -p, --port PORT      UDP port                            [9000]\n"
        "  -c, --count N        tx: number of packets to send       [1000]\n"
        "  -r, --rate PPS       tx: packets per second, 0=unlimited [0]\n"
        "  -l, --payload BYTES  tx: payload size per packet, max %d [32]\n"
        "  -t, --idle-timeout S rx: stop after S seconds with no packets,\n"
        "                       0 = wait forever                    [0]\n"
        "  -h, --help           show this help\n",
        prog, NETVAL_MAX_PAYLOAD);
}

/* strtoul with full error checking; returns 0 on success. */
static int parse_ulong(const char *s, unsigned long max, unsigned long *out)
{
    char *end = NULL;

    errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v > max)
        return -1;
    *out = v;
    return 0;
}

int cli_parse(int argc, char **argv, netval_cfg *cfg)
{
    static const struct option longopts[] = {
        { "mode",    required_argument, NULL, 'm' },
        { "dest",    required_argument, NULL, 'd' },
        { "port",    required_argument, NULL, 'p' },
        { "count",   required_argument, NULL, 'c' },
        { "rate",    required_argument, NULL, 'r' },
        { "payload",      required_argument, NULL, 'l' },
        { "idle-timeout", required_argument, NULL, 't' },
        { "help",         no_argument,       NULL, 'h' },
        { 0, 0, 0, 0 },
    };

    /* Defaults. */
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->dest_ip, sizeof(cfg->dest_ip), "127.0.0.1");
    cfg->port = 9000;
    cfg->count = 1000;
    cfg->rate_pps = 0;
    cfg->payload_len = 32;
    cfg->idle_timeout_s = 0;

    int have_mode = 0;
    int opt;
    unsigned long v;

    while ((opt = getopt_long(argc, argv, "m:d:p:c:r:l:t:h", longopts, NULL)) != -1) {
        switch (opt) {
        case 'm':
            if (strcmp(optarg, "tx") == 0) {
                cfg->mode = MODE_TX;
            } else if (strcmp(optarg, "rx") == 0) {
                cfg->mode = MODE_RX;
            } else {
                fprintf(stderr, "error: --mode must be 'tx' or 'rx'\n");
                usage(argv[0]);
                return -1;
            }
            have_mode = 1;
            break;
        case 'd':
            if (strlen(optarg) >= sizeof(cfg->dest_ip)) {
                fprintf(stderr, "error: --dest address too long\n");
                return -1;
            }
            snprintf(cfg->dest_ip, sizeof(cfg->dest_ip), "%s", optarg);
            break;
        case 'p':
            if (parse_ulong(optarg, 65535, &v) != 0 || v == 0) {
                fprintf(stderr, "error: --port must be 1-65535\n");
                return -1;
            }
            cfg->port = (uint16_t)v;
            break;
        case 'c':
            if (parse_ulong(optarg, UINT32_MAX, &v) != 0 || v == 0) {
                fprintf(stderr, "error: --count must be a positive integer\n");
                return -1;
            }
            cfg->count = (uint32_t)v;
            break;
        case 'r':
            if (parse_ulong(optarg, UINT32_MAX, &v) != 0) {
                fprintf(stderr, "error: --rate must be a non-negative integer\n");
                return -1;
            }
            cfg->rate_pps = (uint32_t)v;
            break;
        case 'l':
            if (parse_ulong(optarg, NETVAL_MAX_PAYLOAD, &v) != 0) {
                fprintf(stderr, "error: --payload must be 0-%d\n", NETVAL_MAX_PAYLOAD);
                return -1;
            }
            cfg->payload_len = (uint16_t)v;
            break;
        case 't':
            /* Cap at 3600 s so seconds→milliseconds for epoll_wait's
             * int timeout can never overflow. */
            if (parse_ulong(optarg, 3600, &v) != 0) {
                fprintf(stderr, "error: --idle-timeout must be 0-3600 seconds\n");
                return -1;
            }
            cfg->idle_timeout_s = (uint32_t)v;
            break;
        case 'h':
            usage(argv[0]);
            exit(0);
        default:
            usage(argv[0]);
            return -1;
        }
    }

    if (!have_mode) {
        fprintf(stderr, "error: --mode is required\n");
        usage(argv[0]);
        return -1;
    }
    return 0;
}
