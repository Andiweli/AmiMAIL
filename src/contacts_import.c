#include "contacts.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TextBuffer {
    char *data;
    size_t length;
    size_t capacity;
} TextBuffer;

typedef struct CsvRecord {
    char **fields;
    size_t count;
    size_t capacity;
} CsvRecord;

typedef struct VcfPhone {
    char group[32];
    char params[160];
    char value[AMG_CONTACT_PHONE_MAX];
} VcfPhone;

typedef struct VcfLabel {
    char group[32];
    char value[64];
} VcfLabel;

static void import_error(AmgError *error, int code, const char *message)
{
    if (!error) return;
    error->code = code;
    snprintf(error->message, sizeof(error->message), "%s", message ? message : "");
}

static int ascii_tolower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int equals_ci(const char *a, const char *b)
{
    while (a && b && *a && *b) {
        if (ascii_tolower((unsigned char)*a++) != ascii_tolower((unsigned char)*b++)) return 0;
    }
    return a && b && *a == *b;
}

static int contains_ci(const char *text, const char *needle)
{
    size_t nlen;
    const char *p;
    if (!text || !needle || !*needle) return 0;
    nlen = strlen(needle);
    for (p = text; *p; ++p) {
        size_t i;
        for (i = 0U; i < nlen; ++i) {
            if (!p[i] || ascii_tolower((unsigned char)p[i]) !=
                         ascii_tolower((unsigned char)needle[i])) break;
        }
        if (i == nlen) return 1;
    }
    return 0;
}

static void trim_in_place(char *text)
{
    char *start;
    size_t len;
    if (!text) return;
    start = text;
    while (*start && isspace((unsigned char)*start)) ++start;
    if (start != text) memmove(text, start, strlen(start) + 1U);
    len = strlen(text);
    while (len && isspace((unsigned char)text[len - 1U])) text[--len] = 0;
}

static void copy_field(char *destination, size_t capacity, const char *source)
{
    size_t length;
    if (!destination || !capacity) return;
    source = source ? source : "";
    length = strlen(source);
    if (length >= capacity) length = capacity - 1U;
    if (length) memcpy(destination, source, length);
    destination[length] = 0;
    trim_in_place(destination);
}

static void copy_first_multi_value(char *destination, size_t capacity,
                                   const char *source)
{
    const char *separator;
    size_t length;
    if (!destination || !capacity) return;
    source = source ? source : "";
    separator = strstr(source, " ::: ");
    length = separator ? (size_t)(separator - source) : strlen(source);
    if (length >= capacity) length = capacity - 1U;
    memcpy(destination, source, length);
    destination[length] = 0;
    trim_in_place(destination);
}

static int buffer_reserve(TextBuffer *buffer, size_t required)
{
    size_t capacity;
    char *data;
    if (required <= buffer->capacity) return AMG_OK;
    capacity = buffer->capacity ? buffer->capacity : 128U;
    while (capacity < required) {
        if (capacity > ((size_t)-1) / 2U) return AMG_ERR_LIMIT;
        capacity *= 2U;
    }
    data = (char *)realloc(buffer->data, capacity);
    if (!data) return AMG_ERR_MEMORY;
    buffer->data = data;
    buffer->capacity = capacity;
    return AMG_OK;
}

static int buffer_append_char(TextBuffer *buffer, char c)
{
    int result = buffer_reserve(buffer, buffer->length + 2U);
    if (result != AMG_OK) return result;
    buffer->data[buffer->length++] = c;
    buffer->data[buffer->length] = 0;
    return AMG_OK;
}

static int buffer_append_text(TextBuffer *buffer, const char *text)
{
    size_t length = text ? strlen(text) : 0U;
    int result = buffer_reserve(buffer, buffer->length + length + 1U);
    if (result != AMG_OK) return result;
    if (length) memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = 0;
    return AMG_OK;
}

static void buffer_reset(TextBuffer *buffer)
{
    if (!buffer) return;
    buffer->length = 0U;
    if (buffer->data) buffer->data[0] = 0;
}

static void buffer_free(TextBuffer *buffer)
{
    if (!buffer) return;
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

static void csv_record_free(CsvRecord *record)
{
    size_t i;
    if (!record) return;
    for (i = 0U; i < record->count; ++i) free(record->fields[i]);
    free(record->fields);
    memset(record, 0, sizeof(*record));
}

static int csv_record_add(CsvRecord *record, const char *text)
{
    char *copy;
    if (record->count == record->capacity) {
        size_t capacity = record->capacity ? record->capacity * 2U : 32U;
        char **fields = (char **)realloc(record->fields, capacity * sizeof(*fields));
        if (!fields) return AMG_ERR_MEMORY;
        record->fields = fields;
        record->capacity = capacity;
    }
    copy = (char *)malloc(strlen(text ? text : "") + 1U);
    if (!copy) return AMG_ERR_MEMORY;
    strcpy(copy, text ? text : "");
    record->fields[record->count++] = copy;
    return AMG_OK;
}

/* RFC4180-style reader: quotes, doubled quotes and embedded newlines are
 * handled because Google Contacts exports can contain multiline address data
 * even when AmiMail deliberately ignores those address columns. */
static int csv_read_record(FILE *file, CsvRecord *record)
{
    TextBuffer field;
    int quoted = 0;
    int have_data = 0;
    int c;
    memset(record, 0, sizeof(*record));
    memset(&field, 0, sizeof(field));
    while ((c = fgetc(file)) != EOF) {
        have_data = 1;
        if (quoted) {
            if (c == '"') {
                int next = fgetc(file);
                if (next == '"') {
                    if (buffer_append_char(&field, '"') != AMG_OK) goto memory_error;
                } else {
                    quoted = 0;
                    if (next != EOF) ungetc(next, file);
                }
            } else if (buffer_append_char(&field, (char)c) != AMG_OK) {
                goto memory_error;
            }
            continue;
        }
        if (c == '"' && field.length == 0U) {
            quoted = 1;
        } else if (c == ',') {
            if (csv_record_add(record, field.data ? field.data : "") != AMG_OK) goto memory_error;
            buffer_reset(&field);
        } else if (c == '\r' || c == '\n') {
            if (c == '\r') {
                int next = fgetc(file);
                if (next != '\n' && next != EOF) ungetc(next, file);
            }
            if (csv_record_add(record, field.data ? field.data : "") != AMG_OK) goto memory_error;
            buffer_free(&field);
            return 1;
        } else if (buffer_append_char(&field, (char)c) != AMG_OK) {
            goto memory_error;
        }
    }
    if (quoted) {
        csv_record_free(record);
        buffer_free(&field);
        return AMG_ERR_PARSE;
    }
    if (have_data || field.length || record->count) {
        if (csv_record_add(record, field.data ? field.data : "") != AMG_OK) goto memory_error;
        buffer_free(&field);
        return 1;
    }
    buffer_free(&field);
    return 0;

memory_error:
    csv_record_free(record);
    buffer_free(&field);
    return AMG_ERR_MEMORY;
}

static int csv_header_index(const CsvRecord *header, const char *name)
{
    size_t i;
    if (!header || !name) return -1;
    for (i = 0U; i < header->count; ++i)
        if (equals_ci(header->fields[i], name)) return (int)i;
    return -1;
}

static const char *csv_value(const CsvRecord *record, int index)
{
    return record && index >= 0 && (size_t)index < record->count
        ? record->fields[index] : "";
}

static int parse_numbered_header(const char *header, const char *prefix,
                                 const char *suffix, unsigned *number)
{
    const char *p;
    char *end = NULL;
    unsigned long value;
    size_t prefix_len = strlen(prefix);
    if (strncmp(header, prefix, prefix_len)) return 0;
    p = header + prefix_len;
    value = strtoul(p, &end, 10);
    if (!end || end == p || strcmp(end, suffix) || !value || value > 99UL) return 0;
    if (number) *number = (unsigned)value;
    return 1;
}

static int header_for_number(const CsvRecord *header, const char *prefix,
                             unsigned number, const char *suffix)
{
    char wanted[64];
    snprintf(wanted, sizeof(wanted), "%s%u%s", prefix, number, suffix);
    return csv_header_index(header, wanted);
}

static int label_is_mobile(const char *label)
{
    return contains_ci(label, "mobile") || contains_ci(label, "cell") ||
           contains_ci(label, "mobil");
}

static void csv_contact_from_record(const CsvRecord *header,
                                    const CsvRecord *record,
                                    AmgContact *contact)
{
    int first = csv_header_index(header, "First Name");
    int last = csv_header_index(header, "Last Name");
    int company = csv_header_index(header, "Organization Name");
    size_t i;
    memset(contact, 0, sizeof(*contact));
    copy_field(contact->first_name, sizeof(contact->first_name), csv_value(record, first));
    copy_field(contact->last_name, sizeof(contact->last_name), csv_value(record, last));
    copy_field(contact->company, sizeof(contact->company), csv_value(record, company));

    for (i = 0U; i < header->count && !contact->email[0]; ++i) {
        unsigned number;
        if (parse_numbered_header(header->fields[i], "E-mail ", " - Value", &number))
            copy_first_multi_value(contact->email, sizeof(contact->email), csv_value(record, (int)i));
    }

    for (i = 0U; i < header->count; ++i) {
        unsigned number;
        int label_index;
        const char *value;
        const char *label;
        if (!parse_numbered_header(header->fields[i], "Phone ", " - Value", &number)) continue;
        value = csv_value(record, (int)i);
        if (!value[0]) continue;
        label_index = header_for_number(header, "Phone ", number, " - Label");
        label = csv_value(record, label_index);
        if (label_is_mobile(label)) {
            if (!contact->mobile[0]) copy_first_multi_value(contact->mobile, sizeof(contact->mobile), value);
        } else if (!contact->phone[0]) {
            copy_first_multi_value(contact->phone, sizeof(contact->phone), value);
        }
    }

    for (i = 0U; i < header->count && !contact->website[0]; ++i) {
        unsigned number;
        if (parse_numbered_header(header->fields[i], "Website ", " - Value", &number))
            copy_first_multi_value(contact->website, sizeof(contact->website), csv_value(record, (int)i));
    }
    amg_contact_trim(contact);
}

static int import_contact(AmgContactBook *book, AmgContact *contact,
                          AmgContactImportResult *result, AmgError *error)
{
    int add_result;
    ++result->records;
    amg_contact_trim(contact);
    if (!amg_contact_has_data(contact)) {
        ++result->skipped;
        return AMG_OK;
    }
    if (amg_contacts_is_duplicate(book, contact, 0UL)) {
        ++result->duplicates;
        return AMG_OK;
    }
    add_result = amg_contacts_add(book, contact, NULL, error);
    if (add_result != AMG_OK) return add_result;
    ++result->imported;
    return AMG_OK;
}

int amg_contacts_import_csv(const char *path, AmgContactBook *book,
                            AmgContactImportResult *result,
                            AmgError *error)
{
    FILE *file;
    CsvRecord header;
    int read_result;
    if (!path || !book || !result) return AMG_ERR_ARGUMENT;
    memset(result, 0, sizeof(*result));
    file = fopen(path, "rb");
    if (!file) {
        import_error(error, AMG_ERR_IO, "CSV contact file could not be opened.");
        return AMG_ERR_IO;
    }
    read_result = csv_read_record(file, &header);
    if (read_result != 1) {
        fclose(file);
        import_error(error, AMG_ERR_PARSE, "CSV contact file has no valid header.");
        return AMG_ERR_PARSE;
    }
    if (header.count && (unsigned char)header.fields[0][0] == 0xEFU &&
        (unsigned char)header.fields[0][1] == 0xBBU &&
        (unsigned char)header.fields[0][2] == 0xBFU)
        memmove(header.fields[0], header.fields[0] + 3U,
                strlen(header.fields[0] + 3U) + 1U);
    {
        size_t i;
        int supported = csv_header_index(&header, "First Name") >= 0 ||
                        csv_header_index(&header, "Last Name") >= 0 ||
                        csv_header_index(&header, "Organization Name") >= 0;
        for (i = 0U; !supported && i < header.count; ++i) {
            unsigned number;
            if (parse_numbered_header(header.fields[i], "E-mail ",
                                      " - Value", &number) ||
                parse_numbered_header(header.fields[i], "Phone ",
                                      " - Value", &number) ||
                parse_numbered_header(header.fields[i], "Website ",
                                      " - Value", &number))
                supported = 1;
        }
        if (!supported) {
            csv_record_free(&header);
            fclose(file);
            import_error(error, AMG_ERR_PARSE,
                         "CSV contact header is not supported.");
            return AMG_ERR_PARSE;
        }
    }
    for (;;) {
        CsvRecord record;
        AmgContact contact;
        int imp;
        read_result = csv_read_record(file, &record);
        if (read_result == 0) break;
        if (read_result < 0) {
            csv_record_free(&header);
            fclose(file);
            import_error(error, read_result, "CSV contact file is malformed.");
            return read_result;
        }
        csv_contact_from_record(&header, &record, &contact);
        imp = import_contact(book, &contact, result, error);
        csv_record_free(&record);
        if (imp != AMG_OK) {
            csv_record_free(&header);
            fclose(file);
            return imp;
        }
    }
    csv_record_free(&header);
    fclose(file);
    import_error(error, AMG_OK, "");
    return AMG_OK;
}

static void vcard_unescape(const char *source, char *destination, size_t capacity)
{
    size_t used = 0U;
    if (!capacity) return;
    while (source && *source && used + 1U < capacity) {
        if (*source == '\\' && source[1]) {
            ++source;
            if (*source == 'n' || *source == 'N') destination[used++] = ' ';
            else destination[used++] = *source;
            ++source;
        } else {
            destination[used++] = *source++;
        }
    }
    destination[used] = 0;
    trim_in_place(destination);
}

static void vcard_component(const char *value, unsigned wanted,
                            char *destination, size_t capacity)
{
    TextBuffer part;
    unsigned component = 0U;
    int escaped = 0;
    memset(&part, 0, sizeof(part));
    while (value && *value) {
        char c = *value++;
        if (escaped) {
            if (component == wanted) {
                if (c == 'n' || c == 'N') c = ' ';
                if (buffer_append_char(&part, c) != AMG_OK) break;
            }
            escaped = 0;
        } else if (c == '\\') {
            escaped = 1;
        } else if (c == ';') {
            if (component == wanted) break;
            ++component;
        } else if (component == wanted) {
            if (buffer_append_char(&part, c) != AMG_OK) break;
        }
    }
    copy_field(destination, capacity, part.data ? part.data : "");
    buffer_free(&part);
}

static void property_parts(const char *line, char *group, size_t group_capacity,
                           char *name, size_t name_capacity,
                           char *params, size_t params_capacity,
                           const char **value)
{
    const char *colon = strchr(line, ':');
    const char *semi;
    const char *dot;
    size_t left_len;
    char left[256];
    if (group_capacity) group[0] = 0;
    if (name_capacity) name[0] = 0;
    if (params_capacity) params[0] = 0;
    if (value) *value = NULL;
    if (!colon) return;
    left_len = (size_t)(colon - line);
    if (left_len >= sizeof(left)) left_len = sizeof(left) - 1U;
    memcpy(left, line, left_len);
    left[left_len] = 0;
    semi = strchr(left, ';');
    if (semi) {
        copy_field(params, params_capacity, semi + 1U);
        left[(size_t)(semi - left)] = 0;
    }
    dot = strrchr(left, '.');
    if (dot) {
        size_t glen = (size_t)(dot - left);
        if (glen >= group_capacity) glen = group_capacity ? group_capacity - 1U : 0U;
        if (group_capacity) { memcpy(group, left, glen); group[glen] = 0; }
        copy_field(name, name_capacity, dot + 1U);
    } else {
        copy_field(name, name_capacity, left);
    }
    if (value) *value = colon + 1U;
}

static const char *group_label(const VcfLabel *labels, size_t label_count,
                               const char *group)
{
    size_t i;
    if (!group || !*group) return "";
    for (i = 0U; i < label_count; ++i)
        if (equals_ci(labels[i].group, group)) return labels[i].value;
    return "";
}

static int process_vcard_lines(char **lines, size_t line_count,
                               AmgContactBook *book,
                               AmgContactImportResult *result,
                               AmgError *error)
{
    AmgContact contact;
    VcfPhone phones[24];
    VcfLabel labels[24];
    size_t phone_count = 0U, label_count = 0U, i;
    memset(&contact, 0, sizeof(contact));
    for (i = 0U; i < line_count; ++i) {
        char group[32], name[64], params[160];
        const char *value;
        property_parts(lines[i], group, sizeof(group), name, sizeof(name),
                       params, sizeof(params), &value);
        if (!value) continue;
        if (equals_ci(name, "N")) {
            vcard_component(value, 1U, contact.first_name, sizeof(contact.first_name));
            vcard_component(value, 0U, contact.last_name, sizeof(contact.last_name));
        } else if (equals_ci(name, "ORG") && !contact.company[0]) {
            vcard_component(value, 0U, contact.company, sizeof(contact.company));
        } else if (equals_ci(name, "EMAIL") && !contact.email[0]) {
            vcard_unescape(value, contact.email, sizeof(contact.email));
        } else if (equals_ci(name, "URL") && !contact.website[0]) {
            vcard_unescape(value, contact.website, sizeof(contact.website));
        } else if (equals_ci(name, "TEL") && phone_count < sizeof(phones)/sizeof(phones[0])) {
            copy_field(phones[phone_count].group, sizeof(phones[phone_count].group), group);
            copy_field(phones[phone_count].params, sizeof(phones[phone_count].params), params);
            vcard_unescape(value, phones[phone_count].value, sizeof(phones[phone_count].value));
            ++phone_count;
        } else if (equals_ci(name, "X-ABLabel") && group[0] &&
                   label_count < sizeof(labels)/sizeof(labels[0])) {
            copy_field(labels[label_count].group, sizeof(labels[label_count].group), group);
            vcard_unescape(value, labels[label_count].value, sizeof(labels[label_count].value));
            ++label_count;
        }
    }
    for (i = 0U; i < phone_count; ++i) {
        const char *label = group_label(labels, label_count, phones[i].group);
        int mobile = contains_ci(phones[i].params, "CELL") || label_is_mobile(label);
        if (mobile) {
            if (!contact.mobile[0]) copy_field(contact.mobile, sizeof(contact.mobile), phones[i].value);
        } else if (!contact.phone[0]) {
            copy_field(contact.phone, sizeof(contact.phone), phones[i].value);
        }
    }
    return import_contact(book, &contact, result, error);
}

static int line_array_add(char ***lines, size_t *count, size_t *capacity,
                          const char *text)
{
    char *copy;
    if (*count == *capacity) {
        size_t cap = *capacity ? *capacity * 2U : 16U;
        char **new_lines = (char **)realloc(*lines, cap * sizeof(*new_lines));
        if (!new_lines) return AMG_ERR_MEMORY;
        *lines = new_lines;
        *capacity = cap;
    }
    copy = (char *)malloc(strlen(text ? text : "") + 1U);
    if (!copy) return AMG_ERR_MEMORY;
    strcpy(copy, text ? text : "");
    (*lines)[(*count)++] = copy;
    return AMG_OK;
}

static void line_array_free(char **lines, size_t count)
{
    size_t i;
    for (i = 0U; i < count; ++i) free(lines[i]);
    free(lines);
}

int amg_contacts_import_vcf(const char *path, AmgContactBook *book,
                            AmgContactImportResult *result,
                            AmgError *error)
{
    FILE *file;
    char physical[2048];
    TextBuffer logical;
    char **card_lines = NULL;
    size_t card_count = 0U, card_capacity = 0U;
    int in_card = 0;
    int result_code = AMG_OK;
    if (!path || !book || !result) return AMG_ERR_ARGUMENT;
    memset(result, 0, sizeof(*result));
    memset(&logical, 0, sizeof(logical));
    file = fopen(path, "rb");
    if (!file) {
        import_error(error, AMG_ERR_IO, "VCF contact file could not be opened.");
        return AMG_ERR_IO;
    }
    while (fgets(physical, sizeof(physical), file)) {
        size_t len = strlen(physical);
        while (len && (physical[len - 1U] == '\n' || physical[len - 1U] == '\r'))
            physical[--len] = 0;
        if ((physical[0] == ' ' || physical[0] == '\t') && logical.length) {
            if (buffer_append_text(&logical, physical + 1U) != AMG_OK) {
                result_code = AMG_ERR_MEMORY; break;
            }
            continue;
        }
        if (logical.length) {
            const char *line = logical.data;
            if (equals_ci(line, "BEGIN:VCARD")) {
                line_array_free(card_lines, card_count);
                card_lines = NULL; card_count = card_capacity = 0U;
                in_card = 1;
            } else if (equals_ci(line, "END:VCARD")) {
                if (in_card) {
                    result_code = process_vcard_lines(card_lines, card_count,
                                                      book, result, error);
                    if (result_code != AMG_OK) break;
                }
                line_array_free(card_lines, card_count);
                card_lines = NULL; card_count = card_capacity = 0U;
                in_card = 0;
            } else if (in_card && line_array_add(&card_lines, &card_count,
                                                 &card_capacity, line) != AMG_OK) {
                result_code = AMG_ERR_MEMORY; break;
            }
        }
        buffer_reset(&logical);
        if (buffer_append_text(&logical, physical) != AMG_OK) {
            result_code = AMG_ERR_MEMORY; break;
        }
    }
    if (result_code == AMG_OK && logical.length) {
        const char *line = logical.data;
        if (equals_ci(line, "END:VCARD") && in_card) {
            result_code = process_vcard_lines(card_lines, card_count,
                                              book, result, error);
            in_card = 0;
        } else if (in_card) {
            if (line_array_add(&card_lines, &card_count, &card_capacity, line) != AMG_OK)
                result_code = AMG_ERR_MEMORY;
        }
    }
    if (result_code == AMG_OK && in_card) result_code = AMG_ERR_PARSE;
    if (ferror(file) && result_code == AMG_OK) result_code = AMG_ERR_IO;
    line_array_free(card_lines, card_count);
    buffer_free(&logical);
    fclose(file);
    if (result_code != AMG_OK) {
        import_error(error, result_code,
                     result_code == AMG_ERR_PARSE
                         ? "VCF contact file is malformed."
                         : "VCF contact file could not be imported.");
        return result_code;
    }
    import_error(error, AMG_OK, "");
    return AMG_OK;
}

static int extension_is(const char *path, const char *extension)
{
    const char *dot = path ? strrchr(path, '.') : NULL;
    return dot && equals_ci(dot, extension);
}

int amg_contacts_import_file(const char *path, AmgContactBook *book,
                             AmgContactImportResult *result,
                             AmgError *error)
{
    FILE *file;
    char probe[32];
    size_t read;
    if (!path || !book || !result) return AMG_ERR_ARGUMENT;
    if (extension_is(path, ".csv")) return amg_contacts_import_csv(path, book, result, error);
    if (extension_is(path, ".vcf")) return amg_contacts_import_vcf(path, book, result, error);
    file = fopen(path, "rb");
    if (!file) {
        import_error(error, AMG_ERR_IO, "Contact file could not be opened.");
        return AMG_ERR_IO;
    }
    read = fread(probe, 1U, sizeof(probe) - 1U, file);
    fclose(file);
    probe[read] = 0;
    if (contains_ci(probe, "BEGIN:VCARD")) return amg_contacts_import_vcf(path, book, result, error);
    return amg_contacts_import_csv(path, book, result, error);
}
