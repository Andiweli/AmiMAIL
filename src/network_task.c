#include "network_task.h"
#include "imap.h"
#include "oauth.h"
#include "oauth_client_config.h"
#include "tls.h"
#include "update.h"
#include "i18n.h"

#define T(id, en) amg_tr((id), (en))

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if AMIGMAIL_AMIGA
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <proto/dos.h>
#include <proto/exec.h>

#define AMG_NET_BODY_MAX 32768U
#define AMG_NET_PATH_MAX 512U
#define AMG_NET_NAME_MAX 256U

typedef struct AmgNetAttachment {
    char path[AMG_NET_PATH_MAX];
    char name_utf8[AMG_NET_NAME_MAX];
    unsigned long size;
    int delete_after_use;
} AmgNetAttachment;

typedef struct AmgNetMessage {
    struct Message message;
    AmgNetCommandType type;
    int result;
    unsigned long uid;
    char argument1[768];
    char argument2[768];
    char from[768];
    char to[768];
    char cc[768];
    char bcc[768];
    char subject[512];
    char body[AMG_NET_BODY_MAX];
    char in_reply_to[512];
    char references[1024];
    char date[96];
    char message_id[256];
    AmgNetAttachment attachments[AMG_MAIL_MAX_ATTACHMENTS];
    size_t attachment_count;
    AmgAccount account_update;
    AmgBuffer payload;
    AmgError error;
} AmgNetMessage;

struct AmgNetwork {
    struct MsgPort *commands;
    struct MsgPort *responses;
    struct Process *process;
    AmgAccount account;
    volatile int worker_ready;
    volatile int connected;
    int running;
};

static AmgNetwork *starting_network;

static void copy_text(char *destination, size_t capacity, const char *source)
{
    if (!source) source = "";
    strncpy(destination, source, capacity - 1U);
    destination[capacity - 1U] = 0;
}

static int text_fits(const char *text, size_t capacity)
{
    return !text || strlen(text) < capacity;
}

static int copy_account_deep(AmgAccount *destination,
                             const AmgAccount *source, AmgError *error)
{
    AmgAccount copy;
    if (!destination || !source) return AMG_ERR_ARGUMENT;
    copy = *source;
    copy.imap_password = NULL;
    copy.smtp_password = NULL;
    copy.refresh_token = NULL;
    if (amg_account_set_secret(&copy.imap_password, source->imap_password) != AMG_OK ||
        amg_account_set_secret(&copy.smtp_password, source->smtp_password) != AMG_OK ||
        amg_account_set_secret(&copy.refresh_token, source->refresh_token) != AMG_OK) {
        amg_account_clear(&copy);
        amg_error_set(error, AMG_ERR_MEMORY,
                      T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory."));
        return AMG_ERR_MEMORY;
    }
    amg_account_clear(destination);
    *destination = copy;
    return AMG_OK;
}

static void free_net_message(AmgNetMessage *message)
{
    if (!message) return;
    amg_account_clear(&message->account_update);
    amg_buffer_free(&message->payload);
    free(message);
}

static int connect_network_account(AmgNetwork *network,
                                   AmgImapSession *imap,
                                   AmgOAuthTokens *tokens,
                                   AmgError *error)
{
    int result = AMG_OK;
    if (!network || !imap || !tokens) return AMG_ERR_ARGUMENT;
    network->connected = 0;
    amg_imap_disconnect(imap);
    amg_oauth_tokens_clear(tokens);
    if (network->account.auth_mode == AMG_AUTH_OAUTH2_GOOGLE) {
        AmgOAuthConfig config = {
            AMIGMAIL_OAUTH_CLIENT_ID,
            AMIGMAIL_OAUTH_CLIENT_SECRET,
            "",
            AMIGMAIL_OAUTH_SCOPE
        };
        if (!AMIGMAIL_OAUTH_CLIENT_ID[0]) {
            amg_error_set(
                error, AMG_ERR_AUTH,
                T(MSG_OAUTH_CLIENT_ID_IS_NOT_CONFIGURED_IN_THIS, "OAuth client ID is not configured in this build."));
            result = AMG_ERR_AUTH;
        } else {
            result = amg_oauth_refresh(
                &config, network->account.refresh_token, tokens, error);
        }
    }
    if (result == AMG_OK)
        result = amg_imap_connect(
            imap, &network->account, tokens->access_token, error);
    if (result == AMG_OK) network->connected = 1;
    return result;
}

static const char *network_operation(AmgNetCommandType type)
{
    switch (type) {
        case AMG_NET_CONNECT:
            return T(MSG_IMAP_CONNECTION, "IMAP connection");
        case AMG_NET_RECONFIGURE:
            return T(MSG_RECONFIGURE_MAIL_SERVER_CONNECTION, "reconfigure mail-server connection");
        case AMG_NET_FETCH_LABELS:
            return T(MSG_FETCH_IMAP_FOLDERS, "fetch IMAP folders");
        case AMG_NET_FETCH_INBOX:
            return T(MSG_FETCH_FOLDER, "fetch folder");
        case AMG_NET_CHECK_INBOX:
            return T(MSG_PERIODIC_INBOX_CHECK, "periodic Inbox check");
        case AMG_NET_FETCH_MESSAGE:
            return T(MSG_FETCH_MESSAGE, "fetch message");
        case AMG_NET_SET_SEEN:
            return T(MSG_SET_READ_STATUS, "set read status");
        case AMG_NET_SET_FLAGGED:
            return T(MSG_SET_STAR, "set star");
        case AMG_NET_DELETE:
            return T(MSG_DELETE_MESSAGE, "delete message");
        case AMG_NET_MOVE:
            return T(MSG_MOVE_MESSAGE, "move message");
        case AMG_NET_EMPTY_TRASH:
            return T(MSG_EMPTY_TRASH_8804, "empty Trash");
        case AMG_NET_EMPTY_SPAM:
            return T(MSG_EMPTY_SPAM_24AD, "empty Spam");
        case AMG_NET_SAVE_DRAFT:
            return T(MSG_SAVE_DRAFT, "save draft");
        case AMG_NET_SEND_REPLY:
            return T(MSG_SEND_REPLY, "send reply");
        case AMG_NET_SEND_MAIL:
            return T(MSG_SEND_MAIL, "send mail");
        case AMG_NET_CHECK_UPDATE:
            return T(MSG_CHECK_AMIMAIL_UPDATE, "check AmiMAIL update");
        case AMG_NET_DOWNLOAD_UPDATE:
            return T(MSG_DOWNLOAD_AMIMAIL_UPDATE, "download AmiMAIL update");
        default:
            return T(MSG_NETWORK_ACCESS, "network access");
    }
}

static void qualify_error(AmgNetCommandType type, int result,
                          AmgError *error)
{
    char detail[256];
    char message[256];
    if (!error || result == AMG_OK) return;
    copy_text(detail, sizeof(detail), error->message);
    if (detail[0])
        snprintf(message, sizeof(message), "%s: %.190s (Code %d)",
                 network_operation(type), detail, result);
    else
        amg_tr_snprintf(message, sizeof(message), MSG_VALUE_FAILED_CODE_VALUE, "%s failed (code %d).", network_operation(type), result);
    amg_error_set(error, result, message);
}

static void finish_message(AmgNetMessage *message, int result,
                           AmgError *error)
{
    message->result = result;
    if (error) message->error = *error;
    ReplyMsg((struct Message *)message);
}

static void append_success_warning(AmgError *error, const char *prefix,
                                   const AmgError *detail)
{
    char addition[224];
    char combined[256];
    const char *reason = detail && detail->message[0]
        ? detail->message
        : T(MSG_UNKNOWN_IMAP_ERROR, "unknown IMAP error");
    if (!error || !prefix) return;
    snprintf(addition, sizeof(addition), "%s: %.150s", prefix, reason);
    if (error->message[0]) {
        snprintf(combined, sizeof(combined), "%.112s; %.135s",
                 error->message, addition);
        amg_error_set(error, AMG_OK, combined);
    } else {
        amg_error_set(error, AMG_OK, addition);
    }
}

static int ensure_imap_connected(AmgNetwork *network, AmgImapSession *imap,
                                 const char *access_token, AmgError *error)
{
    int result;
    if (!network || !imap) return AMG_ERR_ARGUMENT;
    if (network->connected && imap->connection) return AMG_OK;
    amg_imap_disconnect(imap);
    result = amg_imap_connect(imap, &network->account, access_token, error);
    if (result == AMG_OK) network->connected = 1;
    return result;
}

static void cleanup_temp_attachments(AmgNetMessage *message)
{
    size_t i;
    if (!message) return;
    for (i = 0; i < message->attachment_count; ++i) {
        if (message->attachments[i].delete_after_use &&
            message->attachments[i].path[0]) {
            (void)remove(message->attachments[i].path);
            message->attachments[i].path[0] = 0;
        }
    }
}

static int send_new_mail(AmgNetwork *network, AmgImapSession *imap,
                         AmgNetMessage *message, const char *access_token,
                         AmgError *error)
{
    AmgAttachmentInput attachments[AMG_MAIL_MAX_ATTACHMENTS];
    AmgMailDraft draft;
    AmgBuffer raw;
    AmgError detail_error;
    size_t i;
    int result;
    int detail_result;

    for (i = 0; i < message->attachment_count; ++i) {
        attachments[i].path = message->attachments[i].path;
        attachments[i].name_utf8 = message->attachments[i].name_utf8;
        attachments[i].size = message->attachments[i].size;
        attachments[i].delete_after_use =
            message->attachments[i].delete_after_use;
    }
    draft.from = message->from;
    draft.to = message->to;
    draft.cc = message->cc;
    draft.bcc = message->bcc;
    draft.subject = message->subject;
    draft.body_utf8 = message->body;
    draft.date_rfc2822 = message->date;
    draft.message_id = message->message_id;
    draft.in_reply_to = message->in_reply_to;
    draft.references = message->references;
    draft.attachments = attachments;
    draft.attachment_count = message->attachment_count;

    result = amg_smtp_send_mail(&network->account, access_token, &draft, error);
    if (result != AMG_OK) return result;
    amg_error_set(error, AMG_OK, "");

    if (amg_account_should_append_sent(&network->account)) {
        memset(&detail_error, 0, sizeof(detail_error));
        detail_result = ensure_imap_connected(network, imap, access_token,
                                              &detail_error);
        amg_buffer_init(&raw);
        if (detail_result == AMG_OK)
            /* Keep Bcc in the private Sent copy. It was intentionally omitted
             * from the SMTP DATA sent to recipients. */
            detail_result = amg_smtp_build_mail(&draft, 1, &raw,
                                                &detail_error);
        if (detail_result == AMG_OK)
            detail_result = amg_imap_append_sent(
                imap, raw.data, raw.length, &detail_error);
        amg_buffer_free(&raw);
        if (detail_result != AMG_OK) {
            if (detail_result == AMG_ERR_IO || detail_result == AMG_ERR_TLS) {
                network->connected = 0;
                amg_imap_disconnect(imap);
            }
            append_success_warning(
                error,
                T(MSG_MAIL_WAS_SENT_BUT_COULD_NOT_BE_SAVED, "Mail was sent, but could not be saved in the Sent folder"),
                &detail_error);
        }
    }

    /* When this send originated from an existing IMAP draft, the SMTP
     * delivery is the commit point.  Only now may the original draft be
     * removed.  Failure to remove it is a warning, never a failed send. */
    if (message->uid && message->argument1[0]) {
        memset(&detail_error, 0, sizeof(detail_error));
        detail_result = ensure_imap_connected(network, imap, access_token,
                                              &detail_error);
        if (detail_result == AMG_OK)
            detail_result = amg_imap_delete_uid(
                imap, message->uid, message->argument1, &detail_error);
        if (detail_result != AMG_OK) {
            copy_text(message->argument2, sizeof(message->argument2),
                      "draft-kept");
            if (detail_result == AMG_ERR_IO || detail_result == AMG_ERR_TLS) {
                network->connected = 0;
                amg_imap_disconnect(imap);
            }
            append_success_warning(
                error,
                T(MSG_MAIL_WAS_SENT_BUT_THE_OLD_DRAFT_COULD, "Mail was sent, but the old draft could not be removed"),
                &detail_error);
        } else {
            copy_text(message->argument2, sizeof(message->argument2),
                      "draft-removed");
        }
    }
    return AMG_OK;
}

static int save_mail_draft(AmgNetwork *network, AmgImapSession *imap,
                           AmgNetMessage *message, AmgError *error)
{
    AmgAttachmentInput attachments[AMG_MAIL_MAX_ATTACHMENTS];
    AmgMailDraft draft;
    AmgBuffer raw;
    AmgError delete_error;
    size_t i;
    int result;
    int delete_result;

    for (i = 0; i < message->attachment_count; ++i) {
        attachments[i].path = message->attachments[i].path;
        attachments[i].name_utf8 = message->attachments[i].name_utf8;
        attachments[i].size = message->attachments[i].size;
        attachments[i].delete_after_use =
            message->attachments[i].delete_after_use;
    }
    draft.from = message->from;
    draft.to = message->to;
    draft.cc = message->cc;
    draft.bcc = message->bcc;
    draft.subject = message->subject;
    draft.body_utf8 = message->body;
    draft.date_rfc2822 = message->date;
    draft.message_id = message->message_id;
    draft.in_reply_to = message->in_reply_to;
    draft.references = message->references;
    draft.attachments = attachments;
    draft.attachment_count = message->attachment_count;

    amg_buffer_init(&raw);
    result = amg_smtp_build_mail(&draft, 1, &raw, error);
    if (result == AMG_OK)
        result = amg_imap_append_draft(
            imap, message->argument1[0] ? message->argument1 : "\\Drafts",
            raw.data, raw.length, error);
    amg_buffer_free(&raw);
    if (result != AMG_OK) return result;

    /* APPEND completed first, so an update can never destroy the only draft.
     * If removing the former UID fails, keep the new draft and report a
     * success-with-warning instead of inviting another duplicate save. */
    if (message->uid && message->argument2[0]) {
        memset(&delete_error, 0, sizeof(delete_error));
        delete_result = amg_imap_delete_uid(
            imap, message->uid, message->argument2, &delete_error);
        if (delete_result != AMG_OK) {
            copy_text(message->argument2, sizeof(message->argument2),
                      "draft-kept");
            if (delete_result == AMG_ERR_IO || delete_result == AMG_ERR_TLS) {
                network->connected = 0;
                amg_imap_disconnect(imap);
            }
            amg_error_set(error, AMG_OK, "");
            append_success_warning(
                error,
                T(MSG_THE_NEW_DRAFT_WAS_SAVED_BUT_THE_OLD, "The new draft was saved, but the old draft could not be removed"),
                &delete_error);
        } else {
            copy_text(message->argument2, sizeof(message->argument2),
                      "draft-removed");
            amg_error_set(error, AMG_OK, "");
        }
    } else {
        amg_error_set(error, AMG_OK, "");
    }
    return AMG_OK;
}

static void network_worker(void)
{
    AmgNetwork *network = starting_network;
    AmgNetMessage *stop_message = NULL;
    AmgImapSession imap;
    AmgOAuthTokens tokens;
    AmgError error;
    int tls_ready;
    network->commands->mp_SigTask = FindTask(NULL);
    network->worker_ready = 1;
    Signal(network->responses->mp_SigTask,
           1UL << network->responses->mp_SigBit);
    starting_network = NULL;
    amg_imap_session_init(&imap);
    memset(&tokens, 0, sizeof(tokens));
    memset(&error, 0, sizeof(error));
    tls_ready = amg_tls_global_init(&error) == AMG_OK;

    for (;;) {
        AmgNetMessage *message;
        Wait(1UL << network->commands->mp_SigBit);
        while ((message = (AmgNetMessage *)GetMsg(network->commands)) != NULL) {
            int result = AMG_OK;
            amg_error_set(&error, AMG_OK, "");
            if (message->type == AMG_NET_STOP) {
                stop_message = message;
                goto done;
            }
            if (!tls_ready) {
                cleanup_temp_attachments(message);
                finish_message(message, error.code, &error);
                continue;
            }
            switch (message->type) {
                case AMG_NET_CONNECT:
                    result = connect_network_account(
                        network, &imap, &tokens, &error);
                    break;

                case AMG_NET_RECONFIGURE:
                    result = copy_account_deep(
                        &network->account, &message->account_update, &error);
                    if (result == AMG_OK)
                        result = connect_network_account(
                            network, &imap, &tokens, &error);
                    break;

                case AMG_NET_FETCH_LABELS:
                {
                    AmgImapLabel *labels = (AmgImapLabel *)calloc(
                        AMIGMAIL_MAX_LABELS, sizeof(*labels));
                    size_t count = 0, i;
                    if (!labels) {
                        result = AMG_ERR_MEMORY;
                        amg_error_set(&error, result, T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory."));
                        break;
                    }
                    result = amg_imap_list_labels(&imap, labels,
                                                  AMIGMAIL_MAX_LABELS,
                                                  &count, &error);
                    if (result == AMG_OK) {
                        for (i = 0; i < count; ++i) {
                            char line[1400];
                            int length = snprintf(
                                line, sizeof(line), "%lu\t%d\t%c\t%s\n",
                                labels[i].special_use,
                                labels[i].selectable ? 1 : 0,
                                labels[i].delimiter ? labels[i].delimiter : ' ',
                                labels[i].name_utf8);
                            if (length > 0 &&
                                amg_buffer_append(&message->payload, line,
                                                  (size_t)length) != AMG_OK) {
                                result = AMG_ERR_MEMORY;
                                amg_error_set(&error, result,
                                              T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory."));
                                break;
                            }
                        }
                    }
                    free(labels);
                    break;
                }

                case AMG_NET_FETCH_INBOX:
                    result = amg_imap_select(
                        &imap,
                        message->argument1[0] ? message->argument1 : "INBOX",
                        &error);
                    if (result == AMG_OK) {
                        snprintf(message->argument2,
                                 sizeof(message->argument2), "%lu",
                                 imap.uid_validity);
                        result = amg_imap_fetch_recent(
                            &imap,
                            network->account.fetch_days
                                ? network->account.fetch_days : 180U,
                            &message->payload, &error);
                    }
                    break;

                case AMG_NET_CHECK_INBOX:
                {
                    char previous_mailbox[sizeof(imap.selected_mailbox)];
                    AmgError restore_error;
                    unsigned long expected_uid_validity = 0UL;
                    unsigned long inbox_uid_validity = 0UL;
                    if (message->argument1[0])
                        expected_uid_validity = strtoul(
                            message->argument1, NULL, 10);
                    previous_mailbox[0] = 0;
                    if (imap.selected_mailbox[0]) {
                        strncpy(previous_mailbox, imap.selected_mailbox,
                                sizeof(previous_mailbox) - 1U);
                        previous_mailbox[sizeof(previous_mailbox) - 1U] = 0;
                    }
                    result = amg_imap_select(&imap, "INBOX", &error);
                    if (result == AMG_OK) {
                        inbox_uid_validity = imap.uid_validity;
                        /* A persisted UID belongs to exactly one
                         * UIDVALIDITY generation.  If the mailbox was
                         * recreated, do a normal recent fetch so the GUI can
                         * establish a fresh baseline without missing mail. */
                        if (message->uid &&
                            (!expected_uid_validity || !inbox_uid_validity ||
                             expected_uid_validity == inbox_uid_validity))
                            result = amg_imap_fetch_after_uid(
                                &imap, message->uid, &message->payload, &error);
                        else
                            result = amg_imap_fetch_recent(
                                &imap,
                                network->account.fetch_days
                                    ? network->account.fetch_days : 180U,
                                &message->payload, &error);
                    }
                    if (result == AMG_OK)
                        snprintf(message->argument2,
                                 sizeof(message->argument2), "%lu",
                                 inbox_uid_validity);
                    if (previous_mailbox[0] &&
                        strcmp(previous_mailbox, "INBOX")) {
                        memset(&restore_error, 0, sizeof(restore_error));
                        if (amg_imap_select(&imap, previous_mailbox,
                                            &restore_error) != AMG_OK &&
                            result == AMG_OK) {
                            result = restore_error.code != AMG_OK
                                ? restore_error.code : AMG_ERR_PROTOCOL;
                            error = restore_error;
                        }
                    }
                    break;
                }

                case AMG_NET_FETCH_MESSAGE:
                    result = amg_imap_fetch_message(
                        &imap, message->uid, &message->payload, &error);
                    break;

                case AMG_NET_SET_SEEN:
                    result = amg_imap_set_seen(
                        &imap, message->uid, atoi(message->argument1), &error);
                    break;

                case AMG_NET_SET_FLAGGED:
                    result = amg_imap_set_flagged(
                        &imap, message->uid, atoi(message->argument1), &error);
                    break;

                case AMG_NET_DELETE:
                    result = amg_imap_move_label(
                        &imap, message->uid, message->argument2,
                        message->argument1, &error);
                    break;

                case AMG_NET_MOVE:
                    result = amg_imap_move_label(
                        &imap, message->uid, message->argument1,
                        message->argument2, &error);
                    break;

                case AMG_NET_EMPTY_TRASH:
                case AMG_NET_EMPTY_SPAM:
                    result = amg_imap_empty_mailbox(
                        &imap, message->argument1, &error);
                    break;

                case AMG_NET_SAVE_DRAFT:
                    result = save_mail_draft(network, &imap, message, &error);
                    break;

                case AMG_NET_SEND_REPLY:
                {
                    AmgReplyDraft draft = {
                        message->from,
                        message->to,
                        message->subject,
                        message->body,
                        message->in_reply_to,
                        message->references,
                        message->date,
                        message->message_id
                    };
                    result = amg_smtp_send_reply(
                        &network->account, tokens.access_token, &draft, &error);
                    break;
                }

                case AMG_NET_SEND_MAIL:
                    result = send_new_mail(network, &imap, message,
                                           tokens.access_token, &error);
                    break;

                case AMG_NET_CHECK_UPDATE:
                {
                    AmgUpdateInfo info;
                    memset(&info, 0, sizeof(info));
                    result = amg_update_check_latest(&info, &error);
                    if (result == AMG_OK) {
                        copy_text(message->argument1,
                                  sizeof(message->argument1), info.tag);
                        copy_text(message->argument2,
                                  sizeof(message->argument2),
                                  info.download_url);
                    }
                    break;
                }

                case AMG_NET_DOWNLOAD_UPDATE:
                    result = amg_update_download(message->argument1,
                                                 message->argument2,
                                                 &error);
                    break;

                default:
                    result = AMG_ERR_ARGUMENT;
                    amg_error_set(&error, result,
                                  T(MSG_UNKNOWN_NETWORK_REQUEST, "Unknown network request."));
                    break;
            }
            if ((result == AMG_ERR_IO || result == AMG_ERR_TLS) &&
                message->type >= AMG_NET_CONNECT &&
                message->type <= AMG_NET_SAVE_DRAFT)
                network->connected = 0;
            qualify_error(message->type, result, &error);
            cleanup_temp_attachments(message);
            finish_message(message, result, &error);
        }
    }

done:
    amg_imap_disconnect(&imap);
    amg_oauth_tokens_clear(&tokens);
    if (tls_ready) amg_tls_global_cleanup();
    network->running = 0;
    network->worker_ready = 0;
    network->connected = 0;
    if (stop_message) finish_message(stop_message, AMG_OK, &error);
}

AmgNetwork *amg_network_create(void)
{
    AmgNetwork *network = (AmgNetwork *)calloc(1, sizeof(*network));
    if (network) amg_account_init(&network->account);
    return network;
}

void amg_network_destroy(AmgNetwork *network)
{
    if (!network) return;
    amg_network_stop(network);
    if (network->commands) DeleteMsgPort(network->commands);
    if (network->responses) DeleteMsgPort(network->responses);
    amg_account_clear(&network->account);
    free(network);
}

int amg_network_start(AmgNetwork *network, const AmgAccount *account,
                      AmgError *error)
{
    if (!network || !account) return AMG_ERR_ARGUMENT;
    if (network->running) return AMG_OK;
    if (!network->commands) network->commands = CreateMsgPort();
    if (!network->responses) network->responses = CreateMsgPort();
    if (!network->commands || !network->responses) {
        amg_error_set(error, AMG_ERR_MEMORY,
                      T(MSG_MESSAGE_PORTS_COULD_NOT_BE_CREATED, "Message ports could not be created."));
        return AMG_ERR_MEMORY;
    }
    amg_account_clear(&network->account);
    network->account = *account;
    network->account.imap_password = NULL;
    network->account.smtp_password = NULL;
    network->account.refresh_token = NULL;
    if (amg_account_set_secret(&network->account.imap_password,
                               account->imap_password) != AMG_OK ||
        amg_account_set_secret(&network->account.smtp_password,
                               account->smtp_password) != AMG_OK ||
        amg_account_set_secret(&network->account.refresh_token,
                               account->refresh_token) != AMG_OK) {
        amg_account_clear(&network->account);
        amg_account_init(&network->account);
        amg_error_set(error, AMG_ERR_MEMORY, T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory."));
        return AMG_ERR_MEMORY;
    }
    network->worker_ready = 0;
    network->connected = 0;
    starting_network = network;
    network->process = CreateNewProcTags(
        NP_Entry, (ULONG)network_worker,
        NP_Name, (ULONG)"AmiMail network",
        NP_StackSize, 65536UL,
        TAG_DONE);
    if (!network->process) {
        starting_network = NULL;
        amg_error_set(error, AMG_ERR_IO,
                      T(MSG_NETWORK_PROCESS_COULD_NOT_BE_STARTED, "Network process could not be started."));
        return AMG_ERR_IO;
    }
    while (!network->worker_ready)
        Wait(1UL << network->responses->mp_SigBit);
    network->running = 1;
    return AMG_OK;
}

static AmgNetMessage *new_message(AmgNetwork *network,
                                  AmgNetCommandType type)
{
    AmgNetMessage *message = (AmgNetMessage *)calloc(1, sizeof(*message));
    if (!message) return NULL;
    message->message.mn_ReplyPort = network->responses;
    message->message.mn_Length = sizeof(*message);
    message->type = type;
    amg_account_init(&message->account_update);
    amg_buffer_init(&message->payload);
    return message;
}

int amg_network_request(AmgNetwork *network, AmgNetCommandType type,
                        unsigned long uid, const char *argument1,
                        const char *argument2, AmgError *error)
{
    AmgNetMessage *message;
    if (!network || !network->running) return AMG_ERR_ARGUMENT;
    message = new_message(network, type);
    if (!message) {
        amg_error_set(error, AMG_ERR_MEMORY, T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory."));
        return AMG_ERR_MEMORY;
    }
    message->uid = uid;
    copy_text(message->argument1, sizeof(message->argument1), argument1);
    copy_text(message->argument2, sizeof(message->argument2), argument2);
    PutMsg(network->commands, (struct Message *)message);
    return AMG_OK;
}

int amg_network_request_reconfigure(AmgNetwork *network,
                                    const AmgAccount *account,
                                    AmgError *error)
{
    AmgNetMessage *message;
    int result;
    if (!network || !account || !network->running) return AMG_ERR_ARGUMENT;
    message = new_message(network, AMG_NET_RECONFIGURE);
    if (!message) {
        amg_error_set(error, AMG_ERR_MEMORY,
                      T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory."));
        return AMG_ERR_MEMORY;
    }
    result = copy_account_deep(&message->account_update, account, error);
    if (result != AMG_OK) {
        free_net_message(message);
        return result;
    }
    PutMsg(network->commands, (struct Message *)message);
    return AMG_OK;
}

int amg_network_request_reply(AmgNetwork *network, const AmgReplyDraft *draft,
                              AmgError *error)
{
    AmgNetMessage *message;
    if (!network || !draft || !network->running) return AMG_ERR_ARGUMENT;
    if (!text_fits(draft->from, 256U) || !text_fits(draft->to, 768U) ||
        !text_fits(draft->subject, 512U) ||
        !text_fits(draft->body_utf8, AMG_NET_BODY_MAX) ||
        !text_fits(draft->in_reply_to, 512U) ||
        !text_fits(draft->references, 1024U) ||
        !text_fits(draft->date_rfc2822, 96U) ||
        !text_fits(draft->message_id, 256U)) {
        amg_error_set(error, AMG_ERR_LIMIT, T(MSG_REPLY_DRAFT_IS_TOO_LARGE, "Reply draft is too large."));
        return AMG_ERR_LIMIT;
    }
    message = new_message(network, AMG_NET_SEND_REPLY);
    if (!message) {
        amg_error_set(error, AMG_ERR_MEMORY, T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory."));
        return AMG_ERR_MEMORY;
    }
    copy_text(message->from, sizeof(message->from), draft->from);
    copy_text(message->to, sizeof(message->to), draft->to);
    copy_text(message->subject, sizeof(message->subject), draft->subject);
    copy_text(message->body, sizeof(message->body), draft->body_utf8);
    copy_text(message->in_reply_to, sizeof(message->in_reply_to),
              draft->in_reply_to);
    copy_text(message->references, sizeof(message->references),
              draft->references);
    copy_text(message->date, sizeof(message->date), draft->date_rfc2822);
    copy_text(message->message_id, sizeof(message->message_id),
              draft->message_id);
    PutMsg(network->commands, (struct Message *)message);
    return AMG_OK;
}

static int request_mail_message(AmgNetwork *network,
                                const AmgMailDraft *draft,
                                AmgNetCommandType type,
                                const char *mailbox,
                                unsigned long original_draft_uid,
                                const char *original_draft_mailbox,
                                AmgError *error)
{
    AmgNetMessage *message;
    size_t i;
    unsigned long total = 0;
    if (!network || !draft || !network->running) return AMG_ERR_ARGUMENT;
    if (draft->attachment_count > AMG_MAIL_MAX_ATTACHMENTS ||
        !text_fits(draft->from, 768U) || !text_fits(draft->to, 768U) ||
        !text_fits(draft->cc, 768U) || !text_fits(draft->bcc, 768U) ||
        !text_fits(draft->subject, 512U) ||
        !text_fits(draft->body_utf8, AMG_NET_BODY_MAX) ||
        !text_fits(draft->date_rfc2822, 96U) ||
        !text_fits(draft->message_id, 256U) ||
        !text_fits(draft->in_reply_to, 512U) ||
        !text_fits(draft->references, 1024U)) {
        amg_error_set(error, AMG_ERR_LIMIT, T(MSG_MAIL_DRAFT_IS_TOO_LARGE, "Mail draft is too large."));
        return AMG_ERR_LIMIT;
    }
    for (i = 0; i < draft->attachment_count; ++i) {
        if (!draft->attachments ||
            !text_fits(draft->attachments[i].path, AMG_NET_PATH_MAX) ||
            !text_fits(draft->attachments[i].name_utf8, AMG_NET_NAME_MAX) ||
            draft->attachments[i].size > AMG_MAIL_MAX_ATTACHMENT_TOTAL - total) {
            amg_error_set(error, AMG_ERR_LIMIT,
                          T(MSG_ATTACHMENTS_MAY_TOTAL_NO_MORE_THAN_10_MB_UTF8, "Attachments may total no more than 10 MB."));
            return AMG_ERR_LIMIT;
        }
        total += draft->attachments[i].size;
    }
    message = new_message(network, type);
    if (!message) {
        amg_error_set(error, AMG_ERR_MEMORY, T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory."));
        return AMG_ERR_MEMORY;
    }
    copy_text(message->from, sizeof(message->from), draft->from);
    copy_text(message->to, sizeof(message->to), draft->to);
    copy_text(message->cc, sizeof(message->cc), draft->cc);
    copy_text(message->bcc, sizeof(message->bcc), draft->bcc);
    copy_text(message->subject, sizeof(message->subject), draft->subject);
    copy_text(message->body, sizeof(message->body), draft->body_utf8);
    copy_text(message->date, sizeof(message->date), draft->date_rfc2822);
    copy_text(message->message_id, sizeof(message->message_id),
              draft->message_id);
    copy_text(message->in_reply_to, sizeof(message->in_reply_to),
              draft->in_reply_to);
    copy_text(message->references, sizeof(message->references),
              draft->references);
    message->attachment_count = draft->attachment_count;
    for (i = 0; i < draft->attachment_count; ++i) {
        copy_text(message->attachments[i].path,
                  sizeof(message->attachments[i].path),
                  draft->attachments[i].path);
        copy_text(message->attachments[i].name_utf8,
                  sizeof(message->attachments[i].name_utf8),
                  draft->attachments[i].name_utf8);
        message->attachments[i].size = draft->attachments[i].size;
        message->attachments[i].delete_after_use =
            draft->attachments[i].delete_after_use;
    }
    message->uid = original_draft_uid;
    if (type == AMG_NET_SAVE_DRAFT) {
        copy_text(message->argument1, sizeof(message->argument1), mailbox);
        copy_text(message->argument2, sizeof(message->argument2),
                  original_draft_mailbox);
    } else if (type == AMG_NET_SEND_MAIL) {
        copy_text(message->argument1, sizeof(message->argument1),
                  original_draft_mailbox);
    }
    PutMsg(network->commands, (struct Message *)message);
    return AMG_OK;
}

int amg_network_request_mail(AmgNetwork *network, const AmgMailDraft *draft,
                             AmgError *error)
{
    return request_mail_message(network, draft, AMG_NET_SEND_MAIL, NULL,
                                0UL, NULL, error);
}

int amg_network_request_mail_from_draft(AmgNetwork *network,
                                        const AmgMailDraft *draft,
                                        unsigned long original_draft_uid,
                                        const char *original_draft_mailbox,
                                        AmgError *error)
{
    if (!original_draft_uid || !original_draft_mailbox ||
        !*original_draft_mailbox)
        return AMG_ERR_ARGUMENT;
    return request_mail_message(network, draft, AMG_NET_SEND_MAIL, NULL,
                                original_draft_uid,
                                original_draft_mailbox, error);
}

int amg_network_request_draft(AmgNetwork *network, const AmgMailDraft *draft,
                              const char *draft_mailbox, AmgError *error)
{
    if (!draft_mailbox || !*draft_mailbox) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T(MSG_THE_DRAFTS_FOLDER_IS_UNKNOWN, "The Drafts folder is unknown."));
        return AMG_ERR_ARGUMENT;
    }
    return request_mail_message(network, draft, AMG_NET_SAVE_DRAFT,
                                draft_mailbox, 0UL, NULL, error);
}

int amg_network_request_draft_update(AmgNetwork *network,
                                     const AmgMailDraft *draft,
                                     const char *draft_mailbox,
                                     unsigned long original_draft_uid,
                                     const char *original_draft_mailbox,
                                     AmgError *error)
{
    if (!draft_mailbox || !*draft_mailbox || !original_draft_uid ||
        !original_draft_mailbox || !*original_draft_mailbox) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T(MSG_THE_DRAFTS_FOLDER_IS_UNKNOWN, "The Drafts folder is unknown."));
        return AMG_ERR_ARGUMENT;
    }
    return request_mail_message(network, draft, AMG_NET_SAVE_DRAFT,
                                draft_mailbox, original_draft_uid,
                                original_draft_mailbox, error);
}

int amg_network_poll(AmgNetwork *network, AmgNetworkEvent *event)
{
    AmgNetMessage *message;
    if (!network || !event) return AMG_ERR_ARGUMENT;
    message = (AmgNetMessage *)GetMsg(network->responses);
    if (!message) return 0;
    memset(event, 0, sizeof(*event));
    event->type = message->type;
    event->result = message->result;
    event->uid = message->uid;
    copy_text(event->argument1, sizeof(event->argument1), message->argument1);
    copy_text(event->argument2, sizeof(event->argument2), message->argument2);
    copy_text(event->message, sizeof(event->message), message->error.message);
    event->payload = message->payload.data;
    event->payload_length = message->payload.length;
    message->payload.data = NULL;
    message->payload.length = 0;
    message->payload.capacity = 0;
    free_net_message(message);
    return 1;
}

void amg_network_event_clear(AmgNetworkEvent *event)
{
    if (!event) return;
    free(event->payload);
    memset(event, 0, sizeof(*event));
}

unsigned long amg_network_signal_mask(const AmgNetwork *network)
{
    return network && network->responses
               ? 1UL << network->responses->mp_SigBit
               : 0;
}

int amg_network_is_running(const AmgNetwork *network)
{
    return network && network->running;
}

int amg_network_is_connected(const AmgNetwork *network)
{
    return network && network->running && network->connected;
}

void amg_network_stop(AmgNetwork *network)
{
    AmgNetMessage *stop;
    if (!network || !network->running) return;
    stop = new_message(network, AMG_NET_STOP);
    if (stop) {
        PutMsg(network->commands, (struct Message *)stop);
        while (network->running)
            Wait(1UL << network->responses->mp_SigBit);
    }
    while (network->responses) {
        AmgNetMessage *message =
            (AmgNetMessage *)GetMsg(network->responses);
        if (!message) break;
        free_net_message(message);
    }
    network->process = NULL;
    network->connected = 0;
}

#else

struct AmgNetwork {
    int running;
};

AmgNetwork *amg_network_create(void)
{
    return (AmgNetwork *)calloc(1, sizeof(AmgNetwork));
}

void amg_network_destroy(AmgNetwork *network)
{
    free(network);
}

int amg_network_start(AmgNetwork *network, const AmgAccount *account,
                      AmgError *error)
{
    (void)network;
    (void)account;
    amg_error_set(error, AMG_ERR_UNSUPPORTED,
                  T(MSG_NETWORK_TASK_IS_AVAILABLE_ONLY_ON_AMIGAOS, "Network task is available only on AmigaOS."));
    return AMG_ERR_UNSUPPORTED;
}

void amg_network_stop(AmgNetwork *network)
{
    (void)network;
}

int amg_network_is_running(const AmgNetwork *network)
{
    return network && network->running;
}

int amg_network_is_connected(const AmgNetwork *network)
{
    (void)network;
    return 0;
}

unsigned long amg_network_signal_mask(const AmgNetwork *network)
{
    (void)network;
    return 0;
}

int amg_network_request(AmgNetwork *network, AmgNetCommandType type,
                        unsigned long uid, const char *argument1,
                        const char *argument2, AmgError *error)
{
    (void)network;
    (void)type;
    (void)uid;
    (void)argument1;
    (void)argument2;
    amg_error_set(error, AMG_ERR_UNSUPPORTED, T(MSG_AMIGAOS_ONLY, "AmigaOS only."));
    return AMG_ERR_UNSUPPORTED;
}

int amg_network_request_reconfigure(AmgNetwork *network,
                                    const AmgAccount *account,
                                    AmgError *error)
{
    (void)network;
    (void)account;
    amg_error_set(error, AMG_ERR_UNSUPPORTED,
                  T(MSG_AMIGAOS_ONLY, "AmigaOS only."));
    return AMG_ERR_UNSUPPORTED;
}

int amg_network_request_reply(AmgNetwork *network, const AmgReplyDraft *draft,
                              AmgError *error)
{
    (void)network;
    (void)draft;
    amg_error_set(error, AMG_ERR_UNSUPPORTED, T(MSG_AMIGAOS_ONLY, "AmigaOS only."));
    return AMG_ERR_UNSUPPORTED;
}

int amg_network_request_mail(AmgNetwork *network, const AmgMailDraft *draft,
                             AmgError *error)
{
    (void)network;
    (void)draft;
    amg_error_set(error, AMG_ERR_UNSUPPORTED, T(MSG_AMIGAOS_ONLY, "AmigaOS only."));
    return AMG_ERR_UNSUPPORTED;
}

int amg_network_request_mail_from_draft(AmgNetwork *network,
                                        const AmgMailDraft *draft,
                                        unsigned long original_draft_uid,
                                        const char *original_draft_mailbox,
                                        AmgError *error)
{
    (void)network;
    (void)draft;
    (void)original_draft_uid;
    (void)original_draft_mailbox;
    amg_error_set(error, AMG_ERR_UNSUPPORTED, T(MSG_AMIGAOS_ONLY, "AmigaOS only."));
    return AMG_ERR_UNSUPPORTED;
}

int amg_network_request_draft(AmgNetwork *network, const AmgMailDraft *draft,
                              const char *draft_mailbox, AmgError *error)
{
    (void)network;
    (void)draft;
    (void)draft_mailbox;
    amg_error_set(error, AMG_ERR_UNSUPPORTED, T(MSG_AMIGAOS_ONLY, "AmigaOS only."));
    return AMG_ERR_UNSUPPORTED;
}

int amg_network_request_draft_update(AmgNetwork *network,
                                     const AmgMailDraft *draft,
                                     const char *draft_mailbox,
                                     unsigned long original_draft_uid,
                                     const char *original_draft_mailbox,
                                     AmgError *error)
{
    (void)network;
    (void)draft;
    (void)draft_mailbox;
    (void)original_draft_uid;
    (void)original_draft_mailbox;
    amg_error_set(error, AMG_ERR_UNSUPPORTED, T(MSG_AMIGAOS_ONLY, "AmigaOS only."));
    return AMG_ERR_UNSUPPORTED;
}

int amg_network_poll(AmgNetwork *network, AmgNetworkEvent *event)
{
    (void)network;
    (void)event;
    return 0;
}

void amg_network_event_clear(AmgNetworkEvent *event)
{
    if (!event) return;
    free(event->payload);
    memset(event, 0, sizeof(*event));
}

#endif
