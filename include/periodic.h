#ifndef AMIMAIL_PERIODIC_H
#define AMIMAIL_PERIODIC_H

#include "account.h"

/* Absolute monotonic seconds, with wrap-safe comparisons. Account slots are
 * stable; changing tab order must never change another account's deadline. */
typedef struct AmgPeriodicSchedule {
    uint32_t due[AMG_MAX_ACCOUNTS];
    unsigned int seconds[AMG_MAX_ACCOUNTS];
} AmgPeriodicSchedule;

void amg_periodic_configure(AmgPeriodicSchedule *schedule, size_t slot,
                            int enabled, unsigned int minutes, uint32_t now);
unsigned long amg_periodic_take_due(AmgPeriodicSchedule *schedule, uint32_t now);
unsigned long amg_periodic_wait(const AmgPeriodicSchedule *schedule, uint32_t now);

#endif
