#ifndef AMIGMAIL_I18N_H
#define AMIGMAIL_I18N_H
#include <stddef.h>
#include "catalog_ids.h"
void amg_i18n_init(void);
void amg_i18n_cleanup(void);
const char *amg_tr(long string_id, const char *english_fallback);
int amg_tr_snprintf(char *output, size_t capacity,
                    long string_id, const char *english_format, ...);
#endif
