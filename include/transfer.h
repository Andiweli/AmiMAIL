#ifndef AMIMAIL_TRANSFER_H
#define AMIMAIL_TRANSFER_H

#include "amigmail.h"
#include <stdio.h>

/* A callback returns zero to cancel before the next block. COMMIT marks the
 * non-cancellable protocol acknowledgement phase, not a completed delivery. */
typedef enum AmgTransferPhase {
    AMG_TRANSFER_PREPARE = 0,
    AMG_TRANSFER_RECEIVE,
    AMG_TRANSFER_INDEX,
    AMG_TRANSFER_UPLOAD,
    AMG_TRANSFER_COMMIT,
    AMG_TRANSFER_EXPORT
} AmgTransferPhase;
typedef struct AmgTransfer {
    int (*report)(void *context, AmgTransferPhase phase,
                  size_t done, size_t total);
    void *context;
} AmgTransfer;

int amg_transfer_report(AmgTransfer *transfer, AmgTransferPhase phase,
                        size_t done, size_t total, AmgError *error);
#define AMG_SPOOL_PATH_MAX 512U
int amg_spool_directory(char path[AMG_SPOOL_PATH_MAX], AmgError *error);
int amg_spool_create(const char *directory, char path[AMG_SPOOL_PATH_MAX],
                     FILE **file, AmgError *error);
void amg_spool_remove(const char *path);

#endif
