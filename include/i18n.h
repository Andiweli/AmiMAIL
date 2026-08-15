#ifndef AMIGMAIL_I18N_H
#define AMIGMAIL_I18N_H

#include <stddef.h>

void amg_i18n_init(void);
int amg_i18n_is_german(void);
const char *amg_tr(const char *german, const char *english);
int amg_tr_snprintf(char *output, size_t capacity,
                    const char *german_format,
                    const char *english_format, ...);

#endif
