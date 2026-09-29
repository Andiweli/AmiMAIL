#include "mail_notice.h"
#include "imap_parser.h"
#include "mime.h"
#include "codec.h"

#include <string.h>

void amg_mail_notice_clip(const char *source, char *output, size_t capacity)
{
    size_t used = 0U;
    int space_pending = 0, truncated = 0;
    const unsigned char *p = (const unsigned char *)(source ? source : "");
    if (!output || !capacity) return;
    while (*p) {
        unsigned char c = *p++;
        if (c <= 32U || c == 127U || c == '\\' || c == 0xA0U) {
            if (used) space_pending = 1;
            continue;
        }
        if (space_pending) {
            if (used + 1U >= capacity) { truncated = 1; break; }
            output[used++] = ' ';
            space_pending = 0;
        }
        if (used + 1U >= capacity) { truncated = 1; break; }
        output[used++] = (char)c;
    }
    while (used && output[used - 1U] == ' ') --used;
    if (truncated && capacity >= 4U) {
        if (used > capacity - 4U) used = capacity - 4U;
        memcpy(output + used, "...", 3U);
        used += 3U;
    }
    output[used] = 0;
}

int amg_mail_notice_subject(const unsigned char *payload, size_t length,
                            unsigned long previous_uid, const char *fallback,
                            char *local, size_t capacity)
{
    AmgImapFetchRecord record;
    AmgMailHeaders headers;
    AmgBuffer decoded, converted;
    const unsigned char *newest = NULL;
    const char *subject;
    size_t position = 0U, newest_length = 0U;
    unsigned long highest_uid = previous_uid;
    int result = 0;
    if (!local || !capacity || (!payload && length)) return AMG_ERR_ARGUMENT;
    amg_mail_notice_clip(fallback, local, capacity);
    while (length && (result = amg_imap_fetch_record_next(
            payload, length, &position, &record)) > 0) {
        if (!record.deleted && record.uid > highest_uid) {
            highest_uid = record.uid;
            newest = record.literal;
            newest_length = record.literal_length;
        }
    }
    if (result < 0) return result;
    if (!newest) return AMG_ERR_ARGUMENT;
    amg_mail_headers_init(&headers);
    amg_buffer_init(&decoded);
    amg_buffer_init(&converted);
    result = amg_mail_headers_parse((const char *)newest, newest_length,
                                    &headers, NULL);
    subject = result == AMG_OK ? amg_mail_header_get(&headers, "Subject") : NULL;
    if (subject && *subject) {
        result = amg_rfc2047_decode(subject, &decoded);
        if (result == AMG_OK) result = amg_buffer_terminate(&decoded);
        if (result == AMG_OK)
            result = amg_utf8_to_local((const char *)decoded.data, &converted);
        if (result == AMG_OK) result = amg_buffer_terminate(&converted);
        if (result == AMG_OK) {
            amg_mail_notice_clip((const char *)converted.data, local, capacity);
            if (!local[0]) amg_mail_notice_clip(fallback, local, capacity);
        }
    }
    amg_buffer_free(&converted);
    amg_buffer_free(&decoded);
    amg_mail_headers_free(&headers);
    return result;
}
