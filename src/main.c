/* main.c — entry point glue (boilerplate, Claude-generated). */
#include "cli.h"
#include "log.h"
#include "netval.h"

int main(int argc, char **argv)
{
    netval_cfg cfg;

    if (cli_parse(argc, argv, &cfg) != 0)
        return 2;

    log_info("netval %s starting (port %u)",
             cfg.mode == MODE_TX ? "tx" : "rx", cfg.port);

    int rc = (cfg.mode == MODE_TX) ? tx_run(&cfg) : rx_run(&cfg);
    return rc == 0 ? 0 : 1;
}
