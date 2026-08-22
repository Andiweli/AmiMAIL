#include "mailto.h"
#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if AMIGMAIL_AMIGA
#include <dos/dos.h>
#include <dos/dostags.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <workbench/startup.h>
#include <proto/dos.h>
#include <proto/exec.h>
#endif

#define T(id, en) amg_tr((id), (en))

#define AMG_MAILTO_SCHEME "mailto:"
#define AMG_MAILTO_SCHEME_LENGTH 7U
#define AMG_MAILTO_IPC_PORT "AMIMAIL.MAILTO"
#define AMG_MAILTO_IPC_MAGIC 0x414D4D4CUL /* 'AMML' */
#define AMG_MAILTO_IPC_MAX 32768U
#define AMG_MAILTO_FILE_PREFIX "--amm-mailto-file="
#define AMG_MAILTO_TEMP_PREFIX "T:AmiMailMailto."

static int ascii_equal_nocase(const char *a, const char *b)
{
    unsigned char ca, cb;
    if (!a || !b) return 0;
    while (*a && *b) {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + ('a' - 'A'));
        if (ca != cb) return 0;
    }
    return *a == 0 && *b == 0;
}

static int ascii_prefix_nocase(const char *text, const char *prefix)
{
    unsigned char a, b;
    if (!text || !prefix) return 0;
    while (*prefix) {
        if (!*text) return 0;
        a = (unsigned char)*text++;
        b = (unsigned char)*prefix++;
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return 1;
}

static int hex_value(unsigned char c)
{
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    return -1;
}

static char *duplicate_text(const char *text)
{
    char *copy;
    size_t length;
    if (!text) text = "";
    length = strlen(text);
    copy = (char *)malloc(length + 1U);
    if (!copy) return NULL;
    memcpy(copy, text, length + 1U);
    return copy;
}

static int decode_component(const char *source, size_t length,
                            int header_value, char **output,
                            AmgError *error)
{
    char *decoded;
    size_t in_pos, out_pos = 0U;
    if (!output) return AMG_ERR_ARGUMENT;
    *output = NULL;
    decoded = (char *)malloc(length + 1U);
    if (!decoded) {
        amg_error_set(error, AMG_ERR_MEMORY, T(MSG_NOT_ENOUGH_MEMORY_FOR_MAILTO_URL, "Not enough memory for mailto URL."));
        return AMG_ERR_MEMORY;
    }
    for (in_pos = 0U; in_pos < length; ++in_pos) {
        unsigned char c = (unsigned char)source[in_pos];
        if (c == '%') {
            int hi, lo;
            if (in_pos + 2U >= length) {
                free(decoded);
                amg_error_set(error, AMG_ERR_PARSE, T(MSG_INVALID_PERCENT_ESCAPE_IN_MAILTO_URL, "Invalid percent escape in mailto URL."));
                return AMG_ERR_PARSE;
            }
            hi = hex_value((unsigned char)source[in_pos + 1U]);
            lo = hex_value((unsigned char)source[in_pos + 2U]);
            if (hi < 0 || lo < 0) {
                free(decoded);
                amg_error_set(error, AMG_ERR_PARSE, T(MSG_INVALID_PERCENT_ESCAPE_IN_MAILTO_URL, "Invalid percent escape in mailto URL."));
                return AMG_ERR_PARSE;
            }
            c = (unsigned char)((hi << 4) | lo);
            in_pos += 2U;
            if (c == 0U) {
                free(decoded);
                amg_error_set(error, AMG_ERR_PARSE, T(MSG_NUL_BYTE_IS_NOT_ALLOWED_IN_MAILTO_URL, "NUL byte is not allowed in mailto URL."));
                return AMG_ERR_PARSE;
            }
        }
        /* RFC 6068 does not define '+' as form-url-encoded space.  Keep it
         * literally.  Header line breaks are flattened so a mailto URL can
         * never inject additional RFC 5322 headers into the composer. */
        if (header_value && (c == '\r' || c == '\n')) c = ' ';
        decoded[out_pos++] = (char)c;
    }
    decoded[out_pos] = 0;
    *output = decoded;
    return AMG_OK;
}

static int append_recipient(char **destination, const char *value,
                            AmgError *error)
{
    char *combined;
    size_t old_length, value_length;
    if (!destination || !value) return AMG_ERR_ARGUMENT;
    if (!*destination || !**destination) {
        free(*destination);
        *destination = duplicate_text(value);
        if (!*destination) {
            amg_error_set(error, AMG_ERR_MEMORY, T(MSG_NOT_ENOUGH_MEMORY_FOR_MAILTO_RECIPIENT, "Not enough memory for mailto recipient."));
            return AMG_ERR_MEMORY;
        }
        return AMG_OK;
    }
    if (!*value) return AMG_OK;
    old_length = strlen(*destination);
    value_length = strlen(value);
    if (old_length > (size_t)-1 - value_length - 3U) {
        amg_error_set(error, AMG_ERR_LIMIT, T(MSG_MAILTO_RECIPIENT_LIST_IS_TOO_LONG, "mailto recipient list is too long."));
        return AMG_ERR_LIMIT;
    }
    combined = (char *)malloc(old_length + value_length + 3U);
    if (!combined) {
        amg_error_set(error, AMG_ERR_MEMORY, T(MSG_NOT_ENOUGH_MEMORY_FOR_MAILTO_RECIPIENTS, "Not enough memory for mailto recipients."));
        return AMG_ERR_MEMORY;
    }
    memcpy(combined, *destination, old_length);
    combined[old_length] = ',';
    combined[old_length + 1U] = ' ';
    memcpy(combined + old_length + 2U, value, value_length + 1U);
    free(*destination);
    *destination = combined;
    return AMG_OK;
}

void amg_mailto_request_init(AmgMailtoRequest *request)
{
    if (request) memset(request, 0, sizeof(*request));
}

void amg_mailto_request_clear(AmgMailtoRequest *request)
{
    if (!request) return;
    free(request->to_utf8);
    free(request->cc_utf8);
    free(request->bcc_utf8);
    free(request->subject_utf8);
    free(request->body_utf8);
    memset(request, 0, sizeof(*request));
}

int amg_mailto_parse(const char *url, AmgMailtoRequest *request,
                     AmgError *error)
{
    const char *cursor, *query, *fragment;
    size_t address_length;
    int result = AMG_OK;
    char *decoded = NULL;

    if (!url || !request || !ascii_prefix_nocase(url, AMG_MAILTO_SCHEME)) {
        amg_error_set(error, AMG_ERR_ARGUMENT, T(MSG_EXPECTED_A_MAILTO_URL, "Expected a mailto: URL."));
        return AMG_ERR_ARGUMENT;
    }
    if (strlen(url) > AMG_MAILTO_IPC_MAX) {
        amg_error_set(error, AMG_ERR_LIMIT, T(MSG_MAILTO_URL_IS_TOO_LONG, "mailto: URL is too long."));
        return AMG_ERR_LIMIT;
    }
    amg_mailto_request_clear(request);
    cursor = url + AMG_MAILTO_SCHEME_LENGTH;
    query = strchr(cursor, '?');
    fragment = strchr(cursor, '#');
    if (fragment && (!query || fragment < query)) query = NULL;
    if (query)
        address_length = (size_t)(query - cursor);
    else if (fragment)
        address_length = (size_t)(fragment - cursor);
    else
        address_length = strlen(cursor);

    if (address_length) {
        result = decode_component(cursor, address_length, 1, &decoded, error);
        if (result != AMG_OK) goto fail;
        result = append_recipient(&request->to_utf8, decoded, error);
        free(decoded);
        decoded = NULL;
        if (result != AMG_OK) goto fail;
    }

    if (query) {
        const char *end = fragment && fragment > query ? fragment : url + strlen(url);
        cursor = query + 1;
        while (cursor < end) {
            const char *pair_end = cursor;
            const char *equals;
            char *key = NULL;
            char *value = NULL;
            while (pair_end < end && *pair_end != '&') ++pair_end;
            equals = cursor;
            while (equals < pair_end && *equals != '=') ++equals;
            result = decode_component(cursor, (size_t)(equals - cursor), 0,
                                      &key, error);
            if (result != AMG_OK) {
                free(key);
                goto fail;
            }
            if (equals < pair_end) ++equals;
            result = decode_component(equals, (size_t)(pair_end - equals),
                                      !ascii_equal_nocase(key, "body"),
                                      &value, error);
            if (result != AMG_OK) {
                free(key);
                free(value);
                goto fail;
            }

            if (ascii_equal_nocase(key, "to"))
                result = append_recipient(&request->to_utf8, value, error);
            else if (ascii_equal_nocase(key, "cc"))
                result = append_recipient(&request->cc_utf8, value, error);
            else if (ascii_equal_nocase(key, "bcc"))
                result = append_recipient(&request->bcc_utf8, value, error);
            else if (ascii_equal_nocase(key, "subject") &&
                     !request->subject_utf8) {
                request->subject_utf8 = value;
                value = NULL;
            } else if (ascii_equal_nocase(key, "body") &&
                       !request->body_utf8) {
                request->body_utf8 = value;
                value = NULL;
            }
            free(key);
            free(value);
            if (result != AMG_OK) goto fail;
            cursor = pair_end < end ? pair_end + 1 : end;
        }
    }
    return AMG_OK;

fail:
    free(decoded);
    amg_mailto_request_clear(request);
    return result;
}

static const char *find_text_nocase(const char *text, const char *needle)
{
    size_t needle_length;
    const char *cursor;
    if (!text || !needle || !*needle) return NULL;
    needle_length = strlen(needle);
    for (cursor = text; *cursor; ++cursor) {
        size_t i;
        for (i = 0U; i < needle_length; ++i) {
            unsigned char a = (unsigned char)cursor[i];
            unsigned char b = (unsigned char)needle[i];
            if (!a) break;
            if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
            if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
            if (a != b) break;
        }
        if (i == needle_length) return cursor;
    }
    return NULL;
}

static char *copy_argument_token(const char *text, const char *start,
                                 int raw_text)
{
    const char *end;
    char quote = 0;
    size_t length;
    char *copy;
    if (!text || !start) return NULL;
    if (start > text && (start[-1] == '"' || start[-1] == '\''))
        quote = start[-1];
    end = start;
    if (raw_text) {
        while (*end) {
            if (quote) {
                if (*end == quote) break;
            } else if (*end == '\r' || *end == '\n' || *end == ' ' ||
                       *end == '\t') {
                break;
            }
            ++end;
        }
    } else {
        end += strlen(end);
    }
    while (end > start &&
           (end[-1] == '\r' || end[-1] == '\n' || end[-1] == '"' ||
            end[-1] == '\''))
        --end;
    length = (size_t)(end - start);
    copy = (char *)malloc(length + 1U);
    if (!copy) return NULL;
    memcpy(copy, start, length);
    copy[length] = 0;
    return copy;
}

static const char *find_mailto_in_text(const char *text)
{
    return find_text_nocase(text, AMG_MAILTO_SCHEME);
}

const char *amg_mailto_find_argument(int argc, char **argv)
{
    int i;

#if AMIGMAIL_AMIGA
    /* AmigaOS uses argc == 0 for Workbench-style launches.  In that case
     * argv is actually a WBStartup pointer.  Scan all WBArgs as a fallback;
     * browser "Command" launches normally use the CLI path below. */
    if (argc == 0 && argv) {
        const struct WBStartup *startup = (const struct WBStartup *)argv;
        if (startup->sm_NumArgs > 0 && startup->sm_NumArgs <= 64 &&
            startup->sm_ArgList) {
            for (i = 0; i < startup->sm_NumArgs; ++i) {
                const char *name = (const char *)startup->sm_ArgList[i].wa_Name;
                const char *found = find_mailto_in_text(name);
                if (found) return found;
            }
        }
        return NULL;
    }
#endif

    if (argc <= 0 || !argv) return NULL;
    for (i = 0; i < argc; ++i) {
        const char *found = find_mailto_in_text(argv[i]);
        if (found) return found;
    }
    return NULL;
}

static const char *find_private_file_argument(int argc, char **argv,
                                              const char *raw_args,
                                              int *from_raw)
{
    int i;
    if (from_raw) *from_raw = 0;
#if AMIGMAIL_AMIGA
    if (argc == 0 && argv) {
        const struct WBStartup *startup = (const struct WBStartup *)argv;
        if (startup->sm_NumArgs > 0 && startup->sm_NumArgs <= 64 &&
            startup->sm_ArgList) {
            for (i = 0; i < startup->sm_NumArgs; ++i) {
                const char *name = (const char *)startup->sm_ArgList[i].wa_Name;
                const char *found = find_text_nocase(name, AMG_MAILTO_FILE_PREFIX);
                if (found) return found + strlen(AMG_MAILTO_FILE_PREFIX);
            }
        }
    } else
#endif
    if (argc > 0 && argv) {
        for (i = 0; i < argc; ++i) {
            const char *found = find_text_nocase(argv[i], AMG_MAILTO_FILE_PREFIX);
            if (found) return found + strlen(AMG_MAILTO_FILE_PREFIX);
        }
    }
    if (raw_args) {
        const char *found = find_text_nocase(raw_args, AMG_MAILTO_FILE_PREFIX);
        if (found) {
            if (from_raw) *from_raw = 1;
            return found + strlen(AMG_MAILTO_FILE_PREFIX);
        }
    }
    return NULL;
}

static char *load_mailto_temp_file(const char *path, AmgError *error)
{
    FILE *file;
    long size;
    char *url;
    size_t length;
    if (!path || !*path) return NULL;
#if AMIGMAIL_AMIGA
    if (!ascii_prefix_nocase(path, AMG_MAILTO_TEMP_PREFIX)) return NULL;
#endif
    file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
        size > (long)AMG_MAILTO_IPC_MAX || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        amg_error_set(error, AMG_ERR_IO, T(MSG_COULD_NOT_READ_MAILTO_HAND_OFF_FILE, "Could not read mailto hand-off file."));
        return NULL;
    }
    url = (char *)malloc((size_t)size + 1U);
    if (!url) {
        fclose(file);
        amg_error_set(error, AMG_ERR_MEMORY, T(MSG_NOT_ENOUGH_MEMORY_FOR_MAILTO_URL, "Not enough memory for mailto URL."));
        return NULL;
    }
    length = fread(url, 1U, (size_t)size, file);
    fclose(file);
    if (length != (size_t)size) {
        free(url);
        amg_error_set(error, AMG_ERR_IO, T(MSG_COULD_NOT_READ_MAILTO_HAND_OFF_FILE, "Could not read mailto hand-off file."));
        return NULL;
    }
    url[length] = 0;
    while (length && (url[length - 1U] == '\r' || url[length - 1U] == '\n'))
        url[--length] = 0;
    if (!ascii_prefix_nocase(url, AMG_MAILTO_SCHEME)) {
        free(url);
        amg_error_set(error, AMG_ERR_PARSE, T(MSG_INVALID_MAILTO_HAND_OFF_FILE, "Invalid mailto hand-off file."));
        return NULL;
    }
    (void)remove(path);
    return url;
}

char *amg_mailto_startup_url(int argc, char **argv, const char *raw_args,
                             int *detached_child, AmgError *error)
{
    const char *found;
    char *copy;
    int from_raw = 0;
    if (detached_child) *detached_child = 0;

    found = find_private_file_argument(argc, argv, raw_args, &from_raw);
    if (found) {
        char *path = copy_argument_token(from_raw ? raw_args : found,
                                         found, from_raw);
        char *url;
        if (!path) {
            amg_error_set(error, AMG_ERR_MEMORY,
                          T(MSG_NOT_ENOUGH_MEMORY_FOR_MAILTO_HAND_OFF_PATH, "Not enough memory for mailto hand-off path."));
            return NULL;
        }
        url = load_mailto_temp_file(path, error);
        free(path);
        if (url && detached_child) *detached_child = 1;
        return url;
    }

    found = amg_mailto_find_argument(argc, argv);
    if (found) {
        /* argv elements have already been split by the C runtime. */
        copy = copy_argument_token(found, found, 0);
        if (!copy)
            amg_error_set(error, AMG_ERR_MEMORY,
                          T(MSG_NOT_ENOUGH_MEMORY_FOR_MAILTO_URL, "Not enough memory for mailto URL."));
        return copy;
    }

    /* GetArgStr() is the authoritative raw AmigaDOS argument string.  Some
     * browser launchers reach the process through a path where argc/argv do
     * not expose the command parameter as expected, so use it as a second
     * independent source instead of guessing launcher-specific argc rules. */
    found = find_mailto_in_text(raw_args);
    if (found) {
        copy = copy_argument_token(raw_args, found, 1);
        if (!copy)
            amg_error_set(error, AMG_ERR_MEMORY,
                          T(MSG_NOT_ENOUGH_MEMORY_FOR_MAILTO_URL, "Not enough memory for mailto URL."));
        return copy;
    }
    return NULL;
}

#if AMIGMAIL_AMIGA

int amg_mailto_spawn_detached(const char *url)
{
    static unsigned long sequence;
    char temp_path[96];
    char command[384];
    char program_name[128];
    char program_path[320];
    const char *base_name = "AmiMail";
    FILE *file;
    BPTR input;
    LONG system_result;
    size_t length;

    if (!url || !ascii_prefix_nocase(url, AMG_MAILTO_SCHEME)) return 0;
    length = strlen(url);
    if (!length || length > AMG_MAILTO_IPC_MAX) return 0;

    snprintf(temp_path, sizeof(temp_path), "%s%08lx.%04lx",
             AMG_MAILTO_TEMP_PREFIX,
             (unsigned long)(uintptr_t)FindTask(NULL), ++sequence & 0xffffUL);
    file = fopen(temp_path, "wb");
    if (!file) return 0;
    if (fwrite(url, 1U, length, file) != length || fclose(file) != 0) {
        (void)remove(temp_path);
        return 0;
    }

    program_name[0] = 0;
    if (GetProgramName((STRPTR)program_name, (LONG)sizeof(program_name))) {
        char *part = (char *)FilePart((STRPTR)program_name);
        if (part && *part) base_name = part;
    }
    program_path[0] = 0;
    {
        BPTR program_dir = GetProgramDir();
        if (program_dir &&
            NameFromLock(program_dir, (STRPTR)program_path,
                         (LONG)sizeof(program_path)) &&
            AddPart((STRPTR)program_path, (STRPTR)base_name,
                    (LONG)sizeof(program_path))) {
            /* absolute path ready */
        } else {
            snprintf(program_path, sizeof(program_path), "%s", base_name);
        }
    }
    if (strchr(program_path, '"') ||
        snprintf(command, sizeof(command),
                 "\"%s\" %s%s", program_path,
                 AMG_MAILTO_FILE_PREFIX, temp_path) >= (int)sizeof(command)) {
        (void)remove(temp_path);
        return 0;
    }

    input = Open((STRPTR)"NIL:", MODE_OLDFILE);
    system_result = SystemTags((STRPTR)command,
                               SYS_Asynch, TRUE,
                               input ? SYS_Input : TAG_IGNORE, input,
                               SYS_Output, 0L,
                               SYS_Error, 0L,
                               TAG_DONE);
    if (system_result == -1) {
        if (input) Close(input);
        (void)remove(temp_path);
        return 0;
    }
    /* For an asynchronous SystemTags() call AmigaDOS owns and closes the
     * supplied input handle after a successful launch. */
    return 1;
}

typedef struct AmgMailtoIpcMessage {
    struct Message message;
    ULONG magic;
    char url[1];
} AmgMailtoIpcMessage;

struct AmgMailtoServer {
    struct MsgPort *port;
};

AmgMailtoServer *amg_mailto_server_create(void)
{
    AmgMailtoServer *server;
    struct MsgPort *existing;
    int added = 0;

    server = (AmgMailtoServer *)calloc(1, sizeof(*server));
    if (!server) return NULL;
    server->port = CreateMsgPort();
    if (!server->port) {
        free(server);
        return NULL;
    }
    server->port->mp_Node.ln_Name = (char *)AMG_MAILTO_IPC_PORT;
    Forbid();
    existing = FindPort((STRPTR)AMG_MAILTO_IPC_PORT);
    if (!existing) {
        AddPort(server->port);
        added = 1;
    }
    Permit();
    if (!added) {
        DeleteMsgPort(server->port);
        free(server);
        return NULL;
    }
    return server;
}

void amg_mailto_server_destroy(AmgMailtoServer *server)
{
    struct Message *message;
    if (!server) return;
    if (server->port) {
        Forbid();
        RemPort(server->port);
        Permit();
        while ((message = GetMsg(server->port)) != NULL)
            FreeVec(message);
        DeleteMsgPort(server->port);
        server->port = NULL;
    }
    free(server);
}

unsigned long amg_mailto_server_signal_mask(const AmgMailtoServer *server)
{
    if (!server || !server->port) return 0UL;
    return 1UL << (unsigned long)server->port->mp_SigBit;
}

int amg_mailto_server_receive(AmgMailtoServer *server, char **url_out)
{
    AmgMailtoIpcMessage *ipc;
    size_t payload_capacity, length;
    char *copy;
    if (url_out) *url_out = NULL;
    if (!server || !server->port || !url_out) return 0;
    while ((ipc = (AmgMailtoIpcMessage *)GetMsg(server->port)) != NULL) {
        if (ipc->message.mn_Length >= sizeof(AmgMailtoIpcMessage) &&
            ipc->magic == AMG_MAILTO_IPC_MAGIC) {
            payload_capacity = (size_t)ipc->message.mn_Length -
                               (sizeof(AmgMailtoIpcMessage) - 1U);
            length = 0U;
            while (length < payload_capacity && ipc->url[length]) ++length;
            if (length < payload_capacity) {
                copy = (char *)malloc(length + 1U);
                if (copy) {
                    memcpy(copy, ipc->url, length + 1U);
                    FreeVec(ipc);
                    *url_out = copy;
                    return 1;
                }
            }
        }
        FreeVec(ipc);
    }
    return 0;
}

int amg_mailto_forward_to_running(const char *url)
{
    AmgMailtoIpcMessage *ipc;
    struct MsgPort *port;
    size_t length, total;
    int sent = 0;
    if (!url) return 0;
    length = strlen(url);
    if (!length || length > AMG_MAILTO_IPC_MAX) return 0;
    total = sizeof(AmgMailtoIpcMessage) + length;
    if (total > 65535U) return 0;
    ipc = (AmgMailtoIpcMessage *)AllocVec((ULONG)total,
                                          MEMF_PUBLIC | MEMF_CLEAR);
    if (!ipc) return 0;
    ipc->message.mn_Node.ln_Type = NT_MESSAGE;
    ipc->message.mn_Length = (UWORD)total;
    ipc->magic = AMG_MAILTO_IPC_MAGIC;
    memcpy(ipc->url, url, length + 1U);

    /* FindPort() and PutMsg() are kept in one Forbid()/Permit() region so the
     * receiver cannot remove its public port in between.  The receiver owns
     * and frees the message after PutMsg(), allowing this process to exit
     * immediately without waiting for a reply. */
    Forbid();
    port = FindPort((STRPTR)AMG_MAILTO_IPC_PORT);
    if (port) {
        PutMsg(port, &ipc->message);
        sent = 1;
    }
    Permit();
    if (!sent) FreeVec(ipc);
    return sent;
}

#else

int amg_mailto_spawn_detached(const char *url)
{
    (void)url;
    return 0;
}

struct AmgMailtoServer {
    int unavailable;
};

AmgMailtoServer *amg_mailto_server_create(void)
{
    return NULL;
}

void amg_mailto_server_destroy(AmgMailtoServer *server)
{
    free(server);
}

unsigned long amg_mailto_server_signal_mask(const AmgMailtoServer *server)
{
    (void)server;
    return 0UL;
}

int amg_mailto_server_receive(AmgMailtoServer *server, char **url_out)
{
    (void)server;
    if (url_out) *url_out = NULL;
    return 0;
}

int amg_mailto_forward_to_running(const char *url)
{
    (void)url;
    return 0;
}

#endif
