#include "charset.h"

#include <stdint.h>
#include <string.h>

enum CharsetKind {
    CS_UNKNOWN, CS_AUTO, CS_ASCII, CS_UTF8, CS_LATIN1, CS_LATIN9,
    CS_WINDOWS1252, CS_UTF16, CS_UTF16LE, CS_UTF16BE
};

static enum CharsetKind charset_kind(const char *name)
{
    char normalized[48];
    size_t used = 0U;
    if (!name || !*name) return CS_AUTO;
    while (*name) {
        unsigned char c = (unsigned char)*name++;
        if (c == '-' || c == '_' || c == ' ' || c == '\t') continue;
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + ('a' - 'A'));
        if (used + 1U >= sizeof(normalized)) return CS_UNKNOWN;
        normalized[used++] = (char)c;
    }
    normalized[used] = 0;
    if (!strcmp(normalized, "utf8")) return CS_UTF8;
    if (!strcmp(normalized, "usascii") || !strcmp(normalized, "ascii"))
        return CS_ASCII;
    if (!strcmp(normalized, "iso88591") || !strcmp(normalized, "latin1") ||
        !strcmp(normalized, "l1")) return CS_LATIN1;
    if (!strcmp(normalized, "iso885915") || !strcmp(normalized, "latin9"))
        return CS_LATIN9;
    if (!strcmp(normalized, "windows1252") || !strcmp(normalized, "cp1252"))
        return CS_WINDOWS1252;
    if (!strcmp(normalized, "utf16")) return CS_UTF16;
    if (!strcmp(normalized, "utf16le")) return CS_UTF16LE;
    if (!strcmp(normalized, "utf16be")) return CS_UTF16BE;
    return CS_UNKNOWN;
}

static size_t utf8_scalar(const unsigned char *p, size_t left, uint32_t *cp)
{
    uint32_t value, minimum;
    size_t i, count;
    if (!left) return 0U;
    if (p[0] < 0x80U) { *cp = p[0]; return 1U; }
    if (p[0] >= 0xc2U && p[0] <= 0xdfU) {
        count = 2U; value = p[0] & 0x1fU; minimum = 0x80U;
    } else if (p[0] >= 0xe0U && p[0] <= 0xefU) {
        count = 3U; value = p[0] & 0x0fU; minimum = 0x800U;
    } else if (p[0] >= 0xf0U && p[0] <= 0xf4U) {
        count = 4U; value = p[0] & 7U; minimum = 0x10000U;
    } else return 0U;
    if (left < count) return 0U;
    for (i = 1U; i < count; ++i) {
        if ((p[i] & 0xc0U) != 0x80U) return 0U;
        value = (value << 6) | (p[i] & 0x3fU);
    }
    if (value < minimum || value > 0x10ffffU ||
        (value >= 0xd800U && value <= 0xdfffU)) return 0U;
    *cp = value;
    return count;
}

static int is_utf8(const unsigned char *input, size_t length)
{
    size_t pos = 0U;
    uint32_t cp;
    while (pos < length) {
        size_t count = utf8_scalar(input + pos, length - pos, &cp);
        if (!count) return 0;
        pos += count;
    }
    return 1;
}

static int append_scalar(AmgBuffer *output, uint32_t cp)
{
    unsigned char bytes[4];
    size_t count;
    /* NUL must not hide the remainder when the GUI consumes a C string. */
    if (!cp) cp = 0xfffdU;
    if (cp < 0x80U) { bytes[0] = (unsigned char)cp; count = 1U; }
    else if (cp < 0x800U) {
        bytes[0] = (unsigned char)(0xc0U | (cp >> 6));
        bytes[1] = (unsigned char)(0x80U | (cp & 0x3fU)); count = 2U;
    } else if (cp < 0x10000U) {
        bytes[0] = (unsigned char)(0xe0U | (cp >> 12));
        bytes[1] = (unsigned char)(0x80U | ((cp >> 6) & 0x3fU));
        bytes[2] = (unsigned char)(0x80U | (cp & 0x3fU)); count = 3U;
    } else {
        bytes[0] = (unsigned char)(0xf0U | (cp >> 18));
        bytes[1] = (unsigned char)(0x80U | ((cp >> 12) & 0x3fU));
        bytes[2] = (unsigned char)(0x80U | ((cp >> 6) & 0x3fU));
        bytes[3] = (unsigned char)(0x80U | (cp & 0x3fU)); count = 4U;
    }
    return amg_buffer_append(output, bytes, count);
}

static uint32_t read_word(const unsigned char *p, int little)
{
    return little ? ((uint32_t)p[1] << 8) | p[0]
                  : ((uint32_t)p[0] << 8) | p[1];
}

int amg_charset_to_utf8(const char *charset, const unsigned char *input,
                         size_t length, AmgBuffer *output)
{
    static const uint16_t cp1252[32] = {
        0x20ac,0xfffd,0x201a,0x0192,0x201e,0x2026,0x2020,0x2021,
        0x02c6,0x2030,0x0160,0x2039,0x0152,0xfffd,0x017d,0xfffd,
        0xfffd,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,
        0x02dc,0x2122,0x0161,0x203a,0x0153,0xfffd,0x017e,0x0178
    };
    enum CharsetKind kind;
    size_t pos = 0U;
    int little = 0;
    if ((!input && length) || !output) return AMG_ERR_ARGUMENT;
    kind = charset_kind(charset);
    if (kind == CS_UNKNOWN) return AMG_ERR_UNSUPPORTED;
    if (kind == CS_AUTO) {
        if (length >= 2U && input[0] == 0xffU && input[1] == 0xfeU)
            kind = CS_UTF16LE;
        else if (length >= 2U && input[0] == 0xfeU && input[1] == 0xffU)
            kind = CS_UTF16BE;
        else kind = is_utf8(input, length) ? CS_UTF8 : CS_WINDOWS1252;
    }
    if (kind == CS_UTF8 && length >= 3U && input[0] == 0xefU &&
        input[1] == 0xbbU && input[2] == 0xbfU) pos = 3U;
    if (kind == CS_UTF16 || kind == CS_UTF16LE || kind == CS_UTF16BE) {
        little = kind == CS_UTF16LE;
        if (length >= 2U && input[0] == 0xffU && input[1] == 0xfeU) {
            if (kind == CS_UTF16BE) return AMG_ERR_PARSE;
            little = 1; pos = 2U;
        } else if (length >= 2U && input[0] == 0xfeU && input[1] == 0xffU) {
            if (kind == CS_UTF16LE) return AMG_ERR_PARSE;
            little = 0; pos = 2U;
        }
    }
    while (pos < length) {
        uint32_t cp;
        size_t count = 1U;
        int result;
        if (kind == CS_UTF8) {
            count = utf8_scalar(input + pos, length - pos, &cp);
            if (!count) { count = 1U; cp = 0xfffdU; }
        } else if (kind == CS_UTF16 || kind == CS_UTF16LE ||
                   kind == CS_UTF16BE) {
            if (length - pos < 2U) cp = 0xfffdU;
            else {
                count = 2U;
                cp = read_word(input + pos, little);
                if (cp >= 0xd800U && cp <= 0xdbffU) {
                    if (length - pos >= 4U) {
                        uint32_t low = read_word(input + pos + 2U, little);
                        if (low >= 0xdc00U && low <= 0xdfffU) {
                            cp = 0x10000U + ((cp - 0xd800U) << 10) + low - 0xdc00U;
                            count = 4U;
                        } else cp = 0xfffdU;
                    } else cp = 0xfffdU;
                } else if (cp >= 0xdc00U && cp <= 0xdfffU) cp = 0xfffdU;
            }
        } else {
            cp = input[pos];
            if (kind == CS_ASCII && cp >= 0x80U) cp = 0xfffdU;
            else if (kind == CS_WINDOWS1252 && cp >= 0x80U && cp < 0xa0U)
                cp = cp1252[cp - 0x80U];
            else if (kind == CS_LATIN9) {
                switch (cp) {
                    case 0xa4: cp = 0x20ac; break;
                    case 0xa6: cp = 0x0160; break;
                    case 0xa8: cp = 0x0161; break;
                    case 0xb4: cp = 0x017d; break;
                    case 0xb8: cp = 0x017e; break;
                    case 0xbc: cp = 0x0152; break;
                    case 0xbd: cp = 0x0153; break;
                    case 0xbe: cp = 0x0178; break;
                    default: break;
                }
            }
        }
        result = append_scalar(output, cp);
        if (result != AMG_OK) return result;
        pos += count;
    }
    return AMG_OK;
}

int amg_charset_module_present(void) { return 1; }
