#ifndef AMIGMAIL_ACCOUNT_H
#define AMIGMAIL_ACCOUNT_H

#include "amigmail.h"

#define AMG_MAX_ACCOUNTS 5U
#define AMG_PERIODIC_INTERVAL_COUNT 6U
#define AMG_PERIODIC_DEFAULT_MINUTES 5U

typedef struct AmgAccount {
    int enabled;
    char account_name[96];
    char display_name[96];
    char email[256];
    AmgAuthMode auth_mode;
    char imap_host[256];
    unsigned short imap_port;
    int imap_starttls;
    char imap_username[256];
    char smtp_host[256];
    unsigned short smtp_port;
    int smtp_starttls;
    int smtp_same_credentials;
    char smtp_username[256];
    char sent_mailbox[512];
    char drafts_mailbox[512];
    char all_mailbox[512];
    char spam_mailbox[512];
    char trash_mailbox[512];
    int save_sent_copy;
    int fetch_on_start;
    int periodic_fetch;
    unsigned int periodic_fetch_minutes; /* 1, 2, 5, 10, 15 or 30. */
    unsigned int fetch_days;
    int notification_sound;
    int herald_notifications; /* Optional, independent of sound; default off. */
    char notification_sound_path[512];
    char *imap_password;
    char *smtp_password;
    char *refresh_token;
} AmgAccount;

typedef struct AmgAccountSet {
    AmgAccount accounts[AMG_MAX_ACCOUNTS];
    size_t order[AMG_MAX_ACCOUNTS];
    size_t current;
} AmgAccountSet;

unsigned int amg_periodic_interval_minutes(size_t selection);
size_t amg_periodic_interval_index(unsigned int minutes);

void amg_account_init(AmgAccount *account);
void amg_account_clear(AmgAccount *account);
int amg_account_set_secret(char **destination, const char *value);
int amg_account_copy(AmgAccount *destination, const AmgAccount *source);
void amg_account_normalize(AmgAccount *account);
int amg_account_is_google_host(const char *host);
int amg_account_should_append_sent(const AmgAccount *account);
int amg_account_validate(const AmgAccount *account, AmgError *error);
const char *amg_account_imap_user(const AmgAccount *account);
const char *amg_account_smtp_user(const AmgAccount *account);
const char *amg_account_smtp_password(const AmgAccount *account);

void amg_account_set_init(AmgAccountSet *set);
void amg_account_set_clear(AmgAccountSet *set);
size_t amg_account_set_first_enabled(const AmgAccountSet *set);
size_t amg_account_set_enabled_count(const AmgAccountSet *set);

#endif
