#ifndef AMIGMAIL_I18N_H
#define AMIGMAIL_I18N_H
#include <stddef.h>
#include "catalog_ids.h"
void amg_i18n_init(void);
void amg_i18n_cleanup(void);
const char *amg_tr(long string_id, const char *english_fallback);
int amg_tr_snprintf(char *output, size_t capacity,
                    long string_id, const char *english_format, ...);
/* Returns the current Amiga locale offset in minutes west of GMT.
 * East-of-GMT locations therefore use negative values, matching
 * struct Locale::loc_GMTOffset. */
int amg_locale_gmt_offset_minutes(long *minutes_west);
/* Resolve a classic Amiga/POSIX-style TZONE/TZ value for a local wall-clock
 * date.  Explicit Mmonth.week.weekday transition rules are supported; the
 * traditional CET-1CEST form uses the EU CET/CEST transitions. */
int amg_tzone_gmt_offset_minutes(const char *tzone,
                                 long locale_minutes_west,
                                 int have_locale_offset,
                                 unsigned long year,
                                 unsigned int month,
                                 unsigned int day,
                                 unsigned int hour,
                                 unsigned int minute,
                                 long *minutes_west);
/* Prefer ENV:TZONE (then ENV:TZ) so daylight-saving information can override
 * locale.library, which on classic AmigaOS normally only provides the base
 * GMT offset.  Falls back to the current locale offset when no usable
 * environment timezone is available. */
int amg_current_gmt_offset_minutes(unsigned long year,
                                   unsigned int month,
                                   unsigned int day,
                                   unsigned int hour,
                                   unsigned int minute,
                                   long *minutes_west);
/* Format an Amiga-style GMT offset as an RFC 5322 numeric zone.
 * Example: -120 minutes west (UTC+2) becomes "+0200". */
int amg_rfc5322_timezone(long minutes_west, char output[6]);
#endif
