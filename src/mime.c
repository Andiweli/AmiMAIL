#include "mime.h"
#include "i18n.h"

#define T(id, en) amg_tr((id), (en))
#include "codec.h"
#include "charset.h"
#include "smtp.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HTML_VISIBLE_URL_MAX 512U

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
    char *result;
    if (length == SIZE_MAX) return NULL;
    result = (char *)malloc(length + 1U);
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
    if (old_length > AMIGMAIL_MAX_LINE ||
        length > AMIGMAIL_MAX_LINE - old_length) return AMG_ERR_LIMIT;
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
        while (position < length && input[position] != '\n') {
            if (position >= AMIGMAIL_MAX_LINE) return AMG_ERR_LIMIT;
            ++position;
        }
        end = position;
        if (position < length) ++position;
        if (end > start && input[end - 1U] == '\r') --end;
        if (end == start) { if (body_offset) *body_offset = position; return AMG_OK; }
        if (memchr(input + start, 0, end - start)) return AMG_ERR_PARSE;
        if (input[start] == ' ' || input[start] == '\t') {
            if (!headers->count) return AMG_ERR_PARSE;
            {
                int folded = append_fold(&headers->items[headers->count - 1U],
                                          input + start, end - start);
                if (folded != AMG_OK) return folded;
            }
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

        {
            char charset_name[64];
            int converted;
            if (charset_length >= sizeof(charset_name)) {
                amg_buffer_free(&decoded);
                return AMG_ERR_UNSUPPORTED;
            }
            memcpy(charset_name, charset, charset_length);
            charset_name[charset_length] = 0;
            converted = amg_charset_to_utf8(charset_name, decoded.data,
                                             decoded.length, output);
            if (converted == AMG_ERR_UNSUPPORTED)
                converted = amg_buffer_append(output, p, (size_t)(end + 2 - p));
            if (converted != AMG_OK) {
                amg_buffer_free(&decoded);
                return converted;
            }
        }
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

static int html_attr_contains_ci(const char *text, const char *needle)
{
    size_t text_length, needle_length, i;
    if (!text || !needle) return 0;
    text_length = strlen(text);
    needle_length = strlen(needle);
    if (!needle_length || needle_length > text_length) return 0;
    for (i = 0U; i + needle_length <= text_length; ++i)
        if (ci_equal_n(text + i, needle, needle_length)) return 1;
    return 0;
}

static int html_dimension_is_tracking_size(const char *value)
{
    char *end = NULL;
    unsigned long dimension;
    if (!value || !*value) return 0;
    dimension = strtoul(value, &end, 10);
    if (end == value) return 0;
    while (*end && isspace((unsigned char)*end)) ++end;
    if (*end &&
        !((end[0] == 'p' || end[0] == 'P') &&
          (end[1] == 'x' || end[1] == 'X') && end[2] == 0))
        return 0;
    return dimension <= 1UL;
}

static int html_image_is_hidden_or_tracking(const char *tag, size_t tag_length)
{
    char width[32], height[32], style[512], src[1024];
    int tiny_width, tiny_height;

    width[0] = height[0] = style[0] = src[0] = 0;
    html_get_attribute(tag, tag_length, "width", width, sizeof(width));
    html_get_attribute(tag, tag_length, "height", height, sizeof(height));
    html_get_attribute(tag, tag_length, "style", style, sizeof(style));
    html_get_attribute(tag, tag_length, "src", src, sizeof(src));

    tiny_width = html_dimension_is_tracking_size(width);
    tiny_height = html_dimension_is_tracking_size(height);
    if (tiny_width && tiny_height) return 1;

    if (html_attr_contains_ci(style, "display:none") ||
        html_attr_contains_ci(style, "display: none") ||
        html_attr_contains_ci(style, "visibility:hidden") ||
        html_attr_contains_ci(style, "visibility: hidden") ||
        html_attr_contains_ci(style, "opacity:0") ||
        html_attr_contains_ci(style, "opacity: 0"))
        return 1;

    if ((tiny_width || tiny_height) &&
        (html_attr_contains_ci(src, "pixel.gif") ||
         html_attr_contains_ci(src, "tracking") ||
         html_attr_contains_ci(src, "tracker")))
        return 1;
    return 0;
}

static int html_append_image_text(AmgBuffer *output, const char *tag,
                                  size_t tag_length)
{
    char alt[1024];
    int has_alt;
    int result;

    if (!output || !tag) return AMG_ERR_ARGUMENT;
    if (html_image_is_hidden_or_tracking(tag, tag_length)) return AMG_OK;

    alt[0] = 0;
    has_alt = html_get_attribute(tag, tag_length, "alt", alt, sizeof(alt));
    result = html_append_space(output);
    if (result != AMG_OK) return result;
    if (has_alt && alt[0])
        return amg_buffer_append_cstr(output, alt);
    return amg_buffer_append_cstr(
        output, T(MSG_GRAPHIC_PLACEHOLDER_UTF8, "[Graphic]"));
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
    int has_visible_text;
    int graphic_marker_only = 0;
    if (!output || !href || !active || !*active) return AMG_OK;
    *active = 0;
    if (!html_href_is_safe_to_show(href)) {
        href[0] = 0;
        return AMG_OK;
    }

    href_length = strlen(href);
    has_visible_text = output->length > text_start;
    same_as_text = has_visible_text &&
                   output->length - text_start == href_length &&
                   !memcmp(output->data + text_start, href, href_length);
    if (has_visible_text) {
        size_t start = text_start, end = output->length;
        const char *marker = T(MSG_GRAPHIC_PLACEHOLDER_UTF8, "[Graphic]");
        size_t marker_length = strlen(marker);
        while (start < end && isspace((unsigned char)output->data[start]))
            ++start;
        while (end > start && isspace((unsigned char)output->data[end - 1U]))
            --end;
        graphic_marker_only = end - start == marker_length &&
                              !memcmp(output->data + start, marker,
                                      marker_length);
    }

    /* Image-only/social anchors have no useful textual label and used to
     * produce bare tracking URLs in the preview. Modern payment/newsletter
     * mails also carry tracking links well over 1 KB. Such unbroken tokens
     * can exceed classic texteditor.gadget's practical import/wrap limits.
     * Keep the human-readable anchor text, but append only bounded URLs. */
    if (has_visible_text && !graphic_marker_only && !same_as_text &&
        href_length <= HTML_VISIBLE_URL_MAX) {
        if (html_append_space(output) != AMG_OK)
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
                                           "img")) {
                    result = html_append_space(output);
                    if (result == AMG_OK && !escaped_closing)
                        result = amg_buffer_append_cstr(
                            output,
                            T(MSG_GRAPHIC_PLACEHOLDER_UTF8, "[Graphic]"));
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
            } else if (html_name_equal(name, name_length, "img")) {
                if (!closing)
                    result = html_append_image_text(output, tag, tag_length);
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


static int html_name_is_common_markup(const char *name, size_t name_length)
{
    static const char *const tags[] = {
        "html", "head", "body", "div", "span", "p", "br", "img",
        "table", "tr", "td", "th", "ul", "ol", "li", "a", "font",
        "blockquote", "h1", "h2", "h3", "h4", "h5", "h6",
        "style", "script", "section", "article", "header", "footer",
        "pre", "hr"
    };
    size_t i;

    if (!name || !name_length) return 0;
    for (i = 0U; i < sizeof(tags) / sizeof(tags[0]); ++i) {
        size_t tag_length = strlen(tags[i]);
        if (name_length == tag_length &&
            ci_equal_n(name, tags[i], tag_length))
            return 1;
    }
    return 0;
}

static int html_text_looks_mislabeled(const char *text, size_t length)
{
    size_t i;

    if (!text || !length) return 0;
    for (i = 0U; i < length; ++i) {
        if (text[i] == '<') {
            size_t pos = i + 1U, start;
            while (pos < length && isspace((unsigned char)text[pos])) ++pos;
            if (pos < length && text[pos] == '/') ++pos;
            while (pos < length && isspace((unsigned char)text[pos])) ++pos;
            start = pos;
            while (pos < length &&
                   (isalnum((unsigned char)text[pos]) || text[pos] == ':' ||
                    text[pos] == '-' || text[pos] == '_'))
                ++pos;
            if (html_name_is_common_markup(text + start, pos - start))
                return 1;
        } else if (text[i] == '&') {
            size_t after = 0U, name_length = 0U;
            const char *name = NULL;
            int closing = 0;
            if (html_find_escaped_tag(text, length, i, &after, &name,
                                      &name_length, &closing) &&
                html_name_is_common_markup(name, name_length))
                return 1;
        }
    }
    return 0;
}

static int plain_text_decode_html_entities(const char *input, size_t length,
                                           AmgBuffer *output)
{
    size_t i = 0U;
    int result;

    if ((!input && length) || !output) return AMG_ERR_ARGUMENT;
    while (i < length) {
        if (input[i] == '&') {
            size_t end = i + 1U;
            while (end < length && end - i <= 16U && input[end] != ';' &&
                   input[end] != '<' && !isspace((unsigned char)input[end]))
                ++end;
            if (end < length && input[end] == ';') {
                result = append_named_html_entity(input + i + 1U,
                                                  end - i - 1U, output);
                if (result != AMG_OK) return result;
                i = end + 1U;
                continue;
            }
        }
        if (amg_buffer_append_char(output, (unsigned char)input[i]) != AMG_OK)
            return AMG_ERR_MEMORY;
        ++i;
    }
    return AMG_OK;
}

static const char *param_value(const char *header, const char *name,
                               char *buffer, size_t size)
{
    const char *p = header;
    size_t wanted;
    if (!header || !name || !buffer || !size) return NULL;
    wanted = strlen(name);
    buffer[0] = 0;
    p = strchr(p, ';');
    while (p && *p) {
        const char *key, *key_end;
        size_t used = 0U;
        int match, overflow = 0;
        ++p;
        while (*p == ' ' || *p == '\t') ++p;
        key = p;
        while (*p && *p != '=' && *p != ';' && *p != ' ' && *p != '\t') ++p;
        key_end = p;
        while (*p == ' ' || *p == '\t') ++p;
        match = (size_t)(key_end - key) == wanted && ci_equal_n(key, name, wanted);
        if (*p != '=') { p = strchr(p, ';'); continue; }
        ++p;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '"') {
            ++p;
            while (*p && *p != '"') {
                unsigned char c = (unsigned char)*p++;
                if (c == '\\' && *p) c = (unsigned char)*p++;
                if (match) {
                    if (used + 1U < size) buffer[used++] = (char)c;
                    else overflow = 1;
                }
            }
            if (*p != '"') return NULL;
            ++p;
        } else {
            while (*p && *p != ';' && *p != ' ' && *p != '\t') {
                if (match) {
                    if (used + 1U < size) buffer[used++] = *p;
                    else overflow = 1;
                }
                ++p;
            }
        }
        if (match) {
            buffer[used] = 0;
            return overflow ? NULL : buffer;
        }
        p = strchr(p, ';');
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
    const char *first, *second;
    AmgBuffer decoded;
    int result;
    if (!value || !output) return AMG_ERR_ARGUMENT;
    first = strchr(value, '\'');
    second = first ? strchr(first + 1, '\'') : NULL;
    if (first && second && first != value) {
        char charset[64];
        const char *cursor = second + 1;
        size_t size = (size_t)(first - value);
        if (size >= sizeof(charset)) return AMG_ERR_UNSUPPORTED;
        memcpy(charset, value, size); charset[size] = 0;
        if (amg_charset_to_utf8(charset, NULL, 0U, output) == AMG_ERR_UNSUPPORTED)
            return amg_rfc2047_decode(value, output);
        amg_buffer_init(&decoded);
        result = AMG_OK;
        while (*cursor && result == AMG_OK) {
            unsigned char c = (unsigned char)*cursor++;
            if (c == '%') {
                int high, low;
                if (!cursor[0] || !cursor[1] ||
                    (high = hex_value(cursor[0])) < 0 ||
                    (low = hex_value(cursor[1])) < 0) {
                    result = AMG_ERR_PARSE; break;
                }
                c = (unsigned char)((high << 4) | low); cursor += 2;
            }
            result = amg_buffer_append_char(&decoded, c);
        }
        if (result == AMG_OK)
            result = amg_charset_to_utf8(charset, decoded.data, decoded.length, output);
        amg_buffer_free(&decoded);
        return result;
    }
    return amg_rfc2047_decode(value, output);
}

static const char *embedded_image_extension(const char *content_type)
{
    if (!content_type) return "img";
    if (ci_starts_with(content_type, "image/jpeg") ||
        ci_starts_with(content_type, "image/jpg")) return "jpg";
    if (ci_starts_with(content_type, "image/png")) return "png";
    if (ci_starts_with(content_type, "image/gif")) return "gif";
    if (ci_starts_with(content_type, "image/webp")) return "webp";
    if (ci_starts_with(content_type, "image/bmp")) return "bmp";
    if (ci_starts_with(content_type, "image/tiff")) return "tif";
    if (ci_starts_with(content_type, "image/svg+xml")) return "svg";
    return "img";
}

static int entity_attachment_name(const AmgMailHeaders *headers,
                                  int related_context,
                                  char filename[512],
                                  int *embedded_graphic)
{
    const char *content_type, *disposition, *content_id;
    int is_image, is_attachment, is_inline, embedded = 0;
    if (!headers || !filename) return 0;
    content_type = amg_mail_header_get(headers, "Content-Type");
    disposition = amg_mail_header_get(headers, "Content-Disposition");
    content_id = amg_mail_header_get(headers, "Content-ID");
    if (!content_type) content_type = "text/plain";
    is_image = ci_starts_with(content_type, "image/");
    is_attachment = disposition && ci_starts_with(disposition, "attachment");
    is_inline = disposition && ci_starts_with(disposition, "inline");

    filename[0] = 0;
    if (disposition) {
        if (!param_value(disposition, "filename*", filename, 512U))
            param_value(disposition, "filename", filename, 512U);
    }
    if (!filename[0]) {
        if (!param_value(content_type, "name*", filename, 512U))
            param_value(content_type, "name", filename, 512U);
    }

    /* An explicit attachment disposition always wins, even for images with
     * a Content-ID.  Otherwise image parts used inline, referenced by CID,
     * or carried by multipart/related are shown separately as embedded
     * graphics in the preview. */
    if (is_image && !is_attachment &&
        (is_inline || (content_id && *content_id) || related_context))
        embedded = 1;

    /* multipart/related often carries inline image parts without any name.
     * Keep them savable by assigning a deterministic extension-based name. */
    if (!filename[0] && is_image)
        snprintf(filename, 512U, "%s.%s",
                 embedded ? "embedded-image" : "image",
                 embedded_image_extension(content_type));

    if (embedded_graphic) *embedded_graphic = embedded;
    return filename[0] &&
        (is_image ||
         (disposition &&
          (is_attachment || is_inline)) ||
         (!ci_starts_with(content_type, "text/plain") &&
          !ci_starts_with(content_type, "text/html")));
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

/* RFC 2046 section 5.1.1: a delimiter is a complete line, never an
 * arbitrary substring. All searches stay inside the current MIME entity;
 * binary/NUL data and identical prefixes in nested boundaries are safe. */
typedef struct MimeParts {
    const char *body;
    size_t length;
    const char *boundary;
    size_t boundary_length;
    size_t next;
    int started;
    int finished;
} MimeParts;

static int mime_boundary_line(const MimeParts *parts, size_t from,
                               size_t *line, size_t *after, int *closing)
{
    size_t pos = from;
    while (pos < parts->length) {
        size_t remaining = parts->length - pos;
        const char *newline;
        if (remaining >= parts->boundary_length + 2U &&
            parts->body[pos] == '-' && parts->body[pos + 1U] == '-' &&
            !memcmp(parts->body + pos + 2U, parts->boundary,
                    parts->boundary_length)) {
            size_t end = pos + 2U + parts->boundary_length;
            int close = 0;
            if (parts->length - end >= 2U &&
                parts->body[end] == '-' && parts->body[end + 1U] == '-') {
                close = 1; end += 2U;
            }
            while (end < parts->length &&
                   (parts->body[end] == ' ' || parts->body[end] == '\t')) ++end;
            if (end == parts->length || parts->body[end] == '\n' ||
                (parts->length - end >= 2U && parts->body[end] == '\r' &&
                 parts->body[end + 1U] == '\n')) {
                *line = pos;
                if (end < parts->length && parts->body[end] == '\r') ++end;
                if (end < parts->length && parts->body[end] == '\n') ++end;
                *after = end;
                *closing = close;
                return 1;
            }
        }
        newline = (const char *)memchr(parts->body + pos, '\n', remaining);
        if (!newline) break;
        pos = (size_t)(newline - parts->body) + 1U;
    }
    return 0;
}

static void mime_parts_init(MimeParts *parts, const char *body, size_t length,
                            const char *boundary)
{
    memset(parts, 0, sizeof(*parts));
    parts->body = body;
    parts->length = length;
    parts->boundary = boundary;
    parts->boundary_length = strlen(boundary);
}

/* Returns 1 for a part, 0 after the closing delimiter, negative on error. */
static int mime_parts_next(MimeParts *parts, const char **part, size_t *length)
{
    size_t line, after, start, end;
    int closing;
    if (parts->finished) return 0;
    if (!parts->boundary_length) return AMG_ERR_PARSE;
    if (!parts->started) {
        if (!mime_boundary_line(parts, 0U, &line, &after, &closing))
            return AMG_ERR_PARSE;
        parts->started = 1;
        parts->next = after;
        if (closing) { parts->finished = 1; return 0; }
    }
    start = parts->next;
    if (!mime_boundary_line(parts, start, &line, &after, &closing))
        return AMG_ERR_PARSE;
    end = line;
    /* Exactly ONE line ending belongs to the boundary, not all trailing
     * whitespace. Otherwise binary attachments lose legitimate CR/LF bytes. */
    if (end > start && parts->body[end - 1U] == '\n') {
        --end;
        if (end > start && parts->body[end - 1U] == '\r') --end;
    }
    *part = parts->body + start;
    *length = end - start;
    parts->next = after;
    parts->finished = closing;
    return 1;
}

static int collect_attachment_entity(const char *message, size_t length,
                                     unsigned depth, AmgBuffer *output,
                                     size_t *count,
                                     AmgBuffer *attachment_output,
                                     size_t *attachment_count,
                                     AmgBuffer *embedded_output,
                                     size_t *embedded_count,
                                     int related_context)
{
    AmgMailHeaders headers;
    size_t body_offset = 0U;
    const char *content_type;
    char boundary[256], filename[512];
    int result;
    if (depth > 8U || length > AMIGMAIL_MAX_MESSAGE) return AMG_ERR_LIMIT;
    amg_mail_headers_init(&headers);
    result = amg_mail_headers_parse(message, length, &headers, &body_offset);
    if (result != AMG_OK) goto done;
    content_type = amg_mail_header_get(&headers, "Content-Type");
    if (!content_type) content_type = "text/plain";
    if (ci_starts_with(content_type, "multipart/")) {
        MimeParts parts;
        const char *part;
        size_t part_length;
        int step;
        int child_related = related_context ||
            ci_starts_with(content_type, "multipart/related");
        if (!param_value(content_type, "boundary", boundary, sizeof(boundary))) {
            result = AMG_ERR_PARSE;
            goto done;
        }
        mime_parts_init(&parts, message + body_offset, length - body_offset, boundary);
        while ((step = mime_parts_next(&parts, &part, &part_length)) > 0) {
            result = collect_attachment_entity(
                part, part_length, depth + 1U, output, count,
                attachment_output, attachment_count,
                embedded_output, embedded_count, child_related);
            if (result != AMG_OK) goto done;
        }
        result = step < 0 ? step : AMG_OK;
    } else {
        int embedded = 0;
        if (entity_attachment_name(&headers, related_context, filename, &embedded)) {
            AmgBuffer *group = embedded ? embedded_output : attachment_output;
            size_t *group_count = embedded ? embedded_count : attachment_count;
            if (count) ++*count;
            if (group_count) ++*group_count;
            if (group)
                result = append_attachment_line(filename, content_type,
                                                 length - body_offset, group);
            if (result == AMG_OK && output && output != group)
                result = append_attachment_line(filename, content_type,
                                                 length - body_offset, output);
        }
    }
done:
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
    (void)amg_buffer_set_limit(&decoded, AMIMAIL_MAX_TEXT_PART);
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
    if (result != AMG_OK) goto done;
    content_type = amg_mail_header_get(&headers, "Content-Type");
    if (!content_type) content_type = "text/plain";
    if (ci_starts_with(content_type, wanted)) found = 1;
    else if (ci_starts_with(content_type, "multipart/") &&
             param_value(content_type, "boundary", boundary, sizeof(boundary))) {
        MimeParts parts;
        const char *part;
        size_t part_length;
        mime_parts_init(&parts, message + body_offset, length - body_offset, boundary);
        while (!found && mime_parts_next(&parts, &part, &part_length) > 0)
            found = entity_contains_content_type(part, part_length, depth + 1U, wanted);
    }
done:
    amg_mail_headers_free(&headers);
    return found;
}

static int decode_text_entity(const AmgMailHeaders *headers,
                               const char *body, size_t body_length,
                               AmgBuffer *utf8)
{
    AmgBuffer decoded;
    const char *encoding = amg_mail_header_get(headers, "Content-Transfer-Encoding");
    const char *content_type = amg_mail_header_get(headers, "Content-Type");
    char charset[64];
    int result;
    charset[0] = 0;
    (void)param_value(content_type, "charset", charset, sizeof(charset));
    amg_buffer_init(&decoded);
    (void)amg_buffer_set_limit(&decoded, AMIMAIL_MAX_TEXT_PART);
    if (encoding && ci_equal(encoding, "base64"))
        result = amg_base64_decode(body, body_length, &decoded);
    else if (encoding && ci_equal(encoding, "quoted-printable"))
        result = amg_quoted_printable_decode(body, body_length, &decoded);
    else if (!encoding || ci_equal(encoding, "7bit") ||
             ci_equal(encoding, "8bit") || ci_equal(encoding, "binary"))
        result = amg_buffer_append(&decoded, body, body_length);
    else result = AMG_ERR_UNSUPPORTED;
    if (decoded.limit_hit) result = AMG_ERR_LIMIT;
    if (result == AMG_OK)
        result = amg_charset_to_utf8(charset, decoded.data, decoded.length, utf8);
    amg_buffer_free(&decoded);
    return result;
}

static int extract_entity(const char *message, size_t length, unsigned depth,
                          AmgBuffer *output)
{
    AmgMailHeaders headers;
    size_t body_offset = 0U;
    const char *content_type, *disposition;
    char boundary[256];
    int result;
    if (depth > 8U || length > AMIGMAIL_MAX_MESSAGE) return AMG_ERR_LIMIT;
    amg_mail_headers_init(&headers);
    result = amg_mail_headers_parse(message, length, &headers, &body_offset);
    if (result != AMG_OK) goto done;
    content_type = amg_mail_header_get(&headers, "Content-Type");
    disposition = amg_mail_header_get(&headers, "Content-Disposition");
    if (!content_type) content_type = "text/plain";
    /* A text attachment is not the message body; do not decode large images
     * merely to discover after allocating them that they are not text. */
    if (disposition && ci_starts_with(disposition, "attachment")) {
        result = AMG_ERR_UNSUPPORTED;
        goto done;
    }
    if (ci_starts_with(content_type, "multipart/")) {
        unsigned pass, passes;
        int prefer_plain = ci_starts_with(content_type, "multipart/alternative");
        int has_html = prefer_plain &&
            entity_contains_content_type(message, length, depth, "text/html");
        int empty_text = 0, remembered_error = AMG_ERR_PARSE;
        if (!param_value(content_type, "boundary", boundary, sizeof(boundary))) {
            result = AMG_ERR_PARSE;
            goto done;
        }
        passes = prefer_plain ? (has_html ? 3U : 2U) : 1U;
        for (pass = 0U; pass < passes; ++pass) {
            MimeParts parts;
            const char *part;
            size_t part_length;
            int step;
            mime_parts_init(&parts, message + body_offset, length - body_offset, boundary);
            while ((step = mime_parts_next(&parts, &part, &part_length)) > 0) {
                size_t before = output->length;
                int plain = entity_content_type_is(part, part_length, "text/plain");
                int broken = plain && has_html &&
                    plain_entity_looks_generated_css(part, part_length);
                if (prefer_plain &&
                    ((pass == 0U && (!plain || broken)) ||
                     (pass == 1U && plain) ||
                     (pass == 2U && (!plain || !broken)))) continue;
                result = extract_entity(part, part_length, depth + 1U, output);
                if (output->limit_hit) { result = AMG_ERR_LIMIT; goto done; }
                if (result == AMG_OK) {
                    empty_text = 1;
                    if (output->length > before) goto done;
                } else {
                    /* Never concatenate a partially decoded failed part
                     * with a successful alternative. Actual OOM is fatal. */
                    output->length = before;
                    if (output->data) output->data[before] = 0;
                    if (result == AMG_ERR_MEMORY) goto done;
                    if (result != AMG_ERR_UNSUPPORTED || remembered_error == AMG_ERR_PARSE)
                        remembered_error = result;
                }
            }
            if (step < 0) { result = step; goto done; }
        }
        result = empty_text ? AMG_OK : remembered_error;
    } else if (ci_starts_with(content_type, "text/plain") ||
               ci_starts_with(content_type, "text/html")) {
        AmgBuffer utf8;
        amg_buffer_init(&utf8);
        (void)amg_buffer_set_limit(&utf8, AMIMAIL_MAX_TEXT_PART * 3UL);
        result = decode_text_entity(&headers, message + body_offset,
                                    length - body_offset, &utf8);
        if (result == AMG_OK) {
            if (ci_starts_with(content_type, "text/html") ||
                html_text_looks_mislabeled((const char *)utf8.data, utf8.length))
                result = amg_html_to_text((const char *)utf8.data, utf8.length, output);
            else
                result = plain_text_decode_html_entities(
                    (const char *)utf8.data, utf8.length, output);
        }
        if (utf8.limit_hit || output->limit_hit) result = AMG_ERR_LIMIT;
        amg_buffer_free(&utf8);
    } else result = AMG_ERR_UNSUPPORTED;
done:
    amg_mail_headers_free(&headers);
    return result;
}

int amg_mime_extract_text(const char *message, size_t length, AmgBuffer *output, AmgError *error)
{
    int result;
    if (!message || !output) return AMG_ERR_ARGUMENT;
    if (!output->limit || output->limit > AMIMAIL_MAX_TEXT_PART) {
        result = amg_buffer_set_limit(output, AMIMAIL_MAX_TEXT_PART);
        if (result != AMG_OK) return result;
    }
    output->limit_hit = 0;
    result = extract_entity(message, length, 0U, output);
    if (output->limit_hit) result = AMG_ERR_LIMIT;
    if (result != AMG_OK) amg_error_set(error, result, T(MSG_NO_DISPLAYABLE_TEXT_PART_WAS_FOUND_IN_THE, "No displayable text part was found in the message."));
    else amg_error_set(error, AMG_OK, "");
    return result;
}

int amg_mime_attachment_summary(const char *message, size_t length,
                                AmgBuffer *output, AmgError *error)
{
    int result;
    if (!message || !output) return AMG_ERR_ARGUMENT;
    result = collect_attachment_entity(message, length, 0U,
                                       output, NULL,
                                       NULL, NULL, NULL, NULL, 0);
    if (result != AMG_OK)
        amg_error_set(error, result,
                      T(MSG_ATTACHMENTS_COULD_NOT_BE_PARSED, "Attachments could not be parsed."));
    else
        amg_error_set(error, AMG_OK, "");
    return result;
}

int amg_mime_attachment_grouped_summary(
    const char *message, size_t length,
    AmgBuffer *attachments, size_t *attachment_count,
    AmgBuffer *embedded_graphics, size_t *embedded_graphics_count,
    AmgError *error)
{
    int result;
    size_t files = 0U, embedded = 0U;
    if (attachment_count) *attachment_count = 0U;
    if (embedded_graphics_count) *embedded_graphics_count = 0U;
    if (!message || (!attachments && !embedded_graphics &&
                     !attachment_count && !embedded_graphics_count))
        return AMG_ERR_ARGUMENT;
    result = collect_attachment_entity(message, length, 0U,
                                       NULL, NULL,
                                       attachments, &files,
                                       embedded_graphics, &embedded, 0);
    if (result != AMG_OK) {
        amg_error_set(error, result,
                      T(MSG_ATTACHMENTS_COULD_NOT_BE_PARSED, "Attachments could not be parsed."));
    } else {
        if (attachment_count) *attachment_count = files;
        if (embedded_graphics_count) *embedded_graphics_count = embedded;
        amg_error_set(error, AMG_OK, "");
    }
    return result;
}

int amg_mime_attachment_count(const char *message, size_t length,
                              size_t *count, AmgError *error)
{
    int result;
    size_t found = 0U;
    if (!message || !count) return AMG_ERR_ARGUMENT;
    *count = 0U;
    result = collect_attachment_entity(message, length, 0U,
                                       NULL, &found,
                                       NULL, NULL, NULL, NULL, 0);
    if (result == AMG_OK) {
        *count = found;
        amg_error_set(error, AMG_OK, "");
    } else {
        amg_error_set(error, result,
                      T(MSG_ATTACHMENTS_COULD_NOT_BE_PARSED, "Attachments could not be parsed."));
    }
    return result;
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
                                     AmgBuffer *data, int related_context)
{
    AmgMailHeaders headers;
    size_t body_offset = 0U;
    const char *content_type;
    char boundary[256], filename[512];
    int result;
    if (depth > 8U || length > AMIGMAIL_MAX_MESSAGE) return AMG_ERR_LIMIT;
    amg_mail_headers_init(&headers);
    result = amg_mail_headers_parse(message, length, &headers, &body_offset);
    if (result != AMG_OK) goto done;
    content_type = amg_mail_header_get(&headers, "Content-Type");
    if (!content_type) content_type = "text/plain";
    if (ci_starts_with(content_type, "multipart/")) {
        MimeParts parts;
        const char *part;
        size_t part_length;
        int step;
        int child_related = related_context ||
            ci_starts_with(content_type, "multipart/related");
        if (!param_value(content_type, "boundary", boundary, sizeof(boundary))) {
            result = AMG_ERR_PARSE;
            goto done;
        }
        mime_parts_init(&parts, message + body_offset, length - body_offset, boundary);
        while ((step = mime_parts_next(&parts, &part, &part_length)) > 0) {
            result = extract_attachment_entity(part, part_length, depth + 1U,
                         target, current, name_utf8, data, child_related);
            if (result != AMG_ERR_CANCELLED) goto done;
        }
        result = step < 0 ? step : AMG_ERR_CANCELLED;
    } else if (entity_attachment_name(&headers, related_context, filename, NULL)) {
        if (*current == target) {
            result = append_parameter_utf8(filename, name_utf8);
            if (result == AMG_OK) result = amg_buffer_terminate(name_utf8);
            if (result == AMG_OK)
                result = decode_attachment_body(message, length, body_offset, &headers, data);
        } else { ++*current; result = AMG_ERR_CANCELLED; }
    } else result = AMG_ERR_CANCELLED;
done:
    amg_mail_headers_free(&headers);
    return result;
}

int amg_mime_extract_attachment(const char *message, size_t length,
                                size_t index, AmgBuffer *name_utf8,
                                AmgBuffer *data, AmgError *error)
{
    size_t current = 0U;
    int result;
    if (!message || !name_utf8 || !data) return AMG_ERR_ARGUMENT;
    if (!data->limit || data->limit > AMG_MAIL_MAX_ATTACHMENT_TOTAL) {
        result = amg_buffer_set_limit(data, AMG_MAIL_MAX_ATTACHMENT_TOTAL);
        if (result != AMG_OK) return result;
    }
    data->limit_hit = 0;
    result = extract_attachment_entity(message, length, 0U, index, &current,
                                       name_utf8, data, 0);
    if (data->limit_hit) result = AMG_ERR_LIMIT;
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

int amg_mime_parameter(const char *header, const char *key,
                        char *value, size_t capacity)
{
    return param_value(header, key, value, capacity) != NULL;
}

int amg_mime_describe_attachment(const AmgMailHeaders *headers, int related,
                                  AmgBuffer *name_utf8, int *embedded)
{
    char name[512];
    int result;
    if (!headers || !name_utf8) return AMG_ERR_ARGUMENT;
    if (embedded) *embedded = 0;
    if (!entity_attachment_name(headers, related, name, embedded)) return 0;
    result = append_parameter_utf8(name, name_utf8);
    if (result == AMG_OK) result = amg_buffer_terminate(name_utf8);
    return result == AMG_OK ? 1 : result;
}

int amg_mime_plain_is_css(const char *text, size_t length)
{
    return plain_text_looks_generated_css(text, length);
}
