/* cli.h — command-line parsing (boilerplate, Claude-generated). */
#ifndef NETVAL_CLI_H
#define NETVAL_CLI_H

#include <netinet/in.h> /* INET_ADDRSTRLEN */
#include <stdint.h>

typedef enum {
    MODE_TX,
    MODE_RX,
} netval_mode;

typedef struct {
    netval_mode mode;
    char        dest_ip[INET_ADDRSTRLEN]; /* tx: where to send */
    uint16_t    port;                     /* tx: dest port; rx: listen port */
    uint32_t    count;                    /* tx: packets to send */
    uint32_t    rate_pps;                 /* tx: packets/sec, 0 = unlimited */
    uint16_t    payload_len;              /* tx: payload bytes per packet */
    uint32_t    idle_timeout_s;           /* rx: stop after N idle seconds,
                                             0 = wait forever (Ctrl-C only) */
} netval_cfg;

/* Parse argv into *cfg (defaults applied first). Returns 0 on success,
 * -1 on bad/missing arguments (usage already printed to stderr). */
int cli_parse(int argc, char **argv, netval_cfg *cfg);

#endif /* NETVAL_CLI_H */
