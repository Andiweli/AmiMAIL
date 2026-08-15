#include "imap_parser.h"

#include <ctype.h>
#include <limits.h>
#include <string.h>

void amg_imap_parser_init(AmgImapParser *parser)
{
    if (!parser) return;
    amg_buffer_init(&parser->pending);
    amg_buffer_init(&parser->event_data);
    parser->literal_remaining = 0;
    parser->failure_size = 0;
    parser->failure_limit = 0;
    parser->failure = AMG_IMAP_PARSER_FAILURE_NONE;
    parser->waiting_literal = 0;
    parser->failed = 0;
}

void amg_imap_parser_free(AmgImapParser *parser)
{
    if (!parser) return;
    amg_buffer_free(&parser->pending);
    amg_buffer_free(&parser->event_data);
    parser->literal_remaining = 0;
    parser->failure_size = 0;
    parser->failure_limit = 0;
    parser->failure = AMG_IMAP_PARSER_FAILURE_NONE;
    parser->waiting_literal = 0;
    parser->failed = 0;
}

int amg_imap_parser_feed(AmgImapParser *parser, const void *data, size_t length)
{
    int result;
    if (!parser || (!data && length) || parser->failed) return AMG_ERR_ARGUMENT;
    if (length > SIZE_MAX - parser->pending.length ||
        parser->pending.length + length >
            AMIGMAIL_MAX_MESSAGE + AMIGMAIL_MAX_LINE) {
        parser->failure = AMG_IMAP_PARSER_FAILURE_BUFFER_LIMIT;
        parser->failure_size = length > SIZE_MAX - parser->pending.length
                                   ? SIZE_MAX
                                   : parser->pending.length + length;
        parser->failure_limit = AMIGMAIL_MAX_MESSAGE + AMIGMAIL_MAX_LINE;
        parser->failed = 1;
        return AMG_ERR_LIMIT;
    }
    result = amg_buffer_append(&parser->pending, data, length);
    if (result != AMG_OK) parser->failed = 1;
    return result;
}

int amg_imap_parse_literal_length(const unsigned char *line, size_t length, size_t *literal_length)
{
    size_t end, start, value = 0;
    if (!line || !literal_length) return 0;
    *literal_length = 0;
    while (length && (line[length - 1U] == '\r' || line[length - 1U] == '\n')) --length;
    if (length < 3U || line[length - 1U] != '}') return 0;
    end = length - 1U;
    if (end && line[end - 1U] == '+') --end;
    start = end;
    while (start && isdigit(line[start - 1U])) --start;
    if (!start || line[start - 1U] != '{' || start == end) return 0;
    while (start < end) {
        unsigned digit = (unsigned)(line[start++] - '0');
        if (value > (SIZE_MAX - digit) / 10U) return -1;
        value = value * 10U + digit;
    }
    *literal_length = value;
    return 1;
}

static int ascii_ci_token(const unsigned char *text, size_t length,
                          const char *token)
{
    size_t i, token_length = strlen(token);
    if (length < token_length) return 0;
    for (i = 0; i < token_length; ++i) {
        if (tolower(text[i]) != tolower((unsigned char)token[i])) return 0;
    }
    return length == token_length || text[token_length] == ' ' ||
           text[token_length] == '\t' || text[token_length] == '[' ||
           text[token_length] == '\r' || text[token_length] == '\n';
}

int amg_imap_greeting_is_success(const unsigned char *line, size_t length)
{
    size_t position = 0;
    if (!line) return 0;
    while (position < length &&
           (line[position] == ' ' || line[position] == '\t'))
        ++position;
    if (position >= length || line[position++] != '*') return 0;
    while (position < length &&
           (line[position] == ' ' || line[position] == '\t'))
        ++position;
    return ascii_ci_token(line + position, length - position, "OK") ||
           ascii_ci_token(line + position, length - position, "PREAUTH");
}

int amg_imap_greeting_is_preauth(const unsigned char *line, size_t length)
{
    size_t position = 0;
    if (!line) return 0;
    while (position < length &&
           (line[position] == ' ' || line[position] == '\t'))
        ++position;
    if (position >= length || line[position++] != '*') return 0;
    while (position < length &&
           (line[position] == ' ' || line[position] == '\t'))
        ++position;
    return ascii_ci_token(line + position, length - position, "PREAUTH");
}

int amg_imap_greeting_status(const unsigned char *data, size_t length)
{
    size_t position;
    if (!data) return 0;
    for (position = 0; position < length; ++position) {
        size_t status;
        if (data[position] != '*') continue;
        if (position > 0U && data[position - 1U] != '\r' &&
            data[position - 1U] != '\n' && data[position - 1U] != ' ' &&
            data[position - 1U] != '\t' && data[position - 1U] >= 0x20U)
            continue;
        status = position + 1U;
        while (status < length &&
               (data[status] == ' ' || data[status] == '\t'))
            ++status;
        if (ascii_ci_token(data + status, length - status, "OK") ||
            ascii_ci_token(data + status, length - status, "PREAUTH"))
            return 1;
        if (ascii_ci_token(data + status, length - status, "BYE") ||
            ascii_ci_token(data + status, length - status, "NO") ||
            ascii_ci_token(data + status, length - status, "BAD"))
            return -1;
    }
    return 0;
}

static int parse_ulong_token(const unsigned char *data, size_t length,
                             size_t *position, unsigned long *value)
{
    unsigned long parsed = 0;
    size_t cursor = *position;
    if (cursor >= length || !isdigit((unsigned char)data[cursor])) return 0;
    while (cursor < length && isdigit((unsigned char)data[cursor])) {
        unsigned digit = (unsigned)(data[cursor] - '0');
        if (parsed > (ULONG_MAX - digit) / 10UL) return 0;
        parsed = parsed * 10UL + digit;
        ++cursor;
    }
    *position = cursor;
    *value = parsed;
    return 1;
}

static int line_prefix_number(const unsigned char *line, size_t length,
                              unsigned long *number, size_t *position)
{
    size_t cursor = 0;
    if (!length || line[cursor++] != '*') return 0;
    while (cursor < length &&
           (line[cursor] == ' ' || line[cursor] == '\t'))
        ++cursor;
    if (!parse_ulong_token(line, length, &cursor, number)) return 0;
    while (cursor < length &&
           (line[cursor] == ' ' || line[cursor] == '\t'))
        ++cursor;
    *position = cursor;
    return 1;
}

static int find_number_after_token(const unsigned char *line, size_t length,
                                   size_t position, const char *token,
                                   unsigned long *number)
{
    size_t token_length = strlen(token);
    while (position < length) {
        size_t cursor;
        while (position < length &&
               !isalnum((unsigned char)line[position]))
            ++position;
        if (position >= length) break;
        if (!ascii_ci_token(line + position, length - position, token)) {
            while (position < length &&
                   isalnum((unsigned char)line[position]))
                ++position;
            continue;
        }
        cursor = position + token_length;
        while (cursor < length &&
               (line[cursor] == ' ' || line[cursor] == '\t'))
            ++cursor;
        return parse_ulong_token(line, length, &cursor, number);
    }
    return 0;
}

int amg_imap_parse_exists(const unsigned char *data, size_t length,
                          unsigned long *exists)
{
    size_t line_start = 0;
    if (!data || !exists) return 0;
    while (line_start < length) {
        size_t line_end = line_start;
        size_t position;
        unsigned long number;
        while (line_end < length && data[line_end] != '\r' &&
               data[line_end] != '\n')
            ++line_end;
        if (line_prefix_number(data + line_start, line_end - line_start,
                               &number, &position) &&
            ascii_ci_token(data + line_start + position,
                           line_end - line_start - position, "EXISTS")) {
            *exists = number;
            return 1;
        }
        while (line_end < length &&
               (data[line_end] == '\r' || data[line_end] == '\n'))
            ++line_end;
        line_start = line_end;
    }
    return 0;
}

int amg_imap_parse_uidvalidity(const unsigned char *data, size_t length,
                               unsigned long *uid_validity)
{
    size_t line_start = 0;
    if (!data || !uid_validity) return 0;
    while (line_start < length) {
        size_t line_end = line_start;
        unsigned long number;
        while (line_end < length && data[line_end] != '\r' &&
               data[line_end] != '\n')
            ++line_end;
        if (find_number_after_token(data + line_start,
                                    line_end - line_start,
                                    0U, "UIDVALIDITY", &number)) {
            *uid_validity = number;
            return 1;
        }
        while (line_end < length &&
               (data[line_end] == '\r' || data[line_end] == '\n'))
            ++line_end;
        line_start = line_end;
    }
    return 0;
}

int amg_imap_parse_fetch_sequence(const unsigned char *data, size_t length,
                                  unsigned long uid,
                                  unsigned long *sequence)
{
    size_t line_start = 0;
    if (!data || !uid || !sequence) return 0;
    while (line_start < length) {
        size_t line_end = line_start;
        size_t position;
        unsigned long line_sequence, line_uid;
        while (line_end < length && data[line_end] != '\r' &&
               data[line_end] != '\n')
            ++line_end;
        if (line_prefix_number(data + line_start, line_end - line_start,
                               &line_sequence, &position) &&
            ascii_ci_token(data + line_start + position,
                           line_end - line_start - position, "FETCH") &&
            find_number_after_token(data + line_start, line_end - line_start,
                                    position + 5U, "UID", &line_uid) &&
            line_uid == uid) {
            *sequence = line_sequence;
            return 1;
        }
        while (line_end < length &&
               (data[line_end] == '\r' || data[line_end] == '\n'))
            ++line_end;
        line_start = line_end;
    }
    return 0;
}

static int imap_token_character(unsigned char value)
{
    return isalnum(value) || value == '-' || value == '_' || value == '.';
}

static int line_token_at(const unsigned char *line, size_t length,
                         size_t position, const char *token)
{
    size_t i, token_length = strlen(token);
    if (position + token_length > length ||
        (position && imap_token_character(line[position - 1U])) ||
        (position + token_length < length &&
         imap_token_character(line[position + token_length])))
        return 0;
    for (i = 0; i < token_length; ++i) {
        if (tolower((unsigned char)line[position + i]) !=
            tolower((unsigned char)token[i]))
            return 0;
    }
    return 1;
}

static int line_find_number(const unsigned char *line, size_t length,
                            const char *token, unsigned long *number)
{
    size_t position, token_length = strlen(token);
    for (position = 0; position < length; ++position) {
        size_t cursor;
        if (!line_token_at(line, length, position, token)) continue;
        cursor = position + token_length;
        while (cursor < length &&
               (line[cursor] == ' ' || line[cursor] == '\t'))
            ++cursor;
        return parse_ulong_token(line, length, &cursor, number);
    }
    return 0;
}

static int line_contains_text(const unsigned char *line, size_t length,
                              const char *text)
{
    size_t position, i, text_length = strlen(text);
    if (text_length > length) return 0;
    for (position = 0; position + text_length <= length; ++position) {
        for (i = 0; i < text_length; ++i) {
            if (tolower((unsigned char)line[position + i]) !=
                tolower((unsigned char)text[i]))
                break;
        }
        if (i == text_length) return 1;
    }
    return 0;
}

int amg_imap_fetch_record_next(const unsigned char *data, size_t length,
                               size_t *position,
                               AmgImapFetchRecord *record)
{
    size_t cursor;
    if (!data || !position || !record || *position > length)
        return AMG_ERR_ARGUMENT;
    cursor = *position;
    while (cursor < length) {
        size_t line_start = cursor, line_end, next_line, prefix_position;
        size_t literal_length = 0;
        unsigned long sequence = 0, uid = 0, size = 0;
        int literal_status;

        while (cursor < length && data[cursor] != '\n') ++cursor;
        next_line = cursor < length ? cursor + 1U : cursor;
        line_end = cursor;
        if (line_end > line_start && data[line_end - 1U] == '\r') --line_end;
        cursor = next_line;

        if (!line_prefix_number(data + line_start, line_end - line_start,
                                &sequence, &prefix_position) ||
            !ascii_ci_token(data + line_start + prefix_position,
                            line_end - line_start - prefix_position,
                            "FETCH"))
            continue;

        literal_status = amg_imap_parse_literal_length(
            data + line_start, next_line - line_start, &literal_length);
        if (literal_status <= 0 ||
            !line_find_number(data + line_start, line_end - line_start,
                              "UID", &uid))
            continue;
        if (literal_length > length - cursor) {
            *position = length;
            return AMG_ERR_PARSE;
        }
        (void)line_find_number(data + line_start, line_end - line_start,
                               "RFC822.SIZE", &size);
        memset(record, 0, sizeof(*record));
        record->uid = uid;
        record->rfc822_size = size;
        record->seen = line_contains_text(
            data + line_start, line_end - line_start, "\\Seen");
        record->flagged = line_contains_text(
            data + line_start, line_end - line_start, "\\Flagged");
        record->deleted = line_contains_text(
            data + line_start, line_end - line_start, "\\Deleted");
        record->literal = data + cursor;
        record->literal_length = literal_length;
        cursor += literal_length;
        *position = cursor;
        (void)sequence;
        return 1;
    }
    *position = cursor;
    return 0;
}

int amg_imap_parser_next(AmgImapParser *parser, AmgImapEvent *event)
{
    size_t i, line_length, literal_length = 0;
    int literal_status;
    if (!parser || !event) return AMG_ERR_ARGUMENT;
    event->type = AMG_IMAP_EVENT_NONE;
    event->data = NULL;
    event->length = 0;
    parser->event_data.length = 0;

    if (parser->waiting_literal) {
        if (parser->pending.length < parser->literal_remaining) return 0;
        if (amg_buffer_append(&parser->event_data, parser->pending.data, parser->literal_remaining) != AMG_OK) {
            parser->failed = 1; event->type = AMG_IMAP_EVENT_ERROR; return AMG_ERR_MEMORY;
        }
        amg_buffer_consume(&parser->pending, parser->literal_remaining);
        parser->waiting_literal = 0;
        parser->literal_remaining = 0;
        event->type = AMG_IMAP_EVENT_LITERAL;
        event->data = parser->event_data.data;
        event->length = parser->event_data.length;
        return 1;
    }

    for (i = 0; i + 1U < parser->pending.length; ++i) {
        if (parser->pending.data[i] == '\r' && parser->pending.data[i + 1U] == '\n') break;
    }
    if (i + 1U >= parser->pending.length) {
        if (parser->pending.length > AMIGMAIL_MAX_LINE) {
            parser->failure = AMG_IMAP_PARSER_FAILURE_LINE_LIMIT;
            parser->failure_size = parser->pending.length;
            parser->failure_limit = AMIGMAIL_MAX_LINE;
            parser->failed = 1;
            return AMG_ERR_LIMIT;
        }
        return 0;
    }
    line_length = i + 2U;
    if (amg_buffer_append(&parser->event_data, parser->pending.data, line_length) != AMG_OK) {
        parser->failed = 1; return AMG_ERR_MEMORY;
    }
    amg_buffer_consume(&parser->pending, line_length);
    literal_status = amg_imap_parse_literal_length(parser->event_data.data,
                                                   parser->event_data.length,
                                                   &literal_length);
    if (literal_status < 0 ||
        (literal_status > 0 && literal_length > AMIGMAIL_MAX_MESSAGE)) {
        parser->failure = AMG_IMAP_PARSER_FAILURE_LITERAL_LIMIT;
        parser->failure_size = literal_status < 0 ? SIZE_MAX : literal_length;
        parser->failure_limit = AMIGMAIL_MAX_MESSAGE;
        parser->failed = 1; event->type = AMG_IMAP_EVENT_ERROR; return AMG_ERR_LIMIT;
    }
    if (literal_status > 0) {
        parser->waiting_literal = 1;
        parser->literal_remaining = literal_length;
    }
    event->type = AMG_IMAP_EVENT_LINE;
    event->data = parser->event_data.data;
    event->length = parser->event_data.length;
    return 1;
}
