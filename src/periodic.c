#include "periodic.h"

static int deadline_reached(uint32_t now, uint32_t deadline)
{
    /* Deadlines are at most 30 minutes away. Unsigned subtraction avoids an
     * implementation-defined unsigned-to-signed conversion at clock wrap. */
    return (uint32_t)(now - deadline) < UINT32_C(0x80000000);
}

void amg_periodic_configure(AmgPeriodicSchedule *schedule, size_t slot,
                            int enabled, unsigned int minutes, uint32_t now)
{
    unsigned int seconds;
    if (!schedule || slot >= AMG_MAX_ACCOUNTS) return;
    seconds = enabled ? 60U * amg_periodic_interval_minutes(
        amg_periodic_interval_index(minutes)) : 0U;
    if (schedule->seconds[slot] != seconds) {
        schedule->seconds[slot] = seconds;
        schedule->due[slot] = seconds ? now + (uint32_t)seconds : 0U;
    }
}

unsigned long amg_periodic_take_due(AmgPeriodicSchedule *schedule, uint32_t now)
{
    size_t slot;
    unsigned long due = 0UL;
    if (!schedule) return 0UL;
    for (slot = 0U; slot < AMG_MAX_ACCOUNTS; ++slot) {
        if (schedule->seconds[slot] &&
            deadline_reached(now, schedule->due[slot])) {
            due |= 1UL << slot;
            /* A slow server or modal dialog must not cause catch-up bursts.
             * At most one check is issued per slot, then its normal interval. */
            schedule->due[slot] = now + (uint32_t)schedule->seconds[slot];
        }
    }
    return due;
}

unsigned long amg_periodic_wait(const AmgPeriodicSchedule *schedule, uint32_t now)
{
    size_t slot;
    uint32_t soonest = 0U;
    if (!schedule) return 0UL;
    for (slot = 0U; slot < AMG_MAX_ACCOUNTS; ++slot) {
        uint32_t delay;
        if (!schedule->seconds[slot]) continue;
        if (deadline_reached(now, schedule->due[slot])) return 1UL;
        delay = (uint32_t)(schedule->due[slot] - now);
        if (!soonest || delay < soonest) soonest = delay;
    }
    return (unsigned long)soonest;
}
