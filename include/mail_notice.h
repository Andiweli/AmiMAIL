#ifndef AMIMAIL_MAIL_NOTICE_H
#define AMIMAIL_MAIL_NOTICE_H

#include "amigmail.h"

/* Newest means the highest non-deleted mailbox UID ABOVE the previous
 * baseline, not the Date header or the order of FETCH responses. */
int amg_mail_notice_subject(const unsigned char *payload, size_t length,
                            unsigned long previous_uid, const char *fallback,
                            char *local, size_t capacity);
/* Single-byte display text: collapse controls/whitespace, neutralize Herald
 * backslash escapes, and indicate truncation without overrunning capacity. */
void amg_mail_notice_clip(const char *source, char *output, size_t capacity);

#endif
