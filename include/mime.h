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
int amg_mime_attachment_grouped_summary(
    const char *message, size_t length,
    AmgBuffer *attachments, size_t *attachment_count,
    AmgBuffer *embedded_graphics, size_t *embedded_graphics_count,
    AmgError *error);
int amg_mime_attachment_count(const char *message, size_t length,
                              size_t *count, AmgError *error);
int amg_mime_extract_attachment(const char *message, size_t length,
                                size_t index, AmgBuffer *name_utf8,
                                AmgBuffer *data, AmgError *error);

/* Shared MIME interpretation for the bounded in-memory and disk readers. */
int amg_mime_parameter(const char *header, const char *key,
                        char *value, size_t capacity);
int amg_mime_describe_attachment(const AmgMailHeaders *headers, int related,
                                  AmgBuffer *name_utf8, int *embedded);
int amg_mime_plain_is_css(const char *text, size_t length);

#endif
