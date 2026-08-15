#ifndef AMIGMAIL_NETWORK_TASK_H
#define AMIGMAIL_NETWORK_TASK_H

#include "account.h"
#include "smtp.h"

typedef enum AmgNetCommandType {
    AMG_NET_STOP = 0,
    AMG_NET_CONNECT,
    AMG_NET_RECONFIGURE,
    AMG_NET_FETCH_LABELS,
    AMG_NET_FETCH_INBOX,
    AMG_NET_CHECK_INBOX,
    AMG_NET_FETCH_MESSAGE,
    AMG_NET_SET_SEEN,
    AMG_NET_SET_FLAGGED,
    AMG_NET_DELETE,
    AMG_NET_MOVE,
    AMG_NET_EMPTY_TRASH,
    AMG_NET_EMPTY_SPAM,
    AMG_NET_SAVE_DRAFT,
    AMG_NET_SEND_REPLY,
    AMG_NET_SEND_MAIL,
    AMG_NET_CHECK_UPDATE,
    AMG_NET_DOWNLOAD_UPDATE
} AmgNetCommandType;

typedef struct AmgNetwork AmgNetwork;

typedef struct AmgNetworkEvent {
    AmgNetCommandType type;
    int result;
    unsigned long uid;
    char argument1[768];
    char argument2[768];
    char message[256];
    unsigned char *payload;
    size_t payload_length;
} AmgNetworkEvent;

AmgNetwork *amg_network_create(void);
void amg_network_destroy(AmgNetwork *network);
int amg_network_start(AmgNetwork *network, const AmgAccount *account, AmgError *error);
void amg_network_stop(AmgNetwork *network);
int amg_network_is_running(const AmgNetwork *network);
int amg_network_is_connected(const AmgNetwork *network);
unsigned long amg_network_signal_mask(const AmgNetwork *network);
int amg_network_request(AmgNetwork *network, AmgNetCommandType type,
                        unsigned long uid, const char *argument1,
                        const char *argument2, AmgError *error);
int amg_network_request_reconfigure(AmgNetwork *network,
                                    const AmgAccount *account,
                                    AmgError *error);
int amg_network_request_reply(AmgNetwork *network, const AmgReplyDraft *draft,
                              AmgError *error);
int amg_network_request_mail(AmgNetwork *network, const AmgMailDraft *draft,
                             AmgError *error);
int amg_network_request_mail_from_draft(AmgNetwork *network,
                                        const AmgMailDraft *draft,
                                        unsigned long original_draft_uid,
                                        const char *original_draft_mailbox,
                                        AmgError *error);
int amg_network_request_draft(AmgNetwork *network, const AmgMailDraft *draft,
                              const char *draft_mailbox, AmgError *error);
int amg_network_request_draft_update(AmgNetwork *network,
                                     const AmgMailDraft *draft,
                                     const char *draft_mailbox,
                                     unsigned long original_draft_uid,
                                     const char *original_draft_mailbox,
                                     AmgError *error);
int amg_network_poll(AmgNetwork *network, AmgNetworkEvent *event);
void amg_network_event_clear(AmgNetworkEvent *event);

#endif
