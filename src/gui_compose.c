#include "gui_internal.h"
#include "buffer.h"
#include "codec.h"
#include "imap_parser.h"
#include "mime.h"
#include "network_task.h"
#include "i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if AMIGMAIL_AMIGA
#include <clib/alib_protos.h>
#include <classes/window.h>
#include <dos/dos.h>
#include <exec/lists.h>
#include <exec/memory.h>
#include <gadgets/button.h>
#include <gadgets/layout.h>
#include <gadgets/listbrowser.h>
#include <gadgets/scroller.h>
#include <gadgets/string.h>
#include <gadgets/texteditor.h>
#include <intuition/classes.h>
#include <intuition/intuition.h>
#include <libraries/asl.h>
#include <proto/asl.h>
#include <proto/button.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <proto/listbrowser.h>
#include <proto/scroller.h>
#include <proto/string.h>
#include <proto/texteditor.h>
#include <proto/window.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <utility/tagitem.h>
#ifdef NewObject
#undef NewObject
#endif
#ifdef ButtonObject
#undef ButtonObject
#endif
#define ButtonObject NewObject(NULL,(CONST_STRPTR)"button.gadget"
#define T(de,en) amg_tr((de),(en))
enum ComposeGadgetId { GID_COMPOSE_TO=200,GID_COMPOSE_CC,GID_COMPOSE_BCC,GID_COMPOSE_TO_CONTACTS,GID_COMPOSE_CC_CONTACTS,GID_COMPOSE_BCC_CONTACTS,GID_COMPOSE_SUBJECT,GID_COMPOSE_BODY,GID_COMPOSE_BODY_SCROLL,GID_COMPOSE_ATTACHMENTS,GID_COMPOSE_ATTACHMENTS_SCROLL,GID_COMPOSE_ADD_ATTACHMENT,GID_COMPOSE_REMOVE_ATTACHMENT,GID_COMPOSE_STATUS,GID_COMPOSE_SEND,GID_COMPOSE_CANCEL };
static void cleanup_compose_temp_attachments(ComposeAttachment *attachments,
                                             size_t count)
{
    size_t i;
    if (!attachments) return;
    for (i = 0U; i < count; ++i) {
        if (attachments[i].temporary && attachments[i].path[0]) {
            (void)remove(attachments[i].path);
            attachments[i].path[0] = 0;
        }
    }
}

 void cleanup_draft_edit_files(DraftEditData *edit)
{
    size_t i;
    if (!edit) return;
    for (i = 0U; i < edit->attachment_count; ++i) {
        if (edit->attachments[i].temporary && edit->attachments[i].path[0]) {
            (void)remove(edit->attachments[i].path);
            edit->attachments[i].path[0] = 0;
        }
    }
    edit->attachment_count = 0U;
}

 int prepare_draft_edit_payload(AmgGui *gui,
                                      const unsigned char *payload,
                                      size_t payload_length,
                                      unsigned long uid,
                                      const char *mailbox_utf8,
                                      DraftEditData *edit,
                                      AmgError *error)
{
    AmgImapFetchRecord record;
    AmgMailHeaders headers;
    AmgBuffer body_utf8, body_local;
    size_t position = 0U;
    size_t attachment_count = 0U;
    size_t i;
    unsigned long total = 0UL;
    int result;

    if (!gui || !edit || !uid || !mailbox_utf8 || !*mailbox_utf8)
        return AMG_ERR_ARGUMENT;
    memset(edit, 0, sizeof(*edit));
    edit->uid = uid;
    strncpy(edit->mailbox_utf8, mailbox_utf8,
            sizeof(edit->mailbox_utf8) - 1U);
    edit->mailbox_utf8[sizeof(edit->mailbox_utf8) - 1U] = 0;

    result = amg_imap_fetch_record_next(payload, payload_length,
                                        &position, &record);
    if (result <= 0) {
        result = result < 0 ? result : AMG_ERR_PARSE;
        amg_error_set(error, result,
                      T("Entwurf konnte nicht ausgewertet werden.",
                        "Draft could not be parsed."));
        return result;
    }

    amg_mail_headers_init(&headers);
    amg_buffer_init(&body_utf8);
    amg_buffer_init(&body_local);
    result = amg_mail_headers_parse((const char *)record.literal,
                                    record.literal_length, &headers, NULL);
    if (result != AMG_OK) goto done;

    header_to_local(amg_mail_header_get(&headers, "To"), "",
                    edit->to_local, sizeof(edit->to_local));
    header_to_local(amg_mail_header_get(&headers, "Cc"), "",
                    edit->cc_local, sizeof(edit->cc_local));
    header_to_local(amg_mail_header_get(&headers, "Bcc"), "",
                    edit->bcc_local, sizeof(edit->bcc_local));
    header_to_local(amg_mail_header_get(&headers, "Subject"), "",
                    edit->subject_local, sizeof(edit->subject_local));

    {
        const char *message_id = amg_mail_header_get(&headers, "Message-ID");
        const char *in_reply_to = amg_mail_header_get(&headers, "In-Reply-To");
        const char *references = amg_mail_header_get(&headers, "References");
        if ((message_id && strlen(message_id) >= sizeof(edit->message_id_utf8)) ||
            (in_reply_to && strlen(in_reply_to) >= sizeof(edit->in_reply_to_utf8)) ||
            (references && strlen(references) >= sizeof(edit->references_utf8))) {
            result = AMG_ERR_LIMIT;
            amg_error_set(error, result,
                          T("Entwurf enth\344lt zu lange Antwort-Kopfzeilen.",
                            "Draft contains reply headers that are too long."));
            goto done;
        }
        snprintf(edit->message_id_utf8, sizeof(edit->message_id_utf8), "%s",
                 message_id ? message_id : "");
        snprintf(edit->in_reply_to_utf8, sizeof(edit->in_reply_to_utf8), "%s",
                 in_reply_to ? in_reply_to : "");
        snprintf(edit->references_utf8, sizeof(edit->references_utf8), "%s",
                 references ? references : "");
    }

    result = amg_mime_extract_text((const char *)record.literal,
                                   record.literal_length, &body_utf8, error);
    if (result != AMG_OK) goto done;
    result = amg_buffer_terminate(&body_utf8);
    if (result == AMG_OK)
        result = amg_utf8_to_local((const char *)body_utf8.data, &body_local);
    if (result == AMG_OK) result = amg_buffer_terminate(&body_local);
    if (result != AMG_OK) goto done;
    if (body_local.length >= sizeof(edit->body_local)) {
        result = AMG_ERR_LIMIT;
        amg_error_set(error, result,
                      T("Entwurfstext ist zu gro\337 zum Bearbeiten.",
                        "Draft body is too large to edit."));
        goto done;
    }
    if (body_local.length)
        memcpy(edit->body_local, body_local.data, body_local.length);
    edit->body_local[body_local.length] = 0;

    result = amg_mime_attachment_count((const char *)record.literal,
                                       record.literal_length,
                                       &attachment_count, error);
    if (result != AMG_OK) goto done;
    if (attachment_count > AMG_MAIL_MAX_ATTACHMENTS) {
        result = AMG_ERR_LIMIT;
        amg_error_set(error, result,
                      T("Dieser Entwurf enth\344lt mehr als 8 Anlagen.",
                        "This draft contains more than 8 attachments."));
        goto done;
    }

    for (i = 0U; i < attachment_count; ++i) {
        AmgBuffer name_utf8, data;
        char name_local[COMPOSE_NAME_MAX];
        char path[COMPOSE_PATH_MAX];
        FILE *file = NULL;
        int write_failed = 0;

        path[0] = 0;
        amg_buffer_init(&name_utf8);
        amg_buffer_init(&data);
        result = amg_mime_extract_attachment(
            (const char *)record.literal, record.literal_length, i,
            &name_utf8, &data, error);
        if (result == AMG_OK) result = amg_buffer_terminate(&name_utf8);
        if (result != AMG_OK) {
            amg_buffer_free(&name_utf8);
            amg_buffer_free(&data);
            goto done;
        }
        if (data.length > AMG_MAIL_MAX_ATTACHMENT_TOTAL - total) {
            amg_buffer_free(&name_utf8);
            amg_buffer_free(&data);
            result = AMG_ERR_LIMIT;
            amg_error_set(error, result,
                          T("Anlagen des Entwurfs sind zusammen gr\366\337er als 10 MB.",
                            "Draft attachments exceed the 10 MB total limit."));
            goto done;
        }
        if (name_utf8.length >= sizeof(edit->attachments[i].name_utf8)) {
            amg_buffer_free(&name_utf8);
            amg_buffer_free(&data);
            result = AMG_ERR_LIMIT;
            amg_error_set(error, result,
                          T("Ein Anlagenname im Entwurf ist zu lang.",
                            "An attachment name in the draft is too long."));
            goto done;
        }
        sanitize_attachment_name((const char *)name_utf8.data,
                                 name_local, sizeof(name_local));
        result = build_unique_attachment_path("T:", name_local,
                                              path, sizeof(path));
        if (result == AMG_OK) {
            file = fopen(path, "wb");
            if (!file) result = AMG_ERR_IO;
        }
        if (result == AMG_OK && data.length &&
            fwrite(data.data, 1U, data.length, file) != data.length)
            write_failed = 1;
        if (file && fclose(file) != 0) write_failed = 1;
        if (result == AMG_OK && write_failed) result = AMG_ERR_IO;
        if (result != AMG_OK) {
            if (path[0]) (void)remove(path);
            amg_buffer_free(&name_utf8);
            amg_buffer_free(&data);
            amg_error_set(error, result,
                          T("Eine Entwurfsanlage konnte nicht tempor\344r gespeichert werden.",
                            "A draft attachment could not be stored temporarily."));
            goto done;
        }

        strncpy(edit->attachments[i].path, path,
                sizeof(edit->attachments[i].path) - 1U);
        edit->attachments[i].path[sizeof(edit->attachments[i].path) - 1U] = 0;
        strncpy(edit->attachments[i].name_local, name_local,
                sizeof(edit->attachments[i].name_local) - 1U);
        edit->attachments[i].name_local[
            sizeof(edit->attachments[i].name_local) - 1U] = 0;
        memcpy(edit->attachments[i].name_utf8, name_utf8.data,
               name_utf8.length + 1U);
        edit->attachments[i].size = (unsigned long)data.length;
        edit->attachments[i].temporary = 1;
        ++edit->attachment_count;
        total += (unsigned long)data.length;
        amg_buffer_free(&name_utf8);
        amg_buffer_free(&data);
    }
    amg_error_set(error, AMG_OK, "");

done:
    if (result != AMG_OK) cleanup_draft_edit_files(edit);
    amg_mail_headers_free(&headers);
    amg_buffer_free(&body_utf8);
    amg_buffer_free(&body_local);
    return result;
}

static void append_local_limited(char *destination, size_t capacity,
                                 const char *source)
{
    size_t used, available, length;
    if (!destination || !capacity || !source) return;
    used = strlen(destination);
    if (used >= capacity - 1U) return;
    available = capacity - used - 1U;
    length = strlen(source);
    if (length > available) length = available;
    memcpy(destination + used, source, length);
    destination[used + length] = 0;
}

 int prepare_reply_payload(AmgGui *gui, const unsigned char *payload,
                                 size_t payload_length, AmgError *error)
{
    AmgImapFetchRecord record;
    AmgMailHeaders headers;
    AmgBuffer body_utf8, body_local;
    const char *reply_to, *subject, *date, *message_id, *references;
    char from_local[768], subject_local[512], date_local[192];
    const char *cursor;
    size_t position = 0;
    int result;
    if (!gui) return AMG_ERR_ARGUMENT;
    result = amg_imap_fetch_record_next(payload, payload_length,
                                        &position, &record);
    if (result <= 0) {
        amg_error_set(error, result < 0 ? result : AMG_ERR_PARSE,
                      T("Antwortdaten konnten nicht ausgewertet werden.", "Reply data could not be parsed."));
        return result < 0 ? result : AMG_ERR_PARSE;
    }
    amg_mail_headers_init(&headers);
    amg_buffer_init(&body_utf8);
    amg_buffer_init(&body_local);
    result = amg_mail_headers_parse((const char *)record.literal,
                                    record.literal_length, &headers, NULL);
    if (result != AMG_OK) goto done;
    reply_to = amg_mail_header_get(&headers, "Reply-To");
    if (!reply_to || !*reply_to)
        reply_to = amg_mail_header_get(&headers, "From");
    subject = amg_mail_header_get(&headers, "Subject");
    date = amg_mail_header_get(&headers, "Date");
    message_id = amg_mail_header_get(&headers, "Message-ID");
    references = amg_mail_header_get(&headers, "References");
    header_to_local(reply_to, "", gui->reply_to_local,
                    sizeof(gui->reply_to_local));
    header_to_local(amg_mail_header_get(&headers, "From"),
                    T("Absender", "sender"), from_local, sizeof(from_local));
    header_to_local(subject, "", subject_local, sizeof(subject_local));
    header_to_local(date, "", date_local, sizeof(date_local));
    if (!subject_local[0])
        strcpy(gui->reply_subject_local, "Re:");
    else if ((subject_local[0] == 'R' || subject_local[0] == 'r') &&
             (subject_local[1] == 'E' || subject_local[1] == 'e') &&
             subject_local[2] == ':')
        snprintf(gui->reply_subject_local,
                 sizeof(gui->reply_subject_local), "%s", subject_local);
    else
        snprintf(gui->reply_subject_local,
                 sizeof(gui->reply_subject_local), "Re: %.507s", subject_local);

    snprintf(gui->reply_in_reply_to_utf8,
             sizeof(gui->reply_in_reply_to_utf8), "%s",
             message_id ? message_id : "");
    gui->reply_references_utf8[0] = 0;
    if (references && *references) {
        snprintf(gui->reply_references_utf8,
                 sizeof(gui->reply_references_utf8), "%s", references);
    }
    if (message_id && *message_id) {
        if (gui->reply_references_utf8[0])
            append_local_limited(gui->reply_references_utf8,
                                 sizeof(gui->reply_references_utf8), " ");
        append_local_limited(gui->reply_references_utf8,
                             sizeof(gui->reply_references_utf8), message_id);
    }

    result = amg_mime_extract_text((const char *)record.literal,
                                   record.literal_length, &body_utf8, error);
    if (result != AMG_OK) goto done;
    result = amg_buffer_terminate(&body_utf8);
    if (result == AMG_OK)
        result = amg_utf8_to_local((const char *)body_utf8.data, &body_local);
    if (result == AMG_OK) result = amg_buffer_terminate(&body_local);
    if (result != AMG_OK) goto done;

    gui->reply_body_local[0] = 0;
    if (amg_i18n_is_german()) {
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local), "\n\nAm ");
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local),
                             date_local[0] ? date_local : "unbekanntem Datum");
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local), " schrieb ");
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local), from_local);
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local), ":\n");
    } else {
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local), "\n\nOn ");
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local),
                             date_local[0] ? date_local : "an unknown date");
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local), ", ");
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local), from_local);
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local), " wrote:\n");
    }
    cursor = (const char *)body_local.data;
    while (*cursor && strlen(gui->reply_body_local) + 4U <
                         sizeof(gui->reply_body_local)) {
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local), "> ");
        while (*cursor && *cursor != '\r' && *cursor != '\n') {
            char character[2];
            character[0] = *cursor++;
            character[1] = 0;
            append_local_limited(gui->reply_body_local,
                                 sizeof(gui->reply_body_local), character);
        }
        if (*cursor == '\r') ++cursor;
        if (*cursor == '\n') ++cursor;
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local), "\n");
    }
    amg_error_set(error, AMG_OK, "");

done:
    if (result != AMG_OK && error && !error->message[0])
        amg_error_set(error, result,
                      T("Antwort konnte nicht vorbereitet werden.", "Reply could not be prepared."));
    amg_mail_headers_free(&headers);
    amg_buffer_free(&body_utf8);
    amg_buffer_free(&body_local);
    return result;
}

static int selected_file_size(const char *path, unsigned long *size)
{
    FILE *file = fopen(path, "rb");
    long length;
    if (!file) return AMG_ERR_IO;
    if (fseek(file, 0L, SEEK_END) != 0 || (length = ftell(file)) < 0) {
        fclose(file);
        return AMG_ERR_IO;
    }
    fclose(file);
    *size = (unsigned long)length;
    return AMG_OK;
}

static unsigned long attachment_total(const ComposeAttachment *attachments,
                                      size_t count)
{
    size_t i;
    unsigned long total = 0;
    for (i = 0; i < count; ++i) total += attachments[i].size;
    return total;
}

static void rebuild_attachment_list(struct Gadget *list_gadget,
                                    struct Window *window, struct List *list,
                                    const ComposeAttachment *attachments,
                                    size_t count)
{
    size_t i;
    SetGadgetAttrs(list_gadget, window, NULL,
                   LISTBROWSER_Labels, (ULONG)~0UL,
                   TAG_DONE);
    FreeListBrowserList(list);
    NewList(list);
    for (i = 0; i < count; ++i) {
        char line[384];
        struct Node *node;
        snprintf(line, sizeof(line), "%s (%lu KB)",
                 attachments[i].name_local,
                 (attachments[i].size + 1023UL) / 1024UL);
        node = one_column_node(line, (ULONG)i);
        if (node) AddTail(list, node);
    }
    SetGadgetAttrs(list_gadget, window, NULL,
                   LISTBROWSER_Labels, (ULONG)(uintptr_t)list,
                   TAG_DONE);
}

static void update_compose_status(struct Gadget *status_gadget,
                                  struct Window *window,
                                  const ComposeAttachment *attachments,
                                  size_t count)
{
    char text[160];
    unsigned long total = attachment_total(attachments, count);
    amg_tr_snprintf(text, sizeof(text),
                    "%lu Anlage(n), %lu KB von 10240 KB",
                    "%lu attachment(s), %lu KB of 10240 KB",
                    (unsigned long)count, (total + 1023UL) / 1024UL);
    set_string(status_gadget, window, text);
}

static int add_attachment(struct Window *window, struct Gadget *list_gadget,
                          struct Gadget *status_gadget, struct List *list,
                          ComposeAttachment *attachments, size_t *count)
{
    struct FileRequester *request;
    char path[COMPOSE_PATH_MAX];
    unsigned long size, total;
    size_t i;
    if (*count >= AMG_MAIL_MAX_ATTACHMENTS) {
        set_string(status_gadget, window,
                   T("H\366chstens 8 Anlagen sind m\366glich.", "A maximum of 8 attachments is allowed."));
        return 0;
    }
    request = (struct FileRequester *)AllocAslRequestTags(
        ASL_FileRequest,
        ASLFR_TitleText, (ULONG)(uintptr_t)T("Dateianlage ausw\344hlen", "Select attachment"),
        ASLFR_Window, (ULONG)(uintptr_t)window,
        ASLFR_SleepWindow, TRUE,
        ASLFR_RejectIcons, TRUE,
        TAG_DONE);
    if (!request) {
        set_string(status_gadget, window,
                   T("Dateiauswahl konnte nicht ge\366ffnet werden.", "File requester could not be opened."));
        return 0;
    }
    if (!AslRequest(request, NULL)) {
        FreeAslRequest(request);
        return 0;
    }
    strncpy(path, request->rf_Dir ? (const char *)request->rf_Dir : "",
            sizeof(path) - 1U);
    path[sizeof(path) - 1U] = 0;
    if (!AddPart((STRPTR)path, request->rf_File,
                 (LONG)sizeof(path))) {
        FreeAslRequest(request);
        set_string(status_gadget, window, T("Dateipfad ist zu lang.", "File path is too long."));
        return 0;
    }
    for (i = 0; i < *count; ++i) {
        if (!strcmp(attachments[i].path, path)) {
            FreeAslRequest(request);
            set_string(status_gadget, window,
                       T("Diese Datei ist bereits angeh\344ngt.", "This file is already attached."));
            return 0;
        }
    }
    if (selected_file_size(path, &size) != AMG_OK) {
        FreeAslRequest(request);
        set_string(status_gadget, window,
                   T("Datei konnte nicht gelesen werden.", "File could not be read."));
        return 0;
    }
    total = attachment_total(attachments, *count);
    if (size > AMG_MAIL_MAX_ATTACHMENT_TOTAL - total) {
        FreeAslRequest(request);
        set_string(status_gadget, window,
                   T("Anlagen d\374rfen zusammen h\366chstens 10 MB gro\337 sein.", "Attachments may total no more than 10 MB."));
        return 0;
    }
    strncpy(attachments[*count].path, path,
            sizeof(attachments[*count].path) - 1U);
    attachments[*count].path[sizeof(attachments[*count].path) - 1U] = 0;
    strncpy(attachments[*count].name_local,
            request->rf_File ? (const char *)request->rf_File : "attachment.bin",
            sizeof(attachments[*count].name_local) - 1U);
    attachments[*count].name_local[
        sizeof(attachments[*count].name_local) - 1U] = 0;
    if (local_to_utf8(attachments[*count].name_local,
                      attachments[*count].name_utf8,
                      sizeof(attachments[*count].name_utf8)) != AMG_OK)
        strcpy(attachments[*count].name_utf8, "attachment.bin");
    attachments[*count].size = size;
    attachments[*count].temporary = 0;
    ++*count;
    FreeAslRequest(request);
    rebuild_attachment_list(list_gadget, window, list, attachments, *count);
    update_compose_status(status_gadget, window, attachments, *count);
    return 1;
}

static void remove_attachment(struct Window *window, struct Gadget *list_gadget,
                              struct Gadget *status_gadget, struct List *list,
                              ComposeAttachment *attachments, size_t *count)
{
    ULONG selected = (ULONG)~0UL;
    size_t i;
    GetAttr(LISTBROWSER_Selected, (Object *)list_gadget, &selected);
    if (selected == (ULONG)~0UL || selected >= *count) {
        set_string(status_gadget, window,
                   T("Bitte zuerst eine Anlage ausw\344hlen.", "Please select an attachment first."));
        return;
    }
    if (attachments[selected].temporary && attachments[selected].path[0])
        (void)remove(attachments[selected].path);
    for (i = (size_t)selected; i + 1U < *count; ++i)
        attachments[i] = attachments[i + 1U];
    --*count;
    rebuild_attachment_list(list_gadget, window, list, attachments, *count);
    update_compose_status(status_gadget, window, attachments, *count);
}

static int is_leap_year(unsigned long year)
{
    return (year % 4UL == 0UL && year % 100UL != 0UL) ||
           year % 400UL == 0UL;
}

static unsigned long days_in_year(unsigned long year)
{
    return is_leap_year(year) ? 366UL : 365UL;
}

static int make_date_and_message_id(char date[96], char message_id[256])
{
    static const char *weekdays[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    static const unsigned char month_lengths[] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };
    static unsigned long sequence = 0UL;
    struct DateStamp stamp;
    unsigned long remaining_days, year, month, day;
    unsigned long hour, minute, second, weekday;
    int length;

    DateStamp(&stamp);
    if (stamp.ds_Days < 0 || stamp.ds_Minute < 0 || stamp.ds_Tick < 0)
        return AMG_ERR_IO;

    remaining_days = (unsigned long)stamp.ds_Days;
    weekday = remaining_days % 7UL; /* 1 January 1978 was a Sunday. */
    year = 1978UL;
    while (remaining_days >= days_in_year(year)) {
        remaining_days -= days_in_year(year);
        ++year;
    }
    month = 0UL;
    while (month < 11UL) {
        unsigned long length_of_month = month_lengths[month];
        if (month == 1UL && is_leap_year(year)) ++length_of_month;
        if (remaining_days < length_of_month) break;
        remaining_days -= length_of_month;
        ++month;
    }
    day = remaining_days + 1UL;
    hour = (unsigned long)stamp.ds_Minute / 60UL;
    minute = (unsigned long)stamp.ds_Minute % 60UL;
    second = (unsigned long)stamp.ds_Tick / 50UL;
    if (second > 59UL) second = 59UL;

    /* DateStamp contains local time. -0000 denotes an unknown local offset. */
    length = snprintf(date, 96U,
                      "%s, %02lu %s %04lu %02lu:%02lu:%02lu -0000",
                      weekdays[weekday], day, months[month], year,
                      hour, minute, second);
    if (length < 0 || length >= 96) return AMG_ERR_IO;
    ++sequence;
    length = snprintf(message_id, 256U, "<%08lx.%04lx.%04lx.%04lx@amimail.local>",
                      (unsigned long)stamp.ds_Days,
                      (unsigned long)stamp.ds_Minute,
                      (unsigned long)stamp.ds_Tick, sequence);
    if (length < 0 || length >= 256) return AMG_ERR_IO;
    return AMG_OK;
}

static int queue_composed_mail(AmgGui *gui, struct Window *window,
                               struct Gadget *to_gadget,
                               struct Gadget *cc_gadget,
                               struct Gadget *bcc_gadget,
                               struct Gadget *subject_gadget,
                               struct Gadget *body_gadget,
                               const ComposeAttachment *attachments,
                               size_t attachment_count, ComposeMode mode,
                               const DraftEditData *seed,
                               int save_as_draft, AmgError *error)
{
    char to_utf8[1536], cc_utf8[1536], bcc_utf8[1536];
    char subject_utf8[1024];
    char date[96], message_id[256];
    AmgBuffer body_utf8;
    AmgAttachmentInput inputs[AMG_MAIL_MAX_ATTACHMENTS];
    AmgMailDraft draft;
    const unsigned char *p;
    STRPTR body_local = NULL;
    size_t i;
    int result = AMG_OK;
    if (!save_as_draft && !*string_text(to_gadget) &&
        !*string_text(cc_gadget) && !*string_text(bcc_gadget)) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T("Mindestens ein Empf\303\244nger fehlt.", "At least one recipient is required."));
        return AMG_ERR_ARGUMENT;
    }
    if (local_to_utf8(string_text(to_gadget), to_utf8,
                      sizeof(to_utf8)) != AMG_OK ||
        local_to_utf8(string_text(cc_gadget), cc_utf8,
                      sizeof(cc_utf8)) != AMG_OK ||
        local_to_utf8(string_text(bcc_gadget), bcc_utf8,
                      sizeof(bcc_utf8)) != AMG_OK ||
        local_to_utf8(string_text(subject_gadget), subject_utf8,
                      sizeof(subject_utf8)) != AMG_OK) {
        amg_error_set(error, AMG_ERR_LIMIT, T("Eine Kopfzeile ist zu lang.", "A header line is too long."));
        return AMG_ERR_LIMIT;
    }
    body_local = (STRPTR)(uintptr_t)DoGadgetMethod(
        body_gadget, window, NULL, GM_TEXTEDITOR_ExportText, 0UL);
    if (!body_local) {
        amg_error_set(error, AMG_ERR_MEMORY,
                      T("Nachrichtentext konnte nicht gelesen werden.", "Message text could not be read."));
        return AMG_ERR_MEMORY;
    }
    amg_buffer_init(&body_utf8);
    p = (const unsigned char *)body_local;
    while (*p && result == AMG_OK) {
        unsigned char bytes[2];
        if (*p < 0x80U) {
            result = amg_buffer_append_char(&body_utf8, *p);
        } else {
            bytes[0] = (unsigned char)(0xC0U | (*p >> 6));
            bytes[1] = (unsigned char)(0x80U | (*p & 0x3FU));
            result = amg_buffer_append(&body_utf8, bytes, 2U);
        }
        ++p;
    }
    if (result == AMG_OK) result = amg_buffer_terminate(&body_utf8);
    if (result != AMG_OK) {
        amg_buffer_free(&body_utf8);
        FreeVec(body_local);
        amg_error_set(error, result, T("Mailtext ist zu gro\303\237.", "Mail text is too large."));
        return result;
    }
    for (i = 0; i < attachment_count; ++i) {
        inputs[i].path = attachments[i].path;
        inputs[i].name_utf8 = attachments[i].name_utf8;
        inputs[i].size = attachments[i].size;
        inputs[i].delete_after_use = attachments[i].temporary;
    }
    if (make_date_and_message_id(date, message_id) != AMG_OK) {
        amg_buffer_free(&body_utf8);
        FreeVec(body_local);
        amg_error_set(error, AMG_ERR_IO,
                      T("Datum der Nachricht konnte nicht erzeugt werden.", "Message date could not be generated."));
        return AMG_ERR_IO;
    }
    if (mode == COMPOSE_MODE_EDIT_DRAFT && seed &&
        seed->message_id_utf8[0]) {
        strncpy(message_id, seed->message_id_utf8, sizeof(message_id) - 1U);
        message_id[sizeof(message_id) - 1U] = 0;
    }
    draft.from = gui->account->email;
    draft.to = to_utf8;
    draft.cc = cc_utf8;
    draft.bcc = bcc_utf8;
    draft.subject = subject_utf8;
    draft.body_utf8 = (const char *)body_utf8.data;
    draft.date_rfc2822 = date;
    draft.message_id = message_id;
    if (mode == COMPOSE_MODE_REPLY) {
        draft.in_reply_to = gui->reply_in_reply_to_utf8;
        draft.references = gui->reply_references_utf8;
    } else if (mode == COMPOSE_MODE_EDIT_DRAFT && seed) {
        draft.in_reply_to = seed->in_reply_to_utf8;
        draft.references = seed->references_utf8;
    } else {
        draft.in_reply_to = NULL;
        draft.references = NULL;
    }
    draft.attachments = inputs;
    draft.attachment_count = attachment_count;

    if (!amg_network_is_running(gui->network)) {
        result = amg_network_start(gui->network, gui->account, error);
        if (result == AMG_OK)
            result = amg_network_request(gui->network, AMG_NET_CONNECT, 0,
                                         NULL, NULL, error);
    } else if (save_as_draft &&
               !amg_network_is_connected(gui->network)) {
        result = amg_network_request(gui->network, AMG_NET_CONNECT, 0,
                                     NULL, NULL, error);
    }
    if (result == AMG_OK) {
        if (save_as_draft) {
            const char *draft_mailbox = gui->labels[3U].available
                ? gui->labels[3U].server_mailbox_utf8 : "\\Drafts";
            if (mode == COMPOSE_MODE_EDIT_DRAFT && seed &&
                seed->uid && seed->mailbox_utf8[0])
                result = amg_network_request_draft_update(
                    gui->network, &draft, draft_mailbox,
                    seed->uid, seed->mailbox_utf8, error);
            else
                result = amg_network_request_draft(
                    gui->network, &draft, draft_mailbox, error);
        } else if (mode == COMPOSE_MODE_EDIT_DRAFT && seed &&
                   seed->uid && seed->mailbox_utf8[0]) {
            result = amg_network_request_mail_from_draft(
                gui->network, &draft, seed->uid, seed->mailbox_utf8, error);
        } else {
            result = amg_network_request_mail(gui->network, &draft, error);
        }
    }
    amg_buffer_free(&body_utf8);
    FreeVec(body_local);
    return result;
}

int compose_dialog(AmgGui *gui, ComposeMode mode,
                   DraftEditData *seed, AmgError *error)
{
    Object *dialog;
    struct Window *window;
    struct Gadget *to_gadget, *cc_gadget, *bcc_gadget, *subject_gadget;
    struct Gadget *body_gadget, *body_scroller;
    struct Gadget *attachments_gadget, *attachments_scroller;
    struct Gadget *compose_status;
    struct List attachment_list;
    ComposeAttachment attachments[AMG_MAIL_MAX_ATTACHMENTS];
    size_t attachment_count = 0;
    ULONG signal_mask, compose_width = 600UL, compose_height = 400UL;
    ULONG compose_left = 0UL, compose_top = 0UL;
    const char *initial_to = "";
    const char *initial_cc = "";
    const char *initial_bcc = "";
    const char *initial_subject = "";
    const char *initial_body = "";
    int reply_mode = mode == COMPOSE_MODE_REPLY;
    int edit_draft = mode == COMPOSE_MODE_EDIT_DRAFT;
    int done = 0, submitted = 0, sent_queued = 0;

    memset(attachments, 0, sizeof(attachments));
    if (reply_mode) {
        initial_to = gui->reply_to_local;
        initial_subject = gui->reply_subject_local;
        initial_body = gui->reply_body_local;
    } else if (seed) {
        initial_to = seed->to_local;
        initial_cc = seed->cc_local;
        initial_bcc = seed->bcc_local;
        initial_subject = seed->subject_local;
        initial_body = seed->body_local;
    }
    if (edit_draft && seed && seed->attachment_count) {
        attachment_count = seed->attachment_count;
        if (attachment_count > AMG_MAIL_MAX_ATTACHMENTS)
            attachment_count = AMG_MAIL_MAX_ATTACHMENTS;
        memcpy(attachments, seed->attachments,
               attachment_count * sizeof(attachments[0]));
        /* Ownership of temporary files moves into this compose session. */
        seed->attachment_count = 0U;
    }
    NewList(&attachment_list);
    to_gadget = cc_gadget = bcc_gadget = subject_gadget = NULL;
    body_gadget = body_scroller = NULL;
    attachments_gadget = attachments_scroller = compose_status = NULL;
    if (gui->screen && (ULONG)gui->screen->Width > 40UL) {
        ULONG available_width = (ULONG)gui->screen->Width - 20UL;
        if (compose_width > available_width) compose_width = available_width;
    }
    if (compose_width < 440UL) compose_width = 440UL;
    if (gui->screen && (ULONG)gui->screen->Height > 40UL) {
        ULONG available_height = (ULONG)gui->screen->Height - 20UL;
        if (compose_height > available_height) compose_height = available_height;
    }
    if (compose_height < 300UL) compose_height = 300UL;
    if (gui->screen) {
        if ((ULONG)gui->screen->Width > compose_width)
            compose_left = ((ULONG)gui->screen->Width - compose_width) / 2UL;
        if ((ULONG)gui->screen->Height > compose_height)
            compose_top = ((ULONG)gui->screen->Height - compose_height) / 2UL;
    }

    body_scroller = create_vertical_scroller(GID_COMPOSE_BODY_SCROLL);
    attachments_scroller =
        create_vertical_scroller(GID_COMPOSE_ATTACHMENTS_SCROLL);
    if (!body_scroller || !attachments_scroller) {
        if (attachments_scroller)
            DisposeObject((Object *)attachments_scroller);
        if (body_scroller) DisposeObject((Object *)body_scroller);
        cleanup_compose_temp_attachments(attachments, attachment_count);
        amg_error_set(error, AMG_ERR_MEMORY,
                      T("Scrollbar konnte nicht angelegt werden.", "Scrollbar could not be created."));
        return 0;
    }

    dialog = WindowObject,
        WA_Title, edit_draft
            ? T("AmiMail - Entwurf bearbeiten", "AmiMail - Edit draft")
            : (reply_mode ? T("AmiMail - Antworten", "AmiMail - Reply")
                          : T("AmiMail - Neue Mail", "AmiMail - New mail")),
        WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                      WFLG_SIZEGADGET | WFLG_ACTIVATE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_RAWKEY,
        WA_PubScreen, gui->screen,
        WA_Left, compose_left,
        WA_Top, compose_top,
        WA_Width, compose_width,
        WA_Height, compose_height,
        WA_MinWidth, 440,
        WA_MinHeight, 300,
        WINDOW_ParentGroup, VGroupObject,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_SpaceInner, TRUE,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T("An:", "To:")),
                CHILD_MinWidth, 70,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    to_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_COMPOSE_TO,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 767,
                        STRINGA_TextVal, (ULONG)(uintptr_t)initial_to,
                    EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_COMPOSE_TO_CONTACTS,
                    GA_RelVerify, TRUE,
                    GA_Text, "...",
                EndObject,
                CHILD_MinWidth, 30,
                CHILD_MaxWidth, 30,
                CHILD_WeightedWidth, 0,
                EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label("CC:"),
                CHILD_MinWidth, 70,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    cc_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_COMPOSE_CC,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 767,
                        STRINGA_TextVal, (ULONG)(uintptr_t)initial_cc,
                    EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_COMPOSE_CC_CONTACTS,
                    GA_RelVerify, TRUE,
                    GA_Text, "...",
                EndObject,
                CHILD_MinWidth, 30,
                CHILD_MaxWidth, 30,
                CHILD_WeightedWidth, 0,
                EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label("BCC:"),
                CHILD_MinWidth, 70,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    bcc_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_COMPOSE_BCC,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 767,
                        STRINGA_TextVal, (ULONG)(uintptr_t)initial_bcc,
                    EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_COMPOSE_BCC_CONTACTS,
                    GA_RelVerify, TRUE,
                    GA_Text, "...",
                EndObject,
                CHILD_MinWidth, 30,
                CHILD_MaxWidth, 30,
                CHILD_WeightedWidth, 0,
                EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T("Betreff:", "Subject:")),
                CHILD_MinWidth, 70,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    subject_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_COMPOSE_SUBJECT,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 511,
                        STRINGA_TextVal, (ULONG)(uintptr_t)initial_subject,
                    EndObject,
                EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T("Nachricht:", "Message:")),
                CHILD_MinWidth, 70,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                    LAYOUT_AddChild,
                        body_gadget = (struct Gadget *)TextEditorObject,
                            GA_ID, GID_COMPOSE_BODY,
                            GA_TabCycle, TRUE,
                            GA_TEXTEDITOR_Contents,
                                (ULONG)(uintptr_t)initial_body,
                            GA_TEXTEDITOR_TabSize, 4,
                            GA_TEXTEDITOR_IndentWidth, 4,
                            GA_TEXTEDITOR_TabKeyPolicy,
                                GV_TEXTEDITOR_TabKey_IndentsAfter,
                        EndObject,
                    LAYOUT_AddChild, body_scroller,
                    CHILD_MinWidth, GUI_SCROLLBAR_WIDTH,
                    CHILD_MaxWidth, GUI_SCROLLBAR_WIDTH,
                    CHILD_WeightedWidth, 0,
                EndObject,
                CHILD_MinHeight, 100,
                CHILD_WeightedHeight, 100,
            EndObject,
            CHILD_WeightedHeight, 65,
            CHILD_MinHeight, 100,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T("Anlagen:", "Attachments:")),
                CHILD_MinWidth, 70,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_AddChild, HGroupObject,
                        LAYOUT_SpaceOuter, FALSE,
                        LAYOUT_SpaceInner, FALSE,
                        LAYOUT_AddChild,
                            attachments_gadget =
                                (struct Gadget *)ListBrowserObject,
                                GA_ID, GID_COMPOSE_ATTACHMENTS,
                                GA_RelVerify, TRUE,
                                LISTBROWSER_Labels, &attachment_list,
                                LISTBROWSER_ShowSelected, TRUE,
                                LISTBROWSER_VerticalProp, FALSE,
                            EndObject,
                        LAYOUT_AddChild, attachments_scroller,
                        CHILD_MinWidth, GUI_SCROLLBAR_WIDTH,
                        CHILD_MaxWidth, GUI_SCROLLBAR_WIDTH,
                        CHILD_WeightedWidth, 0,
                    EndObject,
                    CHILD_WeightedWidth, 75,
                    CHILD_MinHeight, 45,

                    LAYOUT_AddChild, VGroupObject,
                        LAYOUT_AddChild, ButtonObject,
                            GA_ID, GID_COMPOSE_ADD_ATTACHMENT,
                            GA_RelVerify, TRUE,
                            GA_Text, T("_Anlage...", "_Attachment..."),
                        EndObject,
                        LAYOUT_AddChild, ButtonObject,
                            GA_ID, GID_COMPOSE_REMOVE_ATTACHMENT,
                            GA_RelVerify, TRUE,
                            GA_Text, T("Ent_fernen", "_Remove"),
                        EndObject,
                    EndObject,
                    CHILD_WeightedWidth, 25,
                EndObject,
            EndObject,
            CHILD_WeightedHeight, 25,

            LAYOUT_AddChild,
                compose_status = (struct Gadget *)StringObject,
                    GA_ID, GID_COMPOSE_STATUS,
                    GA_ReadOnly, TRUE,
                    STRINGA_TextVal, T("0 Anlagen, 0 KB von 10240 KB", "0 attachments, 0 KB of 10240 KB"),
                EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_COMPOSE_SEND,
                    GA_RelVerify, TRUE,
                    GA_Text, T("_Senden", "_Send"),
                EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_COMPOSE_CANCEL,
                    GA_RelVerify, TRUE,
                    GA_Text, T("Ab_brechen", "_Cancel"),
                EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,
        EndObject,
    EndWindow;

    if (!dialog) {
        cleanup_compose_temp_attachments(attachments, attachment_count);
        amg_error_set(error, AMG_ERR_MEMORY,
                      T("Fenster f\303\274r neue Mail konnte nicht erzeugt werden.", "New mail window could not be created."));
        return 0;
    }
    window = RA_OpenWindow(dialog);
    if (!window) {
        DisposeObject(dialog);
        cleanup_compose_temp_attachments(attachments, attachment_count);
        amg_error_set(error, AMG_ERR_IO,
                      T("Fenster f\303\274r neue Mail konnte nicht ge\303\266ffnet werden.", "New mail window could not be opened."));
        return 0;
    }
    /* The composer is modal from AmiMail's point of view.  Explicitly bring
     * it to front so a mailto: request cannot lose a Z-order race against
     * the main window on classic Intuition/ReAction. */
    WindowToFront(window);
    ActivateWindow(window);
    GetAttr(WINDOW_SigMask, dialog, &signal_mask);
    if (attachment_count) {
        rebuild_attachment_list(attachments_gadget, window, &attachment_list,
                                attachments, attachment_count);
        update_compose_status(compose_status, window, attachments,
                              attachment_count);
    }
    sync_texteditor_scroller(window, body_gadget, body_scroller, 0, 0);
    sync_listbrowser_scroller(window, attachments_gadget,
                              attachments_scroller);

    while (!done) {
        ULONG signals = Wait(signal_mask | SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C) done = 1;
        if (signals & signal_mask) {
            ULONG result;
            while ((result = RA_HandleInput(dialog, NULL)) != WMHI_LASTMSG) {
                switch (result & WMHI_CLASSMASK) {
                    case WMHI_CLOSEWINDOW:
                        done = 1;
                        break;

                    case WMHI_RAWKEY:
                        if ((result & WMHI_KEYMASK) == 0x45UL) done = 1;
                        break;

                    case WMHI_GADGETUP:
                        switch (result & WMHI_GADGETMASK) {
                            case GID_COMPOSE_CANCEL:
                                if (confirm_question_dialog_for_window(
                                        gui, window,
                                        T("Wollen Sie den Entwurf speichern?",
                                          "Do you want to save the draft?"),
                                        NULL, 310L)) {
                                    if (queue_composed_mail(
                                            gui, window, to_gadget, cc_gadget,
                                            bcc_gadget, subject_gadget,
                                            body_gadget, attachments,
                                            attachment_count, mode,
                                            seed, 1, error) == AMG_OK) {
                                        submitted = 1;
                                        done = 1;
                                    } else {
                                        set_utf8_string(compose_status, window,
                                                        error->message);
                                    }
                                } else {
                                    done = 1;
                                }
                                break;

                            case GID_COMPOSE_TO_CONTACTS:
                                gui_contacts_select_emails(
                                    gui, window, to_gadget, error);
                                break;

                            case GID_COMPOSE_CC_CONTACTS:
                                gui_contacts_select_emails(
                                    gui, window, cc_gadget, error);
                                break;

                            case GID_COMPOSE_BCC_CONTACTS:
                                gui_contacts_select_emails(
                                    gui, window, bcc_gadget, error);
                                break;

                            case GID_COMPOSE_BODY_SCROLL:
                                handle_texteditor_scroller(
                                    window, body_gadget, body_scroller, 0);
                                break;

                            case GID_COMPOSE_ATTACHMENTS_SCROLL:
                                handle_listbrowser_scroller(
                                    window, attachments_gadget,
                                    attachments_scroller);
                                break;

                            case GID_COMPOSE_ADD_ATTACHMENT:
                                add_attachment(
                                    window, attachments_gadget, compose_status,
                                    &attachment_list, attachments,
                                    &attachment_count);
                                break;

                            case GID_COMPOSE_REMOVE_ATTACHMENT:
                                remove_attachment(
                                    window, attachments_gadget, compose_status,
                                    &attachment_list, attachments,
                                    &attachment_count);
                                break;

                            case GID_COMPOSE_SEND:
                                if (queue_composed_mail(
                                        gui, window, to_gadget, cc_gadget,
                                        bcc_gadget,
                                        subject_gadget, body_gadget,
                                        attachments, attachment_count,
                                        mode, seed, 0, error) == AMG_OK) {
                                    submitted = 1;
                                    sent_queued = 1;
                                    done = 1;
                                } else {
                                    set_utf8_string(compose_status, window,
                                                    error->message);
                                }
                                break;
                        }
                        break;
                }
            }
            if (!done) {
                sync_texteditor_scroller(
                    window, body_gadget, body_scroller, 0, 0);
                sync_listbrowser_scroller(
                    window, attachments_gadget, attachments_scroller);
            }
        }
    }
    DisposeObject(dialog);
    FreeListBrowserList(&attachment_list);
    if (!submitted)
        cleanup_compose_temp_attachments(attachments, attachment_count);
    if (sent_queued)
        status_local(gui, T("Mail wird gesendet...", "Sending mail..."));
    else if (submitted)
        status_local(gui, T("Entwurf wird gespeichert...", "Saving draft..."));
    return submitted;
}

#endif /* AMIGMAIL_AMIGA */
