#include "i18n.h"
#include "amigmail.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if AMIGMAIL_AMIGA
#include <dos/dos.h>
#include <dos/var.h>
#include <proto/dos.h>
#endif

static int language_is_german = 0;

#if AMIGMAIL_AMIGA
static int ascii_equal_ci(const char *left, const char *right)
{
    unsigned char a, b;
    if (!left || !right) return 0;
    while (*left && *right) {
        a = (unsigned char)*left++;
        b = (unsigned char)*right++;
        a = (unsigned char)tolower(a);
        b = (unsigned char)tolower(b);
        if (a != b) return 0;
    }
    return *left == 0 && *right == 0;
}

static int ascii_starts_with_ci(const char *text, const char *prefix)
{
    unsigned char a, b;
    if (!text || !prefix) return 0;
    while (*prefix) {
        if (!*text) return 0;
        a = (unsigned char)*text++;
        b = (unsigned char)*prefix++;
        a = (unsigned char)tolower(a);
        b = (unsigned char)tolower(b);
        if (a != b) return 0;
    }
    return 1;
}

static int language_value_is_german(const char *language)
{
    if (!language || !*language) return 0;
    return ascii_equal_ci(language, "german") ||
           ascii_equal_ci(language, "deutsch") ||
           ascii_starts_with_ci(language, "de") ||
           ascii_starts_with_ci(language, "ger");
}

static int read_language_var(const char *name, char *buffer, size_t capacity)
{
    CONST_STRPTR variable_name;
    STRPTR variable_buffer;
    LONG length;
    if (!name || !buffer || capacity == 0U) return 0;

    variable_name = (CONST_STRPTR)name;
    variable_buffer = (STRPTR)buffer;
    length = GetVar(variable_name, variable_buffer, (LONG)capacity, 0L);
    if (length <= 0)
        length = GetVar(variable_name, variable_buffer, (LONG)capacity,
                        GVF_GLOBAL_ONLY);
    if (length <= 0) return 0;

    if ((size_t)length >= capacity)
        length = (LONG)capacity - 1L;
    buffer[length] = 0;
    return 1;
}
#endif

void amg_i18n_init(void)
{
    language_is_german = 0;
#if AMIGMAIL_AMIGA
    {
        static const char *const variable_names[] = {
            "LanguageName", "Language", "language"
        };
        char language[64];
        size_t index;

        for (index = 0U; index < sizeof(variable_names) / sizeof(variable_names[0]);
             ++index) {
            if (read_language_var(variable_names[index], language,
                                  sizeof(language)) &&
                language_value_is_german(language)) {
                language_is_german = 1;
                break;
            }
        }
    }
#endif
}

int amg_i18n_is_german(void)
{
    return language_is_german;
}

const char *amg_tr(const char *german, const char *english)
{
    if (language_is_german) return german ? german : "";
    return english ? english : "";
}

int amg_tr_snprintf(char *output, size_t capacity,
                    const char *german_format,
                    const char *english_format, ...)
{
    va_list arguments;
    int result;
    const char *format;
    if (!output || capacity == 0U) return -1;
    format = amg_tr(german_format, english_format);
    va_start(arguments, english_format);
    result = vsnprintf(output, capacity, format, arguments);
    va_end(arguments);
    return result;
}
