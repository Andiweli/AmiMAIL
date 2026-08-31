#include "gui_internal.h"
#include "imap.h"
#include "imap_parser.h"
#include "mime.h"
#include "i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if AMIGMAIL_AMIGA
#include <clib/alib_protos.h>
#include <exec/lists.h>
#include <gadgets/listbrowser.h>
#include <gadgets/scroller.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/listbrowser.h>
#include <proto/scroller.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <utility/tagitem.h>
#define T(id, en) amg_tr((id), (en))
static const char message_flag_marker[] = { (char)0xb7, '\0' };
static struct Node *message_node(AmgGui *gui,
                                 const char *from, const char *subject,
                                 const char *date, unsigned long size_bytes,
                                 ULONG uid, int seen, int flagged,
                                 LONG unread_pen, LONG text_pen)
{
    char size[32];
    unsigned long size_kb = size_bytes / 1024UL +
                            (size_bytes % 1024UL ? 1UL : 0UL);
    snprintf(size, sizeof(size), "%lu KB", size_kb);
    return AllocListBrowserNode(
        5,
        LBNA_UserData, uid,
        LBNA_Flags, seen ? 0UL : LBFLG_CUSTOMPENS,
        LBNA_Column, 0,
        LBNCA_CopyText, TRUE,
        LBNCA_Text,
            (ULONG)(uintptr_t)(flagged ? message_flag_marker : ""),
        LBNCA_HorizJustify, LCJ_CENTRE,
        LBNCA_FGPen, (ULONG)text_pen,
        LBNCA_RenderHook,
            gui ? (ULONG)(uintptr_t)&gui->message_flag_render_hook : 0UL,
        LBNCA_HookHeight,
            gui && gui->list_row_hook_height ? gui->list_row_hook_height : 10U,
        LBNA_Column, 1,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)from,
        LBNCA_FGPen, (ULONG)unread_pen,
        LBNCA_VertJustify, LRJ_CENTER,
        LBNA_Column, 2,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)subject,
        LBNCA_FGPen, (ULONG)unread_pen,
        LBNCA_VertJustify, LRJ_CENTER,
        LBNA_Column, 3,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)date,
        LBNCA_FGPen, (ULONG)unread_pen,
        LBNCA_VertJustify, LRJ_CENTER,
        LBNA_Column, 4,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)size,
        LBNCA_FGPen, (ULONG)unread_pen,
        LBNCA_VertJustify, LRJ_CENTER,
        TAG_DONE);
}

static void insert_message_node_date_desc(struct List *list,
                                          struct Node *node,
                                          const char *date, ULONG uid)
{
    struct Node *current, *previous = NULL;
    const char *new_date = date ? date : "";
    if (!list || !node) return;

    current = list->lh_Head;
    while (current && current->ln_Succ) {
        STRPTR current_date = NULL;
        ULONG current_uid = 0;
        int comparison;
        GetListBrowserNodeAttrs(
            current,
            LBNA_UserData, (ULONG)(uintptr_t)&current_uid,
            LBNA_Column, 3,
            LBNCA_Text, (ULONG)(uintptr_t)&current_date,
            TAG_DONE);
        comparison = strcmp(new_date, current_date ? (const char *)current_date : "");
        if (comparison > 0 || (comparison == 0 && uid > current_uid))
            break;
        previous = current;
        current = current->ln_Succ;
    }
    Insert(list, node, previous);
}

struct Node *message_placeholder_node(const char *text)
{
    char wrapped[256];
    const char *source = text ? text : "";
    size_t length = strlen(source);
    size_t i, split = 0;

    if (length >= sizeof(wrapped)) length = sizeof(wrapped) - 1U;
    memcpy(wrapped, source, length);
    wrapped[length] = 0;
    if (!strchr(wrapped, '\n')) {
        size_t middle = length / 2U;
        size_t distance = length + 1U;
        for (i = 0; i < length; ++i) {
            size_t current_distance;
            if (wrapped[i] != ' ') continue;
            current_distance = i > middle ? i - middle : middle - i;
            if (current_distance < distance) {
                split = i;
                distance = current_distance;
            }
        }
        if (split) wrapped[split] = '\n';
    }
    return AllocListBrowserNode(
        5,
        LBNA_UserData, 0,
        LBNA_Column, 0,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)"",
        LBNA_Column, 1,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)"",
        LBNA_Column, 2,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)wrapped,
        LBNCA_Justification, LCJ_CENTRE,
        LBNCA_VertJustify, LRJ_CENTER,
        LBNCA_WordWrap, TRUE,
        LBNA_Column, 3,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)"",
        LBNA_Column, 4,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)"",
        TAG_DONE);
}

static void trim_local_text(char *text)
{
    char *start, *end;
    size_t length;
    if (!text) return;
    start = text;
    while (*start == ' ' || *start == '\t') ++start;
    if (start != text) memmove(text, start, strlen(start) + 1U);
    length = strlen(text);
    end = text + length;
    while (end > text && (end[-1] == ' ' || end[-1] == '\t')) --end;
    *end = 0;
}

static int current_mailbox_is_sent(const AmgGui *gui)
{
    size_t i;
    if (!gui || !gui->current_mailbox_utf8[0]) return 0;

    for (i = 0U; i < gui->label_count; ++i) {
        const GuiLabel *label = &gui->labels[i];
        if (!(label->special_use & AMG_LABEL_SENT)) continue;
        if ((label->mailbox_utf8[0] &&
             !strcmp(gui->current_mailbox_utf8, label->mailbox_utf8)) ||
            (label->server_mailbox_utf8[0] &&
             !strcmp(gui->current_mailbox_utf8,
                     label->server_mailbox_utf8)))
            return 1;
    }

    /* Keep manually configured Sent folders working even if the server did
     * not advertise a SPECIAL-USE \Sent attribute. */
    if (gui->account && gui->account->sent_mailbox[0] &&
        !strcmp(gui->current_mailbox_utf8, gui->account->sent_mailbox))
        return 1;

    return 0;
}

static const char *message_party_header(const AmgGui *gui,
                                        const AmgMailHeaders *headers)
{
    const char *value;
    if (!headers) return NULL;
    if (!current_mailbox_is_sent(gui))
        return amg_mail_header_get(headers, "From");

    value = amg_mail_header_get(headers, "To");
    if (value && *value) return value;
    value = amg_mail_header_get(headers, "Cc");
    if (value && *value) return value;
    return amg_mail_header_get(headers, "Bcc");
}

static void update_message_party_column_title(AmgGui *gui)
{
    const char *title;
    if (!gui || !gui->columns) return;
    title = current_mailbox_is_sent(gui)
        ? T(MSG_RECIPIENT, "Recipient")
        : T(MSG_SENDER_F4D4, "Sender");
    SetLBColumnInfoAttrs(gui->columns,
                         LBCIA_Column, 1,
                         LBCIA_Title, (ULONG)(uintptr_t)title,
                         TAG_DONE);
}

static void address_name_only(char *sender, size_t capacity)
{
    char name[513];
    char *angle, *close_angle, *open_parenthesis;
    size_t length;
    if (!sender || !capacity) return;
    angle = strchr(sender, '<');
    if (angle) {
        length = (size_t)(angle - sender);
        if (length >= sizeof(name)) length = sizeof(name) - 1U;
        memcpy(name, sender, length);
        name[length] = 0;
        trim_local_text(name);
        length = strlen(name);
        if (length >= 2U && name[0] == '"' && name[length - 1U] == '"') {
            memmove(name, name + 1, length - 2U);
            name[length - 2U] = 0;
            trim_local_text(name);
        }
        if (name[0]) {
            strncpy(sender, name, capacity - 1U);
            sender[capacity - 1U] = 0;
            return;
        }
        close_angle = strchr(angle + 1, '>');
        if (close_angle) {
            length = (size_t)(close_angle - (angle + 1));
            if (length >= sizeof(name)) length = sizeof(name) - 1U;
            memcpy(name, angle + 1, length);
            name[length] = 0;
            trim_local_text(name);
            if (name[0]) {
                strncpy(sender, name, capacity - 1U);
                sender[capacity - 1U] = 0;
                return;
            }
        }
    }
    length = strlen(sender);
    open_parenthesis = strrchr(sender, '(');
    if (open_parenthesis && length && sender[length - 1U] == ')' &&
        strchr(sender, '@')) {
        size_t name_length = (size_t)(sender + length - 1U -
                                      (open_parenthesis + 1));
        if (name_length >= sizeof(name)) name_length = sizeof(name) - 1U;
        memcpy(name, open_parenthesis + 1, name_length);
        name[name_length] = 0;
        trim_local_text(name);
        if (name[0]) {
            strncpy(sender, name, capacity - 1U);
            sender[capacity - 1U] = 0;
        }
    }
}

 void default_messages(AmgGui *gui)
{
    struct Node *node = message_placeholder_node(
        T(MSG_NOT_CONNECTED_YET_FETCH_STARTS_THE_MAIL_SERVER, "Not connected yet - 'Fetch' starts the mail server connection."));
    if (node) AddTail(&gui->messages_list, node);
}

static void attach_messages_default_date_sort(AmgGui *gui)
{
    if (!gui || !gui->messages_gadget) return;
    update_message_party_column_title(gui);
    if (gui->columns) {
        SetLBColumnInfoAttrs(
            gui->columns,
            LBCIA_Column, 3,
            LBCIA_SortDirection, LBMSORT_REVERSE,
            TAG_DONE);
    }
    if (gui->window)
        SetGadgetAttrs(
            gui->messages_gadget, gui->window, NULL,
            LISTBROWSER_ColumnInfo, (ULONG)(uintptr_t)gui->columns,
            LISTBROWSER_Labels, (ULONG)(uintptr_t)&gui->messages_list,
            LISTBROWSER_Selected, (ULONG)~0UL,
            LISTBROWSER_Top, 0,
            LISTBROWSER_SortColumn, 3,
            TAG_DONE);
    else
        SetAttrs((Object *)gui->messages_gadget,
            LISTBROWSER_ColumnInfo, (ULONG)(uintptr_t)gui->columns,
            LISTBROWSER_Labels, (ULONG)(uintptr_t)&gui->messages_list,
            LISTBROWSER_Selected, (ULONG)~0UL,
            LISTBROWSER_Top, 0,
            LISTBROWSER_SortColumn, 3,
            TAG_DONE);
}

 void show_message_placeholder(AmgGui *gui, const char *text)
{
    struct Node *node;
    gui->active_message_uid = 0;
    gui->message_click_valid = 0;
    gui->message_click_uid = 0UL;
    detach_listbrowser(gui->messages_gadget, gui->window);
    FreeListBrowserList(&gui->messages_list);
    NewList(&gui->messages_list);
    node = message_placeholder_node(text);
    if (node) AddTail(&gui->messages_list, node);
    update_message_party_column_title(gui);
    attach_listbrowser(gui->messages_gadget, gui->window,
                       &gui->messages_list);
}

static unsigned mail_month_number(const char month[4])
{
    static const char *names[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    unsigned i;
    for (i = 0; i < 12U; ++i) {
        if (((month[0] | 0x20) == (names[i][0] | 0x20)) &&
            ((month[1] | 0x20) == (names[i][1] | 0x20)) &&
            ((month[2] | 0x20) == (names[i][2] | 0x20)))
            return i + 1U;
    }
    return 0;
}

static void format_mail_date(const char *header, char *local, size_t capacity)
{
    const char *date = header;
    const char *comma;
    char month_text[4];
    unsigned long day, year, hour, minute;
    unsigned month;
    if (!local || !capacity) return;
    local[0] = 0;
    if (!date || !*date) return;
    comma = strchr(date, ',');
    if (comma) date = comma + 1;
    while (*date == ' ' || *date == '\t') ++date;
    if (sscanf(date, "%lu %3s %lu %lu:%lu",
               &day, month_text, &year, &hour, &minute) == 5) {
        month = mail_month_number(month_text);
        if (month && day >= 1UL && day <= 31UL &&
            hour <= 23UL && minute <= 59UL) {
            snprintf(local, capacity, "%04lu-%02u-%02lu %02lu:%02lu",
                     year, month, day, hour, minute);
            return;
        }
    }
    utf8_to_local_copy(header, local, capacity);
}

 size_t message_uid_stats(const unsigned char *payload, size_t length,
                                unsigned long baseline,
                                unsigned long *max_uid,
                                int *parse_error)
{
    size_t position = 0U, newer = 0U;
    int result = 0;
    AmgImapFetchRecord record;
    unsigned long maximum = 0UL;
    if (parse_error) *parse_error = 0;
    while (length > 0U &&
           (result = amg_imap_fetch_record_next(
                payload, length, &position, &record)) > 0) {
        if (record.deleted) continue;
        if (record.uid > maximum) maximum = record.uid;
        if (record.uid > baseline) ++newer;
    }
    if (result < 0 && parse_error) *parse_error = result;
    if (max_uid) *max_uid = maximum;
    return newer;
}

 size_t message_unseen_count_from_payload(const unsigned char *payload,
                                        size_t length, int *parse_error)
{
    size_t position = 0U, unseen = 0U;
    int result = 0;
    AmgImapFetchRecord record;
    if (parse_error) *parse_error = 0;
    while (length > 0U &&
           (result = amg_imap_fetch_record_next(
                payload, length, &position, &record)) > 0) {
        if (!record.seen) ++unseen;
    }
    if (result < 0 && parse_error) *parse_error = result;
    return unseen;
}

size_t update_messages_from_payload(AmgGui *gui,
                                           const unsigned char *payload,
                                           size_t length, int *parse_error)
{
    size_t position = 0, count = 0;
    int result = 0;
    AmgImapFetchRecord record;
    if (parse_error) *parse_error = 0;
    gui->active_message_uid = 0;
    gui->message_click_valid = 0;
    gui->message_click_uid = 0UL;
    detach_listbrowser(gui->messages_gadget, gui->window);
    FreeListBrowserList(&gui->messages_list);
    NewList(&gui->messages_list);

    /* Ein leerer IMAP-FETCH-Payload ist der normale Zustand eines leeren
     * Ordners. AmgBuffer darf bei length == 0 einen NULL-Datenzeiger liefern;
     * diesen Fall nicht als AMG_ERR_ARGUMENT (-1) an den Parser weiterreichen. */
    while (length > 0U &&
           (result = amg_imap_fetch_record_next(
                payload, length, &position, &record)) > 0) {
        AmgMailHeaders headers;
        const char *party_header, *subject_header, *date_header;
        char from[513], subject[769], date[160];
        struct Node *node;
        if (record.deleted) continue;
        amg_mail_headers_init(&headers);
        if (amg_mail_headers_parse((const char *)record.literal,
                                   record.literal_length, &headers,
                                   NULL) != AMG_OK) {
            amg_mail_headers_free(&headers);
            continue;
        }
        party_header = message_party_header(gui, &headers);
        subject_header = amg_mail_header_get(&headers, "Subject");
        date_header = amg_mail_header_get(&headers, "Date");
        header_to_local(
            party_header,
            current_mailbox_is_sent(gui)
                ? ""
                : T(MSG_UNKNOWN_SENDER_CCD6, "(Unknown sender)"),
            from, sizeof(from));
        address_name_only(from, sizeof(from));
        header_to_local(subject_header, T(MSG_NO_SUBJECT, "(No subject)"),
                        subject, sizeof(subject));
        format_mail_date(date_header, date, sizeof(date));
        node = message_node(gui, from, subject, date,
                            record.rfc822_size, record.uid,
                            record.seen, record.flagged,
                            gui->unread_pen, gui->text_pen);
        if (node) {
            /* Die Anzeige soll unabhaengig von der FETCH-Reihenfolge immer
             * mit der neuesten Datumsspalte beginnen. Das ISO-Format aus
             * format_mail_date() ist lexikographisch chronologisch. */
            insert_message_node_date_desc(
                &gui->messages_list, node, date, record.uid);
            ++count;
        }
        amg_mail_headers_free(&headers);
    }
    if (result < 0 && parse_error) *parse_error = result;
    if (!count) {
        const char *message = result < 0
            ? T(MSG_THE_FOLDER_COULD_NOT_BE_PARSED, "The folder could not be parsed.")
            : T(MSG_THIS_FOLDER_CONTAINS_NO_MESSAGES, "This folder contains no messages.");
        struct Node *node = message_placeholder_node(message);
        if (node) AddTail(&gui->messages_list, node);
    }
    attach_messages_default_date_sort(gui);
    return count;
}

 int selected_node_user_data(struct Gadget *gadget, ULONG *user_data)
{
    struct Node *node = NULL;
    ULONG value = 0;
    if (!gadget || !user_data) return 0;
    GetAttr(LISTBROWSER_SelectedNode, (Object *)gadget, (ULONG *)&node);
    if (!node) return 0;
    GetListBrowserNodeAttrs(
        node, LBNA_UserData, (ULONG)(uintptr_t)&value, TAG_DONE);
    *user_data = value;
    return 1;
}

 int cursor_node_user_data(struct Gadget *gadget, ULONG *user_data)
{
    struct Node *node = NULL;
    ULONG value = 0;
    if (!gadget || !user_data) return 0;
    GetAttr(LISTBROWSER_CursorNode, (Object *)gadget, (ULONG *)&node);
    if (!node)
        GetAttr(LISTBROWSER_SelectedNode, (Object *)gadget, (ULONG *)&node);
    if (!node) return 0;
    GetListBrowserNodeAttrs(
        node, LBNA_UserData, (ULONG)(uintptr_t)&value, TAG_DONE);
    *user_data = value;
    return 1;
}

 size_t merge_new_messages_from_payload(AmgGui *gui,
                                              const unsigned char *payload,
                                              size_t length,
                                              int *parse_error)
{
    size_t position = 0U, added = 0U;
    int result = 0;
    ULONG old_top = 0UL;
    ULONG selected_uid;
    AmgImapFetchRecord record;
    struct Node *node, *next;

    if (parse_error) *parse_error = 0;
    if (!gui || !gui->messages_gadget) return 0U;
    selected_uid = gui->active_message_uid;
    GetAttr(LISTBROWSER_Top, (Object *)gui->messages_gadget, &old_top);
    detach_listbrowser(gui->messages_gadget, gui->window);

    /* Platzhalter aus einer zuvor leeren Inbox entfernen, sobald echte
     * Nachrichten eintreffen. */
    node = gui->messages_list.lh_Head;
    while (node && node->ln_Succ) {
        ULONG uid = 0UL;
        next = node->ln_Succ;
        GetListBrowserNodeAttrs(
            node, LBNA_UserData, (ULONG)(uintptr_t)&uid, TAG_DONE);
        if (uid == 0UL) {
            Remove(node);
            FreeListBrowserNode(node);
        }
        node = next;
    }

    while (length > 0U &&
           (result = amg_imap_fetch_record_next(
                payload, length, &position, &record)) > 0) {
        AmgMailHeaders headers;
        const char *party_header, *subject_header, *date_header;
        char from[513], subject[769], date[160];
        if (record.deleted) continue;
        if (find_node_by_user_data(&gui->messages_list, record.uid))
            continue;
        amg_mail_headers_init(&headers);
        if (amg_mail_headers_parse((const char *)record.literal,
                                   record.literal_length, &headers,
                                   NULL) != AMG_OK) {
            amg_mail_headers_free(&headers);
            continue;
        }
        party_header = message_party_header(gui, &headers);
        subject_header = amg_mail_header_get(&headers, "Subject");
        date_header = amg_mail_header_get(&headers, "Date");
        header_to_local(
            party_header,
            current_mailbox_is_sent(gui)
                ? ""
                : T(MSG_UNKNOWN_SENDER_CCD6, "(Unknown sender)"),
            from, sizeof(from));
        address_name_only(from, sizeof(from));
        header_to_local(subject_header,
                        T(MSG_NO_SUBJECT, "(No subject)"),
                        subject, sizeof(subject));
        format_mail_date(date_header, date, sizeof(date));
        node = message_node(gui, from, subject, date,
                            record.rfc822_size, record.uid,
                            record.seen, record.flagged,
                            gui->unread_pen, gui->text_pen);
        if (node) {
            insert_message_node_date_desc(
                &gui->messages_list, node, date, record.uid);
            ++added;
        }
        amg_mail_headers_free(&headers);
    }
    if (result < 0 && parse_error) *parse_error = result;

    if (!gui->messages_list.lh_Head->ln_Succ) {
        node = message_placeholder_node(
            T(MSG_THIS_FOLDER_CONTAINS_NO_MESSAGES, "This folder contains no messages."));
        if (node) AddTail(&gui->messages_list, node);
    }

    update_message_party_column_title(gui);
    if (gui->columns) {
        SetLBColumnInfoAttrs(gui->columns,
                             LBCIA_Column, 3,
                             LBCIA_SortDirection, LBMSORT_REVERSE,
                             TAG_DONE);
    }
    if (gui->window)
        SetGadgetAttrs(gui->messages_gadget, gui->window, NULL,
                       LISTBROWSER_ColumnInfo,
                           (ULONG)(uintptr_t)gui->columns,
                       LISTBROWSER_Labels,
                           (ULONG)(uintptr_t)&gui->messages_list,
                       LISTBROWSER_Selected, (ULONG)~0UL,
                       LISTBROWSER_Top,
                           old_top ? old_top + (ULONG)added : 0UL,
                       LISTBROWSER_SortColumn, 3,
                       TAG_DONE);
    else
        SetAttrs((Object *)gui->messages_gadget,
                 LISTBROWSER_ColumnInfo, (ULONG)(uintptr_t)gui->columns,
                 LISTBROWSER_Labels,
                     (ULONG)(uintptr_t)&gui->messages_list,
                 LISTBROWSER_Selected, (ULONG)~0UL,
                 LISTBROWSER_Top, old_top ? old_top + (ULONG)added : 0UL,
                 LISTBROWSER_SortColumn, 3,
                 TAG_DONE);
    if (selected_uid)
        set_message_selected_visual(gui, selected_uid);
    sync_messages_scroller(gui);
    return added;
}


void normalize_message_selection_for_click(AmgGui *gui)
{
    struct Node *target = NULL;
    struct Node *node;
    ULONG release_event = LBRE_NORMAL;
    ULONG target_uid = 0UL;
    int changed = 0;

    if (!gui || !gui->messages_gadget) return;

    GetAttr(LISTBROWSER_RelEvent, (Object *)gui->messages_gadget,
            &release_event);
    if (release_event == LBRE_TITLECLICK) return;

    GetAttr(LISTBROWSER_CursorNode, (Object *)gui->messages_gadget,
            (ULONG *)&target);
    if (!target)
        GetAttr(LISTBROWSER_SelectedNode, (Object *)gui->messages_gadget,
                (ULONG *)&target);
    if (!target) return;

    GetListBrowserNodeAttrs(
        target, LBNA_UserData, (ULONG)(uintptr_t)&target_uid, TAG_DONE);
    if (!target_uid) return;

    node = gui->messages_list.lh_Head;
    while (node && node->ln_Succ) {
        ULONG selected = FALSE;
        ULONG desired = node == target ? TRUE : FALSE;
        GetListBrowserNodeAttrs(
            node, LBNA_Selected, (ULONG)(uintptr_t)&selected, TAG_DONE);
        if (selected != desired) {
            struct TagItem tags[2];
            struct lbEditNode edit;
            tags[0].ti_Tag = LBNA_Selected;
            tags[0].ti_Data = desired;
            tags[1].ti_Tag = TAG_DONE;
            tags[1].ti_Data = 0UL;
            edit.MethodID = LBM_EDITNODE;
            edit.lbe_GInfo = NULL;
            edit.lbe_Node = node;
            edit.lbe_NodeAttrs = tags;
            if (gui->window)
                (void)DoGadgetMethodA(gui->messages_gadget, gui->window,
                                      NULL, (Msg)&edit);
            else
                SetListBrowserNodeAttrsA(node, tags);
            changed = 1;
        }
        node = node->ln_Succ;
    }

    if (gui->window) {
        SetGadgetAttrs(gui->messages_gadget, gui->window, NULL,
                       LISTBROWSER_SelectedNode, (ULONG)(uintptr_t)target,
                       TAG_DONE);
        if (changed)
            RefreshGList(gui->messages_gadget, gui->window, NULL, 1);
    } else {
        SetAttrs((Object *)gui->messages_gadget,
                 LISTBROWSER_SelectedNode, (ULONG)(uintptr_t)target,
                 TAG_DONE);
    }
}

 size_t selected_message_uids(AmgGui *gui, ULONG *uids,
                                    size_t capacity)
{
    struct Node *node;
    size_t count = 0;
    ULONG fallback = 0;
    if (!gui || !uids || !capacity) return 0;
    node = gui->messages_list.lh_Head;
    while (node && node->ln_Succ) {
        ULONG selected = FALSE;
        ULONG uid = 0;
        GetListBrowserNodeAttrs(
            node,
            LBNA_Selected, (ULONG)(uintptr_t)&selected,
            LBNA_UserData, (ULONG)(uintptr_t)&uid,
            TAG_DONE);
        if (selected && uid && count < capacity) uids[count++] = uid;
        node = node->ln_Succ;
    }
    if (!count && cursor_node_user_data(gui->messages_gadget, &fallback) &&
        fallback)
        uids[count++] = fallback;
    return count;
}

 ULONG *selected_message_uids_alloc(AmgGui *gui, size_t *count)
{
    struct Node *node;
    size_t capacity = 0U;
    ULONG *uids;
    if (count) *count = 0U;
    if (!gui || !count) return NULL;
    node = gui->messages_list.lh_Head;
    while (node && node->ln_Succ) {
        ++capacity;
        node = node->ln_Succ;
    }
    if (capacity < 1U) capacity = 1U;
    uids = (ULONG *)calloc(capacity, sizeof(*uids));
    if (!uids) return NULL;
    *count = selected_message_uids(gui, uids, capacity);
    return uids;
}

 int message_is_seen(AmgGui *gui, ULONG uid)
{
    struct Node *node = find_node_by_user_data(&gui->messages_list, uid);
    ULONG flags = 0;
    if (!node) return 1;
    GetListBrowserNodeAttrs(
        node, LBNA_Flags, (ULONG)(uintptr_t)&flags, TAG_DONE);
    return (flags & LBFLG_CUSTOMPENS) == 0;
}

 int message_is_flagged(AmgGui *gui, ULONG uid)
{
    struct Node *node = find_node_by_user_data(&gui->messages_list, uid);
    ULONG text_value = 0;
    const char *text;
    if (!node) return 0;
    GetListBrowserNodeAttrs(
        node,
        LBNA_Column, 0,
        LBNCA_Text, (ULONG)(uintptr_t)&text_value,
        TAG_DONE);
    text = (const char *)(uintptr_t)text_value;
    return text && text[0];
}

 void set_message_seen_visual(AmgGui *gui, ULONG uid, int seen)
{
    struct Node *node;
    struct TagItem tags[3];
    struct lbEditNode edit;
    if (!gui || !gui->messages_gadget) return;
    node = find_node_by_user_data(&gui->messages_list, uid);
    if (!node) return;
    tags[0].ti_Tag = LBNA_Flags;
    tags[0].ti_Data = seen ? 0UL : LBFLG_CUSTOMPENS;
    if (uid == gui->active_message_uid) {
        /* Der Wechsel der Lesefarbe darf die aktive Zeilenmarkierung nicht
         * verlieren. Beide Attribute werden deshalb atomar aktualisiert. */
        tags[1].ti_Tag = LBNA_Selected;
        tags[1].ti_Data = TRUE;
        tags[2].ti_Tag = TAG_DONE;
        tags[2].ti_Data = 0;
    } else {
        tags[1].ti_Tag = TAG_DONE;
        tags[1].ti_Data = 0;
    }
    edit.MethodID = LBM_EDITNODE;
    edit.lbe_GInfo = NULL;
    edit.lbe_Node = node;
    edit.lbe_NodeAttrs = tags;
    if (gui->window) {
        (void)DoGadgetMethodA(gui->messages_gadget, gui->window, NULL,
                              (Msg)&edit);
        RefreshGList(gui->messages_gadget, gui->window, NULL, 1);
    } else {
        SetListBrowserNodeAttrsA(node, tags);
    }
}

 void set_message_flagged_visual(AmgGui *gui, ULONG uid, int flagged)
{
    struct Node *node;
    struct TagItem tags[5];
    struct lbEditNode edit;
    size_t tag_count = 0;
    if (!gui || !gui->messages_gadget) return;
    node = find_node_by_user_data(&gui->messages_list, uid);
    if (!node) return;
    tags[tag_count].ti_Tag = LBNA_Column;
    tags[tag_count++].ti_Data = 0;
    tags[tag_count].ti_Tag = LBNCA_CopyText;
    tags[tag_count++].ti_Data = TRUE;
    tags[tag_count].ti_Tag = LBNCA_Text;
    tags[tag_count++].ti_Data =
        (ULONG)(uintptr_t)(flagged ? message_flag_marker : "");
    if (uid == gui->active_message_uid) {
        tags[tag_count].ti_Tag = LBNA_Selected;
        tags[tag_count++].ti_Data = TRUE;
    }
    tags[tag_count].ti_Tag = TAG_DONE;
    tags[tag_count].ti_Data = 0;
    edit.MethodID = LBM_EDITNODE;
    edit.lbe_GInfo = NULL;
    edit.lbe_Node = node;
    edit.lbe_NodeAttrs = tags;
    if (gui->window) {
        (void)DoGadgetMethodA(gui->messages_gadget, gui->window, NULL,
                              (Msg)&edit);
        RefreshGList(gui->messages_gadget, gui->window, NULL, 1);
    } else {
        SetListBrowserNodeAttrsA(node, tags);
    }
}

 void set_message_selected_visual(AmgGui *gui, ULONG uid)
{
    struct Node *node;
    struct TagItem tags[2];
    struct lbEditNode edit;
    if (!gui || !gui->messages_gadget) return;
    node = find_node_by_user_data(&gui->messages_list, uid);
    if (!node) return;
    gui->active_message_uid = uid;
    tags[0].ti_Tag = LBNA_Selected;
    tags[0].ti_Data = TRUE;
    tags[1].ti_Tag = TAG_DONE;
    tags[1].ti_Data = 0;
    edit.MethodID = LBM_EDITNODE;
    edit.lbe_GInfo = NULL;
    edit.lbe_Node = node;
    edit.lbe_NodeAttrs = tags;
    if (gui->window) {
        (void)DoGadgetMethodA(gui->messages_gadget, gui->window, NULL,
                              (Msg)&edit);
        RefreshGList(gui->messages_gadget, gui->window, NULL, 1);
    } else {
        SetListBrowserNodeAttrsA(node, tags);
    }
}

static void message_scroll_geometry(AmgGui *gui, ULONG *top_out,
                                    ULONG *total_out, ULONG *visible_out)
{
    ULONG current = 0, max_top = 0, total = 0, visible = 1;
    if (!gui || !gui->messages_gadget) {
        if (top_out) *top_out = 0;
        if (total_out) *total_out = 1;
        if (visible_out) *visible_out = 1;
        return;
    }

    GetAttr(LISTBROWSER_TotalVisibleNodes,
            (Object *)gui->messages_gadget, &total);
    if (total < 1U) total = 1U;
    GetAttr(LISTBROWSER_Top, (Object *)gui->messages_gadget, &current);

    /* Absichtlich einen zu grossen Top-Wert setzen. ListBrowser clamp't
     * ihn auf den echten letzten Seitenanfang. Ohne Refresh ist dieser
     * kurze Probe-Zustand nicht sichtbar. */
    SetGadgetAttrs(gui->messages_gadget, gui->window, NULL,
                   LISTBROWSER_Top, 0x7fffffffUL,
                   TAG_DONE);
    GetAttr(LISTBROWSER_Top, (Object *)gui->messages_gadget, &max_top);
    if (current > max_top) current = max_top;
    SetGadgetAttrs(gui->messages_gadget, gui->window, NULL,
                   LISTBROWSER_Top, current,
                   TAG_DONE);

    if (max_top == 0U || total <= max_top) {
        visible = total;
        current = 0U;
    } else {
        visible = total - max_top;
        if (visible < 1U) visible = 1U;
        if (visible > total) visible = total;
    }
    if (top_out) *top_out = current;
    if (total_out) *total_out = total;
    if (visible_out) *visible_out = visible;
}

 void sync_messages_scroller(AmgGui *gui)
{
    ULONG top = 0, total = 1, visible = 1;
    if (!gui || !gui->window || !gui->messages_gadget ||
        !gui->messages_scroller)
        return;

    message_scroll_geometry(gui, &top, &total, &visible);
    if (total <= visible) {
        SetGadgetAttrs(gui->messages_gadget, gui->window, NULL,
                       LISTBROWSER_Top, 0,
                       TAG_DONE);
        set_scroller_full(gui->window, gui->messages_scroller);
        return;
    }
    if (top > total - visible) top = total - visible;
    SetGadgetAttrs(gui->messages_scroller, gui->window, NULL,
                   SCROLLER_Top, top,
                   SCROLLER_Total, total,
                   SCROLLER_Visible, visible,
                   TAG_DONE);
    RefreshGList(gui->messages_scroller, gui->window, NULL, 1);
}

 void handle_messages_scroller(AmgGui *gui)
{
    ULONG top = 0, total = 1, visible = 1;
    if (!gui || !gui->window || !gui->messages_gadget ||
        !gui->messages_scroller)
        return;

    message_scroll_geometry(gui, NULL, &total, &visible);
    GetAttr(SCROLLER_Top, (Object *)gui->messages_scroller, &top);
    if (total <= visible) top = 0U;
    else if (top > total - visible) top = total - visible;
    SetGadgetAttrs(gui->messages_gadget, gui->window, NULL,
                   LISTBROWSER_Top, top,
                   TAG_DONE);
    RefreshGList(gui->messages_gadget, gui->window, NULL, 1);

    /* Nicht erneut aus einer geschaetzten Geometrie zurueckskalieren.
     * Der gerade gesetzte Top-Wert und die vom ListBrowser ermittelte
     * letzte Seite verwenden dieselbe Einheit. */
    SetGadgetAttrs(gui->messages_scroller, gui->window, NULL,
                   SCROLLER_Top, top,
                   SCROLLER_Total, total,
                   SCROLLER_Visible, visible,
                   TAG_DONE);
    RefreshGList(gui->messages_scroller, gui->window, NULL, 1);
}

#endif /* AMIGMAIL_AMIGA */
