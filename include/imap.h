#ifndef AMIGMAIL_IMAP_H
#define AMIGMAIL_IMAP_H

#include "account.h"
#include "tls.h"

typedef struct AmgImapSession {
    AmgTlsConnection *connection;
    unsigned long tag_counter;
    unsigned long uid_validity;
    int authenticated;
    int capability_move;
    int capability_uidplus;
    int capability_special_use;
    int capability_starttls;
    int capability_auth_plain;
    int capability_sasl_ir;
    int capability_login_disabled;
    unsigned long selected_exists;
    char selected_mailbox[512];
    char special_mailboxes[7][512];
    char configured_special_mailboxes[7][512];
} AmgImapSession;

typedef struct AmgImapLabel {
    char name_utf8[512];
    char wire_name[768];
    char delimiter;
    unsigned long special_use;
    int selectable;
} AmgImapLabel;

#define AMG_LABEL_INBOX      (1UL << 0)
#define AMG_LABEL_SENT       (1UL << 1)
#define AMG_LABEL_DRAFTS     (1UL << 2)
#define AMG_LABEL_TRASH      (1UL << 3)
#define AMG_LABEL_SPAM       (1UL << 4)
#define AMG_LABEL_ALL        (1UL << 5)
#define AMG_LABEL_FLAGGED    (1UL << 6)

void amg_imap_session_init(AmgImapSession *session);
void amg_imap_disconnect(AmgImapSession *session);
int amg_imap_connect(AmgImapSession *session, const AmgAccount *account,
                     const char *access_token, AmgError *error);
int amg_imap_list_labels(AmgImapSession *session, AmgImapLabel *labels,
                         size_t capacity, size_t *count, AmgError *error);
int amg_imap_select(AmgImapSession *session, const char *mailbox_utf8, AmgError *error);
int amg_imap_fetch_page(AmgImapSession *session, unsigned long before_uid,
                        size_t limit, AmgBuffer *response, AmgError *error);
int amg_imap_fetch_recent(AmgImapSession *session, unsigned int days,
                          AmgBuffer *response, AmgError *error);
int amg_imap_fetch_after_uid(AmgImapSession *session, unsigned long uid,
                             AmgBuffer *response, AmgError *error);
int amg_imap_fetch_message(AmgImapSession *session, unsigned long uid,
                           AmgBuffer *message, AmgError *error);
int amg_imap_set_seen(AmgImapSession *session, unsigned long uid, int seen, AmgError *error);
int amg_imap_set_flagged(AmgImapSession *session, unsigned long uid,
                         int flagged, AmgError *error);
int amg_imap_move_label(AmgImapSession *session, unsigned long uid,
                        const char *source_label, const char *destination_label,
                        AmgError *error);
int amg_imap_move_to_trash(AmgImapSession *session, unsigned long uid,
                           const char *trash_label, AmgError *error);
int amg_imap_delete_uid(AmgImapSession *session, unsigned long uid,
                        const char *mailbox_utf8, AmgError *error);
int amg_imap_empty_mailbox(AmgImapSession *session, const char *mailbox_utf8,
                           AmgError *error);
int amg_imap_append_draft(AmgImapSession *session, const char *mailbox_utf8,
                          const unsigned char *message, size_t length,
                          AmgError *error);
int amg_imap_append_sent(AmgImapSession *session, const unsigned char *message,
                         size_t length, AmgError *error);

#endif
