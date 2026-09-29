#ifndef AMIMAIL_HERALD_H
#define AMIMAIL_HERALD_H

#include "amigmail.h"
#include "account.h"

/* Herald protocol level 1 (Developer/HeraldSend/engine.h). Local single-byte
 * text, not UTF-8. IDs below refer to persistent account slots, NOT tab order. */
#define AMG_HERALD_TEXT_MAX 160U
#define AMG_HERALD_COMMAND_MAX 512U

typedef struct AmgHerald AmgHerald;
typedef enum AmgHeraldResult {
    AMG_HERALD_IDLE = 0,
    AMG_HERALD_QUEUED,
    AMG_HERALD_ACCEPTED,
    AMG_HERALD_NOT_RUNNING,
    AMG_HERALD_UNAVAILABLE,
    AMG_HERALD_REJECTED,
    AMG_HERALD_TIMEOUT,
    AMG_HERALD_BUSY
} AmgHeraldResult;

int amg_herald_build_notify(size_t account_slot, int test,
                            const char *text_local, char *command,
                            size_t capacity);
int amg_herald_running(void);
AmgHerald *amg_herald_create(void);
void amg_herald_destroy(AmgHerald *client);
unsigned long amg_herald_signal_mask(const AmgHerald *client);
void amg_herald_poll(AmgHerald *client);
AmgHeraldResult amg_herald_notify(AmgHerald *client, size_t account_slot,
                                 const char *text_local, int test);
AmgHeraldResult amg_herald_test_result(const AmgHerald *client);
/* Cancel unsent notices when a saved account is disabled/removed/changed. */
void amg_herald_discard_account(AmgHerald *client, size_t account_slot);

#endif
