#ifndef AMIGMAIL_MIME_H
#define AMIGMAIL_MIME_H

#include "buffer.h"

typedef struct AmgMailHeader {
    char *name;
    char *value;
} AmgMailHeader;

typedef struct AmgMailHeaders {
    AmgMailHeader *items;
    size_t count;
    size_t capacity;
} AmgMailHeaders;

void amg_mail_headers_init(AmgMailHeaders *headers);
void amg_mail_headers_free(AmgMailHeaders *headers);
int amg_mail_headers_parse(const char *input, size_t length, AmgMailHeaders *headers, size_t *body_offset);
const char *amg_mail_header_get(const AmgMailHeaders *headers, const char *name);
int amg_rfc2047_decode(const char *input, AmgBuffer *output);
int amg_html_to_text(const char *input, size_t length, AmgBuffer *output);
int amg_mime_extract_text(const char *message, size_t length, AmgBuffer *output, AmgError *error);
int amg_mime_attachment_summary(const char *message, size_t length, AmgBuffer *output, AmgError *error);
int amg_mime_attachment_count(const char *message, size_t length,
                              size_t *count, AmgError *error);
int amg_mime_extract_attachment(const char *message, size_t length,
                                size_t index, AmgBuffer *name_utf8,
                                AmgBuffer *data, AmgError *error);

#endif
