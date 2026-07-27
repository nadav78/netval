/* netval.h — entry points for the two roles. Implemented by YOU in
 * tx.c and rx.c. Both return 0 on success, -1 on failure. */
#ifndef NETVAL_NETVAL_H
#define NETVAL_NETVAL_H

#include "cli.h"

int tx_run(const netval_cfg *cfg);
int rx_run(const netval_cfg *cfg);

#endif /* NETVAL_NETVAL_H */
