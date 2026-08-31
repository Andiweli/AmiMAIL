#include "mime.h"
#include "i18n.h"

#define T(id, en) amg_tr((id), (en))
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

static int append_utf8_codepoint(AmgBuffer *output, unsigned long codepoint)
{
    unsigned char encoded[4];
    size_t count;
    if (!output) return AMG_ERR_ARGUMENT;
    if (codepoint <= 0x7FUL) {
        encoded[0] = (unsigned char)codepoint;
        count = 1U;
    } else if (codepoint <= 0x7FFUL) {
        encoded[0] = (unsigned char)(0xC0U | (codepoint >> 6));
        encoded[1] = (unsigned char)(0x80U | (codepoint & 0x3FUL));
        count = 2U;
    } else if (codepoint <= 0xFFFFUL &&
               !(codepoint >= 0xD800UL && codepoint <= 0xDFFFUL)) {
        encoded[0] = (unsigned char)(0xE0U | (codepoint >> 12));
        encoded[1] = (unsigned char)(0x80U | ((codepoint >> 6) & 0x3FUL));
        encoded[2] = (unsigned char)(0x80U | (codepoint & 0x3FUL));
        count = 3U;
    } else if (codepoint > 0xFFFFUL && codepoint <= 0x10FFFFUL) {
        encoded[0] = (unsigned char)(0xF0U | (codepoint >> 18));
        encoded[1] = (unsigned char)(0x80U | ((codepoint >> 12) & 0x3FUL));
        encoded[2] = (unsigned char)(0x80U | ((codepoint >> 6) & 0x3FUL));
        encoded[3] = (unsigned char)(0x80U | (codepoint & 0x3FUL));
        count = 4U;
    } else {
        return AMG_ERR_PARSE;
    }
    return amg_buffer_append(output, encoded, count);
}

static int append_named_html_entity(const char *entity, size_t length,
                                    AmgBuffer *output)
{
    struct HtmlEntity {
        const char *name;
        unsigned long codepoint;
    };
    static const struct HtmlEntity entities[] = {
        /* Core XML/HTML entities. */
        {"amp", 38UL}, {"lt", 60UL}, {"gt", 62UL}, {"quot", 34UL},
        {"apos", 39UL}, {"nbsp", 32UL},

        /* ISO-8859-1 named entities commonly emitted by HTML mail. */
        {"iexcl", 0xA1UL}, {"cent", 0xA2UL}, {"pound", 0xA3UL},
        {"curren", 0xA4UL}, {"yen", 0xA5UL}, {"brvbar", 0xA6UL},
        {"sect", 0xA7UL}, {"uml", 0xA8UL}, {"copy", 0xA9UL},
        {"ordf", 0xAAUL}, {"laquo", 0xABUL}, {"not", 0xACUL},
        {"shy", 0xADUL}, {"reg", 0xAEUL}, {"macr", 0xAFUL},
        {"deg", 0xB0UL}, {"plusmn", 0xB1UL}, {"sup2", 0xB2UL},
        {"sup3", 0xB3UL}, {"acute", 0xB4UL}, {"micro", 0xB5UL},
        {"para", 0xB6UL}, {"middot", 0xB7UL}, {"cedil", 0xB8UL},
        {"sup1", 0xB9UL}, {"ordm", 0xBAUL}, {"raquo", 0xBBUL},
        {"frac14", 0xBCUL}, {"frac12", 0xBDUL}, {"frac34", 0xBEUL},
        {"iquest", 0xBFUL},
        {"Agrave", 0xC0UL}, {"Aacute", 0xC1UL}, {"Acirc", 0xC2UL},
        {"Atilde", 0xC3UL}, {"Auml", 0xC4UL}, {"Aring", 0xC5UL},
        {"AElig", 0xC6UL}, {"Ccedil", 0xC7UL}, {"Egrave", 0xC8UL},
        {"Eacute", 0xC9UL}, {"Ecirc", 0xCAUL}, {"Euml", 0xCBUL},
        {"Igrave", 0xCCUL}, {"Iacute", 0xCDUL}, {"Icirc", 0xCEUL},
        {"Iuml", 0xCFUL}, {"ETH", 0xD0UL}, {"Ntilde", 0xD1UL},
        {"Ograve", 0xD2UL}, {"Oacute", 0xD3UL}, {"Ocirc", 0xD4UL},
        {"Otilde", 0xD5UL}, {"Ouml", 0xD6UL}, {"times", 0xD7UL},
        {"Oslash", 0xD8UL}, {"Ugrave", 0xD9UL}, {"Uacute", 0xDAUL},
        {"Ucirc", 0xDBUL}, {"Uuml", 0xDCUL}, {"Yacute", 0xDDUL},
        {"THORN", 0xDEUL}, {"szlig", 0xDFUL},
        {"agrave", 0xE0UL}, {"aacute", 0xE1UL}, {"acirc", 0xE2UL},
        {"atilde", 0xE3UL}, {"auml", 0xE4UL}, {"aring", 0xE5UL},
        {"aelig", 0xE6UL}, {"ccedil", 0xE7UL}, {"egrave", 0xE8UL},
        {"eacute", 0xE9UL}, {"ecirc", 0xEAUL}, {"euml", 0xEBUL},
        {"igrave", 0xECUL}, {"iacute", 0xEDUL}, {"icirc", 0xEEUL},
        {"iuml", 0xEFUL}, {"eth", 0xF0UL}, {"ntilde", 0xF1UL},
        {"ograve", 0xF2UL}, {"oacute", 0xF3UL}, {"ocirc", 0xF4UL},
        {"otilde", 0xF5UL}, {"ouml", 0xF6UL}, {"divide", 0xF7UL},
        {"oslash", 0xF8UL}, {"ugrave", 0xF9UL}, {"uacute", 0xFAUL},
        {"ucirc", 0xFBUL}, {"uuml", 0xFCUL}, {"yacute", 0xFDUL},
        {"thorn", 0xFEUL}, {"yuml", 0xFFUL},

        /* Common typographic entities used by newsletters and webmail. */
        {"trade", 0x2122UL}, {"euro", 0x20ACUL},
        {"ndash", 0x2013UL}, {"mdash", 0x2014UL}, {"hellip", 0x2026UL},
        {"bull", 0x2022UL}, {"lsaquo", 0x2039UL}, {"rsaquo", 0x203AUL},
        {"lsquo", 0x2018UL}, {"rsquo", 0x2019UL},
        {"sbquo", 0x201AUL}, {"ldquo", 0x201CUL}, {"rdquo", 0x201DUL},
        {"bdquo", 0x201EUL}, {"dagger", 0x2020UL}, {"Dagger", 0x2021UL},
        {"permil", 0x2030UL}
    };
    size_t i;
    unsigned long codepoint = 0UL;

    if (!entity || !output) return AMG_ERR_ARGUMENT;
    if (length > 1U && entity[0] == '#') {
        size_t pos = 1U;
        unsigned base = 10U;
        int saw_digit = 0;
        if (pos < length && (entity[pos] == 'x' || entity[pos] == 'X')) {
            base = 16U;
            ++pos;
        }
        while (pos < length) {
            int digit;
            unsigned char c = (unsigned char)entity[pos++];
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (base == 16U && c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (base == 16U && c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            else return AMG_ERR_PARSE;
            if ((unsigned)digit >= base || codepoint > 0x10FFFFUL / base)
                return AMG_ERR_PARSE;
            codepoint = codepoint * base + (unsigned long)digit;
            if (codepoint > 0x10FFFFUL) return AMG_ERR_PARSE;
            saw_digit = 1;
        }
        if (!saw_digit) return AMG_ERR_PARSE;
        return append_utf8_codepoint(output, codepoint);
    }

    for (i = 0U; i < sizeof(entities) / sizeof(entities[0]); ++i) {
        size_t name_length = strlen(entities[i].name);
        if (length == name_length &&
            !memcmp(entity, entities[i].name, name_length))
            return append_utf8_codepoint(output, entities[i].codepoint);
    }

    /* Preserve unknown entities rather than replacing mail text with '?'. */
    if (amg_buffer_append_char(output, '&') != AMG_OK ||
        amg_buffer_append(output, entity, length) != AMG_OK ||
        amg_buffer_append_char(output, ';') != AMG_OK)
        return AMG_ERR_MEMORY;
    return AMG_OK;
}

static void html_trim_trailing_space(AmgBuffer *output)
{
    if (!output) return;
    while (output->length &&
           (output->data[output->length - 1U] == ' ' ||
            output->data[output->length - 1U] == '\t'))
        --output->length;
    if (output->data) output->data[output->length] = 0;
}

static int html_ensure_newlines(AmgBuffer *output, unsigned wanted)
{
    unsigned have = 0U;
    size_t pos;
    if (!output) return AMG_ERR_ARGUMENT;
    html_trim_trailing_space(output);
    if (!output->length) return AMG_OK;
    pos = output->length;
    while (pos && output->data[pos - 1U] == '\n') {
        ++have;
        --pos;
    }
    while (have < wanted) {
        if (amg_buffer_append_char(output, '\n') != AMG_OK)
            return AMG_ERR_MEMORY;
        ++have;
    }
    return AMG_OK;
}

static int html_append_space(AmgBuffer *output)
{
    unsigned char last;
    if (!output) return AMG_ERR_ARGUMENT;
    if (!output->length) return AMG_OK;
    last = output->data[output->length - 1U];
    if (last == ' ' || last == '\t' || last == '\n') return AMG_OK;
    return amg_buffer_append_char(output, ' ');
}

static int html_name_equal(const char *name, size_t name_length,
                           const char *expected)
{
    size_t expected_length = strlen(expected);
    return name_length == expected_length &&
           ci_equal_n(name, expected, expected_length);
}

static size_t html_find_tag_end(const char *input, size_t length, size_t start)
{
    size_t i;
    char quote = 0;
    for (i = start; i < length; ++i) {
        char c = input[i];
        if (quote) {
            if (c == quote) quote = 0;
        } else if (c == '\'' || c == '"') {
            quote = c;
        } else if (c == '>') {
            return i;
        }
    }
    return length;
}

static void html_parse_tag(const char *tag, size_t length, int *closing,
                           const char **name, size_t *name_length)
{
    size_t pos = 0U, start;
    *closing = 0;
    *name = tag;
    *name_length = 0U;
    while (pos < length && isspace((unsigned char)tag[pos])) ++pos;
    if (pos < length && tag[pos] == '/') {
        *closing = 1;
        ++pos;
        while (pos < length && isspace((unsigned char)tag[pos])) ++pos;
    }
    start = pos;
    while (pos < length &&
           (isalnum((unsigned char)tag[pos]) || tag[pos] == ':' ||
            tag[pos] == '-' || tag[pos] == '_'))
        ++pos;
    *name = tag + start;
    *name_length = pos - start;
}


static int html_escaped_angle_at(const char *input, size_t length,
                                 size_t pos, int want_lt, size_t *after)
{
    static const char *const lt_forms[] = {"&lt;", "&LT;", "&#60;",
                                            "&#x3c;", "&#x3C;", "&#X3c;",
                                            "&#X3C;"};
    static const char *const gt_forms[] = {"&gt;", "&GT;", "&#62;",
                                            "&#x3e;", "&#x3E;", "&#X3e;",
                                            "&#X3E;"};
    const char *const *forms = want_lt ? lt_forms : gt_forms;
    size_t count = want_lt ? sizeof(lt_forms) / sizeof(lt_forms[0])
                           : sizeof(gt_forms) / sizeof(gt_forms[0]);
    size_t i;
    if (!input || pos >= length) return 0;
    for (i = 0U; i < count; ++i) {
        size_t form_length = strlen(forms[i]);
        if (pos + form_length <= length &&
            !memcmp(input + pos, forms[i], form_length)) {
            if (after) *after = pos + form_length;
            return 1;
        }
    }
    return 0;
}

static int html_find_escaped_tag(const char *input, size_t length,
                                 size_t start, size_t *after,
                                 const char **name, size_t *name_length,
                                 int *closing)
{
    size_t content_start, pos, end_after;
    if (!input || !after || !name || !name_length || !closing) return 0;
    if (!html_escaped_angle_at(input, length, start, 1, &content_start))
        return 0;
    pos = content_start;
    while (pos < length && pos - content_start <= 2048U) {
        if (html_escaped_angle_at(input, length, pos, 0, &end_after)) {
            html_parse_tag(input + content_start, pos - content_start,
                           closing, name, name_length);
            if (!*name_length) return 0;
            *after = end_after;
            return 1;
        }
        ++pos;
    }
    return 0;
}

static int html_decode_attr_value(const char *value, size_t length,
                                  char *buffer, size_t capacity)
{
    size_t i = 0U, used = 0U;
    if (!buffer || !capacity) return 0;
    while (i < length && used + 1U < capacity) {
        if (value[i] == '&') {
            size_t end = i + 1U;
            AmgBuffer entity;
            while (end < length && end - i <= 16U && value[end] != ';')
                ++end;
            if (end < length && value[end] == ';') {
                amg_buffer_init(&entity);
                if (append_named_html_entity(value + i + 1U,
                                             end - i - 1U, &entity) == AMG_OK &&
                    entity.length <= capacity - used - 1U) {
                    memcpy(buffer + used, entity.data, entity.length);
                    used += entity.length;
                    amg_buffer_free(&entity);
                    i = end + 1U;
                    continue;
                }
                amg_buffer_free(&entity);
            }
        }
        buffer[used++] = value[i++];
    }
    buffer[used] = 0;
    return used != 0U;
}

static int html_get_attribute(const char *tag, size_t length,
                              const char *wanted, char *buffer,
                              size_t capacity)
{
    size_t pos = 0U;
    while (pos < length && isspace((unsigned char)tag[pos])) ++pos;
    if (pos < length && tag[pos] == '/') ++pos;
    while (pos < length &&
           (isalnum((unsigned char)tag[pos]) || tag[pos] == ':' ||
            tag[pos] == '-' || tag[pos] == '_'))
        ++pos;

    while (pos < length) {
        size_t name_start, name_length, value_start, value_length;
        char quote = 0;
        while (pos < length &&
               (isspace((unsigned char)tag[pos]) || tag[pos] == '/'))
            ++pos;
        if (pos >= length) break;
        name_start = pos;
        while (pos < length &&
               (isalnum((unsigned char)tag[pos]) || tag[pos] == ':' ||
                tag[pos] == '-' || tag[pos] == '_'))
            ++pos;
        name_length = pos - name_start;
        if (!name_length) {
            ++pos;
            continue;
        }
        while (pos < length && isspace((unsigned char)tag[pos])) ++pos;
        if (pos >= length || tag[pos] != '=') continue;
        ++pos;
        while (pos < length && isspace((unsigned char)tag[pos])) ++pos;
        if (pos >= length) break;
        if (tag[pos] == '\'' || tag[pos] == '"') quote = tag[pos++];
        value_start = pos;
        if (quote) {
            while (pos < length && tag[pos] != quote) ++pos;
        } else {
            while (pos < length && !isspace((unsigned char)tag[pos]) &&
                   tag[pos] != '>')
                ++pos;
        }
        value_length = pos - value_start;
        if (quote && pos < length) ++pos;
        if (strlen(wanted) == name_length &&
            ci_equal_n(tag + name_start, wanted, name_length))
            return html_decode_attr_value(tag + value_start, value_length,
                                          buffer, capacity);
    }
    if (buffer && capacity) buffer[0] = 0;
    return 0;
}

static int html_href_is_safe_to_show(const char *href)
{
    if (!href || !*href || href[0] == '#') return 0;
    if (strlen(href) >= 11U && ci_equal_n(href, "javascript:", 11U)) return 0;
    if (strlen(href) >= 5U && ci_equal_n(href, "data:", 5U)) return 0;
    if (strlen(href) >= 4U && ci_equal_n(href, "cid:", 4U)) return 0;
    return 1;
}

static int html_finish_anchor(AmgBuffer *output, char *href,
                              size_t text_start, int *active)
{
    size_t href_length;
    int same_as_text;
    if (!output || !href || !active || !*active) return AMG_OK;
    *active = 0;
    if (!html_href_is_safe_to_show(href)) {
        href[0] = 0;
        return AMG_OK;
    }
    href_length = strlen(href);
    same_as_text = output->length >= text_start &&
                   output->length - text_start == href_length &&
                   !memcmp(output->data + text_start, href, href_length);
    if (!same_as_text) {
        if (output->length > text_start && html_append_space(output) != AMG_OK)
            return AMG_ERR_MEMORY;
        if (amg_buffer_append_char(output, '<') != AMG_OK ||
            amg_buffer_append_cstr(output, href) != AMG_OK ||
            amg_buffer_append_char(output, '>') != AMG_OK)
            return AMG_ERR_MEMORY;
    }
    href[0] = 0;
    return AMG_OK;
}

int amg_html_to_text(const char *input, size_t length, AmgBuffer *output)
{
    size_t i = 0U;
    int script_depth = 0, style_depth = 0, head_depth = 0;
    int pre_depth = 0, anchor_active = 0;
    size_t anchor_text_start = 0U;
    char href[1024];
    int result = AMG_OK;

    if ((!input && length) || !output) return AMG_ERR_ARGUMENT;
    href[0] = 0;

    while (i < length) {
        if (input[i] == '&') {
            size_t escaped_after = 0U, escaped_name_length = 0U;
            const char *escaped_name = NULL;
            int escaped_closing = 0;
            if (html_find_escaped_tag(input, length, i, &escaped_after,
                                      &escaped_name, &escaped_name_length,
                                      &escaped_closing)) {
                if (html_name_equal(escaped_name, escaped_name_length, "br")) {
                    result = html_ensure_newlines(output, 1U);
                } else if (html_name_equal(escaped_name, escaped_name_length,
                                           "li")) {
                    result = html_ensure_newlines(output, 1U);
                    if (result == AMG_OK && !escaped_closing)
                        result = amg_buffer_append_cstr(output, "- ");
                } else if (html_name_equal(escaped_name, escaped_name_length,
                                           "p") ||
                           html_name_equal(escaped_name, escaped_name_length,
                                           "h1") ||
                           html_name_equal(escaped_name, escaped_name_length,
                                           "h2") ||
                           html_name_equal(escaped_name, escaped_name_length,
                                           "h3") ||
                           html_name_equal(escaped_name, escaped_name_length,
                                           "h4") ||
                           html_name_equal(escaped_name, escaped_name_length,
                                           "h5") ||
                           html_name_equal(escaped_name, escaped_name_length,
                                           "h6") ||
                           html_name_equal(escaped_name, escaped_name_length,
                                           "blockquote")) {
                    result = html_ensure_newlines(output, 2U);
                } else if (html_name_equal(escaped_name, escaped_name_length,
                                           "div") ||
                           html_name_equal(escaped_name, escaped_name_length,
                                           "section") ||
                           html_name_equal(escaped_name, escaped_name_length,
                                           "article") ||
                           html_name_equal(escaped_name, escaped_name_length,
                                           "header") ||
                           html_name_equal(escaped_name, escaped_name_length,
                                           "footer") ||
                           html_name_equal(escaped_name, escaped_name_length,
                                           "tr") ||
                           html_name_equal(escaped_name, escaped_name_length,
                                           "table") ||
                           html_name_equal(escaped_name, escaped_name_length,
                                           "ul") ||
                           html_name_equal(escaped_name, escaped_name_length,
                                           "ol")) {
                    result = html_ensure_newlines(output, 1U);
                }
                if (result != AMG_OK) return result;
                i = escaped_after;
                continue;
            }
        }
        if (input[i] == '<') {
            size_t tag_end;
            const char *tag, *name;
            size_t tag_length, name_length;
            int closing;

            if (i + 4U <= length && !memcmp(input + i, "<!--", 4U)) {
                size_t comment_end = i + 4U;
                while (comment_end + 3U <= length &&
                       memcmp(input + comment_end, "-->", 3U))
                    ++comment_end;
                if (comment_end + 3U > length) break;
                i = comment_end + 3U;
                continue;
            }

            tag_end = html_find_tag_end(input, length, i + 1U);
            if (tag_end == length) {
                if (!script_depth && !style_depth && !head_depth &&
                    amg_buffer_append_char(output, '<') != AMG_OK)
                    return AMG_ERR_MEMORY;
                ++i;
                continue;
            }
            tag = input + i + 1U;
            tag_length = tag_end - i - 1U;
            html_parse_tag(tag, tag_length, &closing, &name, &name_length);

            if (html_name_equal(name, name_length, "script")) {
                if (closing) { if (script_depth > 0) --script_depth; }
                else ++script_depth;
                i = tag_end + 1U;
                continue;
            }
            if (html_name_equal(name, name_length, "style")) {
                if (closing) { if (style_depth > 0) --style_depth; }
                else ++style_depth;
                i = tag_end + 1U;
                continue;
            }
            if (html_name_equal(name, name_length, "head")) {
                if (closing) { if (head_depth > 0) --head_depth; }
                else ++head_depth;
                i = tag_end + 1U;
                continue;
            }
            if (script_depth || style_depth || head_depth) {
                i = tag_end + 1U;
                continue;
            }

            if (html_name_equal(name, name_length, "a")) {
                if (closing) {
                    result = html_finish_anchor(output, href,
                                                anchor_text_start,
                                                &anchor_active);
                    if (result != AMG_OK) return result;
                } else {
                    if (anchor_active) {
                        result = html_finish_anchor(output, href,
                                                    anchor_text_start,
                                                    &anchor_active);
                        if (result != AMG_OK) return result;
                    }
                    href[0] = 0;
                    html_get_attribute(tag, tag_length, "href", href,
                                       sizeof(href));
                    anchor_text_start = output->length;
                    anchor_active = 1;
                }
            } else if (html_name_equal(name, name_length, "br")) {
                result = html_ensure_newlines(output, 1U);
            } else if (html_name_equal(name, name_length, "li")) {
                result = html_ensure_newlines(output, 1U);
                if (result == AMG_OK && !closing)
                    result = amg_buffer_append_cstr(output, "- ");
            } else if (html_name_equal(name, name_length, "p") ||
                       html_name_equal(name, name_length, "h1") ||
                       html_name_equal(name, name_length, "h2") ||
                       html_name_equal(name, name_length, "h3") ||
                       html_name_equal(name, name_length, "h4") ||
                       html_name_equal(name, name_length, "h5") ||
                       html_name_equal(name, name_length, "h6") ||
                       html_name_equal(name, name_length, "blockquote")) {
                result = html_ensure_newlines(output, 2U);
            } else if (html_name_equal(name, name_length, "div") ||
                       html_name_equal(name, name_length, "section") ||
                       html_name_equal(name, name_length, "article") ||
                       html_name_equal(name, name_length, "header") ||
                       html_name_equal(name, name_length, "footer") ||
                       html_name_equal(name, name_length, "tr") ||
                       html_name_equal(name, name_length, "table") ||
                       html_name_equal(name, name_length, "ul") ||
                       html_name_equal(name, name_length, "ol")) {
                result = html_ensure_newlines(output, 1U);
            } else if (html_name_equal(name, name_length, "td") ||
                       html_name_equal(name, name_length, "th")) {
                if (closing) result = html_append_space(output);
            } else if (html_name_equal(name, name_length, "hr")) {
                result = html_ensure_newlines(output, 1U);
                if (result == AMG_OK)
                    result = amg_buffer_append_cstr(output, "---");
                if (result == AMG_OK)
                    result = html_ensure_newlines(output, 1U);
            } else if (html_name_equal(name, name_length, "pre")) {
                if (closing) {
                    if (pre_depth > 0) --pre_depth;
                    result = html_ensure_newlines(output, 1U);
                } else {
                    result = html_ensure_newlines(output, 1U);
                    ++pre_depth;
                }
            }
            if (result != AMG_OK) return result;
            i = tag_end + 1U;
            continue;
        }

        if (script_depth || style_depth || head_depth) {
            ++i;
            continue;
        }

        if (input[i] == '&') {
            size_t end = i + 1U;
            while (end < length && end - i <= 16U && input[end] != ';' &&
                   input[end] != '<' && !isspace((unsigned char)input[end]))
                ++end;
            if (end < length && input[end] == ';') {
                AmgBuffer decoded;
                amg_buffer_init(&decoded);
                result = append_named_html_entity(input + i + 1U,
                                                  end - i - 1U, &decoded);
                if (result == AMG_OK) {
                    size_t pos;
                    for (pos = 0U; pos < decoded.length; ++pos) {
                        unsigned char c = decoded.data[pos];
                        if (!pre_depth && isspace(c))
                            result = html_append_space(output);
                        else
                            result = amg_buffer_append_char(output, c);
                        if (result != AMG_OK) break;
                    }
                }
                amg_buffer_free(&decoded);
                if (result != AMG_OK) return result;
                i = end + 1U;
                continue;
            }
        }

        if (!pre_depth && isspace((unsigned char)input[i])) {
            result = html_append_space(output);
        } else if (pre_depth && input[i] == '\r') {
            if (i + 1U >= length || input[i + 1U] != '\n')
                result = amg_buffer_append_char(output, '\n');
        } else {
            result = amg_buffer_append_char(output, (unsigned char)input[i]);
        }
        if (result != AMG_OK) return result;
        ++i;
    }

    if (anchor_active) {
        result = html_finish_anchor(output, href, anchor_text_start,
                                    &anchor_active);
        if (result != AMG_OK) return result;
    }
    html_trim_trailing_space(output);
    while (output->length && output->data[output->length - 1U] == '\n')
        --output->length;
    if (output->data) output->data[output->length] = 0;
    return AMG_OK;
}


static int html_text_looks_mislabeled(const char *text, size_t length)
{
    static const char *const tags[] = {
        "html", "body", "div", "span", "p", "br", "table", "tr", "td",
        "th", "ul", "ol", "li", "a", "font", "blockquote", "h1", "h2",
        "h3", "h4", "h5", "h6", "style", "script"
    };
    size_t i;
    unsigned tag_hits = 0U, entity_hits = 0U;

    if (!text || !length) return 0;
    for (i = 0U; i < length; ++i) {
        if (text[i] == '<') {
            size_t pos = i + 1U, start, j;
            while (pos < length && isspace((unsigned char)text[pos])) ++pos;
            if (pos < length && text[pos] == '/') ++pos;
            while (pos < length && isspace((unsigned char)text[pos])) ++pos;
            start = pos;
            while (pos < length &&
                   (isalnum((unsigned char)text[pos]) || text[pos] == ':' ||
                    text[pos] == '-' || text[pos] == '_'))
                ++pos;
            if (pos > start) {
                for (j = 0U; j < sizeof(tags) / sizeof(tags[0]); ++j) {
                    size_t tag_length = strlen(tags[j]);
                    if (pos - start == tag_length &&
                        ci_equal_n(text + start, tags[j], tag_length)) {
                        ++tag_hits;
                        break;
                    }
                }
            }
        } else if (text[i] == '&') {
            size_t end = i + 1U;
            while (end < length && end - i <= 16U && text[end] != ';' &&
                   text[end] != '<' && !isspace((unsigned char)text[end]))
                ++end;
            if (end < length && text[end] == ';') {
                AmgBuffer entity;
                amg_buffer_init(&entity);
                if (append_named_html_entity(text + i + 1U,
                                             end - i - 1U, &entity) == AMG_OK) {
                    if (!(entity.length == end - i + 1U && entity.data &&
                          entity.data[0] == '&'))
                        ++entity_hits;
                }
                amg_buffer_free(&entity);
                i = end;
            }
        }
        if (tag_hits >= 1U || entity_hits >= 2U) return 1;
    }
    return 0;
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

static int entity_content_type_is(const char *message, size_t length,
                                  const char *wanted)
{
    AmgMailHeaders headers;
    size_t body_offset = 0U;
    const char *content_type;
    int result;
    int matches = 0;
    if (!message || !wanted) return 0;
    amg_mail_headers_init(&headers);
    result = amg_mail_headers_parse(message, length, &headers, &body_offset);
    if (result == AMG_OK) {
        content_type = amg_mail_header_get(&headers, "Content-Type");
        if (!content_type) content_type = "text/plain";
        matches = ci_starts_with(content_type, wanted);
    }
    amg_mail_headers_free(&headers);
    return matches;
}


static int ci_contains(const char *text, size_t text_length,
                       const char *needle)
{
    size_t needle_length, i;
    if (!text || !needle) return 0;
    needle_length = strlen(needle);
    if (!needle_length || needle_length > text_length) return 0;
    for (i = 0U; i + needle_length <= text_length; ++i)
        if (ci_equal_n(text + i, needle, needle_length)) return 1;
    return 0;
}

static int plain_text_looks_generated_css(const char *text, size_t length)
{
    static const char *const strong_markers[] = {
        "#outlook a", "@media ", "-webkit-text-size-adjust",
        "-ms-text-size-adjust", "mso-table-", "mso-line-height",
        ".moz-text-html", ".mj-column-"
    };
    static const char *const rule_markers[] = {
        "body {", "table, td {", "img {", "p {", "td {",
        "table.mj-", "td.mj-"
    };
    size_t i;
    unsigned rule_hits = 0U;

    if (!text || !length) return 0;
    for (i = 0U; i < sizeof(strong_markers) / sizeof(strong_markers[0]); ++i)
        if (ci_contains(text, length, strong_markers[i])) return 1;

    for (i = 0U; i < sizeof(rule_markers) / sizeof(rule_markers[0]); ++i)
        if (ci_contains(text, length, rule_markers[i]) && ++rule_hits >= 3U)
            return 1;
    return 0;
}

static int plain_entity_looks_generated_css(const char *message,
                                            size_t length)
{
    AmgMailHeaders headers;
    AmgBuffer decoded;
    size_t body_offset = 0U;
    const char *content_type, *encoding, *body;
    size_t body_length;
    int result, looks_css = 0;

    if (!message || !length) return 0;
    amg_mail_headers_init(&headers);
    result = amg_mail_headers_parse(message, length, &headers, &body_offset);
    if (result != AMG_OK) {
        amg_mail_headers_free(&headers);
        return 0;
    }
    content_type = amg_mail_header_get(&headers, "Content-Type");
    if (!content_type) content_type = "text/plain";
    if (!ci_starts_with(content_type, "text/plain")) {
        amg_mail_headers_free(&headers);
        return 0;
    }

    encoding = amg_mail_header_get(&headers, "Content-Transfer-Encoding");
    body = message + body_offset;
    body_length = length - body_offset;
    amg_buffer_init(&decoded);
    if (encoding && ci_equal(encoding, "base64"))
        result = amg_base64_decode(body, body_length, &decoded);
    else if (encoding && ci_equal(encoding, "quoted-printable"))
        result = amg_quoted_printable_decode(body, body_length, &decoded);
    else
        result = amg_buffer_append(&decoded, body, body_length);
    if (result == AMG_OK)
        looks_css = plain_text_looks_generated_css((const char *)decoded.data,
                                                   decoded.length);
    amg_buffer_free(&decoded);
    amg_mail_headers_free(&headers);
    return looks_css;
}

static int entity_contains_content_type(const char *message, size_t length,
                                        unsigned depth, const char *wanted)
{
    AmgMailHeaders headers;
    size_t body_offset = 0U;
    const char *content_type;
    char boundary[256];
    int result, found = 0;

    if (!message || !wanted || depth > 8U || length > AMIGMAIL_MAX_MESSAGE)
        return 0;
    amg_mail_headers_init(&headers);
    result = amg_mail_headers_parse(message, length, &headers, &body_offset);
    if (result != AMG_OK) {
        amg_mail_headers_free(&headers);
        return 0;
    }
    content_type = amg_mail_header_get(&headers, "Content-Type");
    if (!content_type) content_type = "text/plain";
    if (ci_starts_with(content_type, wanted)) {
        amg_mail_headers_free(&headers);
        return 1;
    }

    if (ci_starts_with(content_type, "multipart/") &&
        param_value(content_type, "boundary", boundary, sizeof(boundary))) {
        AmgBuffer marker;
        const char *body = message + body_offset, *end = message + length;
        const char *part;
        amg_buffer_init(&marker);
        result = amg_buffer_append_cstr(&marker, "--");
        if (result == AMG_OK)
            result = amg_buffer_append_cstr(&marker, boundary);
        if (result == AMG_OK) result = amg_buffer_terminate(&marker);
        if (result == AMG_OK) {
            part = strstr(body, (const char *)marker.data);
            while (part && part < end && !found) {
                const char *start = part + marker.length, *next, *part_end;
                if (start + 2 <= end && start[0] == '-' && start[1] == '-')
                    break;
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
                found = entity_contains_content_type(
                    start, (size_t)(part_end - start), depth + 1U, wanted);
                part = next;
            }
        }
        amg_buffer_free(&marker);
    }
    amg_mail_headers_free(&headers);
    return found;
}

static int extract_entity(const char *message, size_t length, unsigned depth,
                          AmgBuffer *output)
{
    AmgMailHeaders headers;
    size_t body_offset = 0U;
    const char *content_type, *encoding;
    char boundary[256];
    int result;
    if (depth > 8U || length > AMIGMAIL_MAX_MESSAGE) return AMG_ERR_LIMIT;
    amg_mail_headers_init(&headers);
    result = amg_mail_headers_parse(message, length, &headers, &body_offset);
    if (result != AMG_OK) {
        amg_mail_headers_free(&headers);
        return result;
    }
    content_type = amg_mail_header_get(&headers, "Content-Type");
    encoding = amg_mail_header_get(&headers, "Content-Transfer-Encoding");
    if (!content_type) content_type = "text/plain";

    if (ci_starts_with(content_type, "multipart/") &&
        param_value(content_type, "boundary", boundary, sizeof(boundary))) {
        AmgBuffer marker;
        const char *body = message + body_offset, *end = message + length;
        const char *part;
        int found_text_part = 0;
        int prefer_plain = ci_starts_with(content_type,
                                          "multipart/alternative");
        int has_html_alternative = prefer_plain &&
            entity_contains_content_type(message, length, depth, "text/html");
        unsigned pass, passes = prefer_plain ?
            (has_html_alternative ? 3U : 2U) : 1U;

        amg_buffer_init(&marker);
        result = amg_buffer_append_cstr(&marker, "--");
        if (result == AMG_OK) result = amg_buffer_append_cstr(&marker, boundary);
        if (result == AMG_OK) result = amg_buffer_terminate(&marker);
        if (result != AMG_OK) {
            amg_buffer_free(&marker);
            amg_mail_headers_free(&headers);
            return result;
        }

        for (pass = 0U; pass < passes; ++pass) {
            part = strstr(body, (const char *)marker.data);
            while (part && part < end) {
                const char *start = part + marker.length, *next, *part_end;
                size_t part_length, previous_length;
                int is_plain;
                if (start + 2 <= end && start[0] == '-' && start[1] == '-')
                    break;
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
                part_length = (size_t)(part_end - start);
                is_plain = entity_content_type_is(start, part_length,
                                                  "text/plain");
                if (prefer_plain) {
                    int broken_plain = is_plain && has_html_alternative &&
                        plain_entity_looks_generated_css(start, part_length);
                    if ((pass == 0U && (!is_plain || broken_plain)) ||
                        (pass == 1U && is_plain) ||
                        (pass == 2U && (!is_plain || !broken_plain))) {
                        part = next;
                        continue;
                    }
                }

                previous_length = output->length;
                result = extract_entity(start, part_length, depth + 1U, output);
                if (result == AMG_OK) {
                    found_text_part = 1;
                    if (output->length > previous_length) {
                        amg_buffer_free(&marker);
                        amg_mail_headers_free(&headers);
                        return AMG_OK;
                    }
                }
                part = next;
            }
        }
        amg_buffer_free(&marker);
        amg_mail_headers_free(&headers);
        return found_text_part ? AMG_OK : AMG_ERR_PARSE;
    }

    {
        AmgBuffer decoded;
        const char *body = message + body_offset;
        size_t body_length = length - body_offset;
        amg_buffer_init(&decoded);
        if (encoding && ci_equal(encoding, "base64"))
            result = amg_base64_decode(body, body_length, &decoded);
        else if (encoding && ci_equal(encoding, "quoted-printable"))
            result = amg_quoted_printable_decode(body, body_length, &decoded);
        else
            result = amg_buffer_append(&decoded, body, body_length);
        if (result == AMG_OK) {
            if (ci_starts_with(content_type, "text/plain")) {
                if (html_text_looks_mislabeled((const char *)decoded.data,
                                               decoded.length))
                    result = amg_html_to_text((const char *)decoded.data,
                                              decoded.length, output);
                else
                    result = amg_buffer_append(output, decoded.data,
                                               decoded.length);
            } else if (ci_starts_with(content_type, "text/html"))
                result = amg_html_to_text((const char *)decoded.data,
                                          decoded.length, output);
            else
                result = AMG_ERR_UNSUPPORTED;
        }
        amg_buffer_free(&decoded);
        amg_mail_headers_free(&headers);
        return result;
    }
}

int amg_mime_extract_text(const char *message, size_t length, AmgBuffer *output, AmgError *error)
{
    int result;
    if (!message || !output) return AMG_ERR_ARGUMENT;
    result = extract_entity(message, length, 0U, output);
    if (result != AMG_OK) amg_error_set(error, result, T(MSG_NO_DISPLAYABLE_TEXT_PART_WAS_FOUND_IN_THE, "No displayable text part was found in the message."));
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
                      T(MSG_ATTACHMENTS_COULD_NOT_BE_PARSED, "Attachments could not be parsed."));
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
                      T(MSG_ATTACHMENTS_COULD_NOT_BE_PARSED, "Attachments could not be parsed."));
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
        amg_error_set(error, result, T(MSG_ATTACHMENT_WAS_NOT_FOUND, "Attachment was not found."));
    } else if (result != AMG_OK) {
        amg_error_set(error, result,
                      T(MSG_ATTACHMENT_COULD_NOT_BE_DECODED, "Attachment could not be decoded."));
    } else {
        amg_error_set(error, AMG_OK, "");
    }
    return result;
}
