#include "mime.h"
#include "i18n.h"

#define T(de, en) amg_tr((de), (en))
#include "codec.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ci_equal_n(const char *a, const char *b, size_t n)
{
    while (n--) if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return 0;
    return 1;
}

static int ci_equal(const char *a, const char *b)
{
    return a && b && strlen(a) == strlen(b) && ci_equal_n(a, b, strlen(a));
}

static char *duplicate_range(const char *start, size_t length)
{
    char *result = (char *)malloc(length + 1U);
    if (!result) return NULL;
    memcpy(result, start, length);
    result[length] = 0;
    return result;
}

void amg_mail_headers_init(AmgMailHeaders *headers)
{
    if (!headers) return;
    headers->items = NULL; headers->count = 0; headers->capacity = 0;
}

void amg_mail_headers_free(AmgMailHeaders *headers)
{
    size_t i;
    if (!headers) return;
    for (i = 0; i < headers->count; ++i) {
        free(headers->items[i].name); free(headers->items[i].value);
    }
    free(headers->items);
    amg_mail_headers_init(headers);
}

static int add_header(AmgMailHeaders *headers, const char *name, size_t name_length,
                      const char *value, size_t value_length)
{
    AmgMailHeader *next;
    while (value_length && isspace((unsigned char)value[0])) { ++value; --value_length; }
    while (value_length && isspace((unsigned char)value[value_length - 1U])) --value_length;
    if (headers->count == headers->capacity) {
        size_t capacity = headers->capacity ? headers->capacity * 2U : 16U;
        next = (AmgMailHeader *)realloc(headers->items, capacity * sizeof(*next));
        if (!next) return AMG_ERR_MEMORY;
        headers->items = next; headers->capacity = capacity;
    }
    headers->items[headers->count].name = duplicate_range(name, name_length);
    headers->items[headers->count].value = duplicate_range(value, value_length);
    if (!headers->items[headers->count].name || !headers->items[headers->count].value) {
        free(headers->items[headers->count].name); free(headers->items[headers->count].value);
        return AMG_ERR_MEMORY;
    }
    ++headers->count;
    return AMG_OK;
}

static int append_fold(AmgMailHeader *header, const char *text, size_t length)
{
    size_t old_length = strlen(header->value);
    char *next;
    while (length && isspace((unsigned char)*text)) { ++text; --length; }
    next = (char *)realloc(header->value, old_length + length + 2U);
    if (!next) return AMG_ERR_MEMORY;
    header->value = next;
    header->value[old_length++] = ' ';
    memcpy(header->value + old_length, text, length);
    header->value[old_length + length] = 0;
    return AMG_OK;
}

int amg_mail_headers_parse(const char *input, size_t length, AmgMailHeaders *headers, size_t *body_offset)
{
    size_t position = 0;
    if ((!input && length) || !headers) return AMG_ERR_ARGUMENT;
    while (position < length) {
        size_t start = position, end, colon;
        while (position < length && input[position] != '\n') ++position;
        end = position;
        if (position < length) ++position;
        if (end > start && input[end - 1U] == '\r') --end;
        if (end == start) { if (body_offset) *body_offset = position; return AMG_OK; }
        if (input[start] == ' ' || input[start] == '\t') {
            if (!headers->count) return AMG_ERR_PARSE;
            if (append_fold(&headers->items[headers->count - 1U], input + start, end - start) != AMG_OK)
                return AMG_ERR_MEMORY;
            continue;
        }
        for (colon = start; colon < end && input[colon] != ':'; ++colon) {}
        if (colon == start || colon == end) return AMG_ERR_PARSE;
        if (headers->count >= AMIGMAIL_MAX_HEADERS) return AMG_ERR_LIMIT;
        if (add_header(headers, input + start, colon - start, input + colon + 1U, end - colon - 1U) != AMG_OK)
            return AMG_ERR_MEMORY;
    }
    if (body_offset) *body_offset = position;
    return AMG_OK;
}

const char *amg_mail_header_get(const AmgMailHeaders *headers, const char *name)
{
    size_t i;
    if (!headers || !name) return NULL;
    for (i = 0; i < headers->count; ++i)
        if (ci_equal(headers->items[i].name, name)) return headers->items[i].value;
    return NULL;
}

static int append_latin1_as_utf8(AmgBuffer *output, const unsigned char *data, size_t length)
{
    size_t i;
    for (i = 0; i < length; ++i) {
        if (data[i] < 0x80U) {
            if (amg_buffer_append_char(output, data[i]) != AMG_OK) return AMG_ERR_MEMORY;
        } else {
            unsigned char utf8[2] = {(unsigned char)(0xC0U | (data[i] >> 6)),
                                     (unsigned char)(0x80U | (data[i] & 0x3FU))};
            if (amg_buffer_append(output, utf8, 2U) != AMG_OK) return AMG_ERR_MEMORY;
        }
    }
    return AMG_OK;
}

int amg_rfc2047_decode(const char *input, AmgBuffer *output)
{
    const char *p = input;
    if (!input || !output) return AMG_ERR_ARGUMENT;
    while (*p) {
        const char *charset, *encoding, *data, *end;
        AmgBuffer decoded;
        size_t charset_length;
        if (p[0] != '=' || p[1] != '?') {
            if (amg_buffer_append_char(output, (unsigned char)*p++) != AMG_OK) return AMG_ERR_MEMORY;
            continue;
        }
        charset = p + 2;
        encoding = strchr(charset, '?');
        if (!encoding || !encoding[1] || encoding[2] != '?') {
            if (amg_buffer_append_char(output, (unsigned char)*p++) != AMG_OK) return AMG_ERR_MEMORY;
            continue;
        }
        data = encoding + 3;
        end = strstr(data, "?=");
        if (!end) { if (amg_buffer_append_char(output, (unsigned char)*p++) != AMG_OK) return AMG_ERR_MEMORY; continue; }
        charset_length = (size_t)(encoding - charset);
        amg_buffer_init(&decoded);
        if (tolower((unsigned char)encoding[1]) == 'b') {
            if (amg_base64_decode(data, (size_t)(end - data), &decoded) != AMG_OK) { amg_buffer_free(&decoded); return AMG_ERR_PARSE; }
        } else if (tolower((unsigned char)encoding[1]) == 'q') {
            size_t i;
            for (i = 0; i < (size_t)(end - data); ++i) {
                char c = data[i] == '_' ? ' ' : data[i];
                if (amg_buffer_append_char(&decoded, (unsigned char)c) != AMG_OK) { amg_buffer_free(&decoded); return AMG_ERR_MEMORY; }
            }
            {
                AmgBuffer qp; amg_buffer_init(&qp);
                if (amg_quoted_printable_decode((const char *)decoded.data, decoded.length, &qp) != AMG_OK) {
                    amg_buffer_free(&decoded); amg_buffer_free(&qp); return AMG_ERR_PARSE;
                }
                amg_buffer_free(&decoded); decoded = qp;
            }
        } else { amg_buffer_free(&decoded); if (amg_buffer_append_char(output, (unsigned char)*p++) != AMG_OK) return AMG_ERR_MEMORY; continue; }

        if ((charset_length == 10U && ci_equal_n(charset, "ISO-8859-1", 10U)) ||
            (charset_length == 6U && ci_equal_n(charset, "LATIN1", 6U))) {
            if (append_latin1_as_utf8(output, decoded.data, decoded.length) != AMG_OK) { amg_buffer_free(&decoded); return AMG_ERR_MEMORY; }
        } else if (amg_buffer_append(output, decoded.data, decoded.length) != AMG_OK) { amg_buffer_free(&decoded); return AMG_ERR_MEMORY; }
        amg_buffer_free(&decoded);
        p = end + 2;
        while ((*p == ' ' || *p == '\t') && p[1] == '=' && p[2] == '?') ++p;
    }
    return AMG_OK;
}

static int append_entity(const char *entity, size_t length, AmgBuffer *output)
{
    if (length == 3U && !memcmp(entity, "amp", 3U)) return amg_buffer_append_char(output, '&');
    if (length == 2U && !memcmp(entity, "lt", 2U)) return amg_buffer_append_char(output, '<');
    if (length == 2U && !memcmp(entity, "gt", 2U)) return amg_buffer_append_char(output, '>');
    if (length == 4U && !memcmp(entity, "quot", 4U)) return amg_buffer_append_char(output, '"');
    if (length == 4U && !memcmp(entity, "nbsp", 4U)) return amg_buffer_append_char(output, ' ');
    return amg_buffer_append_char(output, '?');
}

int amg_html_to_text(const char *input, size_t length, AmgBuffer *output)
{
    size_t i = 0;
    int in_tag = 0, suppress = 0;
    if ((!input && length) || !output) return AMG_ERR_ARGUMENT;
    while (i < length) {
        if (!in_tag && input[i] == '<') {
            size_t j = i + 1U;
            int closing = j < length && input[j] == '/';
            if (closing) ++j;
            while (j < length && isspace((unsigned char)input[j])) ++j;
            if (j + 6U <= length && ci_equal_n(input + j, "script", 6U)) suppress += closing ? -1 : 1;
            else if (j + 5U <= length && ci_equal_n(input + j, "style", 5U)) suppress += closing ? -1 : 1;
            else if (!suppress && ((j + 2U <= length && ci_equal_n(input + j, "br", 2U)) ||
                                   (j + 1U <= length && tolower((unsigned char)input[j]) == 'p') ||
                                   (closing && j + 3U <= length && ci_equal_n(input + j, "div", 3U))))
                amg_buffer_append_char(output, '\n');
            if (suppress < 0) suppress = 0;
            in_tag = 1; ++i; continue;
        }
        if (in_tag) { if (input[i] == '>') in_tag = 0; ++i; continue; }
        if (suppress) { ++i; continue; }
        if (input[i] == '&') {
            size_t j = i + 1U;
            while (j < length && j - i < 12U && input[j] != ';') ++j;
            if (j < length && input[j] == ';') { append_entity(input + i + 1U, j - i - 1U, output); i = j + 1U; continue; }
        }
        if (amg_buffer_append_char(output, (unsigned char)input[i++]) != AMG_OK) return AMG_ERR_MEMORY;
    }
    return AMG_OK;
}

static const char *param_value(const char *header, const char *name, char *buffer, size_t size)
{
    const char *p = header;
    size_t name_length = strlen(name);
    while (p && *p) {
        p = strchr(p, ';');
        if (!p) return NULL;
        ++p; while (*p && isspace((unsigned char)*p)) ++p;
        if (ci_equal_n(p, name, name_length) && p[name_length] == '=') {
            const char *value = p + name_length + 1U, *end;
            size_t length;
            if (*value == '"') { ++value; end = strchr(value, '"'); }
            else { end = value; while (*end && *end != ';' && !isspace((unsigned char)*end)) ++end; }
            if (!end) return NULL;
            length = (size_t)(end - value); if (length >= size) length = size - 1U;
            memcpy(buffer, value, length); buffer[length] = 0; return buffer;
        }
    }
    return NULL;
}

static int ci_starts_with(const char *text, const char *prefix)
{
    size_t prefix_length;
    if (!text || !prefix) return 0;
    prefix_length = strlen(prefix);
    return strlen(text) >= prefix_length &&
           ci_equal_n(text, prefix, prefix_length);
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int append_parameter_utf8(const char *value, AmgBuffer *output)
{
    const char *encoded;
    AmgBuffer decoded;
    int result = AMG_OK;
    if (!value || !output) return AMG_ERR_ARGUMENT;

    encoded = strstr(value, "''");
    if (encoded) {
        const char *cursor = encoded + 2;
        while (*cursor) {
            if (cursor[0] == '%' && isxdigit((unsigned char)cursor[1]) &&
                isxdigit((unsigned char)cursor[2])) {
                int high = hex_value(cursor[1]);
                int low = hex_value(cursor[2]);
                if (amg_buffer_append_char(
                        output, (unsigned char)((high << 4) | low)) !=
                    AMG_OK)
                    return AMG_ERR_MEMORY;
                cursor += 3;
            } else {
                if (amg_buffer_append_char(output,
                                           (unsigned char)*cursor++) != AMG_OK)
                    return AMG_ERR_MEMORY;
            }
        }
        return AMG_OK;
    }

    amg_buffer_init(&decoded);
    result = amg_rfc2047_decode(value, &decoded);
    if (result == AMG_OK && decoded.length)
        result = amg_buffer_append(output, decoded.data, decoded.length);
    else
        result = amg_buffer_append_cstr(output, value);
    amg_buffer_free(&decoded);
    return result;
}

static int append_attachment_line(const char *name, const char *content_type,
                                  size_t encoded_size, AmgBuffer *output)
{
    char size_text[48];
    int result;
    result = amg_buffer_append_cstr(output, "- ");
    if (result == AMG_OK) result = append_parameter_utf8(name, output);
    if (result == AMG_OK && content_type && *content_type) {
        const char *end = strchr(content_type, ';');
        result = amg_buffer_append_cstr(output, " (");
        if (result == AMG_OK)
            result = amg_buffer_append(output, content_type,
                                       end ? (size_t)(end - content_type)
                                           : strlen(content_type));
        if (result == AMG_OK) result = amg_buffer_append_cstr(output, ", ");
    } else if (result == AMG_OK) {
        result = amg_buffer_append_cstr(output, " (");
    }
    snprintf(size_text, sizeof(size_text), "%lu KB",
             (unsigned long)(encoded_size / 1024U +
                             (encoded_size % 1024U ? 1U : 0U)));
    if (result == AMG_OK) result = amg_buffer_append_cstr(output, size_text);
    if (result == AMG_OK) result = amg_buffer_append_cstr(output, ")\n");
    return result;
}

static int collect_attachment_entity(const char *message, size_t length,
                                     unsigned depth, AmgBuffer *output,
                                     size_t *count)
{
    AmgMailHeaders headers;
    size_t body_offset = 0;
    const char *content_type, *disposition;
    char boundary[256], filename[512];
    int result;
    if (depth > 8U || length > AMIGMAIL_MAX_MESSAGE) return AMG_ERR_LIMIT;
    amg_mail_headers_init(&headers);
    result = amg_mail_headers_parse(message, length, &headers, &body_offset);
    if (result != AMG_OK) {
        amg_mail_headers_free(&headers);
        return result;
    }
    content_type = amg_mail_header_get(&headers, "Content-Type");
    disposition = amg_mail_header_get(&headers, "Content-Disposition");
    if (!content_type) content_type = "text/plain";

    if (ci_starts_with(content_type, "multipart/") &&
        param_value(content_type, "boundary", boundary, sizeof(boundary))) {
        AmgBuffer marker;
        const char *body = message + body_offset, *end = message + length;
        const char *part;
        amg_buffer_init(&marker);
        result = amg_buffer_append_cstr(&marker, "--");
        if (result == AMG_OK) result = amg_buffer_append_cstr(&marker, boundary);
        if (result == AMG_OK) result = amg_buffer_terminate(&marker);
        if (result != AMG_OK) {
            amg_buffer_free(&marker);
            amg_mail_headers_free(&headers);
            return result;
        }
        part = strstr(body, (const char *)marker.data);
        while (part && part < end) {
            const char *start = part + marker.length, *next;
            if (start + 2 <= end && start[0] == '-' && start[1] == '-') break;
            if (start + 2 <= end && start[0] == '\r' && start[1] == '\n')
                start += 2;
            else if (start < end && *start == '\n')
                ++start;
            next = strstr(start, (const char *)marker.data);
            if (!next) break;
            while (next > start && (next[-1] == '\r' || next[-1] == '\n'))
                --next;
            result = collect_attachment_entity(
                start, (size_t)(next - start), depth + 1U, output, count);
            if (result != AMG_OK) {
                amg_buffer_free(&marker);
                amg_mail_headers_free(&headers);
                return result;
            }
            part = strstr(next, (const char *)marker.data);
        }
        amg_buffer_free(&marker);
        amg_mail_headers_free(&headers);
        return AMG_OK;
    }

    filename[0] = 0;
    if (disposition) {
        if (!param_value(disposition, "filename", filename, sizeof(filename)))
            param_value(disposition, "filename*", filename, sizeof(filename));
    }
    if (!filename[0]) {
        if (!param_value(content_type, "name", filename, sizeof(filename)))
            param_value(content_type, "name*", filename, sizeof(filename));
    }

    if (filename[0] &&
        ((disposition &&
          (ci_starts_with(disposition, "attachment") ||
           ci_starts_with(disposition, "inline"))) ||
         (!ci_starts_with(content_type, "text/plain") &&
          !ci_starts_with(content_type, "text/html")))) {
        if (count) ++*count;
        if (output)
            result = append_attachment_line(
                filename, content_type,
                length > body_offset ? length - body_offset : 0U,
                output);
    }

    amg_mail_headers_free(&headers);
    return result;
}

static int extract_entity(const char *message, size_t length, unsigned depth, AmgBuffer *output)
{
    AmgMailHeaders headers;
    size_t body_offset = 0;
    const char *content_type, *encoding;
    char boundary[256];
    int result;
    if (depth > 8U || length > AMIGMAIL_MAX_MESSAGE) return AMG_ERR_LIMIT;
    amg_mail_headers_init(&headers);
    result = amg_mail_headers_parse(message, length, &headers, &body_offset);
    if (result != AMG_OK) { amg_mail_headers_free(&headers); return result; }
    content_type = amg_mail_header_get(&headers, "Content-Type");
    encoding = amg_mail_header_get(&headers, "Content-Transfer-Encoding");
    if (!content_type) content_type = "text/plain";
    if (ci_equal_n(content_type, "multipart/", 10U) && param_value(content_type, "boundary", boundary, sizeof(boundary))) {
        AmgBuffer marker; const char *body = message + body_offset, *end = message + length, *part;
        amg_buffer_init(&marker); amg_buffer_append_cstr(&marker, "--"); amg_buffer_append_cstr(&marker, boundary); amg_buffer_terminate(&marker);
        part = strstr(body, (const char *)marker.data);
        while (part && part < end) {
            const char *start = part + marker.length, *next;
            if (start + 2 <= end && start[0] == '-' && start[1] == '-') break;
            if (start + 2 <= end && start[0] == '\r' && start[1] == '\n') start += 2;
            else if (start < end && *start == '\n') ++start;
            next = strstr(start, (const char *)marker.data);
            if (!next) break;
            {
                const char *part_end = next;
                while (part_end > start && (part_end[-1] == '\r' || part_end[-1] == '\n')) --part_end;
                result = extract_entity(start, (size_t)(part_end - start), depth + 1U, output);
                if (result == AMG_OK && output->length) { amg_buffer_free(&marker); amg_mail_headers_free(&headers); return AMG_OK; }
            }
            part = next;
        }
        amg_buffer_free(&marker); amg_mail_headers_free(&headers); return AMG_ERR_PARSE;
    } else {
        AmgBuffer decoded;
        const char *body = message + body_offset;
        size_t body_length = length - body_offset;
        amg_buffer_init(&decoded);
        if (encoding && ci_equal(encoding, "base64")) result = amg_base64_decode(body, body_length, &decoded);
        else if (encoding && ci_equal(encoding, "quoted-printable")) result = amg_quoted_printable_decode(body, body_length, &decoded);
        else result = amg_buffer_append(&decoded, body, body_length);
        if (result == AMG_OK) {
            if (ci_equal_n(content_type, "text/plain", 10U)) result = amg_buffer_append(output, decoded.data, decoded.length);
            else if (ci_equal_n(content_type, "text/html", 9U)) result = amg_html_to_text((const char *)decoded.data, decoded.length, output);
            else result = AMG_ERR_UNSUPPORTED;
        }
        amg_buffer_free(&decoded); amg_mail_headers_free(&headers); return result;
    }
}

int amg_mime_extract_text(const char *message, size_t length, AmgBuffer *output, AmgError *error)
{
    int result;
    if (!message || !output) return AMG_ERR_ARGUMENT;
    result = extract_entity(message, length, 0U, output);
    if (result != AMG_OK) amg_error_set(error, result, T("Kein darstellbarer Textteil in der Nachricht gefunden.", "No displayable text part was found in the message."));
    else amg_error_set(error, AMG_OK, "");
    return result;
}

int amg_mime_attachment_summary(const char *message, size_t length,
                                AmgBuffer *output, AmgError *error)
{
    int result;
    if (!message || !output) return AMG_ERR_ARGUMENT;
    result = collect_attachment_entity(message, length, 0U, output, NULL);
    if (result != AMG_OK)
        amg_error_set(error, result,
                      T("Dateianh\303\244nge konnten nicht ausgewertet werden.", "Attachments could not be parsed."));
    else
        amg_error_set(error, AMG_OK, "");
    return result;
}

int amg_mime_attachment_count(const char *message, size_t length,
                              size_t *count, AmgError *error)
{
    int result;
    size_t found = 0U;
    if (!message || !count) return AMG_ERR_ARGUMENT;
    result = collect_attachment_entity(message, length, 0U, NULL, &found);
    if (result == AMG_OK) {
        *count = found;
        amg_error_set(error, AMG_OK, "");
    } else {
        amg_error_set(error, result,
                      T("Dateianh\303\244nge konnten nicht ausgewertet werden.", "Attachments could not be parsed."));
    }
    return result;
}

static int entity_attachment_name(const AmgMailHeaders *headers,
                                  char filename[512])
{
    const char *content_type, *disposition;
    if (!headers || !filename) return 0;
    content_type = amg_mail_header_get(headers, "Content-Type");
    disposition = amg_mail_header_get(headers, "Content-Disposition");
    if (!content_type) content_type = "text/plain";
    filename[0] = 0;
    if (disposition) {
        if (!param_value(disposition, "filename", filename, 512U))
            param_value(disposition, "filename*", filename, 512U);
    }
    if (!filename[0]) {
        if (!param_value(content_type, "name", filename, 512U))
            param_value(content_type, "name*", filename, 512U);
    }
    return filename[0] &&
        ((disposition &&
          (ci_starts_with(disposition, "attachment") ||
           ci_starts_with(disposition, "inline"))) ||
         (!ci_starts_with(content_type, "text/plain") &&
          !ci_starts_with(content_type, "text/html")));
}

static int decode_attachment_body(const char *message, size_t length,
                                  size_t body_offset,
                                  const AmgMailHeaders *headers,
                                  AmgBuffer *data)
{
    const char *encoding;
    const char *body;
    size_t body_length;
    if (!message || !headers || !data || body_offset > length)
        return AMG_ERR_ARGUMENT;
    encoding = amg_mail_header_get(headers, "Content-Transfer-Encoding");
    body = message + body_offset;
    body_length = length - body_offset;
    if (encoding && ci_equal(encoding, "base64"))
        return amg_base64_decode(body, body_length, data);
    if (encoding && ci_equal(encoding, "quoted-printable"))
        return amg_quoted_printable_decode(body, body_length, data);
    return amg_buffer_append(data, body, body_length);
}

static int extract_attachment_entity(const char *message, size_t length,
                                     unsigned depth, size_t target,
                                     size_t *current, AmgBuffer *name_utf8,
                                     AmgBuffer *data)
{
    AmgMailHeaders headers;
    size_t body_offset = 0U;
    const char *content_type;
    char boundary[256], filename[512];
    int result;
    if (depth > 8U || length > AMIGMAIL_MAX_MESSAGE) return AMG_ERR_LIMIT;
    amg_mail_headers_init(&headers);
    result = amg_mail_headers_parse(message, length, &headers, &body_offset);
    if (result != AMG_OK) {
        amg_mail_headers_free(&headers);
        return result;
    }
    content_type = amg_mail_header_get(&headers, "Content-Type");
    if (!content_type) content_type = "text/plain";

    if (ci_starts_with(content_type, "multipart/") &&
        param_value(content_type, "boundary", boundary, sizeof(boundary))) {
        AmgBuffer marker;
        const char *body = message + body_offset, *end = message + length;
        const char *part;
        amg_buffer_init(&marker);
        result = amg_buffer_append_cstr(&marker, "--");
        if (result == AMG_OK) result = amg_buffer_append_cstr(&marker, boundary);
        if (result == AMG_OK) result = amg_buffer_terminate(&marker);
        if (result != AMG_OK) {
            amg_buffer_free(&marker);
            amg_mail_headers_free(&headers);
            return result;
        }
        part = strstr(body, (const char *)marker.data);
        while (part && part < end) {
            const char *start = part + marker.length, *next, *part_end;
            if (start + 2 <= end && start[0] == '-' && start[1] == '-') break;
            if (start + 2 <= end && start[0] == '\r' && start[1] == '\n')
                start += 2;
            else if (start < end && *start == '\n')
                ++start;
            next = strstr(start, (const char *)marker.data);
            if (!next) break;
            part_end = next;
            while (part_end > start &&
                   (part_end[-1] == '\r' || part_end[-1] == '\n'))
                --part_end;
            result = extract_attachment_entity(
                start, (size_t)(part_end - start), depth + 1U,
                target, current, name_utf8, data);
            if (result == AMG_OK) {
                amg_buffer_free(&marker);
                amg_mail_headers_free(&headers);
                return AMG_OK;
            }
            if (result != AMG_ERR_CANCELLED) {
                amg_buffer_free(&marker);
                amg_mail_headers_free(&headers);
                return result;
            }
            part = next;
        }
        amg_buffer_free(&marker);
        amg_mail_headers_free(&headers);
        return AMG_ERR_CANCELLED;
    }

    if (entity_attachment_name(&headers, filename)) {
        if (*current == target) {
            result = append_parameter_utf8(filename, name_utf8);
            if (result == AMG_OK) result = amg_buffer_terminate(name_utf8);
            if (result == AMG_OK)
                result = decode_attachment_body(message, length, body_offset,
                                                &headers, data);
            amg_mail_headers_free(&headers);
            return result;
        }
        ++*current;
    }
    amg_mail_headers_free(&headers);
    return AMG_ERR_CANCELLED;
}

int amg_mime_extract_attachment(const char *message, size_t length,
                                size_t index, AmgBuffer *name_utf8,
                                AmgBuffer *data, AmgError *error)
{
    size_t current = 0U;
    int result;
    if (!message || !name_utf8 || !data) return AMG_ERR_ARGUMENT;
    result = extract_attachment_entity(message, length, 0U, index, &current,
                                       name_utf8, data);
    if (result == AMG_ERR_CANCELLED) {
        result = AMG_ERR_ARGUMENT;
        amg_error_set(error, result, T("Dateianhang wurde nicht gefunden.", "Attachment was not found."));
    } else if (result != AMG_OK) {
        amg_error_set(error, result,
                      T("Dateianhang konnte nicht dekodiert werden.", "Attachment could not be decoded."));
    } else {
        amg_error_set(error, AMG_OK, "");
    }
    return result;
}
