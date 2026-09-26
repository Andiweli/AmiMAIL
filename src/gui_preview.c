#include "gui_internal.h"
#include "buffer.h"
#include "codec.h"
#include "imap_parser.h"
#include "mime.h"
#include "i18n.h"
#include "mailto.h"

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


static int reply_status_is_leap_year(unsigned long year)
{
    return (year % 4UL == 0UL && year % 100UL != 0UL) ||
           year % 400UL == 0UL;
}

static void reply_status_format_date(LONG days, char *text, size_t capacity)
{
    static const unsigned char month_lengths[] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };
    unsigned long remaining, year = 1978UL, month = 0UL, day;
    if (!text || !capacity) return;
    text[0] = 0;
    if (days < 0) return;
    remaining = (unsigned long)days;
    while (remaining >= (reply_status_is_leap_year(year) ? 366UL : 365UL)) {
        remaining -= reply_status_is_leap_year(year) ? 366UL : 365UL;
        ++year;
    }
    while (month < 11UL) {
        unsigned long length = month_lengths[month];
        if (month == 1UL && reply_status_is_leap_year(year)) ++length;
        if (remaining < length) break;
        remaining -= length;
        ++month;
    }
    day = remaining + 1UL;
    snprintf(text, capacity, "%02lu.%02lu.%04lu",
             day, month + 1UL, year);
}

static GuiReplyStamp *find_reply_stamp(AmgGui *gui, size_t account_index,
                                       const char *mailbox_utf8,
                                       unsigned long uid_validity,
                                       unsigned long uid)
{
    size_t i;
    if (!gui || !mailbox_utf8 || !*mailbox_utf8 || !uid_validity || !uid)
        return NULL;
    for (i = 0U; i < GUI_REPLY_STAMP_CACHE_SIZE; ++i) {
        GuiReplyStamp *stamp = &gui->reply_stamp_cache[i];
        if (stamp->valid && stamp->account_index == account_index &&
            stamp->uid == uid && stamp->uid_validity == uid_validity &&
            !strcmp(stamp->mailbox_utf8, mailbox_utf8))
            return stamp;
    }
    return NULL;
}

static void update_reply_status_gadget(AmgGui *gui)
{
    GuiReplyStamp *stamp = NULL;
    char *status_text;
    if (!gui) return;

    /* Alternate backing buffers so button.gadget always sees a new GA_Text
     * pointer. Some classic implementations optimize identical pointers and
     * would otherwise miss an in-place string update. */
    gui->reply_status_text_index ^= 1U;
    status_text = gui->reply_status_text[gui->reply_status_text_index & 1U];
    status_text[0] = 0;

    if (gui->preview_message_answered) {
        stamp = find_reply_stamp(gui, gui->active_account,
                                 gui->preview_message_mailbox_utf8,
                                 gui->preview_message_uid_validity,
                                 gui->preview_message_uid);
        if (stamp) {
            char date[32], time_text[16];
            reply_status_format_date(stamp->days, date, sizeof(date));
            snprintf(time_text, sizeof(time_text), "%02lu:%02lu",
                     (unsigned long)stamp->minutes / 60UL,
                     (unsigned long)stamp->minutes % 60UL);
            amg_tr_snprintf(status_text, sizeof(gui->reply_status_text[0]),
                            MSG_MAIL_WAS_REPLIED_TO_ON_VALUE_AT_VALUE,
                            "Mail was replied to on %s at %s.",
                            date, time_text);
        } else {
            snprintf(status_text, sizeof(gui->reply_status_text[0]),
                     "%s", T(MSG_MAIL_HAS_BEEN_REPLIED_TO,
                               "Mail has been replied to"));
        }
    }

    if (gui->reply_status_gadget) {
        if (gui->window) {
            SetGadgetAttrs(gui->reply_status_gadget, gui->window, NULL,
                           GA_Text, (ULONG)(uintptr_t)status_text,
                           TAG_DONE);
            RefreshGList(gui->reply_status_gadget, gui->window, NULL, 1);
        } else {
            SetAttrs((Object *)gui->reply_status_gadget,
                     GA_Text, (ULONG)(uintptr_t)status_text,
                     TAG_DONE);
        }
    }
}

static void set_preview_reply_context(AmgGui *gui,
                                      const char *mailbox_utf8,
                                      unsigned long uid_validity,
                                      unsigned long uid, int answered)
{
    if (!gui) return;
    gui->preview_message_uid = uid;
    gui->preview_message_uid_validity = uid_validity;
    snprintf(gui->preview_message_mailbox_utf8,
             sizeof(gui->preview_message_mailbox_utf8), "%s",
             mailbox_utf8 ? mailbox_utf8 : "");
    gui->preview_message_answered = answered ? 1 : 0;
    update_reply_status_gadget(gui);
}

void gui_note_answered_now(AmgGui *gui, size_t account_index,
                           const char *mailbox_utf8,
                           unsigned long uid_validity, unsigned long uid)
{
    struct DateStamp now;
    GuiReplyStamp *stamp;
    if (!gui || !mailbox_utf8 || !*mailbox_utf8 || !uid_validity || !uid)
        return;

    stamp = find_reply_stamp(gui, account_index, mailbox_utf8,
                             uid_validity, uid);
    if (!stamp) {
        stamp = &gui->reply_stamp_cache[
            gui->reply_stamp_next % GUI_REPLY_STAMP_CACHE_SIZE];
        gui->reply_stamp_next =
            (gui->reply_stamp_next + 1U) % GUI_REPLY_STAMP_CACHE_SIZE;
        memset(stamp, 0, sizeof(*stamp));
    }

    DateStamp(&now);
    stamp->account_index = account_index;
    stamp->uid = uid;
    stamp->uid_validity = uid_validity;
    snprintf(stamp->mailbox_utf8, sizeof(stamp->mailbox_utf8), "%s",
             mailbox_utf8);
    stamp->days = now.ds_Days;
    stamp->minutes = now.ds_Minute;
    stamp->valid = 1;

    if (gui->active_account == account_index &&
        gui->preview_message_uid == uid &&
        gui->preview_message_uid_validity == uid_validity &&
        !strcmp(gui->preview_message_mailbox_utf8, mailbox_utf8)) {
        gui->preview_message_answered = 1;
        update_reply_status_gadget(gui);
    }
}

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
    size_t pos = 0, end, email_length;
    int result = AMG_OK;
    if (!styled) return AMG_ERR_ARGUMENT;
    if (!text) text = "";
    while (text[pos] && result == AMG_OK) {
        end = pos;
        if (url_boundary_before(text, pos) && url_prefix_length(text + pos))
            end = url_token_end(text, pos);
        else if ((email_length =
                      amg_email_address_token_length(text + pos)) > 0U)
            end = pos + email_length;

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
    if (start > 0U && (unsigned char)line[start - 1U] == 0x1bU &&
        (line[start] == 'u' || line[start] == 'b' ||
         line[start] == 'i' || line[start] == 'n'))
        ++start;

    end = url_token_end(line, start);
    if (end <= start || !url_prefix_length(line + start))
        return amg_mailto_url_from_email_at(line, pos, output, GUI_URL_MAX);

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
    if (!OpenURLBase && !ascii_prefix_ci(url, "mailto:"))
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

    if (!gui->running || !url[0]) return;

    /* mailto: links and bare addresses belong to AmiMail itself.  The
     * TextEditor hook only queues the action; opening the compose window here
     * keeps ReAction input processing non-reentrant. */
    if (ascii_prefix_ci(url, "mailto:")) {
        AmgError error;
        memset(&error, 0, sizeof(error));
        (void)open_mailto_compose(gui, url, &error);
        return;
    }

    if (!OpenURLBase) return;
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

int gui_message_text(AmgGui *gui, const AmgImapFetchRecord *record,
                     AmgBuffer *body, AmgError *error)
{
    int result;
    if (!record || !body) return AMG_ERR_ARGUMENT;
    if (!gui || !gui->current_mail_file ||
        gui->current_mail_file->uid != record->uid)
        return amg_mime_extract_text((const char *)record->literal,
                                     record->literal_length, body, error);
    /* Disk-backed FETCH carries a small synthetic text entity. Use the
     * original extraction result, rather than decoding its HTML entities or
     * charset twice, and never silently edit/forward a truncated body. */
    result = amg_buffer_append(body, gui->current_mail_file->text.data,
                               gui->current_mail_file->text.length);
    if (result == AMG_OK) result = gui->current_mail_file->text_result;
    amg_error_set(error, result, result == AMG_OK ? "" :
        T(MSG_NO_DISPLAYABLE_TEXT_PART_WAS_FOUND_IN_THE,
          "No displayable text part was found in the message."));
    return result;
}

int display_message_payload(AmgGui *gui, const unsigned char *payload,
                            size_t payload_length,
                            const char *mailbox_utf8,
                            unsigned long uid_validity, AmgError *error)
{
    AmgImapFetchRecord record;
    AmgMailHeaders headers;
    AmgBuffer body, preview, attachments, embedded_graphics;
    char date_local[160];
    size_t position = 0;
    int result;
    result = amg_imap_fetch_record_next(payload, payload_length,
                                        &position, &record);
    if (result <= 0) {
        set_preview_reply_context(gui, NULL, 0UL, 0UL, 0);
        amg_error_set(error, result < 0 ? result : AMG_ERR_PARSE,
                      T(MSG_THE_SELECTED_MESSAGE_CONTAINS_NO_MAIL_DATA_BLOCK, "The selected message contains no mail data block."));
        return result < 0 ? result : AMG_ERR_PARSE;
    }

    set_preview_reply_context(gui, mailbox_utf8, uid_validity,
                              record.uid, record.answered);
    amg_mail_headers_init(&headers);
    amg_buffer_init(&body);
    amg_buffer_init(&preview);
    amg_buffer_init(&attachments);
    amg_buffer_init(&embedded_graphics);
    (void)amg_buffer_set_limit(&body, AMIMAIL_MAX_PREVIEW_TEXT);
    (void)amg_buffer_set_limit(&preview, AMIMAIL_MAX_PREVIEW_TEXT * 2UL);
    (void)amg_buffer_set_limit(&attachments, AMIMAIL_MAX_PREVIEW_TEXT / 4UL);
    (void)amg_buffer_set_limit(&embedded_graphics, AMIMAIL_MAX_PREVIEW_TEXT / 4UL);
    result = amg_mail_headers_parse((const char *)record.literal,
                                    record.literal_length, &headers, NULL);
    if (result == AMG_OK)
        result = append_preview_header(
            &preview, T(MSG_FROM, "From: "), amg_mail_header_get(&headers, "From"));
    if (result == AMG_OK)
        result = append_preview_header(
            &preview, T(MSG_TO, "To: "), amg_mail_header_get(&headers, "To"));
    if (result == AMG_OK) {
        format_mail_date_local(amg_mail_header_get(&headers, "Date"),
                               date_local, sizeof(date_local));
        result = append_preview_header(
            &preview, T(MSG_DATE, "Date: "), date_local);
    }
    if (result == AMG_OK)
        result = append_preview_header(
            &preview, T(MSG_SUBJECT, "Subject: "), amg_mail_header_get(&headers, "Subject"));
    if (result == AMG_OK)
        result = amg_buffer_append_char(&preview, '\n');
    if (result == AMG_OK) {
        int body_result = gui_message_text(gui, &record, &body, error);
        if (body_result == AMG_OK || body_result == AMG_ERR_LIMIT) {
            /* The display ceiling may fall inside a UTF-8 scalar. Do not
             * emit a partial multibyte character at the truncated tail. */
            if (body_result == AMG_ERR_LIMIT && body.length) {
                size_t start = body.length - 1U;
                size_t needed = 1U;
                unsigned char lead;
                while (start && (body.data[start] & 0xc0U) == 0x80U) --start;
                lead = body.data[start];
                if (lead >= 0xc2U && lead <= 0xdfU) needed = 2U;
                else if (lead >= 0xe0U && lead <= 0xefU) needed = 3U;
                else if (lead >= 0xf0U && lead <= 0xf4U) needed = 4U;
                if (body.length - start < needed) body.length = start;
                body.data[body.length] = 0;
            }
            result = amg_buffer_append(&preview, body.data, body.length);
            if (result == AMG_OK && body_result == AMG_ERR_LIMIT)
                result = amg_buffer_append_cstr(&preview,
                    T(MSG_PREVIEW_SIZE_LIMIT,
                      "\n\n[Preview shortened or omitted (size limit). "
                      "Message and attachments are unchanged.]\n"));
        } else if (body_result == AMG_ERR_UNSUPPORTED || body_result == AMG_ERR_PARSE) {
            /* Non-text or unknown-charset messages may still have perfectly
             * valid attachments. Keep their attachment sections available. */
            result = amg_buffer_append_cstr(&preview,
                T(MSG_PREVIEW_TEXT_UNAVAILABLE,
                  "[No supported text body. Attachments can still be saved.]\n"));
        } else result = body_result;
    }
    if (result == AMG_OK &&
        (gui->current_mail_file
            ? amg_mailfile_summary(gui->current_mail_file, &attachments,
                                   &embedded_graphics)
            : amg_mime_attachment_grouped_summary(
                (const char *)record.literal, record.literal_length,
                &attachments, NULL, &embedded_graphics, NULL, NULL)) == AMG_OK) {
        if (attachments.length) {
            result = amg_buffer_append_cstr(
                &preview, T(MSG_ATTACHMENTS_791D, "\n\nAttachments:\n"));
            if (result == AMG_OK)
                result = amg_buffer_append(&preview, attachments.data,
                                           attachments.length);
        }
        if (result == AMG_OK && embedded_graphics.length) {
            result = amg_buffer_append_cstr(
                &preview, T(MSG_EMBEDDED_GRAPHICS_7F31,
                            "\n\nEmbedded graphics:\n"));
            if (result == AMG_OK)
                result = amg_buffer_append(&preview, embedded_graphics.data,
                                           embedded_graphics.length);
        }
    }
    if (result == AMG_OK) result = amg_buffer_terminate(&preview);
    if (result == AMG_OK) {
        set_preview_utf8(gui, preview.data, preview.length);
        amg_error_set(error, AMG_OK, "");
    } else if (result != AMG_OK) {
        set_preview_local(gui, T(MSG_MESSAGE_TEXT_COULD_NOT_BE_DISPLAYED, "Message text could not be displayed."));
    }
    amg_mail_headers_free(&headers);
    amg_buffer_free(&body);
    amg_buffer_free(&preview);
    amg_buffer_free(&attachments);
    amg_buffer_free(&embedded_graphics);
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

static void release_current_message_payload(AmgGui *gui)
{
    if (!gui) return;
    amg_mailfile_close(gui->current_mail_file);
    gui->current_mail_file = NULL;
    free(gui->current_message_payload);
    gui->current_message_payload = NULL;
    gui->current_message_payload_length = 0U;
    gui->current_attachment_count = 0U;
    set_attachment_button_enabled(gui, 0);
}

void clear_current_message_payload(AmgGui *gui)
{
    if (!gui) return;
    release_current_message_payload(gui);
    set_preview_reply_context(gui, NULL, 0UL, 0UL, 0);
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
                                           AmgNetworkEvent *event)
{
    unsigned char *message = NULL;
    size_t message_length = 0U;
    size_t count = 0U;
    int result;
    if (!gui || !event) return;
    if (event->mail_file) {
        release_current_message_payload(gui);
        gui->current_mail_file = event->mail_file;
        event->mail_file = NULL; /* transfer exclusive ownership */
        gui->current_attachment_count = gui->current_mail_file->attachment_count;
        set_attachment_button_enabled(gui, gui->current_attachment_count > 0U);
        return;
    }

    /* Nicht den NetworkEvent-Payload stehlen: Derselbe Event wird beim
     * Antworten unmittelbar danach noch von prepare_reply_payload()
     * ausgewertet. Stattdessen nur den eigentlichen RFC822/MIME-Literalblock
     * separat speichern. Das ist zugleich die korrekte Eingabe fuer die
     * Anhangserkennung und -extraktion. */
    result = copy_first_message_literal(event->payload,
                                        event->payload_length,
                                        &message, &message_length);
    /* Replace only the retained MIME payload.  The preview reply context was
     * set by display_message_payload() immediately before this call and must
     * survive; clearing it here made the \Answered indication flash briefly
     * and then disappear. */
    release_current_message_payload(gui);
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

int gui_prepare_attachment_file(AmgGui *gui, AmgError *error)
{
    char directory[AMG_SPOOL_PATH_MAX], path[AMG_SPOOL_PATH_MAX];
    FILE *file = NULL;
    int result;
    if (!gui) return AMG_ERR_ARGUMENT;
    if (gui->current_mail_file) return AMG_OK;
    if (!gui->current_message_payload) return AMG_ERR_ARGUMENT;
    result = amg_spool_directory(directory, error);
    if (result == AMG_OK) result = amg_spool_create(directory, path, &file, error);
    if (result != AMG_OK) return result;
    if (fwrite(gui->current_message_payload, 1U,
               gui->current_message_payload_length, file) !=
        gui->current_message_payload_length) result = AMG_ERR_IO;
    if (fclose(file) != 0) result = AMG_ERR_IO;
    if (result == AMG_OK)
        result = amg_mailfile_open(path, 1, NULL, &gui->current_mail_file, error);
    if (result != AMG_OK) { amg_spool_remove(path); return result; }
    free(gui->current_message_payload);
    gui->current_message_payload = NULL;
    gui->current_message_payload_length = 0U;
    gui->current_mail_file->uid = gui->active_message_uid;
    gui->current_attachment_count = gui->current_mail_file->attachment_count;
    return AMG_OK;
}

#endif /* AMIGMAIL_AMIGA */
