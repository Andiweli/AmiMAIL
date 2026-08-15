#include "gui_internal.h"
#include "i18n.h"
#include "imap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if AMIGMAIL_AMIGA
#include <clib/alib_protos.h>
#include <gadgets/listbrowser.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/listbrowser.h>
#include <reaction/reaction.h>
#define T(de,en) amg_tr((de),(en))
#define MENU_CONTACTS FULLMENUNUM(0,0,NOSUB)
#define MENU_ACCOUNT FULLMENUNUM(0,2,NOSUB)
#define MENU_ABOUT FULLMENUNUM(0,3,NOSUB)
#define MENU_QUIT FULLMENUNUM(0,5,NOSUB)
#define MENU_EMPTY_TRASH FULLMENUNUM(1,0,NOSUB)
#define MENU_EMPTY_SPAM FULLMENUNUM(1,1,NOSUB)
static int ensure_account(AmgGui *gui, AmgError *error)
{
    if (amg_account_validate(gui->account, error) == AMG_OK) return 1;
    if (!account_dialog(gui, error)) {
        if (error->message[0]) status_utf8(gui, error->message);
        return 0;
    }
    if (amg_account_validate(gui->account, error) != AMG_OK) {
        status_utf8(gui, error->message);
        return 0;
    }
    return 1;
}

static unsigned long inbox_event_uid_validity(const AmgNetworkEvent *event)
{
    char *end = NULL;
    unsigned long value;
    if (!event || !event->argument2[0]) return 0UL;
    value = strtoul(event->argument2, &end, 10);
    return end && *end == 0 ? value : 0UL;
}

static int inbox_generation_changed(const AmgGui *gui,
                                    unsigned long current_uid_validity)
{
    return gui && gui->inbox_baseline_ready &&
        gui->inbox_uid_validity != 0UL && current_uid_validity != 0UL &&
        gui->inbox_uid_validity != current_uid_validity;
}

static void commit_inbox_notification_baseline(
    AmgGui *gui, unsigned long current_uid_validity,
    unsigned long max_uid, int reset_generation)
{
    unsigned long old_uid;
    unsigned long old_uid_validity;
    int old_ready;
    if (!gui) return;
    old_uid = gui->inbox_latest_uid;
    old_uid_validity = gui->inbox_uid_validity;
    old_ready = gui->inbox_baseline_ready;

    if (!old_ready || reset_generation)
        gui->inbox_latest_uid = max_uid;
    else if (max_uid > gui->inbox_latest_uid)
        gui->inbox_latest_uid = max_uid;
    if (current_uid_validity != 0UL)
        gui->inbox_uid_validity = current_uid_validity;
    gui->inbox_baseline_ready = 1;

    if (!old_ready || old_uid != gui->inbox_latest_uid ||
        old_uid_validity != gui->inbox_uid_validity)
        gui_state_save_inbox_notification(gui);
}

static size_t label_index_for_mailbox(const AmgGui *gui,
                                      const char *mailbox_utf8)
{
    size_t i;
    if (!gui || !mailbox_utf8) return gui ? gui->label_count : 0U;
    for (i = 0; i < gui->label_count; ++i) {
        if (gui->labels[i].available &&
            (!strcmp(gui->labels[i].mailbox_utf8, mailbox_utf8) ||
             !strcmp(gui->labels[i].server_mailbox_utf8, mailbox_utf8)))
            return i;
    }
    return gui->label_count;
}

static size_t label_index_for_server_mailbox(const AmgGui *gui,
                                          const char *server_mailbox_utf8)
{
    size_t i;
    if (!gui || !server_mailbox_utf8) return gui ? gui->label_count : 0U;
    for (i = 0; i < gui->label_count; ++i) {
        if (gui->labels[i].available &&
            !strcmp(gui->labels[i].server_mailbox_utf8, server_mailbox_utf8))
            return i;
    }
    return gui->label_count;
}

static int current_mailbox_is_drafts(const AmgGui *gui)
{
    return gui && gui->label_count > 3U && gui->labels[3U].available &&
        (!strcmp(gui->current_mailbox_utf8, gui->labels[3U].mailbox_utf8) ||
         !strcmp(gui->current_mailbox_utf8,
                 gui->labels[3U].server_mailbox_utf8));
}

static void update_reply_button(AmgGui *gui)
{
    const char *label;
    if (!gui || !gui->reply_gadget) return;
    label = current_mailbox_is_drafts(gui)
        ? T("_Bearbeiten", "_Edit")
        : T("A_ntworten", "_Reply");
    if (gui->window) {
        SetGadgetAttrs(gui->reply_gadget, gui->window, NULL,
                       GA_Text, (ULONG)(uintptr_t)label, TAG_DONE);
        RefreshGList(gui->reply_gadget, gui->window, NULL, 1);
    } else {
        SetAttrs((Object *)gui->reply_gadget,
                 GA_Text, (ULONG)(uintptr_t)label, TAG_DONE);
    }
}

static int request_label_index(AmgGui *gui, size_t index, AmgError *error)
{
    char message[192];
    int result;
    if (!gui || index >= gui->label_count) return AMG_ERR_ARGUMENT;
    if (!gui->labels[index].available ||
        !gui->labels[index].mailbox_utf8[0]) {
        status_local(gui,
                     T("Dieser Ordner ist f\374r das Konto nicht verf\374gbar.",
                       "This folder is not available for the account."));
        return AMG_ERR_ARGUMENT;
    }
    if (!gui->labels[index].selectable) {
        status_local(gui,
                     T("Dieser Eintrag ist nur ein Ordner-Container und kann nicht ge\366ffnet werden.",
                       "This entry is only a folder container and cannot be opened."));
        return AMG_ERR_ARGUMENT;
    }
    if (!amg_network_is_connected(gui->network)) {
        status_local(gui, T("Bitte zuerst 'Abrufen' anklicken.", "Please click 'Fetch' first."));
        return AMG_ERR_IO;
    }
    result = amg_network_request(gui->network, AMG_NET_FETCH_INBOX, 0,
                                 gui->labels[index].mailbox_utf8,
                                 NULL, error);
    if (result != AMG_OK) {
        if (error && error->message[0]) status_utf8(gui, error->message);
        return result;
    }
    strncpy(gui->current_mailbox_utf8,
            gui->labels[index].mailbox_utf8,
            sizeof(gui->current_mailbox_utf8) - 1U);
    gui->current_mailbox_utf8[
        sizeof(gui->current_mailbox_utf8) - 1U] = 0;
    strncpy(gui->current_label_local,
            gui->labels[index].path_local[0]
                ? gui->labels[index].path_local
                : gui->labels[index].display_local,
            sizeof(gui->current_label_local) - 1U);
    gui->current_label_local[sizeof(gui->current_label_local) - 1U] = 0;
    select_label_index(gui, index);
    update_reply_button(gui);
    clear_current_message_payload(gui);
    show_message_placeholder(gui, T("Ordner wird geladen...", "Loading folder..."));
    set_preview_local(gui,
                      T("W\344hlen Sie nach dem Abruf eine Nachricht aus.", "Select a message after fetching."));
    amg_tr_snprintf(message, sizeof(message),
                    "%s wird geladen...", "Loading %s...",
                    gui->current_label_local);
    status_local(gui, message);
    return AMG_OK;
}

static void begin_move(AmgGui *gui, AmgError *error)
{
    ULONG uid = 0;
    size_t source_index;
    if (!cursor_node_user_data(gui->messages_gadget, &uid) || !uid) {
        status_local(gui, T("Bitte zuerst eine Nachricht ausw\344hlen.", "Please select a message first."));
        return;
    }
    if (!amg_network_is_connected(gui->network)) {
        status_local(gui, T("Bitte zuerst 'Abrufen' anklicken.", "Please click 'Fetch' first."));
        return;
    }
    if (!gui->current_mailbox_utf8[0]) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T("Der Quellordner ist nicht bekannt.", "The source folder is unknown."));
        status_utf8(gui, error->message);
        return;
    }
    gui->move_uid = uid;
    source_index = label_index_for_mailbox(
        gui, gui->current_mailbox_utf8);
    strncpy(gui->move_source_mailbox_utf8,
            source_index < gui->label_count
                ? gui->labels[source_index].server_mailbox_utf8
                : gui->current_mailbox_utf8,
            sizeof(gui->move_source_mailbox_utf8) - 1U);
    gui->move_source_mailbox_utf8[
        sizeof(gui->move_source_mailbox_utf8) - 1U] = 0;
    gui->move_pending = 1;
    status_local(
        gui,
        T("Bitte seitlich einen Zielordner zum Verschieben ausw\344hlen.", "Please select a destination folder on the left."));
}

void cancel_pending_move(AmgGui *gui)
{
    if (!gui || !gui->move_pending) return;
    gui->move_pending = 0;
    gui->move_uid = 0;
    gui->move_source_mailbox_utf8[0] = 0;
    status_local(gui, T("Verschieben abgebrochen.", "Move cancelled."));
}

static int queue_move_to_label(AmgGui *gui, size_t index, AmgError *error)
{
    char message[640];
    const char *target_name;
    size_t current_index;
    int result;
    if (!gui || index >= gui->label_count || !gui->move_pending)
        return AMG_ERR_ARGUMENT;
    if (!gui->labels[index].available ||
        !gui->labels[index].mailbox_utf8[0]) {
        status_local(gui,
                     T("Dieser Ordner ist f\374r das Konto nicht verf\374gbar.",
                       "This folder is not available for the account."));
        return AMG_ERR_ARGUMENT;
    }
    if (!gui->labels[index].selectable) {
        status_local(gui,
                     T("In diesen Ordner-Container kann nicht verschoben werden.",
                       "Messages cannot be moved into this folder container."));
        return AMG_ERR_ARGUMENT;
    }
    if (!strcmp(gui->move_source_mailbox_utf8,
                gui->labels[index].server_mailbox_utf8)) {
        status_local(gui, T("Quell- und Zielordner sind identisch.", "Source and destination folders are identical."));
        return AMG_ERR_ARGUMENT;
    }
    result = amg_network_request(
        gui->network, AMG_NET_MOVE, gui->move_uid,
        gui->move_source_mailbox_utf8,
        gui->labels[index].server_mailbox_utf8,
        error);
    if (result != AMG_OK) {
        if (error && error->message[0]) status_utf8(gui, error->message);
        return result;
    }
    gui->move_pending = 0;
    current_index = label_index_for_mailbox(gui, gui->current_mailbox_utf8);
    if (current_index < gui->label_count)
        select_label_index(gui, current_index);
    target_name = gui->labels[index].path_local[0]
        ? gui->labels[index].path_local
        : gui->labels[index].display_local;
    amg_tr_snprintf(message, sizeof(message),
                    "Nachricht wird nach %s verschoben...",
                    "Moving message to %s...", target_name);
    status_local(gui, message);
    return AMG_OK;
}

static void remove_message_uid(AmgGui *gui, ULONG uid)
{
    struct Node *node, *next;
    if (!gui || !uid) return;
    detach_listbrowser(gui->messages_gadget, gui->window);
    node = gui->messages_list.lh_Head;
    while (node && node->ln_Succ) {
        ULONG value = 0;
        next = node->ln_Succ;
        GetListBrowserNodeAttrs(
            node, LBNA_UserData, (ULONG)(uintptr_t)&value, TAG_DONE);
        if (value == uid) {
            Remove(node);
            FreeListBrowserNode(node);
            if (gui->active_message_uid == uid)
                gui->active_message_uid = 0;
            break;
        }
        node = next;
    }
    if (!gui->messages_list.lh_Head->ln_Succ) {
        node = message_placeholder_node(
            T("Dieser Ordner enth\344lt keine Nachrichten.", "This folder contains no messages."));
        if (node) AddTail(&gui->messages_list, node);
    }
    attach_listbrowser(gui->messages_gadget, gui->window,
                       &gui->messages_list);
    clear_current_message_payload(gui);
    set_preview_local(gui,
                      T("W\344hlen Sie eine Nachricht zur Anzeige aus.", "Select a message to display."));
}

static void handle_label_gadget(AmgGui *gui, struct Gadget *gadget,
                                int hierarchical, AmgError *error)
{
    ULONG index = 0;
    if (hierarchical && handle_label_tree_event(gui)) return;
    if (!selected_node_user_data(gadget, &index)) return;
    if (gui->move_pending)
        queue_move_to_label(gui, (size_t)index, error);
    else
        request_label_index(gui, (size_t)index, error);
}

static int message_doubleclick_detected(AmgGui *gui, ULONG uid,
                                        ULONG release_event)
{
    ULONG seconds = 0UL, micros = 0UL;
    int is_doubleclick = 0;

    if (!gui || !uid) return 0;

    /* Prefer listbrowser.gadget's own result when it survives unchanged. */
    if (release_event == LBRE_DOUBLECLICK) {
        gui->message_click_valid = 0;
        gui->message_click_uid = 0UL;
        return 1;
    }

    /* AmiMail redraws the selected row after a normal click. On classic
     * ReAction this can reset ListBrowser's internal double-click state before
     * the second click arrives. Keep a second, OS-native detector keyed to the
     * UID so a redraw cannot break the gesture. DoubleClick() uses the user's
     * Intuition double-click preference rather than a hard-coded timeout. */
    CurrentTime(&seconds, &micros);
    if (gui->message_click_valid && gui->message_click_uid == uid &&
        DoubleClick(gui->message_click_seconds, gui->message_click_micros,
                    seconds, micros)) {
        is_doubleclick = 1;
        gui->message_click_valid = 0;
        gui->message_click_uid = 0UL;
    } else {
        gui->message_click_seconds = seconds;
        gui->message_click_micros = micros;
        gui->message_click_uid = uid;
        gui->message_click_valid = 1;
    }
    return is_doubleclick;
}

static void toggle_message_flagged(AmgGui *gui, ULONG uid, AmgError *error)
{
    int flagged;
    int result;
    if (!gui || !uid) return;
    if (!amg_network_is_connected(gui->network)) {
        status_local(gui, T("Bitte zuerst 'Abrufen' anklicken.", "Please click 'Fetch' first."));
        return;
    }
    flagged = !message_is_flagged(gui, uid);
    result = amg_network_request(
        gui->network, AMG_NET_SET_FLAGGED, uid,
        flagged ? "1" : "0", "doubleclick", error);
    if (result == AMG_OK) {
        set_message_flagged_visual(gui, uid, flagged);
        status_local(gui, flagged
            ? T("Sternmarkierung wird gesetzt...", "Setting star...")
            : T("Sternmarkierung wird entfernt...", "Removing star..."));
    } else if (error && error->message[0]) {
        status_utf8(gui, error->message);
    }
}

static void request_message(AmgGui *gui, int reply, AmgError *error)
{
    ULONG uid = 0;
    ULONG selected[2];
    size_t selected_count;
    ULONG release_event = LBRE_NORMAL;
    int is_doubleclick = 0;
    int edit_draft = 0;
    int result;
    if (!gui || !gui->messages_gadget) return;
    edit_draft = reply && current_mailbox_is_drafts(gui);
    if (!reply) {
        GetAttr(LISTBROWSER_RelEvent, (Object *)gui->messages_gadget,
                &release_event);
        if (release_event == LBRE_TITLECLICK) return;
        if (!cursor_node_user_data(gui->messages_gadget, &uid) || !uid) {
            status_local(gui, T("Bitte zuerst eine Nachricht ausw\344hlen.", "Please select a message first."));
            return;
        }
        is_doubleclick = message_doubleclick_detected(gui, uid, release_event);
    } else {
        selected_count = selected_message_uids(gui, selected, 2U);
        if (!selected_count) {
            status_local(gui, edit_draft
                ? T("Bitte zuerst einen Entwurf ausw\344hlen.",
                    "Please select a draft first.")
                : T("Bitte zuerst eine Nachricht ausw\344hlen.",
                    "Please select a message first."));
            return;
        }
        if (selected_count > 1U) {
            status_local(gui, edit_draft
                ? T("Bitte zum Bearbeiten nur einen Entwurf ausw\344hlen.",
                    "Please select only one draft to edit.")
                : T("Bitte zum Antworten nur eine Nachricht ausw\344hlen.",
                    "Please select only one message to reply."));
            return;
        }
        uid = selected[0];
    }
    if (!uid) {
        status_local(gui, T("Bitte zuerst eine Nachricht ausw\344hlen.", "Please select a message first."));
        return;
    }
    set_message_selected_visual(gui, uid);
    if (!reply && is_doubleclick) {
        toggle_message_flagged(gui, uid, error);
        return;
    }
    if (!amg_network_is_connected(gui->network)) {
        status_local(gui, T("Bitte zuerst 'Abrufen' anklicken.", "Please click 'Fetch' first."));
        return;
    }
    result = amg_network_request(
        gui->network, AMG_NET_FETCH_MESSAGE, uid,
        edit_draft ? "editdraft" : (reply ? "reply" : "preview"),
        edit_draft
            ? (gui->labels[3U].server_mailbox_utf8[0]
                   ? gui->labels[3U].server_mailbox_utf8
                   : gui->current_mailbox_utf8)
            : NULL,
        error);
    if (result == AMG_OK) {
        clear_current_message_payload(gui);
        if (edit_draft)
            status_local(gui, T("Entwurf wird zum Bearbeiten geladen...",
                                "Loading draft for editing..."));
        else if (reply)
            status_local(gui, T("Antwort wird vorbereitet...", "Preparing reply..."));
        else {
            set_preview_local(gui, T("Nachricht wird geladen...", "Loading message..."));
            status_local(gui, T("Nachricht wird geladen...", "Loading message..."));
        }
    } else if (error && error->message[0]) {
        status_utf8(gui, error->message);
    }
}

static void toggle_selected_seen(AmgGui *gui, AmgError *error)
{
    ULONG *uids;
    size_t count, i, queued = 0;
    if (!gui) return;
    uids = selected_message_uids_alloc(gui, &count);
    if (!uids) {
        status_local(gui, T("Nicht genug Speicher.", "Not enough memory."));
        return;
    }
    if (!count) {
        free(uids);
        status_local(gui, T("Bitte zuerst eine Nachricht ausw\344hlen.", "Please select a message first."));
        return;
    }
    if (!amg_network_is_connected(gui->network)) {
        free(uids);
        status_local(gui, T("Bitte zuerst 'Abrufen' anklicken.", "Please click 'Fetch' first."));
        return;
    }
    for (i = 0; i < count; ++i) {
        int seen = !message_is_seen(gui, uids[i]);
        int result = amg_network_request(
            gui->network, AMG_NET_SET_SEEN, uids[i],
            seen ? "1" : "0", "button", error);
        if (result != AMG_OK) break;
        set_message_seen_visual(gui, uids[i], seen);
        if (!strcmp(gui->current_mailbox_utf8, "INBOX"))
            gui_state_adjust_inbox_unseen(gui, seen ? -1L : 1L);
        ++queued;
    }
    free(uids);
    if (queued == count) {
        char message[128];
        amg_tr_snprintf(message, sizeof(message),
                        "%lu Nachricht(en): Lesestatus wird ge\344ndert.",
                        "%lu message(s): changing read status.",
                        (unsigned long)queued);
        status_local(gui, message);
    } else if (error && error->message[0]) {
        status_utf8(gui, error->message);
    }
}

static void delete_selected_messages(AmgGui *gui, AmgError *error)
{
    ULONG *uids;
    size_t count, i, source_index;
    const char *source_label, *trash_label;
    char status_text[128];
    if (!gui) return;
    uids = selected_message_uids_alloc(gui, &count);
    if (!uids) {
        status_local(gui, T("Nicht genug Speicher.", "Not enough memory."));
        return;
    }
    if (!count) {
        free(uids);
        status_local(gui, T("Bitte zuerst eine Nachricht ausw\344hlen.", "Please select a message first."));
        return;
    }
    if (!amg_network_is_connected(gui->network)) {
        free(uids);
        status_local(gui, T("Bitte zuerst 'Abrufen' anklicken.", "Please click 'Fetch' first."));
        return;
    }
    if (!confirm_delete_dialog(gui)) {
        free(uids);
        return;
    }
    source_index = label_index_for_mailbox(gui, gui->current_mailbox_utf8);
    source_label = source_index < gui->label_count
        ? gui->labels[source_index].server_mailbox_utf8
        : gui->current_mailbox_utf8;
    trash_label = gui->labels[6U].available
        ? gui->labels[6U].server_mailbox_utf8 : "\\Trash";
    if (!source_label || !*source_label) {
        free(uids);
        status_local(gui, T("Der aktuelle Mailordner ist nicht bekannt.", "The current mail folder is unknown."));
        return;
    }
    if (!strcmp(source_label, trash_label)) {
        free(uids);
        status_local(gui, T("Die Nachricht liegt bereits im Papierkorb.", "The message is already in Trash."));
        return;
    }
    for (i = 0; i < count; ++i) {
        int result = amg_network_request(
            gui->network, AMG_NET_DELETE, uids[i],
            trash_label, source_label, error);
        if (result != AMG_OK) {
            free(uids);
            if (error && error->message[0]) status_utf8(gui, error->message);
            return;
        }
    }
    free(uids);
    amg_tr_snprintf(status_text, sizeof(status_text),
                    "%lu Nachricht(en) werden gel\366scht.",
                    "Deleting %lu message(s).",
                    (unsigned long)count);
    status_local(gui, status_text);
}

static void empty_trash(AmgGui *gui, AmgError *error)
{
    const char *trash_label;
    int result;
    if (!gui) return;
    if (!amg_network_is_connected(gui->network)) {
        status_local(gui, T("Bitte zuerst 'Abrufen' anklicken.", "Please click 'Fetch' first."));
        return;
    }
    if (!confirm_empty_trash_dialog(gui)) return;
    trash_label = gui->labels[6U].available
        ? gui->labels[6U].server_mailbox_utf8 : "\\Trash";
    if (!trash_label || !*trash_label) {
        status_local(gui, T("Der Papierkorb-Ordner ist nicht bekannt.", "The Trash folder is unknown."));
        return;
    }
    result = amg_network_request(gui->network, AMG_NET_EMPTY_TRASH, 0,
                                 trash_label, NULL, error);
    if (result == AMG_OK)
        status_local(gui, T("Papierkorb wird geleert...", "Emptying Trash..."));
    else if (error && error->message[0])
        status_utf8(gui, error->message);
}

static void empty_spam(AmgGui *gui, AmgError *error)
{
    const char *spam_label;
    int result;
    if (!gui) return;
    if (!amg_network_is_connected(gui->network)) {
        status_local(gui, T("Bitte zuerst 'Abrufen' anklicken.",
                            "Please click 'Fetch' first."));
        return;
    }
    if (!confirm_empty_spam_dialog(gui)) return;
    spam_label = gui->labels[5U].available
        ? gui->labels[5U].server_mailbox_utf8 : NULL;
    if (!spam_label || !*spam_label) {
        status_local(gui, T("Der Spam-Ordner ist nicht bekannt.",
                            "The Spam folder is unknown."));
        return;
    }
    result = amg_network_request(gui->network, AMG_NET_EMPTY_SPAM, 0,
                                 spam_label, NULL, error);
    if (result == AMG_OK)
        status_local(gui, T("Spam wird geleert...", "Emptying Spam..."));
    else if (error && error->message[0])
        status_utf8(gui, error->message);
}

 void handle_network(AmgGui *gui)
{
    AmgNetworkEvent event;
    while (amg_network_poll(gui->network, &event) > 0) {
        if (event.type == AMG_NET_CHECK_INBOX)
            gui->periodic_check_pending = 0;

        /* Automatic update checks are intentionally silent. A missing GitHub
         * connection must never disturb normal IMAP/SMTP use. */
        if (event.type == AMG_NET_CHECK_UPDATE) {
            gui_update_handle_check(gui, &event);
            amg_network_event_clear(&event);
            continue;
        }
        if (event.type == AMG_NET_DOWNLOAD_UPDATE) {
            gui_update_handle_download(gui, &event);
            if (event.result == AMG_OK) {
                amg_network_event_clear(&event);
                continue;
            }
        }

        if (event.result != AMG_OK) {
            if (event.type == AMG_NET_RECONFIGURE)
                gui->network_reconfigure_pending = 0;
            if (event.type == AMG_NET_SET_SEEN) {
                set_message_seen_visual(
                    gui, event.uid, atoi(event.argument1) == 0);
                if (!strcmp(gui->current_mailbox_utf8, "INBOX") &&
                    !strcmp(event.argument2, "button"))
                    gui_state_adjust_inbox_unseen(
                        gui, atoi(event.argument1) ? 1L : -1L);
            }
            else if (event.type == AMG_NET_SET_FLAGGED)
                set_message_flagged_visual(
                    gui, event.uid, atoi(event.argument1) == 0);
            status_utf8(gui,
                        event.message[0] ? event.message : T("Netzwerkfehler", "Network error"));
            if (gui->update_check_deferred &&
                (event.type == AMG_NET_CONNECT ||
                 event.type == AMG_NET_RECONFIGURE ||
                 event.type == AMG_NET_FETCH_LABELS ||
                 event.type == AMG_NET_FETCH_INBOX)) {
                gui->update_check_deferred = 0;
                gui_update_request_check(gui);
            }
        } else {
            switch (event.type) {
                case AMG_NET_CONNECT:
                    status_local(gui, T("Mit dem Mailserver verbunden.", "Connected to the mail server."));
                    amg_network_request(gui->network, AMG_NET_FETCH_LABELS,
                                        0, NULL, NULL, NULL);
                    break;
                case AMG_NET_RECONFIGURE:
                    gui->network_reconfigure_pending = 0;
                    status_local(gui,
                        T("Mailserver-Verbindung wurde neu konfiguriert.",
                          "Mail-server connection was reconfigured."));
                    amg_network_request(gui->network, AMG_NET_FETCH_LABELS,
                                        0, NULL, NULL, NULL);
                    break;
                case AMG_NET_FETCH_LABELS:
                {
                    AmgError request_error;
                    char message[96];
                    size_t count = update_labels_from_payload(
                        gui, event.payload, event.payload_length);
                    amg_tr_snprintf(message, sizeof(message),
                                    "%lu IMAP-Ordner wurden geladen.",
                                    "%lu IMAP folders loaded.",
                                    (unsigned long)count);
                    status_local(gui, message);
                    memset(&request_error, 0, sizeof(request_error));
                    request_label_index(gui, 0U, &request_error);
                    break;
                }
                case AMG_NET_FETCH_INBOX:
                {
                    char message[256];
                    int parse_error = 0;
                    int uid_parse_error = 0;
                    unsigned long max_uid = 0UL;
                    size_t index, count;
                    index = label_index_for_mailbox(
                        gui, event.argument1[0] ? event.argument1 : "INBOX");
                    if (index == 0U) {
                        int unseen_parse_error = 0;
                        int had_baseline = gui->inbox_baseline_ready;
                        unsigned long previous_uid = gui->inbox_latest_uid;
                        unsigned long current_uid_validity =
                            inbox_event_uid_validity(&event);
                        int generation_changed = inbox_generation_changed(
                            gui, current_uid_validity);
                        size_t unseen_count = message_unseen_count_from_payload(
                            event.payload, event.payload_length,
                            &unseen_parse_error);
                        size_t new_count = message_uid_stats(
                            event.payload, event.payload_length,
                            had_baseline ? previous_uid : 0UL,
                            &max_uid, &uid_parse_error);
                        if (uid_parse_error >= 0) {
                            size_t notify_count = new_count;
                            if (generation_changed) {
                                notify_count = 0U;
                            } else if (!had_baseline) {
                                /* No persisted high-water mark exists yet
                                 * (first run after upgrading from an older
                                 * AmiMAIL, or a newly configured account).
                                 * Alert once if that first snapshot contains
                                 * unread mail, then persist the generation so
                                 * subsequent starts are precise. */
                                notify_count = unseen_parse_error >= 0
                                    ? unseen_count : 0U;
                            }
                            commit_inbox_notification_baseline(
                                gui, current_uid_validity, max_uid,
                                generation_changed);
                            if (notify_count > 0U)
                                gui_notify_new_mail(gui);
                        }
                        if (unseen_parse_error >= 0)
                            gui_state_set_inbox_unseen(
                                gui, (unsigned long)unseen_count);
                    }
                    count = update_messages_from_payload(
                        gui, event.payload, event.payload_length, &parse_error);
                    if (index < gui->label_count) {
                        strncpy(gui->current_mailbox_utf8,
                                gui->labels[index].mailbox_utf8,
                                sizeof(gui->current_mailbox_utf8) - 1U);
                        gui->current_mailbox_utf8[
                            sizeof(gui->current_mailbox_utf8) - 1U] = 0;
                        strncpy(gui->current_label_local,
                                gui->labels[index].path_local[0]
                                    ? gui->labels[index].path_local
                                    : gui->labels[index].display_local,
                                sizeof(gui->current_label_local) - 1U);
                        gui->current_label_local[
                            sizeof(gui->current_label_local) - 1U] = 0;
                        select_label_index(gui, index);
                        update_reply_button(gui);
                    }
                    set_preview_local(
                        gui, T("W\344hlen Sie eine Nachricht zur Anzeige aus.", "Select a message to display."));
                    if (parse_error < 0) {
                        amg_tr_snprintf(message, sizeof(message),
                                        "IMAP-Nachrichten konnten nicht ausgewertet werden (Code %d).",
                                        "IMAP messages could not be parsed (code %d).",
                                        parse_error);
                    } else {
                        amg_tr_snprintf(message, sizeof(message),
                                        "%lu Nachrichten in %s geladen.",
                                        "%lu messages loaded in %s.",
                                        (unsigned long)count,
                                        gui->current_label_local[0]
                                            ? gui->current_label_local
                                            : T("Ordner", "folder"));
                    }
                    status_local(gui, message);
                    if (gui->update_check_deferred) {
                        gui->update_check_deferred = 0;
                        gui_update_request_check(gui);
                    }
                    break;
                }
                case AMG_NET_CHECK_INBOX:
                {
                    unsigned long max_uid = 0UL;
                    unsigned long previous_uid = gui->inbox_latest_uid;
                    unsigned long current_uid_validity =
                        inbox_event_uid_validity(&event);
                    int parse_error = 0;
                    int unseen_parse_error = 0;
                    int had_baseline = gui->inbox_baseline_ready;
                    int generation_changed = inbox_generation_changed(
                        gui, current_uid_validity);
                    size_t unseen_count = message_unseen_count_from_payload(
                        event.payload, event.payload_length,
                        &unseen_parse_error);
                    size_t new_count = message_uid_stats(
                        event.payload, event.payload_length,
                        had_baseline ? previous_uid : 0UL,
                        &max_uid, &parse_error);
                    if (parse_error < 0) {
                        char message[160];
                        amg_tr_snprintf(message, sizeof(message),
                                        "Periodischer Abruf konnte nicht ausgewertet werden (Code %d).",
                                        "Periodic fetch could not be parsed (code %d).",
                                        parse_error);
                        status_local(gui, message);
                        break;
                    }
                    {
                        size_t notify_count = new_count;
                        if (generation_changed) {
                            new_count = 0U;
                            notify_count = 0U;
                            if (unseen_parse_error >= 0)
                                gui_state_set_inbox_unseen(
                                    gui, (unsigned long)unseen_count);
                        } else if (!had_baseline) {
                            /* See the full-fetch path above.  The first
                             * snapshot cannot distinguish historic unread
                             * mail from mail received since installing this
                             * state format, so issue at most one initial
                             * notification and persist the high-water mark. */
                            new_count = 0U;
                            notify_count = unseen_parse_error >= 0
                                ? unseen_count : 0U;
                            if (unseen_parse_error >= 0)
                                gui_state_set_inbox_unseen(
                                    gui, (unsigned long)unseen_count);
                        } else {
                            if (unseen_parse_error >= 0 && unseen_count > 0U)
                                gui_state_adjust_inbox_unseen(
                                    gui, (long)unseen_count);
                        }

                        commit_inbox_notification_baseline(
                            gui, current_uid_validity, max_uid,
                            generation_changed);

                        /* Ist die Inbox gerade sichtbar, neue Header ohne
                         * kompletten Listen-Neuaufbau einfuegen. Auswahl,
                         * Preview und Scrollposition bleiben dadurch erhalten.
                         * Andere Labels werden vom periodischen Abruf nie
                         * veraendert. */
                        if (new_count > 0U &&
                            !strcmp(gui->current_mailbox_utf8, "INBOX")) {
                            int merge_error = 0;
                            (void)merge_new_messages_from_payload(
                                gui, event.payload, event.payload_length,
                                &merge_error);
                        }
                        if (notify_count > 0U) {
                            char message[128];
                            gui_notify_new_mail(gui);
                            amg_tr_snprintf(
                                message, sizeof(message),
                                "Periodischer Abruf: %lu neue Mail(s) im Posteingang.",
                                "Periodic fetch: %lu new mail(s) in Inbox.",
                                (unsigned long)notify_count);
                            status_local(gui, message);
                        } else {
                            status_local(gui,
                                T("Periodischer Abruf: keine neuen Mails.",
                                  "Periodic fetch: no new mail."));
                        }
                    }
                    break;
                }
                case AMG_NET_FETCH_MESSAGE:
                {
                    AmgError preview_error;
                    int prepare_reply = !strcmp(event.argument1, "reply");
                    int prepare_edit = !strcmp(event.argument1, "editdraft");
                    int unread_before = !prepare_edit &&
                        !message_is_seen(gui, event.uid);
                    memset(&preview_error, 0, sizeof(preview_error));
                    if (display_message_payload(
                            gui, event.payload, event.payload_length,
                            &preview_error) == AMG_OK) {
                        retain_current_message_payload(gui, &event);
                        set_message_selected_visual(gui, event.uid);
                        if (unread_before) {
                            int in_inbox =
                                !strcmp(gui->current_mailbox_utf8, "INBOX");
                            set_message_seen_visual(gui, event.uid, 1);
                            if (in_inbox)
                                gui_state_adjust_inbox_unseen(gui, -1L);
                            (void)amg_network_request(
                                gui->network, AMG_NET_SET_SEEN, event.uid,
                                "1", in_inbox ? "preview-inbox" : "preview",
                                NULL);
                        }
                        if (prepare_edit) {
                            DraftEditData edit;
                            memset(&edit, 0, sizeof(edit));
                            if (prepare_draft_edit_payload(
                                    gui, event.payload, event.payload_length,
                                    event.uid, event.argument2,
                                    &edit, &preview_error) == AMG_OK) {
                                if (!compose_dialog(gui, COMPOSE_MODE_EDIT_DRAFT, &edit,
                                                    &preview_error))
                                    status_local(
                                        gui,
                                        T("Entwurf wurde nicht ge\344ndert.",
                                          "Draft was not changed."));
                                cleanup_draft_edit_files(&edit);
                            } else {
                                cleanup_draft_edit_files(&edit);
                                status_utf8(
                                    gui,
                                    preview_error.message[0]
                                        ? preview_error.message
                                        : T("Entwurf konnte nicht zum Bearbeiten vorbereitet werden.",
                                            "Draft could not be prepared for editing."));
                            }
                        } else if (prepare_reply) {
                            if (prepare_reply_payload(
                                    gui, event.payload, event.payload_length,
                                    &preview_error) == AMG_OK) {
                                if (!compose_dialog(gui, COMPOSE_MODE_REPLY, NULL,
                                                    &preview_error))
                                    status_local(gui,
                                                 T("Antwort wurde nicht gesendet.", "Reply was not sent."));
                            } else {
                                status_utf8(gui,
                                    preview_error.message[0]
                                        ? preview_error.message
                                        : T("Antwort konnte nicht vorbereitet werden.", "Reply could not be prepared."));
                            }
                        } else {
                            status_local(gui, T("Nachricht geladen.", "Message loaded."));
                        }
                    } else {
                        status_utf8(
                            gui, preview_error.message[0]
                                     ? preview_error.message
                                     : T("Nachricht konnte nicht dargestellt werden.", "Message could not be displayed."));
                    }
                    break;
                }
                case AMG_NET_SET_SEEN:
                {
                    int new_seen = atoi(event.argument1) != 0;
                    set_message_seen_visual(gui, event.uid, new_seen);
                    if (strncmp(event.argument2, "preview", 7U))
                        status_local(
                            gui, new_seen
                                ? T("Nachricht als gelesen markiert.", "Message marked as read.")
                                : T("Nachricht als ungelesen markiert.", "Message marked as unread."));
                    break;
                }
                case AMG_NET_SET_FLAGGED:
                    set_message_flagged_visual(
                        gui, event.uid, atoi(event.argument1) != 0);
                    status_local(
                        gui, atoi(event.argument1)
                            ? T("Sternmarkierung wurde gesetzt.", "Star was set.")
                            : T("Sternmarkierung wurde entfernt.", "Star was removed."));
                    break;
                case AMG_NET_DELETE:
                    if (!strcmp(gui->current_mailbox_utf8, "INBOX") &&
                        !message_is_seen(gui, event.uid))
                        gui_state_adjust_inbox_unseen(gui, -1L);
                    remove_message_uid(gui, event.uid);
                    status_local(gui,
                                 T("Nachricht wurde in den Papierkorb verschoben.", "Message moved to Trash."));
                    break;
                case AMG_NET_EMPTY_TRASH:
                    if (label_index_for_mailbox(
                            gui, gui->current_mailbox_utf8) == 6U) {
                        clear_current_message_payload(gui);
                        show_message_placeholder(
                            gui, T("Dieser Ordner enth\344lt keine Nachrichten.", "This folder contains no messages."));
                        set_preview_local(
                            gui, T("W\344hlen Sie eine Nachricht zur Anzeige aus.", "Select a message to display."));
                    }
                    status_local(gui, T("Papierkorb wurde geleert.", "Trash was emptied."));
                    break;
                case AMG_NET_EMPTY_SPAM:
                    if (label_index_for_mailbox(
                            gui, gui->current_mailbox_utf8) == 5U) {
                        clear_current_message_payload(gui);
                        show_message_placeholder(
                            gui, T("Dieser Ordner enth\344lt keine Nachrichten.", "This folder contains no messages."));
                        set_preview_local(
                            gui, T("W\344hlen Sie eine Nachricht zur Anzeige aus.", "Select a message to display."));
                    }
                    status_local(gui, T("Spam wurde geleert.", "Spam was emptied."));
                    break;
                case AMG_NET_SAVE_DRAFT:
                    if (event.uid && !strcmp(event.argument2,
                                             "draft-removed"))
                        remove_message_uid(gui, event.uid);
                    if (event.uid && !event.message[0] &&
                        current_mailbox_is_drafts(gui) &&
                        amg_network_is_connected(gui->network)) {
                        (void)amg_network_request(
                            gui->network, AMG_NET_FETCH_INBOX, 0,
                            gui->current_mailbox_utf8, NULL, NULL);
                    }
                    if (event.message[0])
                        status_utf8(gui, event.message);
                    else if (event.uid && current_mailbox_is_drafts(gui))
                        status_local(gui,
                                     T("Entwurf wurde gespeichert; Ordner wird aktualisiert...",
                                       "Draft was saved; refreshing folder..."));
                    else
                        status_local(gui, T("Entwurf wurde gespeichert.",
                                            "Draft was saved."));
                    break;
                case AMG_NET_SEND_REPLY:
                    status_local(gui, T("Antwort wurde versendet.", "Reply sent."));
                    break;
                case AMG_NET_SEND_MAIL:
                    if (event.uid && !strcmp(event.argument2,
                                             "draft-removed"))
                        remove_message_uid(gui, event.uid);
                    if (event.message[0])
                        status_utf8(gui, event.message);
                    else
                        status_local(gui, T("Mail wurde versendet.", "Mail sent."));
                    break;
                case AMG_NET_MOVE:
                {
                    char message[640];
                    size_t target = label_index_for_server_mailbox(
                        gui, event.argument2);
                    const char *target_name = event.argument2;
                    if (!message_is_seen(gui, event.uid)) {
                        if (!strcmp(gui->current_mailbox_utf8, "INBOX"))
                            gui_state_adjust_inbox_unseen(gui, -1L);
                        else if (target == 0U)
                            gui_state_adjust_inbox_unseen(gui, 1L);
                    }
                    remove_message_uid(gui, event.uid);
                    if (target < gui->label_count)
                        target_name = gui->labels[target].path_local[0]
                            ? gui->labels[target].path_local
                            : gui->labels[target].display_local;
                    amg_tr_snprintf(message, sizeof(message),
                                    "Nachricht wurde nach %s verschoben.",
                                    "Message moved to %s.",
                                    target_name && *target_name
                                        ? target_name
                                        : T("Zielordner", "destination folder"));
                    status_local(gui, message);
                    break;
                }
                default:
                    status_local(gui, T("Aktion abgeschlossen.", "Action completed."));
                    break;
            }
        }
        amg_network_event_clear(&event);
    }
}

 void periodic_fetch_mail(AmgGui *gui, AmgError *error)
{
    int result = AMG_OK;
    if (!gui || !gui->account || !gui->account->periodic_fetch ||
        account_is_locked(gui->account) || gui->periodic_check_pending)
        return;
    if (!amg_network_is_running(gui->network))
        result = amg_network_start(gui->network, gui->account, error);
    if (result != AMG_OK) {
        if (error && error->message[0]) status_utf8(gui, error->message);
        return;
    }
    if (!amg_network_is_connected(gui->network)) {
        status_local(gui, T("Periodischer Abruf: Verbinde mit dem Mailserver...",
                            "Periodic fetch: connecting to the mail server..."));
        result = amg_network_request(gui->network, AMG_NET_CONNECT, 0,
                                     NULL, "periodic", error);
    } else {
        char uid_validity[32];
        snprintf(uid_validity, sizeof(uid_validity), "%lu",
                 gui->inbox_baseline_ready
                    ? gui->inbox_uid_validity : 0UL);
        result = amg_network_request(
            gui->network, AMG_NET_CHECK_INBOX,
            gui->inbox_baseline_ready ? gui->inbox_latest_uid : 0UL,
            uid_validity, "periodic", error);
        if (result == AMG_OK) gui->periodic_check_pending = 1;
    }
    if (result != AMG_OK && error && error->message[0])
        status_utf8(gui, error->message);
}

 void fetch_mail(AmgGui *gui, AmgError *error)
{
    int result = AMG_OK;
    if (!ensure_account(gui, error)) return;
    if (!amg_network_is_running(gui->network)) {
        result = amg_network_start(gui->network, gui->account, error);
    }
    if (result != AMG_OK) {
        status_utf8(gui, error->message);
    } else if (!amg_network_is_connected(gui->network)) {
        status_local(gui, T("Verbinde mit dem Mailserver...", "Connecting to the mail server..."));
        result = amg_network_request(gui->network, AMG_NET_CONNECT, 0,
                                     NULL, NULL, error);
        if (result != AMG_OK) status_utf8(gui, error->message);
    } else {
        result = amg_network_request(gui->network, AMG_NET_FETCH_INBOX, 0,
                                     gui->current_mailbox_utf8[0]
                                         ? gui->current_mailbox_utf8 : "INBOX",
                                     NULL, error);
        if (result != AMG_OK) status_utf8(gui, error->message);
    }
    if (result != AMG_OK && gui->update_check_deferred) {
        gui->update_check_deferred = 0;
        gui_update_request_check(gui);
    }
}

 void handle_main_gadget(AmgGui *gui, ULONG gadget_id,
                               AmgError *error)
{
    switch (gadget_id) {
        case GID_NEW_MAIL:
            if (ensure_account(gui, error)) compose_dialog(gui, COMPOSE_MODE_NEW, NULL, error);
            break;
        case GID_FETCH:
            fetch_mail(gui, error);
            break;
        case GID_UPDATE:
            gui_update_start_download(gui, error);
            break;
        case GID_SYSTEM_LABELS:
            handle_label_gadget(gui, gui->system_labels_gadget, 0, error);
            break;
        case GID_LABELS:
            handle_label_gadget(gui, gui->labels_gadget, 1, error);
            break;
        case GID_LABELS_SCROLL:
            handle_labels_scroller(gui);
            break;
        case GID_MESSAGES:
            request_message(gui, 0, error);
            break;
        case GID_MESSAGES_SCROLL:
            handle_messages_scroller(gui);
            break;
        case GID_PREVIEW_SCROLL:
            handle_preview_scroller(gui);
            break;
        case GID_SAVE_ATTACHMENTS:
            save_current_attachments(gui);
            break;
        case GID_REPLY:
            request_message(gui, 1, error);
            break;
        case GID_DELETE:
            delete_selected_messages(gui, error);
            break;
        case GID_SEEN:
            toggle_selected_seen(gui, error);
            break;
        case GID_MOVE:
            begin_move(gui, error);
            break;
    }
}

 void handle_menu(AmgGui *gui, ULONG menu_code, AmgError *error)
{
    switch (menu_code) {
        case MENU_CONTACTS:
            gui_contacts_dialog(gui, error);
            break;
        case MENU_ACCOUNT:
            account_dialog(gui, error);
            break;
        case MENU_ABOUT:
            about_dialog(gui);
            break;
        case MENU_QUIT:
            gui->running = 0;
            break;
        case MENU_EMPTY_TRASH:
            empty_trash(gui, error);
            break;
        case MENU_EMPTY_SPAM:
            empty_spam(gui, error);
            break;
    }
}

#endif /* AMIGMAIL_AMIGA */
