#include "i18n.h"
#include "amigmail.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#if AMIGMAIL_AMIGA
#include <dos/var.h>
#include <exec/libraries.h>
#include <libraries/locale.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/locale.h>
#include <utility/tagitem.h>
struct LocaleBase *LocaleBase = NULL;
static struct Catalog *catalog = NULL;
#endif

typedef struct AmgTzRule {
    unsigned int month;
    unsigned int week;
    unsigned int weekday;
    unsigned int minute_of_day;
} AmgTzRule;

typedef struct AmgTzSpec {
    char standard_name[16];
    char daylight_name[16];
    long standard_minutes_west;
    long daylight_minutes_west;
    int has_daylight;
    int has_explicit_daylight_offset;
    int has_rules;
    AmgTzRule start_rule;
    AmgTzRule end_rule;
} AmgTzSpec;

void amg_i18n_init(void) {
#if AMIGMAIL_AMIGA
    struct TagItem tags[4];
    if (LocaleBase) return;
    LocaleBase=(struct LocaleBase *)OpenLibrary((CONST_STRPTR)"locale.library",38UL);
    if (!LocaleBase) return;
    tags[0].ti_Tag=OC_BuiltInLanguage; tags[0].ti_Data=(ULONG)(uintptr_t)"english";
    tags[1].ti_Tag=OC_BuiltInCodeSet; tags[1].ti_Data=0UL;
    /* Prefer the current catalog generation. OC_Version requests an exact
     * catalog version, supplied by catalog_ids.h. If an older catalog is installed,
     * retry with version 0 (accept any) instead of falling back completely to
     * the built-in English strings. */
    tags[2].ti_Tag=OC_Version; tags[2].ti_Data=AMIMAIL_CATALOG_VERSION;
    tags[3].ti_Tag=TAG_DONE; tags[3].ti_Data=0UL;
    catalog=OpenCatalogA(NULL,(STRPTR)"AmiMAIL.catalog",tags);
    if (!catalog) {
        tags[2].ti_Data=0UL;
        catalog=OpenCatalogA(NULL,(STRPTR)"AmiMAIL.catalog",tags);
    }
#endif
}
void amg_i18n_cleanup(void) {
#if AMIGMAIL_AMIGA
    if (catalog) { CloseCatalog(catalog); catalog=NULL; }
    if (LocaleBase) { CloseLibrary((struct Library *)LocaleBase); LocaleBase=NULL; }
#endif
}
const char *amg_tr(long string_id,const char *english_fallback) {
    if (!english_fallback) english_fallback="";
#if AMIGMAIL_AMIGA
    if (LocaleBase) return (const char *)GetCatalogStr(catalog,(LONG)string_id,(STRPTR)english_fallback);
#else
    (void)string_id;
#endif
    return english_fallback;
}
int amg_locale_gmt_offset_minutes(long *minutes_west) {
    if (!minutes_west) return 0;
#if AMIGMAIL_AMIGA
    {
        struct Locale *current_locale;
        if (!LocaleBase) return 0;
        current_locale = OpenLocale(NULL);
        if (!current_locale) return 0;
        *minutes_west = (long)current_locale->loc_GMTOffset;
        CloseLocale(current_locale);
        return 1;
    }
#else
    return 0;
#endif
}

static int ascii_name_equal(const char *left, const char *right)
{
    unsigned char a, b;
    if (!left || !right) return 0;
    while (*left && *right) {
        a = (unsigned char)*left++;
        b = (unsigned char)*right++;
        if (toupper(a) != toupper(b)) return 0;
    }
    return *left == 0 && *right == 0;
}

static int parse_tz_name(const char **cursor, char output[16])
{
    const char *p;
    size_t length = 0U;
    if (!cursor || !*cursor || !output) return 0;
    p = *cursor;
    output[0] = 0;
    if (*p == '<') {
        ++p;
        while (*p && *p != '>') {
            if (length + 1U >= 16U) return 0;
            output[length++] = *p++;
        }
        if (*p != '>' || length == 0U) return 0;
        ++p;
    } else {
        while (*p && isalpha((unsigned char)*p)) {
            if (length + 1U >= 16U) return 0;
            output[length++] = *p++;
        }
        if (length < 3U) return 0;
    }
    output[length] = 0;
    *cursor = p;
    return 1;
}

static int parse_unsigned_number(const char **cursor, unsigned long *value)
{
    const char *p;
    unsigned long result = 0UL;
    int digits = 0;
    if (!cursor || !*cursor || !value) return 0;
    p = *cursor;
    while (isdigit((unsigned char)*p)) {
        if (result > 100000UL) return 0;
        result = result * 10UL + (unsigned long)(*p - '0');
        ++p;
        ++digits;
    }
    if (!digits) return 0;
    *value = result;
    *cursor = p;
    return 1;
}

/* POSIX TZ offsets are the amount added to local time to obtain UTC, which
 * matches Amiga's loc_GMTOffset sign convention: positive is west of UTC. */
static int parse_tz_offset(const char **cursor, long *minutes_west)
{
    const char *p;
    int sign = 1;
    unsigned long hours, minutes = 0UL, seconds = 0UL;
    if (!cursor || !*cursor || !minutes_west) return 0;
    p = *cursor;
    if (*p == '+') {
        ++p;
    } else if (*p == '-') {
        sign = -1;
        ++p;
    }
    if (!parse_unsigned_number(&p, &hours) || hours > 24UL) return 0;
    if (*p == ':') {
        ++p;
        if (!parse_unsigned_number(&p, &minutes) || minutes > 59UL) return 0;
        if (*p == ':') {
            ++p;
            if (!parse_unsigned_number(&p, &seconds) || seconds > 59UL) return 0;
        }
    }
    if (hours == 24UL && (minutes != 0UL || seconds != 0UL)) return 0;
    /* RFC 5322 numeric zones have minute precision.  Reject unusual TZ
     * offsets with non-zero seconds rather than silently publishing a wrong
     * Date header. */
    if (seconds != 0UL) return 0;
    *minutes_west = (long)(hours * 60UL + minutes) * (long)sign;
    *cursor = p;
    return 1;
}

static int parse_rule(const char **cursor, AmgTzRule *rule)
{
    const char *p;
    unsigned long month, week, weekday;
    unsigned long hour = 2UL, minute = 0UL, second = 0UL;
    if (!cursor || !*cursor || !rule) return 0;
    p = *cursor;
    if (*p++ != 'M') return 0;
    if (!parse_unsigned_number(&p, &month) || month < 1UL || month > 12UL)
        return 0;
    if (*p++ != '.') return 0;
    if (!parse_unsigned_number(&p, &week) || week < 1UL || week > 5UL)
        return 0;
    if (*p++ != '.') return 0;
    if (!parse_unsigned_number(&p, &weekday) || weekday > 6UL)
        return 0;
    if (*p == '/') {
        ++p;
        if (!parse_unsigned_number(&p, &hour) || hour > 23UL) return 0;
        if (*p == ':') {
            ++p;
            if (!parse_unsigned_number(&p, &minute) || minute > 59UL)
                return 0;
            if (*p == ':') {
                ++p;
                if (!parse_unsigned_number(&p, &second) || second > 59UL)
                    return 0;
            }
        }
    }
    if (second != 0UL) return 0;
    rule->month = (unsigned int)month;
    rule->week = (unsigned int)week;
    rule->weekday = (unsigned int)weekday;
    rule->minute_of_day = (unsigned int)(hour * 60UL + minute);
    *cursor = p;
    return 1;
}

static int parse_tzone(const char *text, AmgTzSpec *spec)
{
    const char *p;
    if (!text || !spec) return 0;
    memset(spec, 0, sizeof(*spec));
    p = text;
    while (*p && isspace((unsigned char)*p)) ++p;
    if (!parse_tz_name(&p, spec->standard_name)) return 0;
    if (!parse_tz_offset(&p, &spec->standard_minutes_west)) return 0;
    if (*p && *p != ',' && !isspace((unsigned char)*p)) {
        if (!parse_tz_name(&p, spec->daylight_name)) return 0;
        spec->has_daylight = 1;
        spec->daylight_minutes_west = spec->standard_minutes_west - 60L;
        if (*p && *p != ',' && !isspace((unsigned char)*p)) {
            if (!parse_tz_offset(&p, &spec->daylight_minutes_west)) return 0;
            spec->has_explicit_daylight_offset = 1;
        }
    }
    if (*p == ',') {
        if (!spec->has_daylight) return 0;
        ++p;
        if (!parse_rule(&p, &spec->start_rule)) return 0;
        if (*p++ != ',') return 0;
        if (!parse_rule(&p, &spec->end_rule)) return 0;
        spec->has_rules = 1;
    }
    while (*p && isspace((unsigned char)*p)) ++p;
    return *p == 0;
}

static int leap_year(unsigned long year)
{
    return (year % 4UL == 0UL && year % 100UL != 0UL) ||
           year % 400UL == 0UL;
}

static unsigned int month_days(unsigned long year, unsigned int month)
{
    static const unsigned char lengths[12] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };
    if (month < 1U || month > 12U) return 0U;
    if (month == 2U && leap_year(year)) return 29U;
    return lengths[month - 1U];
}

/* Gregorian weekday, Sunday=0. */
static unsigned int weekday_for_date(unsigned long year,
                                     unsigned int month,
                                     unsigned int day)
{
    static const unsigned int offsets[12] = {
        0U, 3U, 2U, 5U, 0U, 3U, 5U, 1U, 4U, 6U, 2U, 4U
    };
    unsigned long y = year;
    if (month < 3U) --y;
    return (unsigned int)((y + y / 4UL - y / 100UL + y / 400UL +
                           offsets[month - 1U] + day) % 7UL);
}

static unsigned int rule_day_of_month(unsigned long year,
                                      const AmgTzRule *rule)
{
    unsigned int first_weekday, day, last, last_weekday;
    if (!rule || rule->month < 1U || rule->month > 12U) return 0U;
    if (rule->week < 5U) {
        first_weekday = weekday_for_date(year, rule->month, 1U);
        day = 1U + (rule->weekday + 7U - first_weekday) % 7U;
        day += (rule->week - 1U) * 7U;
        if (day > month_days(year, rule->month)) return 0U;
        return day;
    }
    last = month_days(year, rule->month);
    last_weekday = weekday_for_date(year, rule->month, last);
    return last - (last_weekday + 7U - rule->weekday) % 7U;
}

static int compare_local_to_rule(unsigned long year,
                                 unsigned int month,
                                 unsigned int day,
                                 unsigned int hour,
                                 unsigned int minute,
                                 const AmgTzRule *rule)
{
    unsigned int rule_day = rule_day_of_month(year, rule);
    unsigned int current_minutes = hour * 60U + minute;
    if (month < rule->month) return -1;
    if (month > rule->month) return 1;
    if (day < rule_day) return -1;
    if (day > rule_day) return 1;
    if (current_minutes < rule->minute_of_day) return -1;
    if (current_minutes > rule->minute_of_day) return 1;
    return 0;
}

static int daylight_active(unsigned long year,
                           unsigned int month,
                           unsigned int day,
                           unsigned int hour,
                           unsigned int minute,
                           const AmgTzRule *start_rule,
                           const AmgTzRule *end_rule)
{
    int at_start = compare_local_to_rule(year, month, day, hour, minute,
                                         start_rule);
    int at_end = compare_local_to_rule(year, month, day, hour, minute,
                                       end_rule);
    int start_before_end;
    if (start_rule->month != end_rule->month)
        start_before_end = start_rule->month < end_rule->month;
    else
        start_before_end = rule_day_of_month(year, start_rule) <
                           rule_day_of_month(year, end_rule);
    if (start_before_end) return at_start >= 0 && at_end < 0;
    return at_start >= 0 || at_end < 0;
}

int amg_tzone_gmt_offset_minutes(const char *tzone,
                                 long locale_minutes_west,
                                 int have_locale_offset,
                                 unsigned long year,
                                 unsigned int month,
                                 unsigned int day,
                                 unsigned int hour,
                                 unsigned int minute,
                                 long *minutes_west)
{
    AmgTzSpec spec;
    AmgTzRule start_rule, end_rule;
    if (!minutes_west || month < 1U || month > 12U || day < 1U ||
        day > month_days(year, month) || hour > 23U || minute > 59U)
        return 0;
    if (!parse_tzone(tzone, &spec)) return 0;
    if (!spec.has_daylight) {
        *minutes_west = spec.standard_minutes_west;
        return 1;
    }

    /* A common classic-Amiga setup keeps the system clock manually on
     * summertime and writes that effective offset into TZONE, e.g.
     * "CET-2CEST".  Although that is not strict POSIX notation for Central
     * Europe, it is widely used as an Amiga wall-clock override.  Treat the
     * non-standard CET offset as already effective so we never add another
     * implicit DST hour and accidentally publish +0300.  This remains true
     * when a utility such as SetDST/SetST has also patched locale.library. */
    if (!spec.has_explicit_daylight_offset && !spec.has_rules &&
        ascii_name_equal(spec.standard_name, "CET") &&
        ascii_name_equal(spec.daylight_name, "CEST") &&
        spec.standard_minutes_west != -60L) {
        *minutes_west = spec.standard_minutes_west;
        return 1;
    }
    /* Apply the same conservative rule for other classic-Amiga setups when
     * TZONE is exactly one DST hour ahead of the base locale offset. */
    if (!spec.has_explicit_daylight_offset && !spec.has_rules &&
        have_locale_offset &&
        spec.standard_minutes_west == locale_minutes_west - 60L) {
        *minutes_west = spec.standard_minutes_west;
        return 1;
    }

    if (spec.has_rules) {
        start_rule = spec.start_rule;
        end_rule = spec.end_rule;
    } else if (ascii_name_equal(spec.standard_name, "CET") &&
               ascii_name_equal(spec.daylight_name, "CEST")) {
        /* European Union CET/CEST rule: last Sunday in March at 02:00 local
         * standard time until the last Sunday in October at 03:00 local
         * daylight time.  This covers the traditional Amiga TZONE value
         * "CET-1CEST" even when locale.library itself has no DST concept. */
        start_rule.month = 3U;
        start_rule.week = 5U;
        start_rule.weekday = 0U;
        start_rule.minute_of_day = 120U;
        end_rule.month = 10U;
        end_rule.week = 5U;
        end_rule.weekday = 0U;
        end_rule.minute_of_day = 180U;
    } else {
        /* Without transition rules it is safer to publish the explicit
         * standard offset than to assume US-style POSIX defaults on AmigaOS. */
        *minutes_west = spec.standard_minutes_west;
        return 1;
    }

    *minutes_west = daylight_active(year, month, day, hour, minute,
                                    &start_rule, &end_rule)
                        ? spec.daylight_minutes_west
                        : spec.standard_minutes_west;
    return 1;
}

#if AMIGMAIL_AMIGA
static int read_global_timezone_variable(const char *name,
                                         char *value, size_t capacity)
{
    LONG length;
    if (!name || !value || capacity < 2U) return 0;
    length = GetVar((STRPTR)name, (STRPTR)value, (LONG)capacity - 1L,
                    GVF_GLOBAL_ONLY);
    if (length <= 0L) return 0;
    if ((size_t)length >= capacity) length = (LONG)capacity - 1L;
    value[length] = 0;
    return 1;
}
#endif

int amg_current_gmt_offset_minutes(unsigned long year,
                                   unsigned int month,
                                   unsigned int day,
                                   unsigned int hour,
                                   unsigned int minute,
                                   long *minutes_west)
{
    long locale_minutes = 0L;
    int have_locale;
    if (!minutes_west) return 0;
    have_locale = amg_locale_gmt_offset_minutes(&locale_minutes);
#if AMIGMAIL_AMIGA
    {
        char tzone[128];
        if (read_global_timezone_variable("TZONE", tzone, sizeof(tzone)) &&
            amg_tzone_gmt_offset_minutes(tzone, locale_minutes, have_locale,
                                         year, month, day, hour, minute,
                                         minutes_west))
            return 1;
        if (read_global_timezone_variable("TZ", tzone, sizeof(tzone)) &&
            amg_tzone_gmt_offset_minutes(tzone, locale_minutes, have_locale,
                                         year, month, day, hour, minute,
                                         minutes_west))
            return 1;
    }
#else
    (void)year;
    (void)month;
    (void)day;
    (void)hour;
    (void)minute;
#endif
    if (have_locale) {
        *minutes_west = locale_minutes;
        return 1;
    }
    return 0;
}

int amg_rfc5322_timezone(long minutes_west, char output[6]) {
    unsigned long absolute_minutes;
    unsigned long hours;
    unsigned long minutes;
    char sign;
    int written;
    if (!output) return 0;
    output[0] = 0;
    if (minutes_west < -1439L || minutes_west > 1439L) return 0;
    if (minutes_west > 0L) {
        sign = '-';
        absolute_minutes = (unsigned long)minutes_west;
    } else {
        sign = '+';
        absolute_minutes = (unsigned long)(-minutes_west);
    }
    hours = absolute_minutes / 60UL;
    minutes = absolute_minutes % 60UL;
    written = snprintf(output, 6U, "%c%02lu%02lu", sign, hours, minutes);
    return written == 5;
}
int amg_tr_snprintf(char *output,size_t capacity,long string_id,const char *english_format,...) {
    va_list ap; int r; const char *fmt;
    if (!output || capacity==0U) return -1;
    fmt=amg_tr(string_id,english_format);
    va_start(ap,english_format); r=vsnprintf(output,capacity,fmt,ap); va_end(ap);
    return r;
}
