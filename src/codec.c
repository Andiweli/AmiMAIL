#include "codec.h"

#include <ctype.h>
#include <string.h>

static const char base64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int amg_base64_encode(const unsigned char *input, size_t length, AmgBuffer *output)
{
    size_t i;
    unsigned value;
    if ((!input && length) || !output) return AMG_ERR_ARGUMENT;
    for (i = 0; i < length; i += 3U) {
        value = ((unsigned)input[i]) << 16;
        if (i + 1U < length) value |= ((unsigned)input[i + 1U]) << 8;
        if (i + 2U < length) value |= input[i + 2U];
        if (amg_buffer_append_char(output, (unsigned char)base64_table[(value >> 18) & 63U]) != AMG_OK ||
            amg_buffer_append_char(output, (unsigned char)base64_table[(value >> 12) & 63U]) != AMG_OK ||
            amg_buffer_append_char(output, (unsigned char)(i + 1U < length ? base64_table[(value >> 6) & 63U] : '=')) != AMG_OK ||
            amg_buffer_append_char(output, (unsigned char)(i + 2U < length ? base64_table[value & 63U] : '=')) != AMG_OK)
            return AMG_ERR_MEMORY;
    }
    return AMG_OK;
}

static int b64_value(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == ',') return 62;
    if (c == '/') return 63;
    return -1;
}

int amg_base64_decode(const char *input, size_t length, AmgBuffer *output)
{
    unsigned accumulator = 0;
    unsigned bits = 0;
    size_t i;
    int value;
    if ((!input && length) || !output) return AMG_ERR_ARGUMENT;
    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)input[i];
        if (isspace(c)) continue;
        if (c == '=') break;
        value = b64_value(c);
        if (value < 0) return AMG_ERR_PARSE;
        accumulator = (accumulator << 6) | (unsigned)value;
        bits += 6U;
        if (bits >= 8U) {
            bits -= 8U;
            if (amg_buffer_append_char(output, (unsigned char)(accumulator >> bits)) != AMG_OK)
                return AMG_ERR_MEMORY;
            accumulator &= (1U << bits) - 1U;
        }
    }
    return AMG_OK;
}

int amg_base64url_encode(const unsigned char *input, size_t length, AmgBuffer *output)
{
    size_t start, i;
    int result;
    if (!output) return AMG_ERR_ARGUMENT;
    start = output->length;
    result = amg_base64_encode(input, length, output);
    if (result != AMG_OK) return result;
    for (i = start; i < output->length; ++i) {
        if (output->data[i] == '+') output->data[i] = '-';
        else if (output->data[i] == '/') output->data[i] = '_';
    }
    while (output->length > start && output->data[output->length - 1U] == '=') --output->length;
    output->data[output->length] = 0;
    return AMG_OK;
}

static int hex_value(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

int amg_quoted_printable_decode(const char *input, size_t length, AmgBuffer *output)
{
    size_t i;
    if ((!input && length) || !output) return AMG_ERR_ARGUMENT;
    for (i = 0; i < length; ++i) {
        if (input[i] == '=' && i + 1U < length) {
            if (input[i + 1U] == '\n') { ++i; continue; }
            if (input[i + 1U] == '\r' && i + 2U < length && input[i + 2U] == '\n') { i += 2U; continue; }
            if (i + 2U < length) {
                int high = hex_value((unsigned char)input[i + 1U]);
                int low = hex_value((unsigned char)input[i + 2U]);
                if (high >= 0 && low >= 0) {
                    unsigned char value = (unsigned char)((high << 4) | low);
                    if (amg_buffer_append_char(output, value) != AMG_OK) return AMG_ERR_MEMORY;
                    i += 2U;
                    continue;
                }
            }
        }
        if (amg_buffer_append_char(output, (unsigned char)input[i]) != AMG_OK) return AMG_ERR_MEMORY;
    }
    return AMG_OK;
}

int amg_percent_encode(const char *input, AmgBuffer *output)
{
    static const char hex[] = "0123456789ABCDEF";
    const unsigned char *p = (const unsigned char *)input;
    if (!input || !output) return AMG_ERR_ARGUMENT;
    while (*p) {
        if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') {
            if (amg_buffer_append_char(output, *p) != AMG_OK) return AMG_ERR_MEMORY;
        } else {
            char encoded[3];
            encoded[0] = '%';
            encoded[1] = hex[*p >> 4];
            encoded[2] = hex[*p & 15U];
            if (amg_buffer_append(output, encoded, 3U) != AMG_OK) return AMG_ERR_MEMORY;
        }
        ++p;
    }
    return AMG_OK;
}

static int utf8_next(const unsigned char **cursor, uint32_t *codepoint)
{
    const unsigned char *p = *cursor;
    uint32_t cp;
    if (*p < 0x80U) { *codepoint = *p; *cursor = p + 1; return 1; }
    if ((*p & 0xE0U) == 0xC0U && (p[1] & 0xC0U) == 0x80U) {
        cp = ((uint32_t)(p[0] & 0x1FU) << 6) | (p[1] & 0x3FU);
        if (cp < 0x80U) return 0;
        *codepoint = cp; *cursor = p + 2; return 1;
    }
    if ((*p & 0xF0U) == 0xE0U && (p[1] & 0xC0U) == 0x80U && (p[2] & 0xC0U) == 0x80U) {
        cp = ((uint32_t)(p[0] & 0x0FU) << 12) | ((uint32_t)(p[1] & 0x3FU) << 6) | (p[2] & 0x3FU);
        if (cp < 0x800U || (cp >= 0xD800U && cp <= 0xDFFFU)) return 0;
        *codepoint = cp; *cursor = p + 3; return 1;
    }
    if ((*p & 0xF8U) == 0xF0U && (p[1] & 0xC0U) == 0x80U &&
        (p[2] & 0xC0U) == 0x80U && (p[3] & 0xC0U) == 0x80U) {
        cp = ((uint32_t)(p[0] & 7U) << 18) | ((uint32_t)(p[1] & 0x3FU) << 12) |
             ((uint32_t)(p[2] & 0x3FU) << 6) | (p[3] & 0x3FU);
        if (cp < 0x10000U || cp > 0x10FFFFU) return 0;
        *codepoint = cp; *cursor = p + 4; return 1;
    }
    return 0;
}

static int append_utf8(AmgBuffer *output, uint32_t cp)
{
    unsigned char bytes[4];
    size_t count;
    if (cp <= 0x7FU) { bytes[0] = (unsigned char)cp; count = 1; }
    else if (cp <= 0x7FFU) {
        bytes[0] = (unsigned char)(0xC0U | (cp >> 6)); bytes[1] = (unsigned char)(0x80U | (cp & 0x3FU)); count = 2;
    } else if (cp <= 0xFFFFU) {
        bytes[0] = (unsigned char)(0xE0U | (cp >> 12)); bytes[1] = (unsigned char)(0x80U | ((cp >> 6) & 0x3FU));
        bytes[2] = (unsigned char)(0x80U | (cp & 0x3FU)); count = 3;
    } else if (cp <= 0x10FFFFU) {
        bytes[0] = (unsigned char)(0xF0U | (cp >> 18)); bytes[1] = (unsigned char)(0x80U | ((cp >> 12) & 0x3FU));
        bytes[2] = (unsigned char)(0x80U | ((cp >> 6) & 0x3FU)); bytes[3] = (unsigned char)(0x80U | (cp & 0x3FU)); count = 4;
    } else return AMG_ERR_PARSE;
    return amg_buffer_append(output, bytes, count);
}

static int flush_utf7_run(AmgBuffer *output, AmgBuffer *utf16)
{
    AmgBuffer encoded;
    size_t i;
    int result;
    if (!utf16->length) return AMG_OK;
    amg_buffer_init(&encoded);
    result = amg_base64_encode(utf16->data, utf16->length, &encoded);
    if (result == AMG_OK) {
        for (i = 0; i < encoded.length; ++i) if (encoded.data[i] == '/') encoded.data[i] = ',';
        while (encoded.length && encoded.data[encoded.length - 1U] == '=') --encoded.length;
        result = amg_buffer_append_char(output, '&');
        if (result == AMG_OK) result = amg_buffer_append(output, encoded.data, encoded.length);
        if (result == AMG_OK) result = amg_buffer_append_char(output, '-');
    }
    amg_buffer_free(&encoded);
    utf16->length = 0;
    return result;
}

int amg_modified_utf7_encode(const char *utf8, AmgBuffer *output)
{
    const unsigned char *p = (const unsigned char *)utf8;
    AmgBuffer utf16;
    uint32_t cp;
    int result = AMG_OK;
    if (!utf8 || !output) return AMG_ERR_ARGUMENT;
    amg_buffer_init(&utf16);
    while (*p && result == AMG_OK) {
        if (*p >= 0x20U && *p <= 0x7EU) {
            result = flush_utf7_run(output, &utf16);
            if (result != AMG_OK) break;
            if (*p == '&') result = amg_buffer_append_cstr(output, "&-");
            else result = amg_buffer_append_char(output, *p);
            ++p;
            continue;
        }
        if (!utf8_next(&p, &cp)) { result = AMG_ERR_PARSE; break; }
        if (cp <= 0xFFFFU) {
            unsigned char pair[2] = {(unsigned char)(cp >> 8), (unsigned char)cp};
            result = amg_buffer_append(&utf16, pair, 2U);
        } else {
            unsigned char pair[4];
            uint32_t v = cp - 0x10000U;
            uint16_t high = (uint16_t)(0xD800U + (v >> 10));
            uint16_t low = (uint16_t)(0xDC00U + (v & 0x3FFU));
            pair[0] = (unsigned char)(high >> 8); pair[1] = (unsigned char)high;
            pair[2] = (unsigned char)(low >> 8); pair[3] = (unsigned char)low;
            result = amg_buffer_append(&utf16, pair, 4U);
        }
    }
    if (result == AMG_OK) result = flush_utf7_run(output, &utf16);
    amg_buffer_free(&utf16);
    return result;
}

int amg_modified_utf7_decode(const char *input, AmgBuffer *output)
{
    const char *p = input;
    if (!input || !output) return AMG_ERR_ARGUMENT;
    while (*p) {
        if (*p != '&') {
            if (amg_buffer_append_char(output, (unsigned char)*p++) != AMG_OK) return AMG_ERR_MEMORY;
        } else {
            const char *end = strchr(p, '-');
            AmgBuffer decoded;
            size_t i;
            if (!end) return AMG_ERR_PARSE;
            if (end == p + 1) { if (amg_buffer_append_char(output, '&') != AMG_OK) return AMG_ERR_MEMORY; p = end + 1; continue; }
            amg_buffer_init(&decoded);
            if (amg_base64_decode(p + 1, (size_t)(end - p - 1), &decoded) != AMG_OK || (decoded.length & 1U)) {
                amg_buffer_free(&decoded); return AMG_ERR_PARSE;
            }
            for (i = 0; i < decoded.length; i += 2U) {
                uint32_t cp = ((uint32_t)decoded.data[i] << 8) | decoded.data[i + 1U];
                if (cp >= 0xD800U && cp <= 0xDBFFU) {
                    uint32_t low;
                    if (i + 3U >= decoded.length) { amg_buffer_free(&decoded); return AMG_ERR_PARSE; }
                    low = ((uint32_t)decoded.data[i + 2U] << 8) | decoded.data[i + 3U];
                    if (low < 0xDC00U || low > 0xDFFFU) { amg_buffer_free(&decoded); return AMG_ERR_PARSE; }
                    cp = 0x10000U + ((cp - 0xD800U) << 10) + (low - 0xDC00U); i += 2U;
                }
                if (append_utf8(output, cp) != AMG_OK) { amg_buffer_free(&decoded); return AMG_ERR_MEMORY; }
            }
            amg_buffer_free(&decoded);
            p = end + 1;
        }
    }
    return AMG_OK;
}

int amg_utf8_to_local(const char *utf8, AmgBuffer *output)
{
    const unsigned char *p = (const unsigned char *)utf8;
    uint32_t cp;
    if (!utf8 || !output) return AMG_ERR_ARGUMENT;
    while (*p) {
        if (!utf8_next(&p, &cp)) { ++p; cp = '?'; }
        if (cp <= 255U) {
            if (amg_buffer_append_char(output, (unsigned char)cp) != AMG_OK) return AMG_ERR_MEMORY;
        } else if (amg_buffer_append_char(output, '?') != AMG_OK) return AMG_ERR_MEMORY;
    }
    return AMG_OK;
}
