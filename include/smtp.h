#ifndef AMIGMAIL_SMTP_H
#define AMIGMAIL_SMTP_H

#include "account.h"
#include "buffer.h"

typedef struct AmgReplyDraft {
    const char *from;
    const char *to;
    const char *subject;
    const char *body_utf8;
    const char *in_reply_to;
    const char *references;
    const char *date_rfc2822;
    const char *message_id;
} AmgReplyDraft;

#define AMG_MAIL_MAX_ATTACHMENTS 8U
#define AMG_MAIL_MAX_ATTACHMENT_TOTAL (10UL * 1024UL * 1024UL)

typedef struct AmgAttachmentInput {
    const char *path;
    const char *name_utf8;
    unsigned long size;
    int delete_after_use;
} AmgAttachmentInput;

typedef struct AmgMailDraft {
    const char *from;
    const char *to;
    const char *cc;
    const char *bcc;
    const char *subject;
    const char *body_utf8;
    const char *date_rfc2822;
    const char *message_id;
    const char *in_reply_to;
    const char *references;
    const AmgAttachmentInput *attachments;
    size_t attachment_count;
} AmgMailDraft;

int amg_smtp_dot_stuff(const char *message, size_t length, AmgBuffer *output);
int amg_smtp_reply_subject(const char *subject, AmgBuffer *output);
int amg_smtp_response_has_capability(const AmgBuffer *response,
                                     const char *capability);
int amg_smtp_response_auth_has_mechanism(const AmgBuffer *response,
                                         const char *mechanism);
int amg_smtp_build_reply(const AmgReplyDraft *draft, AmgBuffer *output, AmgError *error);
int amg_smtp_send_reply(const AmgAccount *account, const char *access_token,
                        const AmgReplyDraft *draft, AmgError *error);
int amg_smtp_build_mail(const AmgMailDraft *draft, int include_bcc,
                        AmgBuffer *output, AmgError *error);
int amg_smtp_send_mail(const AmgAccount *account, const char *access_token,
                       const AmgMailDraft *draft, AmgError *error);

#endif
