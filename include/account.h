#ifndef AMIGMAIL_ACCOUNT_H
#define AMIGMAIL_ACCOUNT_H

#include "amigmail.h"

typedef struct AmgAccount {
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
    unsigned int fetch_days;
    int notification_sound;
    char notification_sound_path[512];
    char *imap_password;
    char *smtp_password;
    char *refresh_token;
} AmgAccount;

void amg_account_init(AmgAccount *account);
void amg_account_clear(AmgAccount *account);
int amg_account_set_secret(char **destination, const char *value);
void amg_account_normalize(AmgAccount *account);
int amg_account_is_google_host(const char *host);
int amg_account_should_append_sent(const AmgAccount *account);
int amg_account_validate(const AmgAccount *account, AmgError *error);
const char *amg_account_imap_user(const AmgAccount *account);
const char *amg_account_smtp_user(const AmgAccount *account);
const char *amg_account_smtp_password(const AmgAccount *account);

#endif
