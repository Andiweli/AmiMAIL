#include "contacts.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if AMIGMAIL_AMIGA
#include <dos/dos.h>
#include <proto/dos.h>
#endif

#define CONTACTS_HEADER "AMIMAIL-CONTACTS-1"

static void contact_error(AmgError *error, int code, const char *message)
{
    if (!error) return;
    error->code = code;
    snprintf(error->message, sizeof(error->message), "%s", message ? message : "");
}

static int ascii_tolower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int text_equal_ci(const char *a, const char *b)
{
    unsigned char ca, cb;
    if (!a || !b) return a == b;
    while (*a && *b) {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if (ascii_tolower(ca) != ascii_tolower(cb)) return 0;
    }
    return *a == 0 && *b == 0;
}

static void trim_text(char *text)
{
    char *start, *end;
    size_t len;
    if (!text || !*text) return;
    start = text;
    while (*start && isspace((unsigned char)*start)) ++start;
    if (start != text) memmove(text, start, strlen(start) + 1U);
    len = strlen(text);
    end = text + len;
    while (end > text && isspace((unsigned char)end[-1])) --end;
    *end = 0;
}

static void canonical_phone(const char *input, char *output, size_t capacity)
{
    size_t used = 0U;
    if (!capacity) return;
    while (input && *input && used + 1U < capacity) {
        unsigned char c = (unsigned char)*input++;
        if (c >= '0' && c <= '9') output[used++] = (char)c;
        else if (c == '+' && used == 0U) output[used++] = '+';
    }
    output[used] = 0;
}

static int phones_overlap(const AmgContact *a, const AmgContact *b)
{
    char ap[AMG_CONTACT_PHONE_MAX], am[AMG_CONTACT_PHONE_MAX];
    char bp[AMG_CONTACT_PHONE_MAX], bm[AMG_CONTACT_PHONE_MAX];
    canonical_phone(a ? a->phone : "", ap, sizeof(ap));
    canonical_phone(a ? a->mobile : "", am, sizeof(am));
    canonical_phone(b ? b->phone : "", bp, sizeof(bp));
    canonical_phone(b ? b->mobile : "", bm, sizeof(bm));
    if (ap[0] && ((bp[0] && !strcmp(ap, bp)) || (bm[0] && !strcmp(ap, bm)))) return 1;
    if (am[0] && ((bp[0] && !strcmp(am, bp)) || (bm[0] && !strcmp(am, bm)))) return 1;
    return 0;
}

void amg_contact_trim(AmgContact *contact)
{
    if (!contact) return;
    trim_text(contact->first_name);
    trim_text(contact->last_name);
    trim_text(contact->company);
    trim_text(contact->email);
    trim_text(contact->phone);
    trim_text(contact->mobile);
    trim_text(contact->website);
}

int amg_contact_has_data(const AmgContact *contact)
{
    return contact && (contact->first_name[0] || contact->last_name[0] ||
        contact->company[0] || contact->email[0] || contact->phone[0] ||
        contact->mobile[0] || contact->website[0]);
}

void amg_contacts_init(AmgContactBook *book)
{
    if (!book) return;
    memset(book, 0, sizeof(*book));
    book->next_id = 1UL;
}

void amg_contacts_free(AmgContactBook *book)
{
    if (!book) return;
    free(book->items);
    amg_contacts_init(book);
}

static int reserve_contacts(AmgContactBook *book, size_t required)
{
    size_t capacity;
    AmgContact *items;
    if (required <= book->capacity) return AMG_OK;
    capacity = book->capacity ? book->capacity : 32U;
    while (capacity < required) {
        if (capacity > ((size_t)-1) / 2U) return AMG_ERR_LIMIT;
        capacity *= 2U;
    }
    items = (AmgContact *)realloc(book->items, capacity * sizeof(*items));
    if (!items) return AMG_ERR_MEMORY;
    book->items = items;
    book->capacity = capacity;
    return AMG_OK;
}

const AmgContact *amg_contacts_find(const AmgContactBook *book,
                                    unsigned long id)
{
    size_t i;
    if (!book || !id) return NULL;
    for (i = 0U; i < book->count; ++i)
        if (book->items[i].id == id) return &book->items[i];
    return NULL;
}

AmgContact *amg_contacts_find_mutable(AmgContactBook *book,
                                      unsigned long id)
{
    size_t i;
    if (!book || !id) return NULL;
    for (i = 0U; i < book->count; ++i)
        if (book->items[i].id == id) return &book->items[i];
    return NULL;
}

int amg_contacts_is_duplicate(const AmgContactBook *book,
                              const AmgContact *candidate,
                              unsigned long ignore_id)
{
    size_t i;
    int has_name;
    if (!book || !candidate) return 0;
    has_name = candidate->first_name[0] || candidate->last_name[0];
    for (i = 0U; i < book->count; ++i) {
        const AmgContact *existing = &book->items[i];
        if (existing->id == ignore_id) continue;
        if (candidate->email[0] && existing->email[0] &&
            text_equal_ci(candidate->email, existing->email)) return 1;
        if (!candidate->email[0] && has_name && phones_overlap(candidate, existing) &&
            text_equal_ci(candidate->first_name, existing->first_name) &&
            text_equal_ci(candidate->last_name, existing->last_name)) return 1;
        if (!candidate->email[0] && !has_name && candidate->company[0] &&
            existing->company[0] && phones_overlap(candidate, existing) &&
            text_equal_ci(candidate->company, existing->company)) return 1;
    }
    return 0;
}

int amg_contacts_add(AmgContactBook *book, const AmgContact *contact,
                     unsigned long *new_id, AmgError *error)
{
    AmgContact copy;
    int result;
    if (!book || !contact) return AMG_ERR_ARGUMENT;
    copy = *contact;
    copy.id = 0UL;
    amg_contact_trim(&copy);
    if (!amg_contact_has_data(&copy)) {
        contact_error(error, AMG_ERR_ARGUMENT, "Contact is empty.");
        return AMG_ERR_ARGUMENT;
    }
    if (amg_contacts_is_duplicate(book, &copy, 0UL)) {
        contact_error(error, AMG_ERR_ARGUMENT, "Contact already exists.");
        return AMG_ERR_ARGUMENT;
    }
    result = reserve_contacts(book, book->count + 1U);
    if (result != AMG_OK) {
        contact_error(error, result, "Not enough memory for contact.");
        return result;
    }
    if (!book->next_id) book->next_id = 1UL;
    copy.id = book->next_id++;
    book->items[book->count++] = copy;
    if (new_id) *new_id = copy.id;
    contact_error(error, AMG_OK, "");
    return AMG_OK;
}

int amg_contacts_update(AmgContactBook *book, const AmgContact *contact,
                        AmgError *error)
{
    AmgContact *existing;
    AmgContact copy;
    if (!book || !contact || !contact->id) return AMG_ERR_ARGUMENT;
    existing = amg_contacts_find_mutable(book, contact->id);
    if (!existing) {
        contact_error(error, AMG_ERR_ARGUMENT, "Contact was not found.");
        return AMG_ERR_ARGUMENT;
    }
    copy = *contact;
    amg_contact_trim(&copy);
    if (!amg_contact_has_data(&copy)) {
        contact_error(error, AMG_ERR_ARGUMENT, "Contact is empty.");
        return AMG_ERR_ARGUMENT;
    }
    if (amg_contacts_is_duplicate(book, &copy, copy.id)) {
        contact_error(error, AMG_ERR_ARGUMENT, "Contact already exists.");
        return AMG_ERR_ARGUMENT;
    }
    *existing = copy;
    contact_error(error, AMG_OK, "");
    return AMG_OK;
}

int amg_contacts_delete(AmgContactBook *book, unsigned long id,
                        AmgError *error)
{
    size_t i;
    if (!book || !id) return AMG_ERR_ARGUMENT;
    for (i = 0U; i < book->count; ++i) {
        if (book->items[i].id == id) {
            if (i + 1U < book->count)
                memmove(&book->items[i], &book->items[i + 1U],
                        (book->count - i - 1U) * sizeof(book->items[0]));
            --book->count;
            contact_error(error, AMG_OK, "");
            return AMG_OK;
        }
    }
    contact_error(error, AMG_ERR_ARGUMENT, "Contact was not found.");
    return AMG_ERR_ARGUMENT;
}

static const char hex_digits[] = "0123456789abcdef";

static int write_hex(FILE *file, const char *name, const char *text)
{
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    if (fprintf(file, "%s=", name) < 0) return AMG_ERR_IO;
    while (*p) {
        if (fputc(hex_digits[*p >> 4], file) == EOF ||
            fputc(hex_digits[*p & 15U], file) == EOF) return AMG_ERR_IO;
        ++p;
    }
    return fputc('\n', file) == EOF ? AMG_ERR_IO : AMG_OK;
}

static int hex_value(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int decode_hex(const char *hex, char *output, size_t capacity)
{
    size_t used = 0U;
    if (!hex || !output || !capacity) return AMG_ERR_ARGUMENT;
    while (*hex) {
        int high, low;
        if (!hex[1]) return AMG_ERR_PARSE;
        high = hex_value((unsigned char)hex[0]);
        low = hex_value((unsigned char)hex[1]);
        if (high < 0 || low < 0 || used + 1U >= capacity) return AMG_ERR_PARSE;
        output[used++] = (char)((high << 4) | low);
        hex += 2;
    }
    output[used] = 0;
    return AMG_OK;
}

static int path_exists(const char *path)
{
#if AMIGMAIL_AMIGA
    BPTR lock = Lock((CONST_STRPTR)path, ACCESS_READ);
    if (!lock) return 0;
    UnLock(lock);
    return 1;
#else
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
#endif
}

static int delete_path(const char *path)
{
#if AMIGMAIL_AMIGA
    return DeleteFile((CONST_STRPTR)path) ? AMG_OK : AMG_ERR_IO;
#else
    return remove(path) == 0 ? AMG_OK : AMG_ERR_IO;
#endif
}

static int rename_path(const char *from, const char *to)
{
#if AMIGMAIL_AMIGA
    return Rename((CONST_STRPTR)from, (CONST_STRPTR)to) ? AMG_OK : AMG_ERR_IO;
#else
    return rename(from, to) == 0 ? AMG_OK : AMG_ERR_IO;
#endif
}

/* Keep the previous contact file recoverable until the new file is in place.
 * This avoids losing the address book if the final rename fails. */
static int replace_file(const char *temporary, const char *path)
{
    char backup[1024];
    int had_old;
    int written;
    if (snprintf(backup, sizeof(backup), "%s.old", path) >=
        (int)sizeof(backup)) return AMG_ERR_LIMIT;

    had_old = path_exists(path);
    if (path_exists(backup)) (void)delete_path(backup);
    if (had_old && rename_path(path, backup) != AMG_OK) return AMG_ERR_IO;

    written = rename_path(temporary, path);
    if (written == AMG_OK) {
        if (had_old && path_exists(backup)) (void)delete_path(backup);
        return AMG_OK;
    }

    if (had_old) (void)rename_path(backup, path);
    return written;
}

#if AMIGMAIL_AMIGA
static void ensure_contacts_drawer(void)
{
    BPTR lock = Lock((CONST_STRPTR)"ENVARC:AmiMail", ACCESS_READ);
    if (lock) {
        UnLock(lock);
        return;
    }
    lock = CreateDir((CONST_STRPTR)"ENVARC:AmiMail");
    if (lock) UnLock(lock);
}
#endif

int amg_contacts_save(const char *path, const AmgContactBook *book,
                      AmgError *error)
{
    char temporary[1024];
    FILE *file;
    size_t i;
    int result = AMG_OK;
    if (!path || !book) return AMG_ERR_ARGUMENT;
#if AMIGMAIL_AMIGA
    ensure_contacts_drawer();
#endif
    if (!strcmp(path, AMG_CONTACTS_DEFAULT_PATH))
        snprintf(temporary, sizeof(temporary), "%s", AMG_CONTACTS_TEMP_PATH);
    else
        snprintf(temporary, sizeof(temporary), "%s.new", path);
    file = fopen(temporary, "wb");
    if (!file) {
        contact_error(error, AMG_ERR_IO, "Contact file could not be created.");
        return AMG_ERR_IO;
    }
    if (fprintf(file, "%s\nnext_id=%lu\n", CONTACTS_HEADER, book->next_id) < 0)
        result = AMG_ERR_IO;
    for (i = 0U; result == AMG_OK && i < book->count; ++i) {
        const AmgContact *c = &book->items[i];
        if (fprintf(file, "contact=%lu\n", c->id) < 0 ||
            write_hex(file, "first_name", c->first_name) != AMG_OK ||
            write_hex(file, "last_name", c->last_name) != AMG_OK ||
            write_hex(file, "company", c->company) != AMG_OK ||
            write_hex(file, "email", c->email) != AMG_OK ||
            write_hex(file, "phone", c->phone) != AMG_OK ||
            write_hex(file, "mobile", c->mobile) != AMG_OK ||
            write_hex(file, "website", c->website) != AMG_OK ||
            fputs("end\n", file) == EOF) result = AMG_ERR_IO;
    }
    if (fclose(file) != 0 && result == AMG_OK) result = AMG_ERR_IO;
    if (result == AMG_OK) result = replace_file(temporary, path);
    if (result != AMG_OK) {
#if AMIGMAIL_AMIGA
        DeleteFile((CONST_STRPTR)temporary);
#else
        remove(temporary);
#endif
        contact_error(error, result, "Contact file could not be saved.");
        return result;
    }
    contact_error(error, AMG_OK, "");
    return AMG_OK;
}

static int parse_field_line(AmgContact *contact, const char *key,
                            const char *value)
{
    if (!strcmp(key, "first_name")) return decode_hex(value, contact->first_name, sizeof(contact->first_name));
    if (!strcmp(key, "last_name")) return decode_hex(value, contact->last_name, sizeof(contact->last_name));
    if (!strcmp(key, "company")) return decode_hex(value, contact->company, sizeof(contact->company));
    if (!strcmp(key, "email")) return decode_hex(value, contact->email, sizeof(contact->email));
    if (!strcmp(key, "phone")) return decode_hex(value, contact->phone, sizeof(contact->phone));
    if (!strcmp(key, "mobile")) return decode_hex(value, contact->mobile, sizeof(contact->mobile));
    if (!strcmp(key, "website")) return decode_hex(value, contact->website, sizeof(contact->website));
    return AMG_OK;
}

int amg_contacts_load(const char *path, AmgContactBook *book, AmgError *error)
{
    FILE *file;
    char line[2048];
    AmgContactBook loaded;
    AmgContact current;
    int in_contact = 0;
    int result = AMG_OK;
    if (!path || !book) return AMG_ERR_ARGUMENT;
    file = fopen(path, "rb");
    if (!file) {
        amg_contacts_free(book);
        amg_contacts_init(book);
        contact_error(error, AMG_OK, "");
        return AMG_OK;
    }
    amg_contacts_init(&loaded);
    memset(&current, 0, sizeof(current));
    if (!fgets(line, sizeof(line), file)) result = AMG_ERR_PARSE;
    if (result == AMG_OK) {
        trim_text(line);
        if (strcmp(line, CONTACTS_HEADER)) result = AMG_ERR_PARSE;
    }
    while (result == AMG_OK && fgets(line, sizeof(line), file)) {
        char *eq;
        trim_text(line);
        if (!line[0]) continue;
        if (!strncmp(line, "next_id=", 8U) && !in_contact) {
            char *end = NULL;
            unsigned long value = strtoul(line + 8U, &end, 10);
            if (!end || *end || !value) result = AMG_ERR_PARSE;
            else loaded.next_id = value;
            continue;
        }
        if (!strncmp(line, "contact=", 8U)) {
            char *end = NULL;
            unsigned long id;
            if (in_contact) { result = AMG_ERR_PARSE; break; }
            memset(&current, 0, sizeof(current));
            id = strtoul(line + 8U, &end, 10);
            if (!end || *end || !id) { result = AMG_ERR_PARSE; break; }
            current.id = id;
            in_contact = 1;
            continue;
        }
        if (!strcmp(line, "end")) {
            int reserve_result;
            if (!in_contact || !amg_contact_has_data(&current) ||
                amg_contacts_find(&loaded, current.id)) {
                result = AMG_ERR_PARSE;
                break;
            }
            reserve_result = reserve_contacts(&loaded, loaded.count + 1U);
            if (reserve_result != AMG_OK) {
                result = reserve_result;
                break;
            }
            loaded.items[loaded.count++] = current;
            if (current.id >= loaded.next_id) {
                if (current.id == ~0UL) {
                    result = AMG_ERR_LIMIT;
                    break;
                }
                loaded.next_id = current.id + 1UL;
            }
            in_contact = 0;
            continue;
        }
        if (!in_contact) continue;
        eq = strchr(line, '=');
        if (!eq) { result = AMG_ERR_PARSE; break; }
        *eq++ = 0;
        if (parse_field_line(&current, line, eq) != AMG_OK) {
            result = AMG_ERR_PARSE;
            break;
        }
    }
    if (in_contact) result = AMG_ERR_PARSE;
    if (ferror(file) && result == AMG_OK) result = AMG_ERR_IO;
    fclose(file);
    if (result != AMG_OK) {
        amg_contacts_free(&loaded);
        contact_error(error, result, "Contact file is invalid or could not be read.");
        return result;
    }
    amg_contacts_free(book);
    *book = loaded;
    contact_error(error, AMG_OK, "");
    return AMG_OK;
}
