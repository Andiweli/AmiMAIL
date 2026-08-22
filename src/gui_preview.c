#include "gui_internal.h"
#include "buffer.h"
#include "codec.h"
#include "imap_parser.h"
#include "mime.h"
#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if AMIGMAIL_AMIGA
#include <clib/alib_protos.h>
#include <dos/dos.h>
#include <exec/libraries.h>
#include <gadgets/texteditor.h>
#include <inline/macros.h>
#include <libraries/asl.h>
#include <proto/asl.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <utility/tagitem.h>

#define T(id, en) amg_tr((id), (en))

/* Minimaler openurl.library-Aufruf ohne Abhaengigkeit vom OpenURL-SDK.
 * URL_OpenA() liegt bei der klassischen API am Library-Vektor 0x1e. */
extern struct Library *OpenURLBase;
#define AMG_URL_OpenA(url, tags) \
    LP2(0x1e, ULONG, URL_OpenA, STRPTR, (url), a0, \
        struct TagItem *, (tags), a1, , OpenURLBase)

static int ascii_prefix_ci(const char *text, const char *prefix)
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

static int url_prefix_length(const char *text)
{
    if (ascii_prefix_ci(text, "https://")) return 8;
    if (ascii_prefix_ci(text, "http://")) return 7;
    if (ascii_prefix_ci(text, "ftp://")) return 6;
    if (ascii_prefix_ci(text, "mailto:")) return 7;
    if (ascii_prefix_ci(text, "www.")) return 4;
    return 0;
}

static int url_boundary_before(const char *text, size_t pos)
{
    unsigned char c;
    if (!text || pos == 0) return 1;
    c = (unsigned char)text[pos - 1U];
    return c <= ' ' || c == '<' || c == '(' || c == '[' || c == '{' ||
           c == '"' || c == '\'';
}

static size_t url_token_end(const char *text, size_t start)
{
    size_t end = start;
    unsigned char c;
    while (text && text[end]) {
        c = (unsigned char)text[end];
        if (c <= ' ' || c == '<' || c == '>' || c == '"' || c == '\'')
            break;
        ++end;
    }
    while (end > start) {
        c = (unsigned char)text[end - 1U];
        if (c == '.' || c == ',' || c == ';' || c == '!' ||
            c == ')' || c == ']' || c == '}')
            --end;
        else
            break;
    }
    return end;
}

static int decorate_preview_links(const char *text, AmgBuffer *styled)
{
    size_t pos = 0, end;
    int result = AMG_OK;
    if (!styled) return AMG_ERR_ARGUMENT;
    if (!text) text = "";
    while (text[pos] && result == AMG_OK) {
        if (url_boundary_before(text, pos) && url_prefix_length(text + pos)) {
            end = url_token_end(text, pos);
            if (end > pos) {
                const unsigned char underline[2] = {0x1bU, 'u'};
                const unsigned char normal[2] = {0x1bU, 'n'};
                result = amg_buffer_append(styled, underline, sizeof(underline));
                if (result == AMG_OK)
                    result = amg_buffer_append(
                        styled, (const unsigned char *)text + pos, end - pos);
                if (result == AMG_OK)
                    result = amg_buffer_append(styled, normal, sizeof(normal));
                pos = end;
                continue;
            }
        }
        result = amg_buffer_append_char(styled, (unsigned char)text[pos++]);
    }
    return result;
}

static int extract_clicked_url(const struct ClickMessage *clickmsg,
                               char output[GUI_URL_MAX])
{
    const char *line;
    size_t length, pos, start, end, used;
    if (!clickmsg || !clickmsg->LineContents || !output) return 0;
    line = (const char *)clickmsg->LineContents;
    length = strlen(line);
    pos = (size_t)clickmsg->ClickPosition;
    if (pos > length) pos = length;
    if (pos == length && pos) --pos;

    start = pos;
    while (start > 0U) {
        unsigned char c = (unsigned char)line[start - 1U];
        if (c <= ' ' || c == '<' || c == '>' || c == '"' || c == '\'')
            break;
        --start;
    }
    while (start < length &&
           (line[start] == '(' || line[start] == '[' || line[start] == '{'))
        ++start;

    end = url_token_end(line, start);
    if (end <= start || !url_prefix_length(line + start)) return 0;

    used = end - start;
    if (ascii_prefix_ci(line + start, "www.")) {
        static const char prefix[] = "http://";
        if (sizeof(prefix) - 1U + used + 1U > GUI_URL_MAX) return 0;
        memcpy(output, prefix, sizeof(prefix) - 1U);
        memcpy(output + sizeof(prefix) - 1U, line + start, used);
        output[sizeof(prefix) - 1U + used] = 0;
    } else {
        if (used + 1U > GUI_URL_MAX) return 0;
        memcpy(output, line + start, used);
        output[used] = 0;
    }
    return 1;
}

static ULONG preview_url_doubleclick_subentry(struct Hook *hook,
                                               Object *object,
                                               APTR message)
{
    AmgGui *gui = hook ? (AmgGui *)hook->h_Data : NULL;
    struct ClickMessage *clickmsg = (struct ClickMessage *)message;
    char url[GUI_URL_MAX];
    (void)object;

    if (!gui || !extract_clicked_url(clickmsg, url))
        return FALSE;

    /* URL_OpenA() darf nicht direkt aus dem TextEditor-DoubleClickHook
     * gestartet werden. Beim Start eines Browsers kann openurl.library
     * synchron warten; innerhalb des Gadget-Hooks blockiert das die
     * ReAction-Eingabeverarbeitung. Deshalb nur die URL vormerken und
     * nach RA_HandleInput() im normalen GUI-Kontext oeffnen. */
    if (!OpenURLBase)
        return TRUE;

    strncpy(gui->pending_preview_url, url,
            sizeof(gui->pending_preview_url) - 1U);
    gui->pending_preview_url[sizeof(gui->pending_preview_url) - 1U] = 0;
    gui->pending_preview_url_ready = 1;

    /* Der DoubleClickHook kann erst am Ende eines ReAction-Inputzyklus
     * aufgerufen werden. Ein eigenes Exec-Signal weckt den Hauptloop dann
     * sofort wieder auf, ohne URL_OpenA() reentrant aus dem Hook zu starten. */
    if (gui->preview_url_signal_task && gui->preview_url_signal_mask)
        Signal(gui->preview_url_signal_task, gui->preview_url_signal_mask);
    return TRUE;
}

void open_pending_preview_url(AmgGui *gui)
{
    struct TagItem tags[1];
    char url[GUI_URL_MAX];

    if (!gui || !gui->pending_preview_url_ready) return;

    strncpy(url, gui->pending_preview_url, sizeof(url) - 1U);
    url[sizeof(url) - 1U] = 0;
    gui->pending_preview_url_ready = 0;
    gui->pending_preview_url[0] = 0;

    if (!gui->running || !OpenURLBase || !url[0]) return;

    tags[0].ti_Tag = TAG_END;
    tags[0].ti_Data = 0;
    (void)AMG_URL_OpenA((STRPTR)url, tags);
}

void init_preview_url_hook(AmgGui *gui)
{
    if (!gui) return;
    memset(&gui->preview_url_hook, 0, sizeof(gui->preview_url_hook));
    gui->preview_url_hook.h_Entry = (__typeof__(gui->preview_url_hook.h_Entry))HookEntry;
    gui->preview_url_hook.h_SubEntry =
        (__typeof__(gui->preview_url_hook.h_SubEntry))preview_url_doubleclick_subentry;
    gui->preview_url_hook.h_Data = gui;
    gui->pending_preview_url[0] = 0;
    gui->pending_preview_url_ready = 0;
    gui->preview_url_signal_bit = -1;
    gui->preview_url_signal_mask = 0;
    gui->preview_url_signal_task = NULL;
}

static ULONG count_preview_lines(const char *text)
{
    ULONG lines = 1;
    const unsigned char *cursor = (const unsigned char *)(text ? text : "");
    while (*cursor) {
        if (*cursor == '\n')
            ++lines;
        else if (*cursor == '\r' && cursor[1] != '\n')
            ++lines;
        ++cursor;
    }
    return lines ? lines : 1;
}

void sync_preview_scroller(AmgGui *gui, int reset_top)
{
    if (!gui || !gui->preview_gadget || !gui->preview_scroller ||
        !gui->window) return;
    sync_texteditor_scroller(gui->window, gui->preview_gadget,
                             gui->preview_scroller,
                             gui->preview_line_count, reset_top);
}

void handle_preview_scroller(AmgGui *gui)
{
    if (!gui || !gui->preview_gadget || !gui->preview_scroller ||
        !gui->window) return;
    handle_texteditor_scroller(gui->window, gui->preview_gadget,
                               gui->preview_scroller,
                               gui->preview_line_count);
}

void set_preview_local(AmgGui *gui, const char *local)
{
    AmgBuffer styled;
    const char *contents = local ? local : "";
    if (!gui || !gui->preview_gadget) return;

    gui->preview_line_count = count_preview_lines(contents);
    amg_buffer_init(&styled);
    if (decorate_preview_links(contents, &styled) == AMG_OK &&
        amg_buffer_terminate(&styled) == AMG_OK)
        contents = (const char *)styled.data;

    if (gui->window)
        SetGadgetAttrs(gui->preview_gadget, gui->window, NULL,
                       GA_TEXTEDITOR_Contents, (ULONG)(uintptr_t)contents,
                       GA_TEXTEDITOR_Prop_First, 0,
                       TAG_DONE);
    else
        SetAttrs((Object *)gui->preview_gadget,
                 GA_TEXTEDITOR_Contents, (ULONG)(uintptr_t)contents,
                 GA_TEXTEDITOR_Prop_First, 0,
                 TAG_DONE);
    sync_preview_scroller(gui, 1);
    amg_buffer_free(&styled);
}

static void set_preview_utf8(AmgGui *gui, const unsigned char *utf8,
                             size_t length)
{
    AmgBuffer local;
    amg_buffer_init(&local);
    if (utf8 && amg_utf8_to_local((const char *)utf8, &local) == AMG_OK &&
        amg_buffer_terminate(&local) == AMG_OK)
        set_preview_local(gui, (const char *)local.data);
    else
        set_preview_local(gui, T(MSG_MESSAGE_COULD_NOT_BE_DISPLAYED, "Message could not be displayed."));
    amg_buffer_free(&local);
    (void)length;
}

static int append_preview_header(AmgBuffer *preview, const char *name,
                                 const char *value)
{
    AmgBuffer decoded;
    int result;
    amg_buffer_init(&decoded);
    result = amg_buffer_append_cstr(preview, name);
    if (result == AMG_OK) {
        if (value && *value && amg_rfc2047_decode(value, &decoded) == AMG_OK)
            result = amg_buffer_append(preview, decoded.data, decoded.length);
        else
            result = amg_buffer_append_cstr(preview, "-");
    }
    if (result == AMG_OK) result = amg_buffer_append_char(preview, '\n');
    amg_buffer_free(&decoded);
    return result;
}

int display_message_payload(AmgGui *gui, const unsigned char *payload,
                                   size_t payload_length, AmgError *error)
{
    AmgImapFetchRecord record;
    AmgMailHeaders headers;
    AmgBuffer body, preview, attachments;
    size_t position = 0;
    int result;
    result = amg_imap_fetch_record_next(payload, payload_length,
                                        &position, &record);
    if (result <= 0) {
        amg_error_set(error, result < 0 ? result : AMG_ERR_PARSE,
                      T(MSG_THE_SELECTED_MESSAGE_CONTAINS_NO_MAIL_DATA_BLOCK, "The selected message contains no mail data block."));
        return result < 0 ? result : AMG_ERR_PARSE;
    }

    amg_mail_headers_init(&headers);
    amg_buffer_init(&body);
    amg_buffer_init(&preview);
    amg_buffer_init(&attachments);
    result = amg_mail_headers_parse((const char *)record.literal,
                                    record.literal_length, &headers, NULL);
    if (result == AMG_OK)
        result = append_preview_header(
            &preview, T(MSG_FROM, "From: "), amg_mail_header_get(&headers, "From"));
    if (result == AMG_OK)
        result = append_preview_header(
            &preview, T(MSG_TO, "To: "), amg_mail_header_get(&headers, "To"));
    if (result == AMG_OK)
        result = append_preview_header(
            &preview, T(MSG_DATE, "Date: "), amg_mail_header_get(&headers, "Date"));
    if (result == AMG_OK)
        result = append_preview_header(
            &preview, T(MSG_SUBJECT, "Subject: "), amg_mail_header_get(&headers, "Subject"));
    if (result == AMG_OK)
        result = amg_buffer_append_char(&preview, '\n');
    if (result == AMG_OK)
        result = amg_mime_extract_text((const char *)record.literal,
                                       record.literal_length, &body, error);
    if (result == AMG_OK)
        result = amg_buffer_append(&preview, body.data, body.length);
    if (result == AMG_OK &&
        amg_mime_attachment_summary((const char *)record.literal,
                                    record.literal_length, &attachments,
                                    NULL) == AMG_OK &&
        attachments.length) {
        result = amg_buffer_append_cstr(&preview, T(MSG_ATTACHMENTS_791D, "\n\nAttachments:\n"));
        if (result == AMG_OK)
            result = amg_buffer_append(&preview, attachments.data,
                                       attachments.length);
    }
    if (result == AMG_OK && amg_buffer_terminate(&preview) == AMG_OK) {
        set_preview_utf8(gui, preview.data, preview.length);
        amg_error_set(error, AMG_OK, "");
    } else if (result != AMG_OK) {
        set_preview_local(gui, T(MSG_MESSAGE_TEXT_COULD_NOT_BE_DISPLAYED, "Message text could not be displayed."));
    }
    amg_mail_headers_free(&headers);
    amg_buffer_free(&body);
    amg_buffer_free(&preview);
    amg_buffer_free(&attachments);
    return result;
}

static void set_attachment_button_enabled(AmgGui *gui, int enabled)
{
    if (!gui || !gui->save_attachments_gadget) return;
    if (gui->window) {
        SetGadgetAttrs(gui->save_attachments_gadget, gui->window, NULL,
                       GA_Disabled, enabled ? FALSE : TRUE,
                       TAG_DONE);
        RefreshGList(gui->save_attachments_gadget, gui->window, NULL, 1);
    } else {
        SetAttrs((Object *)gui->save_attachments_gadget,
                 GA_Disabled, enabled ? FALSE : TRUE,
                 TAG_DONE);
    }
}

void clear_current_message_payload(AmgGui *gui)
{
    if (!gui) return;
    free(gui->current_message_payload);
    gui->current_message_payload = NULL;
    gui->current_message_payload_length = 0U;
    gui->current_attachment_count = 0U;
    set_attachment_button_enabled(gui, 0);
}

static int copy_first_message_literal(const unsigned char *payload,
                                      size_t payload_length,
                                      unsigned char **message_out,
                                      size_t *message_length_out)
{
    AmgImapFetchRecord record;
    unsigned char *copy;
    size_t position = 0U;
    int result;
    if (!message_out || !message_length_out ||
        (!payload && payload_length != 0U))
        return AMG_ERR_ARGUMENT;
    *message_out = NULL;
    *message_length_out = 0U;
    result = amg_imap_fetch_record_next(payload, payload_length,
                                        &position, &record);
    if (result <= 0) return result < 0 ? result : AMG_ERR_PARSE;
    if (record.literal_length > AMIGMAIL_MAX_MESSAGE)
        return AMG_ERR_LIMIT;
    copy = (unsigned char *)malloc(record.literal_length + 1U);
    if (!copy) return AMG_ERR_MEMORY;
    if (record.literal_length)
        memcpy(copy, record.literal, record.literal_length);
    copy[record.literal_length] = 0;
    *message_out = copy;
    *message_length_out = record.literal_length;
    return AMG_OK;
}

void retain_current_message_payload(AmgGui *gui,
                                           const AmgNetworkEvent *event)
{
    unsigned char *message = NULL;
    size_t message_length = 0U;
    size_t count = 0U;
    int result;
    if (!gui || !event) return;

    /* Nicht den NetworkEvent-Payload stehlen: Derselbe Event wird beim
     * Antworten unmittelbar danach noch von prepare_reply_payload()
     * ausgewertet. Stattdessen nur den eigentlichen RFC822/MIME-Literalblock
     * separat speichern. Das ist zugleich die korrekte Eingabe fuer die
     * Anhangserkennung und -extraktion. */
    result = copy_first_message_literal(event->payload,
                                        event->payload_length,
                                        &message, &message_length);
    clear_current_message_payload(gui);
    if (result != AMG_OK) return;

    gui->current_message_payload = message;
    gui->current_message_payload_length = message_length;
    if (amg_mime_attachment_count(
            (const char *)gui->current_message_payload,
            gui->current_message_payload_length, &count, NULL) == AMG_OK) {
        gui->current_attachment_count = count;
    }
    set_attachment_button_enabled(gui, count > 0U);
}

void sanitize_attachment_name(const char *name_utf8,
                                     char *name_local, size_t capacity)
{
    size_t i, used;
    if (!name_local || !capacity) return;
    utf8_to_local_copy(name_utf8 && *name_utf8 ? name_utf8 : T(MSG_ATTACHMENT_BIN, "attachment.bin"),
                       name_local, capacity);
    used = strlen(name_local);
    for (i = 0U; i < used; ++i) {
        unsigned char c = (unsigned char)name_local[i];
        if (c < 32U || c == ':' || c == '/' || c == '\\')
            name_local[i] = '_';
    }
    while (name_local[0] == '.')
        memmove(name_local, name_local + 1, strlen(name_local));
    if (!name_local[0]) strcpy(name_local, T(MSG_ATTACHMENT_BIN, "attachment.bin"));
}

int build_unique_attachment_path(const char *drawer,
                                        const char *name,
                                        char *path, size_t capacity)
{
    unsigned long suffix = 0UL;
    if (!drawer || !path || capacity < 4U) return AMG_ERR_ARGUMENT;
    for (;;) {
        char candidate[COMPOSE_NAME_MAX + 32U];
        BPTR lock;
        if (suffix == 0UL)
            snprintf(candidate, sizeof(candidate), "%s", name);
        else
            snprintf(candidate, sizeof(candidate), "%s.%lu", name, suffix);
        strncpy(path, drawer, capacity - 1U);
        path[capacity - 1U] = 0;
        if (!AddPart((STRPTR)path, (STRPTR)candidate, (LONG)capacity))
            return AMG_ERR_LIMIT;
        lock = Lock((STRPTR)path, ACCESS_READ);
        if (!lock) return AMG_OK;
        UnLock(lock);
        if (++suffix > 9999UL) return AMG_ERR_LIMIT;
    }
}

void save_current_attachments(AmgGui *gui)
{
    struct FileRequester *request;
    char drawer[COMPOSE_PATH_MAX];
    size_t i, saved = 0U;
    if (!gui || !gui->current_message_payload ||
        !gui->current_attachment_count) {
        status_local(gui, T(MSG_THIS_MESSAGE_HAS_NO_SAVABLE_ATTACHMENTS, "This message has no savable attachments."));
        return;
    }
    request = (struct FileRequester *)AllocAslRequestTags(
        ASL_FileRequest,
        ASLFR_TitleText, (ULONG)(uintptr_t)T(MSG_SAVE_ATTACHMENTS, "Save attachments"),
        ASLFR_Window, (ULONG)(uintptr_t)gui->window,
        ASLFR_SleepWindow, TRUE,
        ASLFR_DrawersOnly, TRUE,
        ASLFR_RejectIcons, TRUE,
        TAG_DONE);
    if (!request) {
        status_local(gui, T(MSG_DESTINATION_FOLDER_COULD_NOT_BE_SELECTED, "Destination folder could not be selected."));
        return;
    }
    if (!AslRequest(request, NULL)) {
        FreeAslRequest(request);
        return;
    }
    strncpy(drawer, request->rf_Dir ? (const char *)request->rf_Dir : "",
            sizeof(drawer) - 1U);
    drawer[sizeof(drawer) - 1U] = 0;
    FreeAslRequest(request);
    if (!drawer[0]) {
        status_local(gui, T(MSG_NO_DESTINATION_FOLDER_SELECTED, "No destination folder selected."));
        return;
    }

    for (i = 0U; i < gui->current_attachment_count; ++i) {
        AmgBuffer name_utf8, data;
        AmgError attachment_error;
        char name_local[COMPOSE_NAME_MAX];
        char path[COMPOSE_PATH_MAX];
        FILE *file;
        int result;
        amg_buffer_init(&name_utf8);
        amg_buffer_init(&data);
        memset(&attachment_error, 0, sizeof(attachment_error));
        result = amg_mime_extract_attachment(
            (const char *)gui->current_message_payload,
            gui->current_message_payload_length, i,
            &name_utf8, &data, &attachment_error);
        if (result != AMG_OK) {
            amg_buffer_free(&name_utf8);
            amg_buffer_free(&data);
            status_utf8(gui, attachment_error.message[0]
                                 ? attachment_error.message
                                 : T(MSG_ATTACHMENT_COULD_NOT_BE_SAVED, "Attachment could not be saved."));
            return;
        }
        sanitize_attachment_name((const char *)name_utf8.data,
                                 name_local, sizeof(name_local));
        result = build_unique_attachment_path(
            drawer, name_local, path, sizeof(path));
        if (result != AMG_OK) {
            amg_buffer_free(&name_utf8);
            amg_buffer_free(&data);
            status_local(gui, T(MSG_ATTACHMENT_PATH_IS_TOO_LONG, "Attachment path is too long."));
            return;
        }
        file = fopen(path, "wb");
        if (!file) {
            amg_buffer_free(&name_utf8);
            amg_buffer_free(&data);
            status_local(gui, T(MSG_ATTACHMENT_COULD_NOT_BE_WRITTEN_TO_DISK, "Attachment could not be written to disk."));
            return;
        }
        {
            int write_failed = data.length &&
                fwrite(data.data, 1U, data.length, file) != data.length;
            int close_failed = fclose(file) != 0;
            if (write_failed || close_failed) {
                amg_buffer_free(&name_utf8);
                amg_buffer_free(&data);
                status_local(gui, T(MSG_ATTACHMENT_COULD_NOT_BE_WRITTEN_TO_DISK, "Attachment could not be written to disk."));
                return;
            }
        }
        ++saved;
        amg_buffer_free(&name_utf8);
        amg_buffer_free(&data);
    }
    {
        char message[128];
        amg_tr_snprintf(message, sizeof(message), MSG_VALUE_ATTACHMENT_S_SAVED, "%lu attachment(s) saved.", (unsigned long)saved);
        status_local(gui, message);
    }
}

#endif /* AMIGMAIL_AMIGA */
