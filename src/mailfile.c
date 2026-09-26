#include "mailfile.h"
#include "charset.h"
#include "i18n.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#define T(id, en) amg_tr((id), (en))
#define NO_PART ((size_t)-1)
#define IO_BLOCK 8192U

typedef struct FileLines {
    FILE *file;
    size_t next, end, pos, used;
    AmgTransfer *transfer;
    AmgError *error;
    size_t origin, total;
    unsigned char block[IO_BLOCK];
} FileLines;

typedef struct FileLine {
    size_t start, end, next, prefix_length;
    unsigned char prefix[512];
} FileLine;

static int ci_equal(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b)
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++))
            return 0;
    return !*a && !*b;
}

static int ci_prefix(const char *a, const char *b)
{
    if (!a) return 0;
    while (*b)
        if (!*a || tolower((unsigned char)*a++) !=
                   tolower((unsigned char)*b++)) return 0;
    return 1;
}

static void lines_init(FileLines *lines, FILE *file, size_t start, size_t end)
{
    memset(lines, 0, sizeof(*lines));
    lines->file = file; lines->next = start; lines->end = end;
}

/* The line length is unbounded, the prefix storage is bounded. Embedded NULs
 * and block boundaries have no special meaning. Refill seeks explicitly so
 * recursive readers may safely share the same FILE without stale offsets. */
static int line_next(FileLines *lines, FileLine *line)
{
    size_t position = lines->next - (lines->used - lines->pos);
    unsigned char previous = 0;
    memset(line, 0, sizeof(*line));
    if (position >= lines->end) return 0;
    line->start = position;
    for (;;) {
        const unsigned char *nl;
        size_t length, copy;
        if (lines->pos == lines->used) {
            size_t want = lines->end - lines->next;
            if (!want) break;
            if (want > sizeof(lines->block)) want = sizeof(lines->block);
            if (fseek(lines->file, (long)lines->next, SEEK_SET) != 0)
                return AMG_ERR_IO;
            lines->used = fread(lines->block, 1U, want, lines->file);
            lines->pos = 0U;
            if (!lines->used) return AMG_ERR_IO;
            lines->next += lines->used;
            {
                int report = amg_transfer_report(lines->transfer,
                    AMG_TRANSFER_INDEX, lines->next - lines->origin,
                    lines->total, lines->error);
                if (report != AMG_OK) return report;
            }
        }
        nl = (const unsigned char *)memchr(lines->block + lines->pos, '\n',
                                           lines->used - lines->pos);
        length = nl ? (size_t)(nl - (lines->block + lines->pos))
                    : lines->used - lines->pos;
        copy = sizeof(line->prefix) - line->prefix_length;
        if (copy > length) copy = length;
        if (copy) {
            memcpy(line->prefix + line->prefix_length,
                   lines->block + lines->pos, copy);
            line->prefix_length += copy;
        }
        if (length) previous = lines->block[lines->pos + length - 1U];
        lines->pos += length;
        position += length;
        if (nl) {
            ++lines->pos;
            line->end = position - (previous == '\r' ? 1U : 0U);
            line->next = position + 1U;
            if (line->prefix_length > line->end - line->start)
                line->prefix_length = line->end - line->start;
            return 1;
        }
    }
    line->end = line->next = position;
    return 1;
}

static int read_range(FILE *file, size_t start, size_t end, AmgBuffer *out)
{
    unsigned char block[IO_BLOCK];
    int result = AMG_OK;
    if (end < start || fseek(file, (long)start, SEEK_SET)) return AMG_ERR_IO;
    while (start < end && result == AMG_OK) {
        size_t want = end - start;
        size_t got;
        if (want > sizeof(block)) want = sizeof(block);
        got = fread(block, 1U, want, file);
        if (!got) return AMG_ERR_IO;
        result = amg_buffer_append(out, block, got);
        start += got;
    }
    return result;
}

static int read_headers(FILE *file, size_t start, size_t end,
                         AmgMailHeaders *headers, size_t *body)
{
    FileLines *lines;
    FileLine line;
    AmgBuffer raw;
    size_t stop = start, ignored = 0U;
    int step = 0, result = AMG_OK;
    lines = (FileLines *)malloc(sizeof(*lines));
    if (!lines) return AMG_ERR_MEMORY;
    lines_init(lines, file, start, end);
    while ((step = line_next(lines, &line)) > 0) {
        stop = line.next;
        if (stop - start > AMIGMAIL_MAX_LINE) { result = AMG_ERR_LIMIT; break; }
        if (line.end == line.start) break;
    }
    free(lines);
    if (step < 0) result = step;
    amg_buffer_init(&raw);
    (void)amg_buffer_set_limit(&raw, AMIGMAIL_MAX_LINE);
    if (result == AMG_OK) result = read_range(file, start, stop, &raw);
    if (result == AMG_OK)
        result = amg_mail_headers_parse((const char *)raw.data, raw.length,
                                        headers, &ignored);
    amg_buffer_free(&raw);
    if (result == AMG_OK) *body = stop;
    return result;
}

static int boundary_line(const FileLine *line, const char *boundary,
                           int *closing)
{
    size_t n = strlen(boundary), pos = n + 2U;
    size_t length = line->end - line->start;
    const unsigned char *p = line->prefix;
    if (length < pos || length > line->prefix_length ||
        p[0] != '-' || p[1] != '-' || memcmp(p + 2U, boundary, n)) return 0;
    *closing = 0;
    if (pos + 2U <= length && p[pos] == '-' && p[pos + 1U] == '-') {
        pos += 2U; *closing = 1;
    }
    while (pos < length && (p[pos] == ' ' || p[pos] == '\t')) ++pos;
    return pos == length;
}

static size_t part_end(FILE *file, size_t start, size_t end)
{
    unsigned char last[2];
    size_t n = end - start < 2U ? end - start : 2U;
    if (!n) return end;
    if (fseek(file, (long)(end - n), SEEK_SET) || fread(last, 1U, n, file) != n)
        return end;
    if (last[n - 1U] == '\n') {
        --end;
        if (n == 2U && last[0] == '\r') --end;
    }
    return end;
}

static int add_part(AmgMailFile *mail, size_t *index)
{
    AmgMailFilePart *grown;
    if (mail->count == mail->capacity) {
        size_t capacity = mail->capacity ? mail->capacity * 2U : 16U;
        if (capacity < mail->capacity || capacity > SIZE_MAX / sizeof(*grown))
            return AMG_ERR_LIMIT;
        grown = (AmgMailFilePart *)realloc(mail->parts, capacity * sizeof(*grown));
        if (!grown) return AMG_ERR_MEMORY;
        mail->parts = grown; mail->capacity = capacity;
    }
    *index = mail->count++;
    memset(&mail->parts[*index], 0, sizeof(mail->parts[*index]));
    mail->parts[*index].first_child = NO_PART;
    mail->parts[*index].next_sibling = NO_PART;
    return AMG_OK;
}

static int index_part(AmgMailFile *mail, FILE *file, size_t start, size_t end,
                       unsigned depth, int related, size_t *index,
                       AmgTransfer *transfer, AmgError *error)
{
    AmgMailHeaders headers;
    AmgBuffer name;
    const char *type, *encoding, *disposition;
    char boundary[256];
    size_t body = start, idx = NO_PART;
    int result;
    if (depth > 8U || end < start) return AMG_ERR_LIMIT;
    result = amg_transfer_report(transfer, AMG_TRANSFER_INDEX, start - mail->offset,
                                 mail->length, error);
    if (result != AMG_OK) return result;
    amg_mail_headers_init(&headers); amg_buffer_init(&name);
    result = read_headers(file, start, end, &headers, &body);
    if (result != AMG_OK) goto done;
    result = add_part(mail, &idx);
    if (result != AMG_OK) goto done;
    *index = idx;
    mail->parts[idx].start = start;
    mail->parts[idx].body = body;
    mail->parts[idx].end = end;
    type = amg_mail_header_get(&headers, "Content-Type");
    if (!type) type = "text/plain";
    disposition = amg_mail_header_get(&headers, "Content-Disposition");
    encoding = amg_mail_header_get(&headers, "Content-Transfer-Encoding");
    mail->parts[idx].encoding = !encoding || ci_equal(encoding, "7bit") ||
        ci_equal(encoding, "8bit") || ci_equal(encoding, "binary") ? 0 :
        ci_equal(encoding, "base64") ? 1 :
        ci_equal(encoding, "quoted-printable") ? 2 : -1;
    {
        size_t n = strcspn(type, "; \t\r\n");
        if (n >= sizeof(mail->parts[idx].content_type))
            n = sizeof(mail->parts[idx].content_type) - 1U;
        memcpy(mail->parts[idx].content_type, type, n);
    }
    if (ci_prefix(type, "multipart/")) {
        FileLines *lines;
        FileLine line;
        size_t child_start = NO_PART, previous = NO_PART;
        int step, closing, closed = 0;
        mail->parts[idx].kind = ci_prefix(type, "multipart/alternative") ? 4 : 3;
        related = related || ci_prefix(type, "multipart/related");
        if (!amg_mime_parameter(type, "boundary", boundary, sizeof(boundary))) {
            result = AMG_ERR_PARSE; goto done;
        }
        lines = (FileLines *)malloc(sizeof(*lines));
        if (!lines) { result = AMG_ERR_MEMORY; goto done; }
        lines_init(lines, file, body, end);
        lines->transfer = transfer; lines->error = error;
        lines->origin = mail->offset; lines->total = mail->length;
        while ((step = line_next(lines, &line)) > 0) {
            if (!boundary_line(&line, boundary, &closing)) continue;
            if (child_start != NO_PART) {
                size_t child;
                result = index_part(mail, file, child_start,
                    part_end(file, child_start, line.start), depth + 1U,
                    related, &child, transfer, error);
                if (result != AMG_OK) break;
                if (previous == NO_PART) mail->parts[idx].first_child = child;
                else mail->parts[previous].next_sibling = child;
                previous = child;
            }
            if (closing) { closed = 1; break; }
            child_start = line.next;
        }
        free(lines);
        if (step < 0) result = step;
        if (result == AMG_OK && !closed) result = AMG_ERR_PARSE;
    } else {
        int found = amg_mime_describe_attachment(&headers, related, &name,
                                                  &mail->parts[idx].embedded);
        if (found < 0) { result = found; goto done; }
        if (found) {
            mail->parts[idx].name_utf8 = (char *)name.data;
            name.data = NULL;
            ++mail->attachment_count;
        }
        if (!ci_prefix(disposition, "attachment"))
            mail->parts[idx].kind = ci_prefix(type, "text/plain") ? 1 :
                                    ci_prefix(type, "text/html") ? 2 : 0;
    }
    if (idx == 0U && result == AMG_OK) {
        mail->headers = headers;
        amg_mail_headers_init(&headers);
    }
done:
    amg_mail_headers_free(&headers); amg_buffer_free(&name);
    return result;
}

typedef struct DecodeSink {
    FILE *file;
    AmgBuffer *buffer;
    size_t written, limit, used;
    unsigned char bytes[IO_BLOCK];
} DecodeSink;

static int sink_flush(DecodeSink *sink)
{
    int result = AMG_OK;
    if (sink->used > sink->limit - sink->written) return AMG_ERR_LIMIT;
    if (sink->file) {
        if (fwrite(sink->bytes, 1U, sink->used, sink->file) != sink->used)
            return AMG_ERR_IO;
    } else result = amg_buffer_append(sink->buffer, sink->bytes, sink->used);
    if (result == AMG_OK) { sink->written += sink->used; sink->used = 0U; }
    return result;
}

static int sink_byte(DecodeSink *sink, unsigned value)
{
    if (sink->written + sink->used >= sink->limit) return AMG_ERR_LIMIT;
    sink->bytes[sink->used++] = (unsigned char)value;
    return sink->used == sizeof(sink->bytes) ? sink_flush(sink) : AMG_OK;
}

static int b64_value(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int hex_value(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int decode_part(FILE *file, const AmgMailFilePart *part,
                         DecodeSink *sink, AmgTransferPhase phase,
                         AmgTransfer *transfer, AmgError *error)
{
    unsigned char bytes[IO_BLOCK];
    size_t pos = part->body;
    unsigned long bits = 0UL;
    unsigned q = 0U;
    int pad = 0, qp = 0, high = 0, result = AMG_OK;
    if (part->encoding < 0) return AMG_ERR_UNSUPPORTED;
    if (fseek(file, (long)pos, SEEK_SET)) return AMG_ERR_IO;
    while (pos < part->end && result == AMG_OK) {
        size_t want = part->end - pos, got, i;
        result = amg_transfer_report(transfer, phase, pos - part->body,
                                     part->end - part->body, error);
        if (result != AMG_OK) break;
        if (want > sizeof(bytes)) want = sizeof(bytes);
        got = fread(bytes, 1U, want, file);
        if (!got) { result = AMG_ERR_IO; break; }
        pos += got;
        for (i = 0U; i < got && result == AMG_OK; ++i) {
            unsigned char c = bytes[i];
            if (part->encoding == 0) result = sink_byte(sink, c);
            else if (part->encoding == 1) {
                int v;
                if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
                if (pad) {
                    if (pad == 1 && c == '=') { pad = 2; continue; }
                    result = AMG_ERR_PARSE; break;
                }
                if (c == '=') {
                    if (q == 2U) { result = sink_byte(sink, bits >> 4U); pad = 1; }
                    else if (q == 3U) {
                        result = sink_byte(sink, bits >> 10U);
                        if (result == AMG_OK) result = sink_byte(sink, bits >> 2U);
                        pad = 2;
                    } else result = AMG_ERR_PARSE;
                    q = 0U; continue;
                }
                v = b64_value(c);
                if (v < 0) { result = AMG_ERR_PARSE; break; }
                bits = ((bits << 6U) | (unsigned)v) & 0xffffffUL;
                if (++q == 4U) {
                    result = sink_byte(sink, bits >> 16U);
                    if (result == AMG_OK) result = sink_byte(sink, bits >> 8U);
                    if (result == AMG_OK) result = sink_byte(sink, bits);
                    q = 0U; bits = 0UL;
                }
            } else {
                if (qp == 0) {
                    if (c == '=') qp = 1;
                    else result = sink_byte(sink, c);
                } else if (qp == 1) {
                    if (c == '\r') qp = 3;
                    else if (c == '\n') qp = 0;
                    else if ((high = hex_value(c)) >= 0) qp = 2;
                    else { result = AMG_ERR_PARSE; break; }
                } else if (qp == 2) {
                    int low = hex_value(c);
                    if (low < 0) result = AMG_ERR_PARSE;
                    else result = sink_byte(sink, (unsigned)((high << 4) | low));
                    qp = 0;
                } else {
                    if (c != '\n') result = AMG_ERR_PARSE;
                    qp = 0;
                }
            }
        }
    }
    if (result == AMG_OK && part->encoding == 1) {
        if (q == 1U || pad == 1) result = AMG_ERR_PARSE;
        else if (q == 2U) result = sink_byte(sink, bits >> 4U);
        else if (q == 3U) {
            result = sink_byte(sink, bits >> 10U);
            if (result == AMG_OK) result = sink_byte(sink, bits >> 2U);
        }
    }
    if (result == AMG_OK && part->encoding == 2 && qp) result = AMG_ERR_PARSE;
    if (result == AMG_OK) result = sink_flush(sink);
    if (result == AMG_OK)
        result = amg_transfer_report(transfer, phase, part->end - part->body,
                                     part->end - part->body, error);
    return result;
}

static int render_part(AmgMailFile *mail, FILE *file, size_t idx,
                         AmgBuffer *text, AmgTransfer *transfer, AmgError *error)
{
    AmgMailFilePart *part = &mail->parts[idx];
    if (part->kind >= 3) {
        unsigned pass, passes = part->kind == 4 ? 3U : 1U;
        int remembered = AMG_ERR_UNSUPPORTED, empty = 0;
        for (pass = 0U; pass < passes; ++pass) {
            size_t child;
            for (child = part->first_child; child != NO_PART;
                 child = mail->parts[child].next_sibling) {
                AmgBuffer candidate;
                int result, plain = mail->parts[child].kind == 1;
                if (passes > 1U && ((pass == 0U && !plain) ||
                                   (pass == 1U && plain) ||
                                   (pass == 2U && !plain))) continue;
                amg_buffer_init(&candidate);
                (void)amg_buffer_set_limit(&candidate, AMIMAIL_MAX_PREVIEW_TEXT);
                result = render_part(mail, file, child, &candidate, transfer, error);
                if (result == AMG_ERR_MEMORY || result == AMG_ERR_IO ||
                    result == AMG_ERR_CANCELLED) { amg_buffer_free(&candidate); return result; }
                if (passes > 1U && pass == 0U && plain && result == AMG_OK &&
                    amg_mime_plain_is_css((const char *)candidate.data, candidate.length)) {
                    amg_buffer_free(&candidate); continue;
                }
                if ((result == AMG_OK && candidate.length) || result == AMG_ERR_LIMIT) {
                    int append_result = amg_buffer_append(text, candidate.data,
                                                          candidate.length);
                    amg_buffer_free(&candidate);
                    return append_result == AMG_OK ? result : append_result;
                }
                if (result == AMG_OK) empty = 1;
                else remembered = result;
                amg_buffer_free(&candidate);
            }
        }
        return empty ? AMG_OK : remembered;
    }
    if (part->kind == 1 || part->kind == 2) {
        AmgMailHeaders headers;
        AmgBuffer decoded, entity;
        DecodeSink *sink;
        size_t body = 0U, i;
        int result;
        amg_mail_headers_init(&headers);
        amg_buffer_init(&decoded); amg_buffer_init(&entity);
        (void)amg_buffer_set_limit(&decoded, AMIMAIL_MAX_TEXT_PART);
        (void)amg_buffer_set_limit(&entity, AMIMAIL_MAX_TEXT_PART + AMIGMAIL_MAX_LINE);
        sink = (DecodeSink *)calloc(1U, sizeof(*sink));
        if (!sink) return AMG_ERR_MEMORY;
        sink->buffer = &decoded; sink->limit = AMIMAIL_MAX_TEXT_PART;
        result = read_headers(file, part->start, part->end, &headers, &body);
        if (result == AMG_OK)
            result = decode_part(file, part, sink, AMG_TRANSFER_INDEX, transfer, error);
        free(sink);
        for (i = 0U; result == AMG_OK && i < headers.count; ++i) {
            const AmgMailHeader *h = &headers.items[i];
            if (ci_equal(h->name, "Content-Transfer-Encoding")) continue;
            result = amg_buffer_append_cstr(&entity, h->name);
            if (result == AMG_OK) result = amg_buffer_append_cstr(&entity, ": ");
            if (result == AMG_OK) result = amg_buffer_append_cstr(&entity, h->value);
            if (result == AMG_OK) result = amg_buffer_append_cstr(&entity, "\r\n");
        }
        if (result == AMG_OK) result = amg_buffer_append_cstr(&entity,
                                      "Content-Transfer-Encoding: 8bit\r\n\r\n");
        if (result == AMG_OK) result = amg_buffer_append(&entity, decoded.data, decoded.length);
        amg_buffer_free(&decoded);
        if (result == AMG_OK)
            result = amg_mime_extract_text((const char *)entity.data, entity.length, text, error);
        amg_mail_headers_free(&headers); amg_buffer_free(&entity);
        return result;
    }
    return AMG_ERR_UNSUPPORTED;
}

int amg_mailfile_open_range(const char *path, size_t offset, size_t requested_length,
                            int remove_on_close, AmgTransfer *transfer,
                            AmgMailFile **output,
                      AmgError *error)
{
    FILE *file;
    AmgMailFile *mail;
    long length;
    size_t root = NO_PART;
    int result;
    if (!output || !path || strlen(path) >= AMG_SPOOL_PATH_MAX)
        return AMG_ERR_ARGUMENT;
    *output = NULL;
    file = fopen(path, "rb");
    if (!file) return AMG_ERR_IO;
    if (fseek(file, 0L, SEEK_END) || (length = ftell(file)) < 0) {
        fclose(file); return AMG_ERR_IO;
    }
    if (requested_length == SIZE_MAX) requested_length = (size_t)length;
    if (offset > (size_t)length || requested_length > (size_t)length - offset ||
        requested_length > AMIMAIL_MAX_MIME_MESSAGE) {
        fclose(file); return AMG_ERR_LIMIT;
    }
    mail = (AmgMailFile *)calloc(1U, sizeof(*mail));
    if (!mail) { fclose(file); return AMG_ERR_MEMORY; }
    strcpy(mail->path, path); mail->length = requested_length; mail->offset = offset;
    amg_mail_headers_init(&mail->headers); amg_buffer_init(&mail->text);
    (void)amg_buffer_set_limit(&mail->text, AMIMAIL_MAX_PREVIEW_TEXT);
    result = index_part(mail, file, offset, offset + mail->length, 0U, 0, &root, transfer, error);
    if (result == AMG_OK) {
        mail->text_result = render_part(mail, file, root, &mail->text, transfer, error);
        if (mail->text_result == AMG_ERR_MEMORY || mail->text_result == AMG_ERR_IO ||
            mail->text_result == AMG_ERR_CANCELLED) result = mail->text_result;
    }
    fclose(file);
    if (result != AMG_OK) { amg_mailfile_close(mail); return result; }
    mail->remove_on_close = remove_on_close;
    *output = mail;
    amg_error_set(error, AMG_OK, "");
    return AMG_OK;
}

void amg_mailfile_close(AmgMailFile *file)
{
    size_t i;
    if (!file) return;
    for (i = 0U; i < file->count; ++i) free(file->parts[i].name_utf8);
    free(file->parts);
    amg_mail_headers_free(&file->headers); amg_buffer_free(&file->text);
    if (file->remove_on_close) amg_spool_remove(file->path);
    free(file);
}

const AmgMailFilePart *amg_mailfile_attachment(const AmgMailFile *file, size_t index)
{
    size_t i;
    if (!file) return NULL;
    for (i = 0U; i < file->count; ++i)
        if (file->parts[i].name_utf8 && index-- == 0U) return &file->parts[i];
    return NULL;
}

int amg_mailfile_summary(const AmgMailFile *file, AmgBuffer *attachments,
                         AmgBuffer *graphics)
{
    size_t i;
    int result = AMG_OK;
    if (!file || !attachments || !graphics) return AMG_ERR_ARGUMENT;
    for (i = 0U; i < file->count && result == AMG_OK; ++i) {
        const AmgMailFilePart *part = &file->parts[i];
        AmgBuffer *out = part->embedded ? graphics : attachments;
        char detail[176];
        if (!part->name_utf8) continue;
        snprintf(detail, sizeof(detail), " (%s, %lu KB)\n", part->content_type,
                 (unsigned long)((part->end - part->body + 1023U) / 1024U));
        result = amg_buffer_append_cstr(out, "- ");
        if (result == AMG_OK) result = amg_buffer_append_cstr(out, part->name_utf8);
        if (result == AMG_OK) result = amg_buffer_append_cstr(out, detail);
    }
    return result;
}

int amg_mailfile_payload(AmgMailFile *file, const AmgImapFetchRecord *record,
                         AmgBuffer *payload, AmgError *error)
{
    AmgBuffer entity;
    size_t i;
    char prefix[192];
    int result = AMG_OK;
    if (!file || !record || !record->uid || !payload) return AMG_ERR_ARGUMENT;
    amg_buffer_init(&entity);
    (void)amg_buffer_set_limit(&entity, AMIMAIL_MAX_PREVIEW_TEXT + AMIGMAIL_MAX_LINE);
    for (i = 0U; i < file->headers.count && result == AMG_OK; ++i) {
        const AmgMailHeader *h = &file->headers.items[i];
        if (ci_prefix(h->name, "Content-") || ci_equal(h->name, "MIME-Version")) continue;
        result = amg_buffer_append_cstr(&entity, h->name);
        if (result == AMG_OK) result = amg_buffer_append_cstr(&entity, ": ");
        if (result == AMG_OK) result = amg_buffer_append_cstr(&entity, h->value);
        if (result == AMG_OK) result = amg_buffer_append_cstr(&entity, "\r\n");
    }
    if (result == AMG_OK) result = amg_buffer_append_cstr(&entity,
        "Content-Type: text/plain; charset=UTF-8\r\n"
        "Content-Transfer-Encoding: 8bit\r\n\r\n");
    if (result == AMG_OK) result = amg_buffer_append(&entity, file->text.data, file->text.length);
    snprintf(prefix, sizeof(prefix), "* 1 FETCH (UID %lu FLAGS (%s%s%s%s) BODY[] {%lu}\r\n",
             record->uid, record->seen ? "\\Seen " : "",
             record->flagged ? "\\Flagged " : "", record->answered ? "\\Answered " : "",
             record->deleted ? "\\Deleted " : "", (unsigned long)entity.length);
    if (result == AMG_OK) result = amg_buffer_append_cstr(payload, prefix);
    if (result == AMG_OK) result = amg_buffer_append(payload, entity.data, entity.length);
    if (result == AMG_OK) result = amg_buffer_append_cstr(payload, ")\r\n");
    amg_buffer_free(&entity);
    if (result != AMG_OK) amg_error_set(error, result, T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory."));
    file->uid = record->uid;
    return result;
}

int amg_mailfile_extract(const AmgMailFile *file, size_t index, FILE *output,
                         size_t limit, size_t *written,
                         AmgTransfer *transfer, AmgError *error)
{
    const AmgMailFilePart *part = amg_mailfile_attachment(file, index);
    FILE *input;
    DecodeSink *sink;
    int result;
    if (written) *written = 0U;
    if (!part || !output) return AMG_ERR_ARGUMENT;
    input = fopen(file->path, "rb");
    if (!input) return AMG_ERR_IO;
    sink = (DecodeSink *)calloc(1U, sizeof(*sink));
    if (!sink) { fclose(input); return AMG_ERR_MEMORY; }
    sink->file = output; sink->limit = limit;
    result = decode_part(input, part, sink, AMG_TRANSFER_EXPORT, transfer, error);
    if (written) *written = sink->written;
    free(sink); fclose(input);
    if (result != AMG_OK && (!error || error->code == AMG_OK))
        amg_error_set(error, result,
                      T(MSG_ATTACHMENT_COULD_NOT_BE_DECODED, "Attachment could not be decoded."));
    return result;
}

int amg_mailfile_open(const char *path, int remove_on_close,
                      AmgTransfer *transfer, AmgMailFile **output,
                      AmgError *error)
{
    return amg_mailfile_open_range(path, 0U, SIZE_MAX, remove_on_close,
                                  transfer, output, error);
}
