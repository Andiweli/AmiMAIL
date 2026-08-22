#include "gui_internal.h"
#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if AMIGMAIL_AMIGA
#include <clib/alib_protos.h>
#include <classes/window.h>
#include <dos/dos.h>
#include <exec/memory.h>
#include <gadgets/button.h>
#include <gadgets/layout.h>
#include <gadgets/listbrowser.h>
#include <gadgets/texteditor.h>
#include <libraries/gadtools.h>
#include <proto/button.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <proto/listbrowser.h>
#include <proto/texteditor.h>
#include <proto/window.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <utility/tagitem.h>

#define T(id, en) amg_tr((id), (en))

#ifdef NewObject
#undef NewObject
#endif
#ifdef ButtonObject
#undef ButtonObject
#endif
#define ButtonObject NewObject(NULL, (CONST_STRPTR)"button.gadget"

enum MessageRequestAction {
    MESSAGE_ACTION_PREVIEW = 0,
    MESSAGE_ACTION_REPLY,
    MESSAGE_ACTION_EDIT_DRAFT,
    MESSAGE_ACTION_REPLY_ALL,
    MESSAGE_ACTION_FORWARD
};

enum ReplyMenuGadgetId {
    GID_REPLY_MENU_REPLY_ALL = 400,
    GID_REPLY_MENU_FORWARD
};

enum SignatureGadgetId {
    GID_SIGNATURE_EDITOR = 420,
    GID_SIGNATURE_SAVE,
    GID_SIGNATURE_CANCEL
};

#define MENU_ACCOUNT FULLMENUNUM(0, 0, NOSUB)
#define MENU_ABOUT FULLMENUNUM(0, 1, NOSUB)
#define MENU_QUIT FULLMENUNUM(0, 3, NOSUB)
#define MENU_CONTACTS FULLMENUNUM(1, 0, NOSUB)
#define MENU_SIGNATURE FULLMENUNUM(1, 1, NOSUB)
#define MENU_EMPTY_TRASH FULLMENUNUM(1, 3, NOSUB)
#define MENU_EMPTY_SPAM FULLMENUNUM(1, 4, NOSUB)

#define GUI_PREFS_DRAWER "ENVARC:AmiMail"
#define SIGNATURE_PATH "ENVARC:AmiMail/signature.txt"
#define SIGNATURE_TEMP "ENVARC:AmiMail/signature.txt.new"


static void ensure_gui_prefs_drawer(void)
{
    BPTR lock = Lock((STRPTR)GUI_PREFS_DRAWER, ACCESS_READ);
    if (lock) {
        UnLock(lock);
        return;
    }
    lock = CreateDir((STRPTR)GUI_PREFS_DRAWER);
    if (lock) UnLock(lock);
}

void gui_signature_load(char *buffer, size_t capacity)
{
    FILE *file;
    size_t length;
    if (!buffer || !capacity) return;
    buffer[0] = 0;
    file = fopen(SIGNATURE_PATH, "rb");
    if (!file) return;
    length = fread(buffer, 1U, capacity - 1U, file);
    buffer[length] = 0;
    fclose(file);
}

int gui_signature_save(const char *text)
{
    FILE *file;
    size_t length;
    int write_failed = 0;
    if (!text) text = "";
    length = strlen(text);
    if (length >= GUI_SIGNATURE_MAX) return 0;

    ensure_gui_prefs_drawer();
    file = fopen(SIGNATURE_TEMP, "wb");
    if (!file) return 0;
    if (length && fwrite(text, 1U, length, file) != length)
        write_failed = 1;
    if (fclose(file) != 0) write_failed = 1;
    if (write_failed) {
        DeleteFile((STRPTR)SIGNATURE_TEMP);
        return 0;
    }
    DeleteFile((STRPTR)SIGNATURE_PATH);
    if (!Rename((STRPTR)SIGNATURE_TEMP, (STRPTR)SIGNATURE_PATH)) {
        DeleteFile((STRPTR)SIGNATURE_TEMP);
        return 0;
    }
    return 1;
}

static void signature_dialog(AmgGui *gui)
{
    Object *dialog;
    struct Window *window;
    struct Gadget *editor;
    char signature[GUI_SIGNATURE_MAX];
    ULONG signal_mask = 0UL;
    LONG char_width = 8L, line_height = 8L;
    LONG editor_width, editor_height, signature_gap, button_height;
    int done = 0;

    if (!gui || !gui->screen) return;
    gui_signature_load(signature, sizeof(signature));
    if (gui->screen->RastPort.TxWidth > 0)
        char_width = (LONG)gui->screen->RastPort.TxWidth;
    if (gui->screen->RastPort.TxHeight > 0)
        line_height = (LONG)gui->screen->RastPort.TxHeight;

    /* Visible editing area: roughly 60 screen-font characters by 5 lines. */
    editor_width = char_width * 60L + 12L;
    editor_height = line_height * 5L + 8L;
    signature_gap = (line_height + 1L) / 2L;
    if (signature_gap < 2L) signature_gap = 2L;
    button_height = line_height + 8L;

    editor = NULL;
    dialog = WindowObject,
        WA_Title, T(MSG_AMIMAIL_SIGNATURE, "AmiMail - Signature"),
        WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                  WFLG_ACTIVATE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_RAWKEY,
        WA_PubScreen, gui->screen,
        WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_ParentGroup, VGroupObject,
            /* Let the layout determine the complete requester height.  This
             * keeps the normal ReAction outer margin below the buttons instead
             * of turning an oversized fixed WA_Height into empty lines. */
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_SpaceInner, FALSE,
            LAYOUT_ShrinkWrap, TRUE,

            LAYOUT_AddChild,
                editor = (struct Gadget *)TextEditorObject,
                    GA_ID, GID_SIGNATURE_EDITOR,
                    GA_TabCycle, TRUE,
                    GA_TEXTEDITOR_Contents,
                        (ULONG)(uintptr_t)signature,
                EndObject,
            CHILD_MinWidth, editor_width,
            CHILD_MaxWidth, editor_width,
            CHILD_WeightedWidth, 0,
            CHILD_MinHeight, editor_height,
            CHILD_MaxHeight, editor_height,
            CHILD_WeightedHeight, 0,

            /* Exactly half a text line between editor and buttons. */
            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
            EndObject,
            CHILD_MinHeight, signature_gap,
            CHILD_MaxHeight, signature_gap,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_SIGNATURE_SAVE,
                    GA_RelVerify, TRUE,
                    GA_Text, T(MSG_SAVE, "_Save"),
                EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_SIGNATURE_CANCEL,
                    GA_RelVerify, TRUE,
                    GA_Text, T(MSG_CANCEL, "_Cancel"),
                EndObject,
            EndObject,
            CHILD_MinHeight, button_height,
            CHILD_MaxHeight, button_height,
            CHILD_WeightedHeight, 0,
        EndObject,
    EndWindow;

    if (!dialog) {
        status_local(gui, T(MSG_SIGNATURE_EDITOR_COULD_NOT_BE_OPENED, "Signature editor could not be opened."));
        return;
    }
    window = RA_OpenWindow(dialog);
    if (!window) {
        DisposeObject(dialog);
        status_local(gui, T(MSG_SIGNATURE_EDITOR_COULD_NOT_BE_OPENED, "Signature editor could not be opened."));
        return;
    }
    WindowToFront(window);
    ActivateWindow(window);
    if (editor) ActivateGadget(editor, window, NULL);
    GetAttr(WINDOW_SigMask, dialog, &signal_mask);

    while (!done && signal_mask) {
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
                        if (rawkey_is_cancel(result)) done = 1;
                        break;
                    case WMHI_GADGETUP:
                        if ((result & WMHI_GADGETMASK) ==
                            GID_SIGNATURE_CANCEL) {
                            done = 1;
                        } else if ((result & WMHI_GADGETMASK) ==
                                   GID_SIGNATURE_SAVE) {
                            STRPTR text = (STRPTR)(uintptr_t)DoGadgetMethod(
                                editor, window, NULL,
                                GM_TEXTEDITOR_ExportText, 0UL);
                            if (!text) {
                                status_local(gui,
                                    T(MSG_SIGNATURE_COULD_NOT_BE_READ, "Signature could not be read."));
                            } else if (strlen((const char *)text) >= GUI_SIGNATURE_MAX) {
                                status_local(gui,
                                    T(MSG_SIGNATURE_IS_TOO_LONG, "Signature is too long."));
                                FreeVec(text);
                            } else if (!gui_signature_save((const char *)text)) {
                                status_local(gui,
                                    T(MSG_SIGNATURE_COULD_NOT_BE_SAVED, "Signature could not be saved."));
                                FreeVec(text);
                            } else {
                                FreeVec(text);
                                status_local(gui,
                                    T(MSG_SIGNATURE_WAS_SAVED, "Signature was saved."));
                                done = 1;
                            }
                        }
                        break;
                }
            }
        }
    }
    DisposeObject(dialog);
}

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

static int suppress_next_reply_menu_click = 0;

static void set_reply_menu_arrow(AmgGui *gui, int expanded)
{
    (void)expanded;
    if (!gui || !gui->reply_menu_gadget) return;
    /* Keep the same proven classic-font down-arrow in both states. */
    if (gui->window) {
        SetGadgetAttrs(gui->reply_menu_gadget, gui->window, NULL,
                       GA_Text, (ULONG)(uintptr_t)"v",
                       TAG_DONE);
        RefreshGList(gui->reply_menu_gadget, gui->window, NULL, 1);
    } else {
        SetAttrs((Object *)gui->reply_menu_gadget,
                 GA_Text, (ULONG)(uintptr_t)"v",
                 TAG_DONE);
    }
}

static int main_pointer_over_reply_menu(const AmgGui *gui)
{
    LONG mouse_x, mouse_y, left, top, right, bottom;
    if (!gui || !gui->window || !gui->reply_menu_gadget) return 0;

    if (gui->screen) {
        mouse_x = (LONG)gui->screen->MouseX;
        mouse_y = (LONG)gui->screen->MouseY;
        left = (LONG)gui->window->LeftEdge +
               (LONG)gui->reply_menu_gadget->LeftEdge;
        top = (LONG)gui->window->TopEdge +
              (LONG)gui->reply_menu_gadget->TopEdge;
    } else {
        mouse_x = (LONG)gui->window->MouseX;
        mouse_y = (LONG)gui->window->MouseY;
        left = (LONG)gui->reply_menu_gadget->LeftEdge;
        top = (LONG)gui->reply_menu_gadget->TopEdge;
    }
    right = left + (LONG)gui->reply_menu_gadget->Width - 1L;
    bottom = top + (LONG)gui->reply_menu_gadget->Height - 1L;
    return mouse_x >= left && mouse_x <= right &&
           mouse_y >= top && mouse_y <= bottom;
}

static void update_reply_button_mode(AmgGui *gui)
{
    const char *text;
    int drafts;
    if (!gui || !gui->reply_gadget) return;
    drafts = current_mailbox_is_drafts(gui);
    text = drafts
        ? T(MSG_EDIT, "_Edit")
        : T(MSG_REPLY, "_Reply");
    if (gui->window) {
        SetGadgetAttrs(gui->reply_gadget, gui->window, NULL,
                       GA_Text, (ULONG)(uintptr_t)text,
                       TAG_DONE);
        if (gui->reply_menu_gadget)
            SetGadgetAttrs(gui->reply_menu_gadget, gui->window, NULL,
                           GA_Disabled, drafts ? TRUE : FALSE,
                           TAG_DONE);
        RefreshGList(gui->reply_gadget, gui->window, NULL, 1);
        if (gui->reply_menu_gadget)
            RefreshGList(gui->reply_menu_gadget, gui->window, NULL, 1);
    } else {
        SetAttrs((Object *)gui->reply_gadget,
                 GA_Text, (ULONG)(uintptr_t)text,
                 TAG_DONE);
        if (gui->reply_menu_gadget)
            SetAttrs((Object *)gui->reply_menu_gadget,
                     GA_Disabled, drafts ? TRUE : FALSE,
                     TAG_DONE);
    }
    suppress_next_reply_menu_click = 0;
    set_reply_menu_arrow(gui, 0);
}

static int reply_action_popup(AmgGui *gui)
{
    Object *popup;
    struct Window *window;
    ULONG signal_mask = 0UL;
    LONG left, top, width, button_height, popup_height;
    int selection = 0;
    int done = 0;
    int inactive = 0;

    if (!gui || !gui->window || !gui->reply_gadget ||
        !gui->reply_menu_gadget || current_mailbox_is_drafts(gui))
        return 0;

    /* The drop-down buttons deliberately use the exact width and height of
     * the normal Reply button. With zero inner/outer layout spacing and a
     * borderless window there is no grey popup background around them. */
    width = (LONG)gui->reply_gadget->Width;
    button_height = (LONG)gui->reply_gadget->Height;
    if (width < 1L || button_height < 1L) return 0;
    popup_height = button_height * 2L;

    left = (LONG)gui->window->LeftEdge +
           (LONG)gui->reply_gadget->LeftEdge;
    top = (LONG)gui->window->TopEdge +
          (LONG)gui->reply_gadget->TopEdge + button_height;
    if (gui->screen) {
        if (left + width > (LONG)gui->screen->Width)
            left = (LONG)gui->screen->Width - width;
        if (left < 0L) left = 0L;
        if (top + popup_height > (LONG)gui->screen->Height)
            top = (LONG)gui->window->TopEdge +
                  (LONG)gui->reply_gadget->TopEdge - popup_height;
        if (top < 0L) top = 0L;
    }

    popup = WindowObject,
        WA_Left, left,
        WA_Top, top,
        WA_Width, width,
        WA_Height, popup_height,
        WA_Borderless, TRUE,
        WA_Flags, WFLG_ACTIVATE | WFLG_RMBTRAP,
        WA_IDCMP, IDCMP_GADGETUP | IDCMP_RAWKEY | IDCMP_INACTIVEWINDOW,
        WA_PubScreen, gui->screen,
        WINDOW_ParentGroup, VGroupObject,
            LAYOUT_SpaceOuter, FALSE,
            LAYOUT_SpaceInner, FALSE,
            LAYOUT_AddChild, ButtonObject,
                GA_ID, GID_REPLY_MENU_REPLY_ALL,
                GA_RelVerify, TRUE,
                GA_Text, T(MSG_REPLY_ALL, "Reply _All"),
            EndObject,
            CHILD_MinWidth, width,
            CHILD_MaxWidth, width,
            CHILD_WeightedWidth, 0,
            CHILD_MinHeight, button_height,
            CHILD_MaxHeight, button_height,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, ButtonObject,
                GA_ID, GID_REPLY_MENU_FORWARD,
                GA_RelVerify, TRUE,
                GA_Text, T(MSG_FORWARD, "_Forward"),
            EndObject,
            CHILD_MinWidth, width,
            CHILD_MaxWidth, width,
            CHILD_WeightedWidth, 0,
            CHILD_MinHeight, button_height,
            CHILD_MaxHeight, button_height,
            CHILD_WeightedHeight, 0,
        EndObject,
    EndWindow;
    if (!popup) return 0;
    window = RA_OpenWindow(popup);
    if (!window) {
        DisposeObject(popup);
        return 0;
    }

    set_reply_menu_arrow(gui, 1);
    WindowToFront(window);
    ActivateWindow(window);
    GetAttr(WINDOW_SigMask, popup, &signal_mask);
    if (!signal_mask) {
        DisposeObject(popup);
        set_reply_menu_arrow(gui, 0);
        return 0;
    }

    while (!done) {
        ULONG signals = Wait(signal_mask | SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C) done = 1;
        if (signals & signal_mask) {
            ULONG result;
            while ((result = RA_HandleInput(popup, NULL)) != WMHI_LASTMSG) {
                switch (result & WMHI_CLASSMASK) {
                    case WMHI_INACTIVE:
                        inactive = 1;
                        done = 1;
                        break;
                    case WMHI_RAWKEY:
                        if (rawkey_is_cancel(result)) done = 1;
                        break;
                    case WMHI_GADGETUP:
                        if ((result & WMHI_GADGETMASK) ==
                            GID_REPLY_MENU_REPLY_ALL) {
                            selection = MESSAGE_ACTION_REPLY_ALL;
                            done = 1;
                        } else if ((result & WMHI_GADGETMASK) ==
                                   GID_REPLY_MENU_FORWARD) {
                            selection = MESSAGE_ACTION_FORWARD;
                            done = 1;
                        }
                        break;
                }
            }
        }
    }

    /* Clicking the arrow again first makes this borderless popup inactive.
     * That click is still queued for the main window after we return. Detect
     * that exact case and suppress only the queued arrow event, so the menu
     * closes instead of immediately reopening. */
    if (inactive && (gui->window->Flags & WFLG_WINDOWACTIVE) &&
        main_pointer_over_reply_menu(gui))
        suppress_next_reply_menu_click = 1;

    DisposeObject(popup);
    set_reply_menu_arrow(gui, 0);
    return selection;
}


static int request_label_index(AmgGui *gui, size_t index, AmgError *error)
{
    char message[192];
    int result;
    if (!gui || index >= gui->label_count) return AMG_ERR_ARGUMENT;
    if (!gui->labels[index].available ||
        !gui->labels[index].mailbox_utf8[0]) {
        status_local(gui,
                     T(MSG_THIS_FOLDER_IS_NOT_AVAILABLE_FOR_THE_ACCOUNT, "This folder is not available for the account."));
        return AMG_ERR_ARGUMENT;
    }
    if (!gui->labels[index].selectable) {
        status_local(gui,
                     T(MSG_THIS_ENTRY_IS_ONLY_A_FOLDER_CONTAINER_AND, "This entry is only a folder container and cannot be opened."));
        return AMG_ERR_ARGUMENT;
    }
    if (!amg_network_is_connected(gui->network)) {
        status_local(gui, T(MSG_PLEASE_CLICK_FETCH_FIRST, "Please click 'Fetch' first."));
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
    update_reply_button_mode(gui);
    clear_current_message_payload(gui);
    show_message_placeholder(gui, T(MSG_LOADING_FOLDER, "Loading folder..."));
    set_preview_local(gui,
                      T(MSG_SELECT_A_MESSAGE_AFTER_FETCHING, "Select a message after fetching."));
    amg_tr_snprintf(message, sizeof(message), MSG_LOADING_VALUE, "Loading %s...", gui->current_label_local);
    status_local(gui, message);
    return AMG_OK;
}

static void begin_move(AmgGui *gui, AmgError *error)
{
    ULONG uid = 0;
    size_t source_index;
    if (!cursor_node_user_data(gui->messages_gadget, &uid) || !uid) {
        status_local(gui, T(MSG_PLEASE_SELECT_A_MESSAGE_FIRST, "Please select a message first."));
        return;
    }
    if (!amg_network_is_connected(gui->network)) {
        status_local(gui, T(MSG_PLEASE_CLICK_FETCH_FIRST, "Please click 'Fetch' first."));
        return;
    }
    if (!gui->current_mailbox_utf8[0]) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T(MSG_THE_SOURCE_FOLDER_IS_UNKNOWN, "The source folder is unknown."));
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
        T(MSG_PLEASE_SELECT_A_DESTINATION_FOLDER_ON_THE_LEFT, "Please select a destination folder on the left."));
}

void cancel_pending_move(AmgGui *gui)
{
    if (!gui || !gui->move_pending) return;
    gui->move_pending = 0;
    gui->move_uid = 0;
    gui->move_source_mailbox_utf8[0] = 0;
    status_local(gui, T(MSG_MOVE_CANCELLED, "Move cancelled."));
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
                     T(MSG_THIS_FOLDER_IS_NOT_AVAILABLE_FOR_THE_ACCOUNT, "This folder is not available for the account."));
        return AMG_ERR_ARGUMENT;
    }
    if (!gui->labels[index].selectable) {
        status_local(gui,
                     T(MSG_MESSAGES_CANNOT_BE_MOVED_INTO_THIS_FOLDER_CONTAINER, "Messages cannot be moved into this folder container."));
        return AMG_ERR_ARGUMENT;
    }
    if (!strcmp(gui->move_source_mailbox_utf8,
                gui->labels[index].server_mailbox_utf8)) {
        status_local(gui, T(MSG_SOURCE_AND_DESTINATION_FOLDERS_ARE_IDENTICAL, "Source and destination folders are identical."));
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
    amg_tr_snprintf(message, sizeof(message), MSG_MOVING_MESSAGE_TO_VALUE, "Moving message to %s...", target_name);
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
            T(MSG_THIS_FOLDER_CONTAINS_NO_MESSAGES, "This folder contains no messages."));
        if (node) AddTail(&gui->messages_list, node);
    }
    attach_listbrowser(gui->messages_gadget, gui->window,
                       &gui->messages_list);
    clear_current_message_payload(gui);
    set_preview_local(gui,
                      T(MSG_SELECT_A_MESSAGE_TO_DISPLAY, "Select a message to display."));
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
        status_local(gui, T(MSG_PLEASE_CLICK_FETCH_FIRST, "Please click 'Fetch' first."));
        return;
    }
    flagged = !message_is_flagged(gui, uid);
    result = amg_network_request(
        gui->network, AMG_NET_SET_FLAGGED, uid,
        flagged ? "1" : "0", "doubleclick", error);
    if (result == AMG_OK) {
        set_message_flagged_visual(gui, uid, flagged);
        status_local(gui, flagged
            ? T(MSG_SETTING_STAR, "Setting star...")
            : T(MSG_REMOVING_STAR, "Removing star..."));
    } else if (error && error->message[0]) {
        status_utf8(gui, error->message);
    }
}

static void request_message(AmgGui *gui, int action, AmgError *error)
{
    ULONG uid = 0;
    ULONG selected[2];
    size_t selected_count;
    ULONG release_event = LBRE_NORMAL;
    int is_doubleclick = 0;
    int prepare_reply = action == MESSAGE_ACTION_REPLY;
    int prepare_reply_all = action == MESSAGE_ACTION_REPLY_ALL;
    int prepare_forward = action == MESSAGE_ACTION_FORWARD;
    int edit_draft = action == MESSAGE_ACTION_EDIT_DRAFT;
    int compose_action = prepare_reply || prepare_reply_all ||
                         prepare_forward || edit_draft;
    const char *request_kind = "preview";
    int result;
    if (!gui || !gui->messages_gadget) return;
    if (!compose_action) {
        GetAttr(LISTBROWSER_RelEvent, (Object *)gui->messages_gadget,
                &release_event);
        if (release_event == LBRE_TITLECLICK) return;
        if (!cursor_node_user_data(gui->messages_gadget, &uid) || !uid) {
            status_local(gui, T(MSG_PLEASE_SELECT_A_MESSAGE_FIRST, "Please select a message first."));
            return;
        }
        is_doubleclick = message_doubleclick_detected(gui, uid, release_event);
    } else {
        selected_count = selected_message_uids(gui, selected, 2U);
        if (!selected_count) {
            status_local(gui, T(MSG_PLEASE_SELECT_A_MESSAGE_FIRST, "Please select a message first."));
            return;
        }
        if (selected_count > 1U) {
            if (edit_draft)
                status_local(gui,
                    T(MSG_PLEASE_SELECT_ONLY_ONE_DRAFT_TO_EDIT, "Please select only one draft to edit."));
            else if (prepare_forward)
                status_local(gui,
                    T(MSG_PLEASE_SELECT_ONLY_ONE_MESSAGE_TO_FORWARD, "Please select only one message to forward."));
            else
                status_local(gui,
                    T(MSG_PLEASE_SELECT_ONLY_ONE_MESSAGE_TO_REPLY, "Please select only one message to reply."));
            return;
        }
        uid = selected[0];
    }
    if (!uid) {
        status_local(gui, T(MSG_PLEASE_SELECT_A_MESSAGE_FIRST, "Please select a message first."));
        return;
    }
    if (edit_draft && !current_mailbox_is_drafts(gui)) {
        update_reply_button_mode(gui);
        status_local(gui,
                     T(MSG_DRAFTS_CAN_ONLY_BE_EDITED_IN_THE_DRAFTS, "Drafts can only be edited in the Drafts folder."));
        return;
    }
    set_message_selected_visual(gui, uid);
    if (!compose_action && is_doubleclick) {
        toggle_message_flagged(gui, uid, error);
        return;
    }
    if (!amg_network_is_connected(gui->network)) {
        status_local(gui, T(MSG_PLEASE_CLICK_FETCH_FIRST, "Please click 'Fetch' first."));
        return;
    }
    if (edit_draft) request_kind = "editdraft";
    else if (prepare_reply_all) request_kind = "reply-all";
    else if (prepare_forward) request_kind = "forward";
    else if (prepare_reply) request_kind = "reply";

    result = amg_network_request(gui->network, AMG_NET_FETCH_MESSAGE, uid,
                                 request_kind,
                                 edit_draft
                                    ? (gui->labels[3U].server_mailbox_utf8[0]
                                           ? gui->labels[3U].server_mailbox_utf8
                                           : gui->current_mailbox_utf8)
                                    : NULL,
                                 error);
    if (result == AMG_OK) {
        clear_current_message_payload(gui);
        if (edit_draft)
            status_local(gui, T(MSG_LOADING_DRAFT_FOR_EDITING, "Loading draft for editing..."));
        else if (prepare_forward)
            status_local(gui, T(MSG_PREPARING_FORWARD, "Preparing forward..."));
        else if (prepare_reply_all)
            status_local(gui, T(MSG_PREPARING_REPLY_TO_ALL, "Preparing reply to all..."));
        else if (prepare_reply)
            status_local(gui, T(MSG_PREPARING_REPLY, "Preparing reply..."));
        else {
            set_preview_local(gui, T(MSG_LOADING_MESSAGE, "Loading message..."));
            status_local(gui, T(MSG_LOADING_MESSAGE, "Loading message..."));
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
        status_local(gui, T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory."));
        return;
    }
    if (!count) {
        free(uids);
        status_local(gui, T(MSG_PLEASE_SELECT_A_MESSAGE_FIRST, "Please select a message first."));
        return;
    }
    if (!amg_network_is_connected(gui->network)) {
        free(uids);
        status_local(gui, T(MSG_PLEASE_CLICK_FETCH_FIRST, "Please click 'Fetch' first."));
        return;
    }
    for (i = 0; i < count; ++i) {
        int seen = !message_is_seen(gui, uids[i]);
        int in_inbox = !strcmp(gui->current_mailbox_utf8, "INBOX");
        int result = amg_network_request(
            gui->network, AMG_NET_SET_SEEN, uids[i],
            seen ? "1" : "0", in_inbox ? "button-inbox" : "button",
            error);
        if (result != AMG_OK) break;
        set_message_seen_visual(gui, uids[i], seen);
        if (in_inbox)
            gui_state_adjust_inbox_unseen(gui, seen ? -1L : 1L);
        ++queued;
    }
    free(uids);
    if (queued == count) {
        char message[128];
        amg_tr_snprintf(message, sizeof(message), MSG_VALUE_MESSAGE_S_CHANGING_READ_STATUS, "%lu message(s): changing read status.", (unsigned long)queued);
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
        status_local(gui, T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory."));
        return;
    }
    if (!count) {
        free(uids);
        status_local(gui, T(MSG_PLEASE_SELECT_A_MESSAGE_FIRST, "Please select a message first."));
        return;
    }
    if (!amg_network_is_connected(gui->network)) {
        free(uids);
        status_local(gui, T(MSG_PLEASE_CLICK_FETCH_FIRST, "Please click 'Fetch' first."));
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
        status_local(gui, T(MSG_THE_CURRENT_MAIL_FOLDER_IS_UNKNOWN, "The current mail folder is unknown."));
        return;
    }
    if (!strcmp(source_label, trash_label)) {
        free(uids);
        status_local(gui, T(MSG_THE_MESSAGE_IS_ALREADY_IN_TRASH, "The message is already in Trash."));
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
    amg_tr_snprintf(status_text, sizeof(status_text), MSG_DELETING_VALUE_MESSAGE_S, "Deleting %lu message(s).", (unsigned long)count);
    status_local(gui, status_text);
}

static void empty_trash(AmgGui *gui, AmgError *error)
{
    const char *trash_label;
    int result;
    if (!gui) return;
    if (!amg_network_is_connected(gui->network)) {
        status_local(gui, T(MSG_PLEASE_CLICK_FETCH_FIRST, "Please click 'Fetch' first."));
        return;
    }
    if (!confirm_empty_trash_dialog(gui)) return;
    trash_label = gui->labels[6U].available
        ? gui->labels[6U].server_mailbox_utf8 : "\\Trash";
    if (!trash_label || !*trash_label) {
        status_local(gui, T(MSG_THE_TRASH_FOLDER_IS_UNKNOWN, "The Trash folder is unknown."));
        return;
    }
    result = amg_network_request(gui->network, AMG_NET_EMPTY_TRASH, 0,
                                 trash_label, NULL, error);
    if (result == AMG_OK)
        status_local(gui, T(MSG_EMPTYING_TRASH, "Emptying Trash..."));
    else if (error && error->message[0])
        status_utf8(gui, error->message);
}

static void empty_spam(AmgGui *gui, AmgError *error)
{
    const char *spam_label;
    int result;
    if (!gui) return;
    if (!amg_network_is_connected(gui->network)) {
        status_local(gui, T(MSG_PLEASE_CLICK_FETCH_FIRST, "Please click 'Fetch' first."));
        return;
    }
    if (!confirm_empty_spam_dialog(gui)) return;
    spam_label = gui->labels[5U].available
        ? gui->labels[5U].server_mailbox_utf8 : NULL;
    if (!spam_label || !*spam_label) {
        status_local(gui, T(MSG_THE_SPAM_FOLDER_IS_UNKNOWN, "The Spam folder is unknown."));
        return;
    }
    result = amg_network_request(gui->network, AMG_NET_EMPTY_SPAM, 0,
                                 spam_label, NULL, error);
    if (result == AMG_OK)
        status_local(gui, T(MSG_EMPTYING_SPAM, "Emptying Spam..."));
    else if (error && error->message[0])
        status_utf8(gui, error->message);
}

void handle_network(AmgGui *gui)
{
    AmgNetworkEvent event;
    while (amg_network_poll(gui->network, &event) > 0) {
        if (event.type == AMG_NET_CHECK_INBOX)
            gui->periodic_check_pending = 0;
        if (event.type == AMG_NET_RECONFIGURE)
            gui->network_reconfigure_pending = 0;

        /* Update-Pruefungen sind bewusst still. Ein fehlendes GitHub oder
         * Netzwerk darf beim Programmstart keinen Fehlerdialog/-status erzeugen. */
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
            if (event.type == AMG_NET_SET_SEEN) {
                set_message_seen_visual(
                    gui, event.uid, atoi(event.argument1) == 0);
                if (strstr(event.argument2, "-inbox"))
                    gui_state_adjust_inbox_unseen(
                        gui, atoi(event.argument1) ? 1L : -1L);
            } else if (event.type == AMG_NET_SET_FLAGGED) {
                set_message_flagged_visual(
                    gui, event.uid, atoi(event.argument1) == 0);
            }
            status_utf8(gui,
                        event.message[0] ? event.message : T(MSG_NETWORK_ERROR, "Network error"));
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
                    status_local(gui, T(MSG_CONNECTED_TO_THE_MAIL_SERVER, "Connected to the mail server."));
                    amg_network_request(gui->network, AMG_NET_FETCH_LABELS,
                                        0, NULL, NULL, NULL);
                    break;
                case AMG_NET_RECONFIGURE:
                    gui->network_reconfigure_pending = 0;
                    status_local(gui,
                        T(MSG_MAIL_SERVER_CONNECTION_WAS_RECONFIGURED, "Mail-server connection was reconfigured."));
                    amg_network_request(gui->network, AMG_NET_FETCH_LABELS,
                                        0, NULL, NULL, NULL);
                    break;
                case AMG_NET_FETCH_LABELS:
                {
                    AmgError request_error;
                    char message[96];
                    size_t count = update_labels_from_payload(
                        gui, event.payload, event.payload_length);
                    amg_tr_snprintf(message, sizeof(message), MSG_VALUE_IMAP_FOLDERS_LOADED, "%lu IMAP folders loaded.", (unsigned long)count);
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
                                /* First run after installing the persistent
                                 * notification state: alert once if the first
                                 * snapshot contains unread mail, then save the
                                 * high-water mark for subsequent starts. */
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
                        update_reply_button_mode(gui);
                    }
                    set_preview_local(
                        gui, T(MSG_SELECT_A_MESSAGE_TO_DISPLAY, "Select a message to display."));
                    if (parse_error < 0) {
                        amg_tr_snprintf(message, sizeof(message), MSG_IMAP_MESSAGES_COULD_NOT_BE_PARSED_CODE_VALUE, "IMAP messages could not be parsed (code %d).", parse_error);
                    } else {
                        amg_tr_snprintf(message, sizeof(message), MSG_VALUE_MESSAGES_LOADED_IN_VALUE, "%lu messages loaded in %s.", (unsigned long)count, gui->current_label_local[0]
                                            ? gui->current_label_local
                                            : T(MSG_FOLDER, "folder"));
                    }
                    status_local(gui, message);
                    if (index == 0U && gui->update_check_deferred) {
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
                        amg_tr_snprintf(message, sizeof(message), MSG_PERIODIC_FETCH_COULD_NOT_BE_PARSED_CODE_VALUE, "Periodic fetch could not be parsed (code %d).", parse_error);
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
                            /* Migration from the old RAM-only baseline:
                             * issue at most one initial notification, then
                             * persist UID/UIDVALIDITY for future starts. */
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
                            amg_tr_snprintf(message, sizeof(message), MSG_PERIODIC_FETCH_VALUE_NEW_MAIL_S_IN_INBOX, "Periodic fetch: %lu new mail(s) in Inbox.", (unsigned long)notify_count);
                            status_local(gui, message);
                        } else {
                            status_local(gui,
                                T(MSG_PERIODIC_FETCH_NO_NEW_MAIL, "Periodic fetch: no new mail."));
                        }
                    }
                    break;
                }
                case AMG_NET_FETCH_MESSAGE:
                {
                    AmgError preview_error;
                    int prepare_reply = !strcmp(event.argument1, "reply");
                    int prepare_reply_all =
                        !strcmp(event.argument1, "reply-all");
                    int prepare_forward =
                        !strcmp(event.argument1, "forward");
                    int edit_draft = !strcmp(event.argument1, "editdraft");
                    int unread_before = !edit_draft &&
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
                        if (edit_draft) {
                            DraftEditData edit;
                            memset(&edit, 0, sizeof(edit));
                            if (prepare_draft_edit_payload(
                                    gui, event.payload, event.payload_length,
                                    event.uid, event.argument2, &edit,
                                    &preview_error) == AMG_OK) {
                                if (!compose_dialog(gui, COMPOSE_MODE_EDIT_DRAFT,
                                                    &edit, &preview_error))
                                    status_local(gui,
                                        T(MSG_DRAFT_WAS_NOT_CHANGED, "Draft was not changed."));
                                cleanup_draft_edit_files(&edit);
                            } else {
                                cleanup_draft_edit_files(&edit);
                                status_utf8(gui,
                                    preview_error.message[0]
                                        ? preview_error.message
                                        : T(MSG_DRAFT_COULD_NOT_BE_PREPARED_FOR_EDITING, "Draft could not be prepared for editing."));
                            }
                        } else if (prepare_forward) {
                            DraftEditData forward_seed;
                            memset(&forward_seed, 0, sizeof(forward_seed));
                            if (prepare_forward_payload(
                                    gui, event.payload, event.payload_length,
                                    event.uid, &forward_seed,
                                    &preview_error) == AMG_OK) {
                                if (!compose_dialog(gui, COMPOSE_MODE_FORWARD,
                                                    &forward_seed, &preview_error))
                                    status_local(gui,
                                        T(MSG_FORWARD_WAS_NOT_SENT, "Forward was not sent."));
                                cleanup_draft_edit_files(&forward_seed);
                            } else {
                                cleanup_draft_edit_files(&forward_seed);
                                status_utf8(gui,
                                    preview_error.message[0]
                                        ? preview_error.message
                                        : T(MSG_FORWARD_COULD_NOT_BE_PREPARED, "Forward could not be prepared."));
                            }
                        } else if (prepare_reply_all || prepare_reply) {
                            int prep_result = prepare_reply_all
                                ? prepare_reply_all_payload(
                                    gui, event.payload, event.payload_length,
                                    &preview_error)
                                : prepare_reply_payload(
                                    gui, event.payload, event.payload_length,
                                    &preview_error);
                            if (prep_result == AMG_OK) {
                                if (!compose_dialog(gui, COMPOSE_MODE_REPLY,
                                                    NULL, &preview_error))
                                    status_local(gui, prepare_reply_all
                                        ? T(MSG_REPLY_TO_ALL_WAS_NOT_SENT, "Reply to all was not sent.")
                                        : T(MSG_REPLY_WAS_NOT_SENT, "Reply was not sent."));
                            } else {
                                status_utf8(gui,
                                    preview_error.message[0]
                                        ? preview_error.message
                                        : (prepare_reply_all
                                            ? T(MSG_REPLY_TO_ALL_COULD_NOT_BE_PREPARED, "Reply to all could not be prepared.")
                                            : T(MSG_REPLY_COULD_NOT_BE_PREPARED, "Reply could not be prepared.")));
                            }
                        } else {
                            status_local(gui, T(MSG_MESSAGE_LOADED, "Message loaded."));
                        }
                    } else {
                        status_utf8(
                            gui, preview_error.message[0]
                                     ? preview_error.message
                                     : T(MSG_MESSAGE_COULD_NOT_BE_DISPLAYED, "Message could not be displayed."));
                    }
                    break;
                }
                case AMG_NET_SET_SEEN:
                    set_message_seen_visual(
                        gui, event.uid, atoi(event.argument1) != 0);
                    if (strncmp(event.argument2, "preview", 7U))
                        status_local(
                            gui, atoi(event.argument1)
                                ? T(MSG_MESSAGE_MARKED_AS_READ, "Message marked as read.")
                                : T(MSG_MESSAGE_MARKED_AS_UNREAD, "Message marked as unread."));
                    break;
                case AMG_NET_SET_FLAGGED:
                    set_message_flagged_visual(
                        gui, event.uid, atoi(event.argument1) != 0);
                    status_local(
                        gui, atoi(event.argument1)
                            ? T(MSG_STAR_WAS_SET, "Star was set.")
                            : T(MSG_STAR_WAS_REMOVED, "Star was removed."));
                    break;
                case AMG_NET_DELETE:
                    if (!strcmp(gui->current_mailbox_utf8, "INBOX") &&
                        !message_is_seen(gui, event.uid))
                        gui_state_adjust_inbox_unseen(gui, -1L);
                    remove_message_uid(gui, event.uid);
                    status_local(gui,
                                 T(MSG_MESSAGE_MOVED_TO_TRASH, "Message moved to Trash."));
                    break;
                case AMG_NET_EMPTY_TRASH:
                    if (label_index_for_mailbox(
                            gui, gui->current_mailbox_utf8) == 6U) {
                        clear_current_message_payload(gui);
                        show_message_placeholder(
                            gui, T(MSG_THIS_FOLDER_CONTAINS_NO_MESSAGES, "This folder contains no messages."));
                        set_preview_local(
                            gui, T(MSG_SELECT_A_MESSAGE_TO_DISPLAY, "Select a message to display."));
                    }
                    status_local(gui, T(MSG_TRASH_WAS_EMPTIED, "Trash was emptied."));
                    break;
                case AMG_NET_EMPTY_SPAM:
                    if (label_index_for_mailbox(
                            gui, gui->current_mailbox_utf8) == 5U) {
                        clear_current_message_payload(gui);
                        show_message_placeholder(
                            gui, T(MSG_THIS_FOLDER_CONTAINS_NO_MESSAGES, "This folder contains no messages."));
                        set_preview_local(
                            gui, T(MSG_SELECT_A_MESSAGE_TO_DISPLAY, "Select a message to display."));
                    }
                    status_local(gui, T(MSG_SPAM_WAS_EMPTIED, "Spam was emptied."));
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
                                     T(MSG_DRAFT_WAS_SAVED_REFRESHING_FOLDER, "Draft was saved; refreshing folder..."));
                    else
                        status_local(gui, T(MSG_DRAFT_WAS_SAVED, "Draft was saved."));
                    break;
                case AMG_NET_SEND_REPLY:
                    status_local(gui, T(MSG_REPLY_SENT, "Reply sent."));
                    break;
                case AMG_NET_SEND_MAIL:
                    if (event.uid && !strcmp(event.argument2,
                                             "draft-removed"))
                        remove_message_uid(gui, event.uid);
                    if (event.message[0])
                        status_utf8(gui, event.message);
                    else
                        status_local(gui, T(MSG_MAIL_SENT, "Mail sent."));
                    break;
                case AMG_NET_MOVE:
                {
                    char message[640];
                    size_t target = label_index_for_server_mailbox(
                        gui, event.argument2);
                    const char *target_name = event.argument2;
                    int was_unseen = !message_is_seen(gui, event.uid);
                    if (was_unseen) {
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
                    amg_tr_snprintf(message, sizeof(message), MSG_MESSAGE_MOVED_TO_VALUE, "Message moved to %s.", target_name && *target_name
                                        ? target_name
                                        : T(MSG_DESTINATION_FOLDER, "destination folder"));
                    status_local(gui, message);
                    break;
                }
                default:
                    status_local(gui, T(MSG_ACTION_COMPLETED, "Action completed."));
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
        status_local(gui, T(MSG_PERIODIC_FETCH_CONNECTING_TO_THE_MAIL_SERVER, "Periodic fetch: connecting to the mail server..."));
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
        status_local(gui, T(MSG_CONNECTING_TO_THE_MAIL_SERVER, "Connecting to the mail server..."));
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
            if (ensure_account(gui, error))
                compose_dialog(gui, COMPOSE_MODE_NEW, NULL, error);
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
            request_message(gui, MESSAGE_ACTION_PREVIEW, error);
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
            request_message(gui, current_mailbox_is_drafts(gui)
                                     ? MESSAGE_ACTION_EDIT_DRAFT
                                     : MESSAGE_ACTION_REPLY,
                            error);
            break;
        case GID_REPLY_MENU:
            if (suppress_next_reply_menu_click) {
                suppress_next_reply_menu_click = 0;
                break;
            }
            if (!current_mailbox_is_drafts(gui)) {
                int action = reply_action_popup(gui);
                if (action) request_message(gui, action, error);
            }
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
        case MENU_SIGNATURE:
            signature_dialog(gui);
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
