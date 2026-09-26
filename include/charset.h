#ifndef AMIGMAIL_CHARSET_H
#define AMIGMAIL_CHARSET_H

#include "buffer.h"

int amg_charset_module_present(void);
/* Input is length-bounded and need not be NUL-terminated. Unknown declared
 * charsets return AMG_ERR_UNSUPPORTED. Missing charset: UTF-8 if valid,
 * otherwise Windows-1252 as a conservative legacy-mail display fallback. */
int amg_charset_to_utf8(const char *charset, const unsigned char *input,
                         size_t length, AmgBuffer *output);

#endif
