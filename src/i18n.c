#include "i18n.h"
#include "amigmail.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#if AMIGMAIL_AMIGA
#include <exec/libraries.h>
#include <libraries/locale.h>
#include <proto/exec.h>
#include <proto/locale.h>
#include <utility/tagitem.h>
struct LocaleBase *LocaleBase = NULL;
static struct Catalog *catalog = NULL;
static long locale_gmt_offset_minutes = 0L;
static int locale_gmt_offset_valid = 0;
#endif
void amg_i18n_init(void) {
#if AMIGMAIL_AMIGA
    struct TagItem tags[4];
    if (LocaleBase) return;
    LocaleBase=(struct LocaleBase *)OpenLibrary((CONST_STRPTR)"locale.library",38UL);
    if (!LocaleBase) return;
    {
        struct Locale *current_locale = OpenLocale(NULL);
        if (current_locale) {
            locale_gmt_offset_minutes = (long)current_locale->loc_GMTOffset;
            locale_gmt_offset_valid = 1;
            CloseLocale(current_locale);
        }
    }
    tags[0].ti_Tag=OC_BuiltInLanguage; tags[0].ti_Data=(ULONG)(uintptr_t)"english";
    tags[1].ti_Tag=OC_BuiltInCodeSet; tags[1].ti_Data=0UL;
    /* Require the current catalog generation. locale.library caches catalogs
     * across application restarts, so without OC_Version an older cached
     * AmiMAIL.catalog can keep serving English fallbacks for newly added IDs. */
    tags[2].ti_Tag=OC_Version; tags[2].ti_Data=6UL;
    tags[3].ti_Tag=TAG_DONE; tags[3].ti_Data=0UL;
    catalog=OpenCatalogA(NULL,(STRPTR)"AmiMAIL.catalog",tags);
#endif
}
void amg_i18n_cleanup(void) {
#if AMIGMAIL_AMIGA
    if (catalog) { CloseCatalog(catalog); catalog=NULL; }
    if (LocaleBase) { CloseLibrary((struct Library *)LocaleBase); LocaleBase=NULL; }
    locale_gmt_offset_minutes = 0L;
    locale_gmt_offset_valid = 0;
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
    if (!locale_gmt_offset_valid) return 0;
    *minutes_west = locale_gmt_offset_minutes;
    return 1;
#else
    return 0;
#endif
}
int amg_tr_snprintf(char *output,size_t capacity,long string_id,const char *english_format,...) {
    va_list ap; int r; const char *fmt;
    if (!output || capacity==0U) return -1;
    fmt=amg_tr(string_id,english_format);
    va_start(ap,english_format); r=vsnprintf(output,capacity,fmt,ap); va_end(ap);
    return r;
}
