#include "update.h"
#include "buffer.h"
#include "i18n.h"
#include "tls.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define T(id, en) amg_tr((id), (en))
#define AMG_UPDATE_API_URL \
    "https://api.github.com/repos/Andiweli/AmiMAIL/releases/latest"
#define AMG_UPDATE_DOWNLOAD_PREFIX \
    "https://github.com/Andiweli/AmiMAIL/releases/download/"
#define AMG_UPDATE_JSON_MAX (512UL * 1024UL)
#define AMG_UPDATE_ARCHIVE_MAX (16UL * 1024UL * 1024UL)
#define AMG_UPDATE_HTTP_URL_MAX 2304U
#define AMG_UPDATE_HTTP_HOST_MAX 256U
#define AMG_UPDATE_HTTP_PATH_MAX 2048U
#define AMG_UPDATE_REDIRECT_MAX 5U

#if AMIGMAIL_AMIGA
static int ascii_equal_nocase(char a, char b)
{
    return tolower((unsigned char)a) == tolower((unsigned char)b);
}

static int text_equal_nocase_n(const char *a, const char *b, size_t length)
{
    size_t i;
    for (i = 0; i < length; ++i) {
        if (!a[i] || !b[i] || !ascii_equal_nocase(a[i], b[i])) return 0;
    }
    return 1;
}
#endif

typedef struct AmgParsedVersion {
    unsigned long parts[8];
    size_t count;
    int prerelease;
    unsigned long rc_number;
} AmgParsedVersion;

static int ascii_prefix_nocase(const char *text, const char *prefix)
{
    if (!text || !prefix) return 0;
    while (*prefix) {
        if (!*text ||
            tolower((unsigned char)*text) !=
            tolower((unsigned char)*prefix))
            return 0;
        ++text;
        ++prefix;
    }
    return 1;
}

static int parse_version(const char *text, AmgParsedVersion *version)
{
    const char *cursor;
    size_t used = 0U;
    if (!text || !*text || !version) return 0;
    memset(version, 0, sizeof(*version));
    cursor = text;
    if (*cursor == 'v' || *cursor == 'V') ++cursor;
    if (!isdigit((unsigned char)*cursor)) return 0;

    while (*cursor) {
        unsigned long value = 0UL;
        if (used >= sizeof(version->parts) / sizeof(version->parts[0]) ||
            !isdigit((unsigned char)*cursor))
            return 0;
        while (isdigit((unsigned char)*cursor)) {
            unsigned digit = (unsigned)(*cursor - '0');
            if (value > 1000000UL) return 0;
            value = value * 10UL + (unsigned long)digit;
            ++cursor;
        }
        version->parts[used++] = value;
        if (!*cursor) break;
        if (*cursor == '.') {
            ++cursor;
            if (!isdigit((unsigned char)*cursor)) return 0;
            continue;
        }
        break;
    }
    version->count = used;
    if (!used) return 0;

    while (*cursor == ' ' || *cursor == '-' || *cursor == '_') ++cursor;
    if (!*cursor) return 1;
    if (!ascii_prefix_nocase(cursor, "RC")) return 0;
    cursor += 2;
    version->prerelease = 1;
    if (*cursor) {
        unsigned long number = 0UL;
        if (!isdigit((unsigned char)*cursor)) return 0;
        while (isdigit((unsigned char)*cursor)) {
            unsigned digit = (unsigned)(*cursor - '0');
            if (number > 1000000UL) return 0;
            number = number * 10UL + (unsigned long)digit;
            ++cursor;
        }
        version->rc_number = number;
    }
    return *cursor == 0;
}

int amg_update_is_newer(const char *candidate_tag,
                        const char *current_version)
{
    AmgParsedVersion candidate, current;
    size_t i, max_count;
    if (!parse_version(candidate_tag, &candidate) ||
        !parse_version(current_version, &current))
        return 0;
    max_count = candidate.count > current.count
        ? candidate.count : current.count;
    for (i = 0U; i < max_count; ++i) {
        unsigned long a = i < candidate.count ? candidate.parts[i] : 0UL;
        unsigned long b = i < current.count ? current.parts[i] : 0UL;
        if (a > b) return 1;
        if (a < b) return 0;
    }
    /* For the same numeric version a final release supersedes an RC. */
    if (candidate.prerelease != current.prerelease)
        return current.prerelease && !candidate.prerelease;
    if (candidate.prerelease && current.prerelease)
        return candidate.rc_number > current.rc_number;
    return 0;
}

static int json_extract_string(const unsigned char *json, size_t length,
                               const char *key, char *output,
                               size_t capacity)
{
    size_t key_length, i;
    if (!json || !key || !output || capacity < 2U) return 0;
    output[0] = 0;
    key_length = strlen(key);
    for (i = 0; i + key_length + 3U < length; ++i) {
        size_t p, out = 0U;
        if (json[i] != '"' ||
            memcmp(json + i + 1U, key, key_length) != 0 ||
            json[i + key_length + 1U] != '"')
            continue;
        p = i + key_length + 2U;
        while (p < length && isspace((unsigned char)json[p])) ++p;
        if (p >= length || json[p++] != ':') continue;
        while (p < length && isspace((unsigned char)json[p])) ++p;
        if (p >= length || json[p++] != '"') continue;
        while (p < length) {
            unsigned char c = json[p++];
            if (c == '"') {
                output[out] = 0;
                return 1;
            }
            if (c == '\\') {
                if (p >= length) return 0;
                c = json[p++];
                switch (c) {
                    case '"': case '\\': case '/': break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case 'n': c = '\n'; break;
                    case 'r': c = '\r'; break;
                    case 't': c = '\t'; break;
                    default:
                        /* tag_name is ASCII in our release convention. */
                        return 0;
                }
            }
            if (out + 1U >= capacity) return 0;
            output[out++] = (char)c;
        }
        return 0;
    }
    return 0;
}

int amg_update_parse_latest_json(const unsigned char *json, size_t length,
                                 AmgUpdateInfo *info, AmgError *error)
{
    AmgParsedVersion parsed_version;
    int written;
    if (!json || !info) return AMG_ERR_ARGUMENT;
    memset(info, 0, sizeof(*info));
    memset(&parsed_version, 0, sizeof(parsed_version));
    if (!json_extract_string(json, length, "tag_name",
                             info->tag, sizeof(info->tag)) ||
        !parse_version(info->tag, &parsed_version)) {
        amg_error_set(error, AMG_ERR_PARSE,
                      T(MSG_GITHUB_RESPONSE_DOES_NOT_CONTAIN_A_VALID_RELEASE, "GitHub response does not contain a valid release version."));
        return AMG_ERR_PARSE;
    }
    written = snprintf(info->download_url, sizeof(info->download_url),
                       AMG_UPDATE_DOWNLOAD_PREFIX "%s/AmiMAIL-%s.lha",
                       info->tag, info->tag);
    if (written < 0 || (size_t)written >= sizeof(info->download_url)) {
        amg_error_set(error, AMG_ERR_LIMIT,
                      T(MSG_GITHUB_DOWNLOAD_ADDRESS_IS_TOO_LONG, "GitHub download address is too long."));
        return AMG_ERR_LIMIT;
    }
    return AMG_OK;
}

#if AMIGMAIL_AMIGA

static const unsigned char *find_header_end(const unsigned char *data,
                                            size_t length)
{
    size_t i;
    if (!data) return NULL;
    for (i = 0; i + 3U < length; ++i) {
        if (data[i] == '\r' && data[i + 1U] == '\n' &&
            data[i + 2U] == '\r' && data[i + 3U] == '\n')
            return data + i + 4U;
    }
    return NULL;
}

static int header_value(const unsigned char *data, size_t header_length,
                        const char *name, char *output, size_t capacity)
{
    size_t name_length, position = 0U;
    if (!data || !name || !output || !capacity) return 0;
    output[0] = 0;
    name_length = strlen(name);
    while (position < header_length) {
        size_t line_end = position;
        size_t value_start, value_end, copy_length;
        while (line_end + 1U < header_length &&
               !(data[line_end] == '\r' && data[line_end + 1U] == '\n'))
            ++line_end;
        if (line_end == position) break;
        if (line_end > position + name_length &&
            data[position + name_length] == ':' &&
            text_equal_nocase_n((const char *)data + position,
                                name, name_length)) {
            value_start = position + name_length + 1U;
            while (value_start < line_end &&
                   (data[value_start] == ' ' || data[value_start] == '\t'))
                ++value_start;
            value_end = line_end;
            while (value_end > value_start &&
                   (data[value_end - 1U] == ' ' ||
                    data[value_end - 1U] == '\t'))
                --value_end;
            copy_length = value_end - value_start;
            if (copy_length >= capacity) copy_length = capacity - 1U;
            memcpy(output, data + value_start, copy_length);
            output[copy_length] = 0;
            return 1;
        }
        if (line_end + 2U > header_length) break;
        position = line_end + 2U;
    }
    return 0;
}

static int string_contains_nocase(const char *text, const char *needle)
{
    size_t text_length, needle_length, i;
    if (!text || !needle) return 0;
    text_length = strlen(text);
    needle_length = strlen(needle);
    if (!needle_length || needle_length > text_length) return 0;
    for (i = 0; i + needle_length <= text_length; ++i) {
        if (text_equal_nocase_n(text + i, needle, needle_length)) return 1;
    }
    return 0;
}

static int decode_chunked_body(const unsigned char *data, size_t length,
                               AmgBuffer *output)
{
    size_t position = 0U;
    while (position < length) {
        char number[32], *endptr;
        size_t line_end = position, digits, chunk;
        while (line_end + 1U < length &&
               !(data[line_end] == '\r' && data[line_end + 1U] == '\n'))
            ++line_end;
        if (line_end + 1U >= length) return AMG_ERR_PARSE;
        digits = line_end - position;
        if (!digits || digits >= sizeof(number)) return AMG_ERR_PARSE;
        memcpy(number, data + position, digits);
        number[digits] = 0;
        chunk = strtoul(number, &endptr, 16);
        if (endptr == number) return AMG_ERR_PARSE;
        position = line_end + 2U;
        if (!chunk) return AMG_OK;
        if (chunk > length - position) return AMG_ERR_PARSE;
        if (amg_buffer_append(output, data + position, chunk) != AMG_OK)
            return AMG_ERR_MEMORY;
        position += chunk;
        if (position + 2U > length || data[position] != '\r' ||
            data[position + 1U] != '\n')
            return AMG_ERR_PARSE;
        position += 2U;
    }
    return AMG_ERR_PARSE;
}

static int parse_https_url(const char *url,
                           char *host, size_t host_capacity,
                           char *path, size_t path_capacity)
{
    const char *start, *slash;
    size_t host_length;
    if (!url || strncmp(url, "https://", 8U) != 0 ||
        !host || !host_capacity || !path || path_capacity < 2U)
        return 0;
    start = url + 8U;
    slash = strchr(start, '/');
    host_length = slash ? (size_t)(slash - start) : strlen(start);
    if (!host_length || host_length >= host_capacity ||
        (slash && strlen(slash) >= path_capacity))
        return 0;
    memcpy(host, start, host_length);
    host[host_length] = 0;
    if (slash)
        strcpy(path, slash);
    else
        strcpy(path, "/");
    return 1;
}

static int resolve_redirect(const char *current_url, const char *location,
                            char *next_url, size_t capacity)
{
    char host[AMG_UPDATE_HTTP_HOST_MAX];
    char path[AMG_UPDATE_HTTP_PATH_MAX];
    int written;
    if (!current_url || !location || !*location || !next_url || !capacity)
        return 0;
    if (!strncmp(location, "https://", 8U)) {
        if (strlen(location) >= capacity) return 0;
        strcpy(next_url, location);
        return 1;
    }
    if (location[0] != '/' ||
        !parse_https_url(current_url, host, sizeof(host), path, sizeof(path)))
        return 0;
    written = snprintf(next_url, capacity, "https://%s%s", host, location);
    return written >= 0 && (size_t)written < capacity;
}

static int https_get_once(const char *url, size_t max_body,
                          AmgBuffer *body, int *status,
                          char *location, size_t location_capacity,
                          AmgError *error)
{
    char host[AMG_UPDATE_HTTP_HOST_MAX];
    char path[AMG_UPDATE_HTTP_PATH_MAX];
    char transfer_encoding[64];
    char content_length_text[32];
    AmgTlsConnection *connection = NULL;
    AmgBuffer request, response;
    unsigned char block[4096];
    const unsigned char *body_start;
    size_t header_length, raw_body_length;
    long count;
    int result = AMG_OK;

    if (!parse_https_url(url, host, sizeof(host), path, sizeof(path))) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T(MSG_INVALID_HTTPS_ADDRESS, "Invalid HTTPS address."));
        return AMG_ERR_ARGUMENT;
    }
    if (location && location_capacity) location[0] = 0;
    if (status) *status = 0;
    amg_buffer_init(&request);
    amg_buffer_init(&response);

    connection = amg_tls_connect(host, 443U, 30UL, error);
    if (!connection) {
        result = error && error->code ? error->code : AMG_ERR_TLS;
        goto done;
    }
    if (amg_buffer_append_cstr(&request, "GET ") != AMG_OK ||
        amg_buffer_append_cstr(&request, path) != AMG_OK ||
        amg_buffer_append_cstr(&request, " HTTP/1.1\r\nHost: ") != AMG_OK ||
        amg_buffer_append_cstr(&request, host) != AMG_OK ||
        amg_buffer_append_cstr(&request,
            "\r\nUser-Agent: AmiMail/" AMIMAIL_VERSION
            "\r\nAccept: application/vnd.github+json, application/octet-stream"
            "\r\nConnection: close\r\n\r\n") != AMG_OK) {
        result = AMG_ERR_MEMORY;
        amg_error_set(error, result, T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory."));
        goto done;
    }
    result = amg_tls_write_all(connection, request.data, request.length, error);
    if (result != AMG_OK) goto done;

    while (response.length <= max_body + 65536U) {
        count = amg_tls_read(connection, block, sizeof(block), error);
        if (count <= 0) break;
        result = amg_buffer_append(&response, block, (size_t)count);
        if (result != AMG_OK) {
            amg_error_set(error, result,
                          T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory."));
            goto done;
        }
    }
    if (response.length > max_body + 65536U) {
        result = AMG_ERR_LIMIT;
        amg_error_set(error, result,
                      T(MSG_HTTPS_RESPONSE_IS_TOO_LARGE, "HTTPS response is too large."));
        goto done;
    }
    if (amg_buffer_terminate(&response) != AMG_OK) {
        result = AMG_ERR_MEMORY;
        goto done;
    }
    if (sscanf((const char *)response.data, "HTTP/%*u.%*u %d", status) != 1) {
        result = AMG_ERR_PROTOCOL;
        amg_error_set(error, result,
                      T(MSG_INVALID_HTTPS_RESPONSE, "Invalid HTTPS response."));
        goto done;
    }
    body_start = find_header_end(response.data, response.length);
    if (!body_start) {
        result = AMG_ERR_PROTOCOL;
        amg_error_set(error, result,
                      T(MSG_HTTPS_HEADER_IS_INCOMPLETE, "HTTPS header is incomplete."));
        goto done;
    }
    header_length = (size_t)(body_start - response.data);
    if (location && location_capacity)
        (void)header_value(response.data, header_length,
                           "Location", location, location_capacity);
    if (status && (*status == 301 || *status == 302 || *status == 303 ||
                   *status == 307 || *status == 308)) {
        result = AMG_OK;
        goto done;
    }
    if (!status || *status < 200 || *status >= 300) {
        char message[256];
        result = AMG_ERR_PROTOCOL;
        amg_tr_snprintf(message, sizeof(message), MSG_GITHUB_RETURNED_HTTP_STATUS_VALUE, "GitHub returned HTTP status %d.", status ? *status : 0);
        amg_error_set(error, result, message);
        goto done;
    }
    raw_body_length = response.length - header_length;
    transfer_encoding[0] = 0;
    content_length_text[0] = 0;
    (void)header_value(response.data, header_length, "Transfer-Encoding",
                       transfer_encoding, sizeof(transfer_encoding));
    (void)header_value(response.data, header_length, "Content-Length",
                       content_length_text, sizeof(content_length_text));
    if (string_contains_nocase(transfer_encoding, "chunked")) {
        result = decode_chunked_body(body_start, raw_body_length, body);
    } else if (content_length_text[0]) {
        char *endptr = NULL;
        unsigned long expected = strtoul(content_length_text, &endptr, 10);
        if (endptr == content_length_text || *endptr ||
            expected > (unsigned long)raw_body_length) {
            result = AMG_ERR_PROTOCOL;
        } else {
            result = amg_buffer_append(body, body_start, (size_t)expected);
        }
    } else {
        result = amg_buffer_append(body, body_start, raw_body_length);
    }
    if (result == AMG_OK && body->length > max_body) result = AMG_ERR_LIMIT;
    if (result != AMG_OK && error && error->code == AMG_OK)
        amg_error_set(error, result,
                      result == AMG_ERR_LIMIT
                        ? T(MSG_DOWNLOAD_IS_TOO_LARGE, "Download is too large.")
                        : T(MSG_HTTPS_DATA_COULD_NOT_BE_PARSED, "HTTPS data could not be parsed."));

done:
    if (connection) amg_tls_close(connection);
    amg_buffer_free(&request);
    amg_buffer_free(&response);
    if (result == AMG_OK) amg_error_set(error, AMG_OK, "");
    return result;
}

static int https_get_follow(const char *url, size_t max_body,
                            AmgBuffer *body, AmgError *error)
{
    char current[AMG_UPDATE_HTTP_URL_MAX];
    unsigned redirect;
    if (!url || strlen(url) >= sizeof(current)) return AMG_ERR_LIMIT;
    strcpy(current, url);
    for (redirect = 0U; redirect <= AMG_UPDATE_REDIRECT_MAX; ++redirect) {
        char location[AMG_UPDATE_HTTP_URL_MAX];
        char next[AMG_UPDATE_HTTP_URL_MAX];
        int status = 0;
        int result;
        amg_buffer_free(body);
        amg_buffer_init(body);
        result = https_get_once(current, max_body, body, &status,
                                location, sizeof(location), error);
        if (result != AMG_OK) return result;
        if (status >= 200 && status < 300) return AMG_OK;
        if (!(status == 301 || status == 302 || status == 303 ||
              status == 307 || status == 308) || !location[0] ||
            !resolve_redirect(current, location, next, sizeof(next))) {
            amg_error_set(error, AMG_ERR_PROTOCOL,
                          T(MSG_GITHUB_REDIRECT_IS_INVALID, "GitHub redirect is invalid."));
            return AMG_ERR_PROTOCOL;
        }
        strcpy(current, next);
    }
    amg_error_set(error, AMG_ERR_PROTOCOL,
                  T(MSG_TOO_MANY_GITHUB_REDIRECTS, "Too many GitHub redirects."));
    return AMG_ERR_PROTOCOL;
}

int amg_update_check_latest(AmgUpdateInfo *info, AmgError *error)
{
    AmgBuffer body;
    int result;
    if (!info) return AMG_ERR_ARGUMENT;
    amg_buffer_init(&body);
    result = amg_tls_global_init(error);
    if (result != AMG_OK) return result;
    result = https_get_follow(AMG_UPDATE_API_URL, AMG_UPDATE_JSON_MAX,
                              &body, error);
    if (result == AMG_OK)
        result = amg_update_parse_latest_json(body.data, body.length,
                                              info, error);
    amg_buffer_free(&body);
    amg_tls_global_cleanup();
    return result;
}

int amg_update_download(const char *url, const char *destination,
                        AmgError *error)
{
    AmgBuffer body;
    FILE *file = NULL;
    int result;
    if (!url || !*url || !destination || !*destination)
        return AMG_ERR_ARGUMENT;
    amg_buffer_init(&body);
    result = amg_tls_global_init(error);
    if (result != AMG_OK) return result;
    result = https_get_follow(url, AMG_UPDATE_ARCHIVE_MAX, &body, error);
    if (result == AMG_OK) {
        int write_failed = 0;
        file = fopen(destination, "wb");
        if (!file) {
            write_failed = 1;
        } else {
            if (body.length &&
                fwrite(body.data, 1U, body.length, file) != body.length)
                write_failed = 1;
            if (fclose(file) != 0) write_failed = 1;
            file = NULL;
        }
        if (write_failed) {
            remove(destination);
            result = AMG_ERR_IO;
            amg_error_set(error, result,
                          T(MSG_UPDATE_COULD_NOT_BE_WRITTEN_TO_RAM, "Update could not be written to RAM:."));
        }
    }
    amg_buffer_free(&body);
    amg_tls_global_cleanup();
    return result;
}

#else

int amg_update_check_latest(AmgUpdateInfo *info, AmgError *error)
{
    (void)info;
    amg_error_set(error, AMG_ERR_UNSUPPORTED,
                  T(MSG_UPDATE_CHECK_IS_AVAILABLE_ONLY_ON_AMIGAOS, "Update check is available only on AmigaOS."));
    return AMG_ERR_UNSUPPORTED;
}

int amg_update_download(const char *url, const char *destination,
                        AmgError *error)
{
    (void)url;
    (void)destination;
    amg_error_set(error, AMG_ERR_UNSUPPORTED,
                  T(MSG_UPDATE_DOWNLOAD_IS_AVAILABLE_ONLY_ON_AMIGAOS, "Update download is available only on AmigaOS."));
    return AMG_ERR_UNSUPPORTED;
}

#endif
