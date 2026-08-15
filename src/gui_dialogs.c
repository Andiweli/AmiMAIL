#include "gui_internal.h"
#include "storage.h"
#include "charset.h"
#include "i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if AMIGMAIL_AMIGA
#include <clib/alib_protos.h>
#include <classes/window.h>
#include <dos/dos.h>
#include <gadgets/button.h>
#include <gadgets/layout.h>
#include <gadgets/string.h>
#include <intuition/classes.h>
#include <intuition/intuition.h>
#include <libraries/asl.h>
#include <proto/asl.h>
#include <proto/button.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <proto/string.h>
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
#define GUI_ACCOUNT_FIELD_GAP 1
#define GUI_ACCOUNT_LABEL_WIDTH 150
#define GUI_RAWKEY_NP_ENTER 0x43UL
#define GUI_RAWKEY_RETURN 0x44UL
#define GUI_RAWKEY_ESCAPE 0x45UL
#define T(de,en) amg_tr((de),(en))
enum AccountGadgetId { GID_ACCOUNT_NAME=100,GID_ACCOUNT_EMAIL,GID_ACCOUNT_IMAP_HOST,GID_ACCOUNT_IMAP_PORT,GID_ACCOUNT_IMAP_STARTTLS,GID_ACCOUNT_IMAP_USERNAME,GID_ACCOUNT_IMAP_PASSWORD,GID_ACCOUNT_SMTP_HOST,GID_ACCOUNT_SMTP_PORT,GID_ACCOUNT_SMTP_STARTTLS,GID_ACCOUNT_SMTP_USERNAME,GID_ACCOUNT_SMTP_PASSWORD,GID_ACCOUNT_FOLDER_MAPPING,GID_ACCOUNT_MASTER_PASSWORD,GID_ACCOUNT_FETCH_DAYS,GID_ACCOUNT_FETCH_ON_START,GID_ACCOUNT_PERIODIC_FETCH,GID_ACCOUNT_NOTIFICATION_SOUND,GID_ACCOUNT_NOTIFICATION_PATH,GID_ACCOUNT_NOTIFICATION_CHOOSE,GID_ACCOUNT_STATUS,GID_ACCOUNT_UNLOCK,GID_ACCOUNT_SAVE,GID_ACCOUNT_CANCEL };
enum FolderMappingGadgetId { GID_FOLDER_SENT=140,GID_FOLDER_DRAFTS,GID_FOLDER_ALL,GID_FOLDER_SPAM,GID_FOLDER_TRASH,GID_FOLDER_SAVE_SENT,GID_FOLDER_OK,GID_FOLDER_CANCEL };
enum ConfirmGadgetId { GID_CONFIRM_YES=300,GID_CONFIRM_NO };
enum AboutGadgetId { GID_ABOUT_OK=400 };

static void center_window_over_window(struct Window *window,
                                      const struct Window *reference)
{
    LONG left, top, max_left, max_top;
    if (!window || !reference || !window->WScreen) return;

    left = (LONG)reference->LeftEdge +
           ((LONG)reference->Width - (LONG)window->Width) / 2L;
    top = (LONG)reference->TopEdge +
          ((LONG)reference->Height - (LONG)window->Height) / 2L;

    max_left = (LONG)window->WScreen->Width - (LONG)window->Width;
    max_top = (LONG)window->WScreen->Height - (LONG)window->Height;
    if (max_left < 0) max_left = 0;
    if (max_top < 0) max_top = 0;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (left > max_left) left = max_left;
    if (top > max_top) top = max_top;

    if (left != (LONG)window->LeftEdge || top != (LONG)window->TopEdge)
        MoveWindow(window, left - (LONG)window->LeftEdge,
                   top - (LONG)window->TopEdge);
}

static int requester_rawkey_accept(ULONG key)
{
    return key == GUI_RAWKEY_RETURN || key == GUI_RAWKEY_NP_ENTER;
}

static int requester_string_accept_code(UWORD code)
{
    /* Intuition returns the keymapped Return/Enter character in the
     * GADGETUP Code field.  TAB is 0x09 and must keep cycling fields. */
    return code == (UWORD)'\r';
}

static int requester_password_accept_code(UWORD code)
{
    /* On classic ReAction the SHK_PASSWORD hook may terminate a string
     * gadget with Code 0 instead of '\r' when Return/Enter is pressed.
     * TAB must never submit the requester; it remains normal tab cycling. */
    return code == 0U || code == (UWORD)'\r';
}

static int folder_mapping_string_id(ULONG gadget_id)
{
    return gadget_id >= GID_FOLDER_SENT && gadget_id <= GID_FOLDER_TRASH;
}

static int account_string_id(ULONG gadget_id)
{
    switch (gadget_id) {
        case GID_ACCOUNT_NAME:
        case GID_ACCOUNT_EMAIL:
        case GID_ACCOUNT_IMAP_HOST:
        case GID_ACCOUNT_IMAP_PORT:
        case GID_ACCOUNT_IMAP_USERNAME:
        case GID_ACCOUNT_IMAP_PASSWORD:
        case GID_ACCOUNT_SMTP_HOST:
        case GID_ACCOUNT_SMTP_PORT:
        case GID_ACCOUNT_SMTP_USERNAME:
        case GID_ACCOUNT_SMTP_PASSWORD:
        case GID_ACCOUNT_MASTER_PASSWORD:
        case GID_ACCOUNT_FETCH_DAYS:
            return 1;
        default:
            return 0;
    }
}

static int ensure_config_drawer(AmgError *error)
{
    BPTR lock = Lock((CONST_STRPTR)ACCOUNT_DRAWER, ACCESS_READ);
    if (lock) {
        UnLock(lock);
        return AMG_OK;
    }
    lock = CreateDir((CONST_STRPTR)ACCOUNT_DRAWER);
    if (!lock) {
        amg_error_set(error, AMG_ERR_IO,
                      T("ENVARC:AmiMail konnte nicht angelegt werden.", "ENVARC:AmiMail could not be created."));
        return AMG_ERR_IO;
    }
    UnLock(lock);
    return AMG_OK;
}

 int account_is_locked(const AmgAccount *account)
{
    if (!account || !account->email[0]) return 1;
    if (account->auth_mode == AMG_AUTH_OAUTH2_GOOGLE)
        return !account->refresh_token;
    return !account->imap_password;
}

static void replace_account(AmgGui *gui, AmgAccount *replacement)
{
    amg_account_clear(gui->account);
    *gui->account = *replacement;
    replacement->imap_password = NULL;
    replacement->smtp_password = NULL;
    replacement->refresh_token = NULL;
    /* The persisted Inbox UID high-water mark is account-specific.  Reload
     * it after unlock or account replacement so the first fetch can already
     * recognise mail that arrived while AmiMAIL was not running. */
    gui_state_load_inbox_notification(gui);
}

static int nullable_text_equal(const char *left, const char *right)
{
    if (!left) left = "";
    if (!right) right = "";
    return strcmp(left, right) == 0;
}

/* Only settings copied into and used by the worker belong here.  Pure local
 * preferences such as fetch-on-start, periodic-fetch enablement and the
 * notification sound must never tear down a healthy IMAP/SMTP session. */
static int account_network_settings_equal(const AmgAccount *left,
                                          const AmgAccount *right)
{
    if (!left || !right) return 0;
    return !strcmp(left->display_name, right->display_name) &&
           !strcmp(left->email, right->email) &&
           left->auth_mode == right->auth_mode &&
           !strcmp(left->imap_host, right->imap_host) &&
           left->imap_port == right->imap_port &&
           left->imap_starttls == right->imap_starttls &&
           !strcmp(left->imap_username, right->imap_username) &&
           !strcmp(left->smtp_host, right->smtp_host) &&
           left->smtp_port == right->smtp_port &&
           left->smtp_starttls == right->smtp_starttls &&
           !strcmp(left->smtp_username, right->smtp_username) &&
           !strcmp(left->sent_mailbox, right->sent_mailbox) &&
           !strcmp(left->drafts_mailbox, right->drafts_mailbox) &&
           !strcmp(left->all_mailbox, right->all_mailbox) &&
           !strcmp(left->spam_mailbox, right->spam_mailbox) &&
           !strcmp(left->trash_mailbox, right->trash_mailbox) &&
           left->save_sent_copy == right->save_sent_copy &&
           left->fetch_days == right->fetch_days &&
           nullable_text_equal(left->imap_password, right->imap_password) &&
           nullable_text_equal(left->smtp_password, right->smtp_password) &&
           nullable_text_equal(left->refresh_token, right->refresh_token);
}

static void notification_sound_initial_parts(const char *path,
                                             char *drawer,
                                             size_t drawer_capacity,
                                             char *file,
                                             size_t file_capacity)
{
    STRPTR part;
    if (!drawer || !drawer_capacity || !file || !file_capacity) return;
    drawer[0] = 0;
    file[0] = 0;
    if (!path || !*path) return;
    strncpy(drawer, path, drawer_capacity - 1U);
    drawer[drawer_capacity - 1U] = 0;
    part = FilePart((STRPTR)drawer);
    if (part && *part) {
        strncpy(file, (const char *)part, file_capacity - 1U);
        file[file_capacity - 1U] = 0;
        *part = 0;
    }
}

static void choose_notification_sound(AmgGui *gui,
                                      struct Window *window,
                                      struct Gadget *path_gadget,
                                      struct Gadget *enabled_gadget,
                                      struct Gadget *status_gadget)
{
    struct FileRequester *requester;
    char drawer[512];
    char file[256];
    char selected[512];
    char accept_pattern[96];
    LONG pattern_result;
    if (!path_gadget) return;

    pattern_result = ParsePatternNoCase(
        (CONST_STRPTR)"#?.(iff|8svx|wav)",
        (STRPTR)accept_pattern, (LONG)sizeof(accept_pattern));
    notification_sound_initial_parts(
        string_text(path_gadget), drawer, sizeof(drawer), file, sizeof(file));
    requester = AllocAslRequestTags(
        ASL_FileRequest,
        ASLFR_TitleText,
            (ULONG)(uintptr_t)T("Benachrichtigungston ausw\344hlen",
                                "Select notification sound"),
        ASLFR_Window, (ULONG)(uintptr_t)window,
        ASLFR_SleepWindow, TRUE,
        ASLFR_RejectIcons, TRUE,
        pattern_result >= 0 ? ASLFR_AcceptPattern : TAG_IGNORE,
            (ULONG)(uintptr_t)accept_pattern,
        drawer[0] ? ASLFR_InitialDrawer : TAG_IGNORE,
            (ULONG)(uintptr_t)drawer,
        file[0] ? ASLFR_InitialFile : TAG_IGNORE,
            (ULONG)(uintptr_t)file,
        TAG_DONE);
    if (!requester) return;

    if (AslRequest(requester, NULL)) {
        strncpy(selected,
                requester->fr_Drawer ? (const char *)requester->fr_Drawer : "",
                sizeof(selected) - 1U);
        selected[sizeof(selected) - 1U] = 0;
        if (requester->fr_File && requester->fr_File[0] &&
            AddPart((STRPTR)selected, (CONST_STRPTR)requester->fr_File,
                    (LONG)sizeof(selected))) {
            set_string(path_gadget, window, selected);
            if (gui_notify_preview_sound(gui, selected)) {
                if (status_gadget)
                    set_string(status_gadget, window,
                               T("Ton wird probeweise abgespielt...",
                                 "Playing sound preview..."));
            } else if (status_gadget) {
                set_string(status_gadget, window,
                           T("Tondatei konnte nicht geladen/abgespielt werden.",
                             "Sound file could not be loaded/played."));
            }
            if (enabled_gadget)
                SetGadgetAttrs(enabled_gadget, window, NULL,
                               GA_Selected, TRUE, TAG_DONE);
        }
    }
    FreeAslRequest(requester);
}

static int system_folder_mapping_dialog(AmgGui *gui,
                                        struct Window *ref_window,
                                        char sent_mailbox[512],
                                        char drafts_mailbox[512],
                                        char all_mailbox[512],
                                        char spam_mailbox[512],
                                        char trash_mailbox[512],
                                        int *save_sent_copy)
{
    Object *dialog;
    struct Window *window;
    struct Gadget *sent_gadget = NULL, *drafts_gadget = NULL;
    struct Gadget *all_gadget = NULL, *spam_gadget = NULL;
    struct Gadget *trash_gadget = NULL, *save_sent_gadget = NULL;
    ULONG signal_mask = 0;
    ULONG selected = 0;
    int done = 0, accepted = 0;

    if (!gui || !gui->screen || !gui->window || !ref_window ||
        !save_sent_copy) return 0;
    /* Der Systemordner-Requester soll bewusst ueber dem AmiMail-Hauptfenster
     * zentriert erscheinen, nicht relativ zum breiteren Kontodialog. */
    dialog = WindowObject,
        WA_Title, T("AmiMail - Systemordner", "AmiMail - System folders"),
        WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                      WFLG_ACTIVATE,
        WA_PubScreen, gui->screen,
        WA_Width, 470,
        WA_MinWidth, 440,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_RAWKEY,
        WINDOW_RefWindow, gui->window,
        WINDOW_Position, WPOS_CENTERWINDOW,
        WINDOW_ParentGroup, VGroupObject,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_SpaceInner, FALSE,
            LAYOUT_ShrinkWrap, TRUE,

            LAYOUT_AddChild, static_text_label(
                T("Leer = automatisch; sonst exakten IMAP-Namen eintragen.",
                  "Empty = automatic; otherwise enter the exact IMAP name.")),
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T("Gesendet:", "Sent:")),
                CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    sent_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_FOLDER_SENT,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 511,
                        STRINGA_TextVal, sent_mailbox,
                    EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T("Entw\374rfe:", "Drafts:")),
                CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    drafts_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_FOLDER_DRAFTS,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 511,
                        STRINGA_TextVal, drafts_mailbox,
                    EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T("Alle Nachrichten:", "All Mail:")),
                CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    all_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_FOLDER_ALL,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 511,
                        STRINGA_TextVal, all_mailbox,
                    EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T("Spam:", "Spam:")),
                CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    spam_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_FOLDER_SPAM,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 511,
                        STRINGA_TextVal, spam_mailbox,
                    EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T("Papierkorb:", "Trash:")),
                CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    trash_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_FOLDER_TRASH,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 511,
                        STRINGA_TextVal, trash_mailbox,
                    EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(
                    T("Gesendete Mails speichern:", "Save sent mail:")),
                CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    save_sent_gadget = (struct Gadget *)ButtonObject,
                        GA_ID, GID_FOLDER_SAVE_SENT,
                        GA_RelVerify, TRUE,
                        GA_Selected, *save_sent_copy ? TRUE : FALSE,
                        BUTTON_AutoButton, BAG_CHECKBOX,
                        BUTTON_PushButton, TRUE,
                    EndObject,
                CHILD_MinWidth, 24,
                CHILD_MaxWidth, 24,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, static_text_label(
                    T("per IMAP (Gmail speichert automatisch)",
                      "via IMAP (Gmail stores automatically)")),
            EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_FOLDER_OK,
                    GA_RelVerify, TRUE,
                    GA_Text, T("_\334bernehmen", "_Apply"),
                EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_FOLDER_CANCEL,
                    GA_RelVerify, TRUE,
                    GA_Text, T("Ab_brechen", "_Cancel"),
                EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,
        EndObject,
    EndWindow;
    if (!dialog) return 0;
    window = RA_OpenWindow(dialog);
    if (!window) {
        DisposeObject(dialog);
        return 0;
    }
    GetAttr(WINDOW_SigMask, dialog, &signal_mask);
    while (!done) {
        ULONG signals = Wait(signal_mask | SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C) done = 1;
        if (signals & signal_mask) {
            ULONG result;
            UWORD input_code = 0U;
            while ((result = RA_HandleInput(dialog, &input_code)) !=
                   WMHI_LASTMSG) {
                switch (result & WMHI_CLASSMASK) {
                    case WMHI_CLOSEWINDOW:
                        done = 1;
                        break;
                    case WMHI_RAWKEY:
                        if ((result & WMHI_KEYMASK) == GUI_RAWKEY_ESCAPE) {
                            done = 1;
                            break;
                        }
                        if (!requester_rawkey_accept(
                                result & WMHI_KEYMASK))
                            break;
                        result = WMHI_GADGETUP | GID_FOLDER_OK;
                        /* fall through: Return/Enter activates Apply */
                    case WMHI_GADGETUP:
                        if (folder_mapping_string_id(
                                result & WMHI_GADGETMASK) &&
                            requester_string_accept_code(input_code))
                            result = WMHI_GADGETUP | GID_FOLDER_OK;
                        switch (result & WMHI_GADGETMASK) {
                            case GID_FOLDER_OK:
                                snprintf(sent_mailbox, 512U, "%s",
                                         string_text(sent_gadget));
                                snprintf(drafts_mailbox, 512U, "%s",
                                         string_text(drafts_gadget));
                                snprintf(all_mailbox, 512U, "%s",
                                         string_text(all_gadget));
                                snprintf(spam_mailbox, 512U, "%s",
                                         string_text(spam_gadget));
                                selected = 0;
                                snprintf(trash_mailbox, 512U, "%s",
                                         string_text(trash_gadget));
                                GetAttr(GA_Selected,
                                        (Object *)save_sent_gadget,
                                        &selected);
                                *save_sent_copy = selected ? 1 : 0;
                                accepted = 1;
                                done = 1;
                                break;
                            case GID_FOLDER_CANCEL:
                                done = 1;
                                break;
                        }
                        break;
                }
            }
        }
    }
    DisposeObject(dialog);
    return accepted;
}

int unlock_account_dialog(AmgGui *gui, AmgError *error)
{
    Object *dialog;
    struct Window *window;
    struct Gadget *master_gadget;
    struct Gadget *status_gadget;
    ULONG signal_mask = 0;
    ULONG unlock_width = 460UL;
    LONG unlock_left, unlock_top;
    int done = 0;
    int unlocked = 0;

    if (!gui || !gui->screen || !gui->window) return 0;

    /* Keep all geometry changes before RA_OpenWindow().  On classic
     * ReAction the password field reliably retains input focus only when this
     * shrink-wrapped requester is not repositioned after it becomes visible. */
    unlock_left = gui->window->LeftEdge +
        ((LONG)gui->window->Width - (LONG)unlock_width) / 2L;
    unlock_top = gui->window->TopEdge + ((LONG)gui->window->Height / 2L) - 40L;
    if (unlock_left < 0L) unlock_left = 0L;
    if (unlock_top < 0L) unlock_top = 0L;
    if (unlock_left + (LONG)unlock_width > (LONG)gui->screen->Width)
        unlock_left = (LONG)gui->screen->Width - (LONG)unlock_width;
    if (unlock_left < 0L) unlock_left = 0L;

    master_gadget = NULL;
    status_gadget = NULL;
    dialog = WindowObject,
        WA_Title, T("AmiMail - Konto entsperren", "AmiMail - Unlock account"),
        WA_Left, unlock_left,
        WA_Top, unlock_top,
        WA_Width, unlock_width,
        WA_MinWidth, unlock_width,
        WA_MaxWidth, unlock_width,
        WA_PubScreen, gui->screen,
        WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                  WFLG_ACTIVATE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_RAWKEY,
        WINDOW_ParentGroup, VGroupObject,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_SpaceInner, TRUE,
            LAYOUT_ShrinkWrap, TRUE,

            LAYOUT_AddChild, static_text_label(
                T("Master-Passwort:", "Master password:")),
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild,
                master_gadget = (struct Gadget *)StringObject,
                    GA_ID, GID_ACCOUNT_MASTER_PASSWORD,
                    GA_RelVerify, TRUE,
                    GA_TabCycle, TRUE,
                    STRINGA_MaxChars, 127,
                    STRINGA_HookType, SHK_PASSWORD,
                    STRINGA_TextVal, "",
                EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild,
                status_gadget = (struct Gadget *)StringObject,
                    GA_ID, GID_ACCOUNT_STATUS,
                    GA_ReadOnly, TRUE,
                    STRINGA_TextVal,
                        T("Einmal pro Amiga-Sitzung; Schl\374ssel nur in ENV:.",
                          "Once per Amiga session; key only in ENV:."),
                EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_ACCOUNT_UNLOCK,
                    GA_RelVerify, TRUE,
                    GA_Text, T("_Entsperren", "_Unlock"),
                EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_ACCOUNT_CANCEL,
                    GA_RelVerify, TRUE,
                    GA_Text, T("Ab_brechen", "_Cancel"),
                EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,
        EndObject,
    EndWindow;
    if (!dialog) {
        amg_error_set(error, AMG_ERR_MEMORY,
                      T("Entsperrfenster konnte nicht erzeugt werden.",
                        "Unlock window could not be created."));
        return 0;
    }

    window = RA_OpenWindow(dialog);
    if (!window) {
        DisposeObject(dialog);
        amg_error_set(error, AMG_ERR_IO,
                      T("Entsperrfenster konnte nicht geöffnet werden.",
                        "Unlock window could not be opened."));
        return 0;
    }
    /* Restore the exact focus path that was proven to work before the
     * post-open centring regression: open -> activate password gadget.
     * Do not MoveWindow(), delay or re-activate the window afterwards. */
    ActivateGadget(master_gadget, window, NULL);
    GetAttr(WINDOW_SigMask, dialog, &signal_mask);

    while (!done) {
        ULONG signals = Wait(signal_mask | SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C) {
            done = 1;
            continue;
        }
        if (signals & signal_mask) {
            ULONG result;
            UWORD input_code = 0U;
            while ((result = RA_HandleInput(dialog, &input_code)) !=
                   WMHI_LASTMSG) {
                switch (result & WMHI_CLASSMASK) {
                    case WMHI_CLOSEWINDOW:
                        done = 1;
                        break;
                    case WMHI_RAWKEY:
                        if ((result & WMHI_KEYMASK) == GUI_RAWKEY_ESCAPE) {
                            done = 1;
                            break;
                        }
                        if (!requester_rawkey_accept(result & WMHI_KEYMASK))
                            break;
                        result = WMHI_GADGETUP | GID_ACCOUNT_UNLOCK;
                        /* fall through */
                    case WMHI_GADGETUP:
                        if ((result & WMHI_GADGETMASK) ==
                                GID_ACCOUNT_MASTER_PASSWORD &&
                            requester_password_accept_code(input_code))
                            result = WMHI_GADGETUP | GID_ACCOUNT_UNLOCK;
                        if ((result & WMHI_GADGETMASK) == GID_ACCOUNT_CANCEL) {
                            done = 1;
                        } else if ((result & WMHI_GADGETMASK) ==
                                   GID_ACCOUNT_UNLOCK) {
                            const char *master = string_text(master_gadget);
                            AmgAccount loaded;
                            if (!master[0]) {
                                set_string(status_gadget, window,
                                           T("Master-Passwort eingeben.",
                                             "Enter the master password."));
                                break;
                            }
                            amg_account_init(&loaded);
                            if (amg_storage_load_account(
                                    ACCOUNT_PATH, master, &loaded,
                                    error) == AMG_OK) {
                                AmgError session_error;
                                memset(&session_error, 0, sizeof(session_error));
                                replace_account(gui, &loaded);
                                unlocked =
                                    amg_storage_cache_session_key(
                                        ACCOUNT_PATH, SESSION_KEY_PATH,
                                        master, &session_error) == AMG_OK
                                    ? 1 : 2;
                                done = 1;
                            } else {
                                set_utf8_string(status_gadget, window,
                                                error->message);
                                amg_account_clear(&loaded);
                            }
                        }
                        break;
                }
            }
        }
    }

    set_string(master_gadget, window, "");
    RA_CloseWindow(dialog);
    DisposeObject(dialog);
    return unlocked;
}

 int account_dialog(AmgGui *gui, AmgError *error)
{
    Object *dialog;
    struct Window *window;
    struct Gadget *name_gadget, *email_gadget;
    struct Gadget *imap_host_gadget, *imap_port_gadget;
    struct Gadget *imap_starttls_gadget;
    struct Gadget *imap_username_gadget, *imap_password_gadget;
    struct Gadget *smtp_host_gadget, *smtp_port_gadget;
    struct Gadget *smtp_starttls_gadget;
    struct Gadget *smtp_username_gadget, *smtp_password_gadget;
    struct Gadget *master_password_gadget, *fetch_days_gadget;
    struct Gadget *fetch_on_start_gadget, *periodic_fetch_gadget;
    struct Gadget *notification_sound_gadget, *notification_sound_path_gadget;
    struct Gadget *dialog_status;
    ULONG signal_mask;
    ULONG account_width = 470UL;
    ULONG hint_gap = 4UL;
    char fetch_days_text[16];
    char imap_port_text[8];
    char smtp_port_text[8];
    char sent_mailbox[512], drafts_mailbox[512], all_mailbox[512];
    char spam_mailbox[512], trash_mailbox[512];
    int save_sent_copy;
    int done = 0, changed = 0;
    int session_cache_warning = 0;
    int network_was_running = amg_network_is_running(gui->network);
    int network_settings_changed = 0;
    int periodic_fetch_changed = 0;

    name_gadget = NULL;
    email_gadget = NULL;
    imap_host_gadget = NULL;
    imap_port_gadget = NULL;
    imap_starttls_gadget = NULL;
    imap_username_gadget = NULL;
    imap_password_gadget = NULL;
    smtp_host_gadget = NULL;
    smtp_port_gadget = NULL;
    smtp_starttls_gadget = NULL;
    smtp_username_gadget = NULL;
    smtp_password_gadget = NULL;
    master_password_gadget = NULL;
    fetch_days_gadget = NULL;
    fetch_on_start_gadget = NULL;
    periodic_fetch_gadget = NULL;
    notification_sound_gadget = NULL;
    notification_sound_path_gadget = NULL;
    dialog_status = NULL;
    snprintf(fetch_days_text, sizeof(fetch_days_text), "%u",
             gui->account->fetch_days ? gui->account->fetch_days : 180U);
    snprintf(imap_port_text, sizeof(imap_port_text), "%u",
             (unsigned)(gui->account->imap_port ? gui->account->imap_port : 993U));
    snprintf(smtp_port_text, sizeof(smtp_port_text), "%u",
             (unsigned)(gui->account->smtp_port ? gui->account->smtp_port : 465U));
    utf8_to_local_copy(gui->account->sent_mailbox, sent_mailbox,
                       sizeof(sent_mailbox));
    utf8_to_local_copy(gui->account->drafts_mailbox, drafts_mailbox,
                       sizeof(drafts_mailbox));
    utf8_to_local_copy(gui->account->all_mailbox, all_mailbox,
                       sizeof(all_mailbox));
    utf8_to_local_copy(gui->account->spam_mailbox, spam_mailbox,
                       sizeof(spam_mailbox));
    utf8_to_local_copy(gui->account->trash_mailbox, trash_mailbox,
                       sizeof(trash_mailbox));
    save_sent_copy = gui->account->save_sent_copy ? 1 : 0;
    if (gui->screen && (ULONG)gui->screen->Width > 40UL) {
        ULONG available_width = (ULONG)gui->screen->Width - 20UL;
        if (account_width > available_width) account_width = available_width;
    }
    if (account_width < 440UL) account_width = 440UL;

    /* Eine halbe Textzeile Abstand ober- und unterhalb des Hinweises.
     * Bei der klassischen 8-Pixel-Topaz-Schrift sind das 4 Pixel. */
    if (gui->screen && gui->screen->Font && gui->screen->Font->ta_YSize)
        hint_gap = ((ULONG)gui->screen->Font->ta_YSize + 1UL) / 2UL;
    if (hint_gap < 2UL) hint_gap = 2UL;

    dialog = WindowObject,
        WA_Title, T("AmiMail - Konto-Einstellungen", "AmiMail - Account settings"),
        WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                      WFLG_ACTIVATE,
        WA_PubScreen, gui->screen,
        WA_Width, account_width,
        WA_MinWidth, 440,
        WA_MaxWidth, 8192,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_RAWKEY,
        WINDOW_RefWindow, gui->window,
        WINDOW_Position, WPOS_CENTERWINDOW,
        WINDOW_ParentGroup, VGroupObject,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_SpaceInner, FALSE,

            /* Alle Dialoginhalte liegen in EINER ShrinkWrap-Gruppe.
             * So landet ueberschuessige Fensterhoehe am Aussenrand und
             * nicht als zwei Zeilen Leerraum ueber/unter dem Hinweistext. */
            LAYOUT_AddChild, VGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
                LAYOUT_ShrinkWrap, TRUE,

            /* Die Kontofelder bilden eine eigene ShrinkWrap-Gruppe.
             * layout.gadget verteilt freie Fensterhoehe sonst gleichmaessig
             * zwischen festen Kindern. Dadurch entstanden die grossen
             * vertikalen Luecken zwischen den Eingabezeilen. In dieser
             * Gruppe bleiben nur die expliziten 1-Pixel-Abstaende erhalten. */
            LAYOUT_AddChild, VGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
                LAYOUT_ShrinkWrap, TRUE,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T("Name:", "Name:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        name_gadget = (struct Gadget *)StringObject,
                            GA_ID, GID_ACCOUNT_NAME,
                            GA_RelVerify, TRUE,
                            GA_TabCycle, TRUE,
                            STRINGA_MaxChars, 95,
                            STRINGA_TextVal, gui->account->display_name,
                        EndObject,
                    EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_MaxHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T("E-Mail-Adresse:", "Email address:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        email_gadget = (struct Gadget *)StringObject,
                            GA_ID, GID_ACCOUNT_EMAIL,
                            GA_RelVerify, TRUE,
                            GA_TabCycle, TRUE,
                            STRINGA_MaxChars, 255,
                            STRINGA_TextVal, gui->account->email,
                        EndObject,
                    EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_MaxHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T("IMAP-Server / Port:", "IMAP server / port:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, HGroupObject,
                        LAYOUT_SpaceOuter, FALSE,
                        LAYOUT_SpaceInner, TRUE,
                        LAYOUT_AddChild,
                            imap_host_gadget = (struct Gadget *)StringObject,
                                GA_ID, GID_ACCOUNT_IMAP_HOST,
                                GA_RelVerify, TRUE,
                                GA_TabCycle, TRUE,
                                STRINGA_MaxChars, 255,
                                STRINGA_TextVal, gui->account->imap_host,
                            EndObject,
                        LAYOUT_AddChild,
                            imap_port_gadget = (struct Gadget *)StringObject,
                                GA_ID, GID_ACCOUNT_IMAP_PORT,
                                GA_RelVerify, TRUE,
                                GA_TabCycle, TRUE,
                                STRINGA_MaxChars, 5,
                                STRINGA_TextVal, imap_port_text,
                            EndObject,
                        CHILD_MinWidth, 55,
                        CHILD_MaxWidth, 55,
                        CHILD_WeightedWidth, 0,
                    EndObject,
                EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_MaxHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T("IMAP-Sicherheit:", "IMAP security:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        imap_starttls_gadget =
                            (struct Gadget *)ButtonObject,
                            GA_ID, GID_ACCOUNT_IMAP_STARTTLS,
                            GA_RelVerify, TRUE,
                            GA_Selected,
                                gui->account->imap_starttls ? TRUE : FALSE,
                            BUTTON_AutoButton, BAG_CHECKBOX,
                            BUTTON_PushButton, TRUE,
                        EndObject,
                    CHILD_MinWidth, 24,
                    CHILD_MaxWidth, 24,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, static_text_label(
                        T("STARTTLS (typisch Port 143)",
                          "STARTTLS (typically port 143)")),
                EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_MaxHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T("IMAP-Benutzer:", "IMAP user:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        imap_username_gadget = (struct Gadget *)StringObject,
                            GA_ID, GID_ACCOUNT_IMAP_USERNAME,
                            GA_RelVerify, TRUE,
                            GA_TabCycle, TRUE,
                            STRINGA_MaxChars, 255,
                            STRINGA_TextVal, gui->account->imap_username,
                        EndObject,
                    EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_MaxHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T("IMAP-Passwort:", "IMAP password:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        imap_password_gadget = (struct Gadget *)StringObject,
                            GA_ID, GID_ACCOUNT_IMAP_PASSWORD,
                            GA_RelVerify, TRUE,
                            GA_TabCycle, TRUE,
                            STRINGA_MaxChars, 255,
                            STRINGA_HookType, SHK_PASSWORD,
                            STRINGA_TextVal,
                                gui->account->imap_password ?
                                    gui->account->imap_password : "",
                        EndObject,
                    EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_MaxHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T("SMTP-Server / Port:", "SMTP server / port:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, HGroupObject,
                        LAYOUT_SpaceOuter, FALSE,
                        LAYOUT_SpaceInner, TRUE,
                        LAYOUT_AddChild,
                            smtp_host_gadget = (struct Gadget *)StringObject,
                                GA_ID, GID_ACCOUNT_SMTP_HOST,
                                GA_RelVerify, TRUE,
                                GA_TabCycle, TRUE,
                                STRINGA_MaxChars, 255,
                                STRINGA_TextVal, gui->account->smtp_host,
                            EndObject,
                        LAYOUT_AddChild,
                            smtp_port_gadget = (struct Gadget *)StringObject,
                                GA_ID, GID_ACCOUNT_SMTP_PORT,
                                GA_RelVerify, TRUE,
                                GA_TabCycle, TRUE,
                                STRINGA_MaxChars, 5,
                                STRINGA_TextVal, smtp_port_text,
                            EndObject,
                        CHILD_MinWidth, 55,
                        CHILD_MaxWidth, 55,
                        CHILD_WeightedWidth, 0,
                    EndObject,
                EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_MaxHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T("SMTP-Sicherheit:", "SMTP security:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        smtp_starttls_gadget =
                            (struct Gadget *)ButtonObject,
                            GA_ID, GID_ACCOUNT_SMTP_STARTTLS,
                            GA_RelVerify, TRUE,
                            GA_Selected,
                                gui->account->smtp_starttls ? TRUE : FALSE,
                            BUTTON_AutoButton, BAG_CHECKBOX,
                            BUTTON_PushButton, TRUE,
                        EndObject,
                    CHILD_MinWidth, 24,
                    CHILD_MaxWidth, 24,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, static_text_label(
                        T("STARTTLS (typisch Port 587)",
                          "STARTTLS (typically port 587)")),
                EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_MaxHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T("SMTP-Benutzer:", "SMTP user:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        smtp_username_gadget = (struct Gadget *)StringObject,
                            GA_ID, GID_ACCOUNT_SMTP_USERNAME,
                            GA_RelVerify, TRUE,
                            GA_TabCycle, TRUE,
                            STRINGA_MaxChars, 255,
                            STRINGA_TextVal, gui->account->smtp_username,
                        EndObject,
                    EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_MaxHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T("SMTP-Passwort:", "SMTP password:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        smtp_password_gadget = (struct Gadget *)StringObject,
                            GA_ID, GID_ACCOUNT_SMTP_PASSWORD,
                            GA_RelVerify, TRUE,
                            GA_TabCycle, TRUE,
                            STRINGA_MaxChars, 255,
                            STRINGA_HookType, SHK_PASSWORD,
                            STRINGA_TextVal,
                                gui->account->smtp_password ?
                                    gui->account->smtp_password : "",
                        EndObject,
                    EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_MaxHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T("Systemordner:", "System folders:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, ButtonObject,
                        GA_ID, GID_ACCOUNT_FOLDER_MAPPING,
                        GA_RelVerify, TRUE,
                        GA_Text, T("_Zuordnen...", "_Map..."),
                    EndObject,
                EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_MaxHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T("Master-Passwort:", "Master password:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        master_password_gadget = (struct Gadget *)StringObject,
                            GA_ID, GID_ACCOUNT_MASTER_PASSWORD,
                            GA_RelVerify, TRUE,
                            GA_TabCycle, TRUE,
                            STRINGA_MaxChars, 127,
                            STRINGA_HookType, SHK_PASSWORD,
                            STRINGA_TextVal, "",
                        EndObject,
                    EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_MaxHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T("Abruf-Zeitraum (Tage):", "Fetch period (days):")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        fetch_days_gadget = (struct Gadget *)StringObject,
                            GA_ID, GID_ACCOUNT_FETCH_DAYS,
                            GA_RelVerify, TRUE,
                            GA_TabCycle, TRUE,
                            STRINGA_MaxChars, 5,
                            STRINGA_TextVal, fetch_days_text,
                        EndObject,
                    EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_MaxHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, HGroupObject,
                        LAYOUT_SpaceOuter, FALSE,
                        LAYOUT_SpaceInner, FALSE,
                    EndObject,
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        fetch_on_start_gadget =
                            (struct Gadget *)ButtonObject,
                            GA_ID, GID_ACCOUNT_FETCH_ON_START,
                            GA_RelVerify, TRUE,
                            GA_Selected,
                                gui->account->fetch_on_start ? TRUE : FALSE,
                            BUTTON_AutoButton, BAG_CHECKBOX,
                            BUTTON_PushButton, TRUE,
                        EndObject,
                    CHILD_MinWidth, 24,
                    CHILD_MaxWidth, 24,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, static_text_label(
                        T("Mail-Abruf beim Start", "Fetch mail at startup")),
                EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_MaxHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, HGroupObject,
                        LAYOUT_SpaceOuter, FALSE,
                        LAYOUT_SpaceInner, FALSE,
                    EndObject,
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        periodic_fetch_gadget =
                            (struct Gadget *)ButtonObject,
                            GA_ID, GID_ACCOUNT_PERIODIC_FETCH,
                            GA_RelVerify, TRUE,
                            GA_Selected,
                                gui->account->periodic_fetch ? TRUE : FALSE,
                            BUTTON_AutoButton, BAG_CHECKBOX,
                            BUTTON_PushButton, TRUE,
                        EndObject,
                    CHILD_MinWidth, 24,
                    CHILD_MaxWidth, 24,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, static_text_label(
                        T("Periodischer Abruf (5 Min.)",
                          "Periodic fetch (5 min)")),
                EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_MaxHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, HGroupObject,
                        LAYOUT_SpaceOuter, FALSE,
                        LAYOUT_SpaceInner, FALSE,
                    EndObject,
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        notification_sound_gadget =
                            (struct Gadget *)ButtonObject,
                            GA_ID, GID_ACCOUNT_NOTIFICATION_SOUND,
                            GA_RelVerify, TRUE,
                            GA_Selected,
                                gui->account->notification_sound ? TRUE : FALSE,
                            BUTTON_AutoButton, BAG_CHECKBOX,
                            BUTTON_PushButton, TRUE,
                        EndObject,
                    CHILD_MinWidth, 24,
                    CHILD_MaxWidth, 24,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, static_text_label(
                        T("Benachrichtigungston", "Notification Sound")),
                EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_MaxHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(
                        T("Tondatei:", "Sound file:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        notification_sound_path_gadget =
                            (struct Gadget *)StringObject,
                            GA_ID, GID_ACCOUNT_NOTIFICATION_PATH,
                            GA_ReadOnly, TRUE,
                            STRINGA_MaxChars, 511,
                            STRINGA_TextVal,
                                gui->account->notification_sound_path,
                        EndObject,
                    LAYOUT_AddChild, ButtonObject,
                        GA_ID, GID_ACCOUNT_NOTIFICATION_CHOOSE,
                        GA_RelVerify, TRUE,
                        GA_Text, "...",
                    EndObject,
                    CHILD_MinWidth, 32,
                    CHILD_MaxWidth, 32,
                    CHILD_WeightedWidth, 0,
                EndObject,
                CHILD_WeightedHeight, 0,

            EndObject,
            CHILD_WeightedHeight, 0,

            /* Der Hinweis bekommt bewusst genau eine halbe Textzeile
             * Luft nach oben und unten. */
            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
            EndObject,
            CHILD_MinHeight, hint_gap,
            CHILD_MaxHeight, hint_gap,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild,
                dialog_status = (struct Gadget *)StringObject,
                    GA_ID, GID_ACCOUNT_STATUS,
                    GA_ReadOnly, TRUE,
                    STRINGA_TextVal,
                        T("STARTTLS nur wenn unterst\374tzt. Gmail: IMAP 993 aus; SMTP 587 an.",
                          "STARTTLS only if supported. Gmail: IMAP 993 off; SMTP 587 on."),
                EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
            EndObject,
            CHILD_MinHeight, hint_gap,
            CHILD_MaxHeight, hint_gap,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_ACCOUNT_UNLOCK,
                    GA_RelVerify, TRUE,
                    GA_Text, T("_Entsperren", "_Unlock"),
                EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_ACCOUNT_SAVE,
                    GA_RelVerify, TRUE,
                    GA_Text, T("_Speichern", "_Save"),
                EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_ACCOUNT_CANCEL,
                    GA_RelVerify, TRUE,
                    GA_Text, T("Ab_brechen", "_Cancel"),
                EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,

            EndObject,
            CHILD_WeightedHeight, 0,
        EndObject,
    EndWindow;

    if (!dialog) {
        amg_error_set(error, AMG_ERR_MEMORY,
                      T("Kontodialog konnte nicht erzeugt werden.", "Account dialog could not be created."));
        return 0;
    }
    window = RA_OpenWindow(dialog);
    if (!window) {
        DisposeObject(dialog);
        amg_error_set(error, AMG_ERR_IO,
                      T("Kontodialog konnte nicht ge\303\266ffnet werden.", "Account dialog could not be opened."));
        return 0;
    }
    /* WPOS_CENTERWINDOW is only the initial placement.  The account dialog
     * has a large dynamic layout whose final size is known only now.  Use the
     * real Intuition geometry for the definitive centre over AmiMail. */
    center_window_over_window(window, gui->window);
    GetAttr(WINDOW_SigMask, dialog, &signal_mask);

    while (!done) {
        ULONG signals = Wait(signal_mask | SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C) done = 1;
        if (signals & signal_mask) {
            ULONG result;
            UWORD input_code = 0U;
            while ((result = RA_HandleInput(dialog, &input_code)) !=
                   WMHI_LASTMSG) {
                switch (result & WMHI_CLASSMASK) {
                    case WMHI_CLOSEWINDOW:
                        done = 1;
                        break;

                    case WMHI_RAWKEY:
                        if ((result & WMHI_KEYMASK) == GUI_RAWKEY_ESCAPE) {
                            done = 1;
                            break;
                        }
                        if (!requester_rawkey_accept(
                                result & WMHI_KEYMASK))
                            break;
                        result = WMHI_GADGETUP | GID_ACCOUNT_SAVE;
                        /* fall through: Return/Enter activates Save */

                    case WMHI_GADGETUP:
                        if (account_string_id(result & WMHI_GADGETMASK) &&
                            requester_string_accept_code(input_code))
                            result = WMHI_GADGETUP | GID_ACCOUNT_SAVE;
                        switch (result & WMHI_GADGETMASK) {
                            case GID_ACCOUNT_CANCEL:
                                done = 1;
                                break;

                            case GID_ACCOUNT_UNLOCK:
                            {
                                AmgAccount loaded;
                                const char *master =
                                    string_text(master_password_gadget);
                                if (!*master) {
                                    set_string(dialog_status, window,
                                               T("Master-Passwort eingeben.", "Enter the master password."));
                                    break;
                                }
                                amg_account_init(&loaded);
                                if (amg_storage_load_account(
                                        ACCOUNT_PATH, master, &loaded,
                                        error) == AMG_OK) {
                                    if (amg_storage_save_account(
                                            ACCOUNT_PATH, &loaded, master,
                                            error) == AMG_OK) {
                                        AmgError session_error;
                                        memset(&session_error, 0,
                                               sizeof(session_error));
                                        if (amg_storage_cache_session_key(
                                                ACCOUNT_PATH,
                                                SESSION_KEY_PATH, master,
                                                &session_error) != AMG_OK)
                                            session_cache_warning = 1;
                                        replace_account(gui, &loaded);
                                        changed = 1;
                                        done = 1;
                                    } else {
                                        set_utf8_string(
                                            dialog_status, window,
                                            error->message);
                                        amg_account_clear(&loaded);
                                    }
                                } else {
                                    set_utf8_string(dialog_status, window,
                                                    error->message);
                                    amg_account_clear(&loaded);
                                }
                                break;
                            }

                            case GID_ACCOUNT_FOLDER_MAPPING:
                                system_folder_mapping_dialog(
                                    gui, window, sent_mailbox, drafts_mailbox,
                                    all_mailbox, spam_mailbox, trash_mailbox,
                                    &save_sent_copy);
                                break;

                            case GID_ACCOUNT_NOTIFICATION_CHOOSE:
                                choose_notification_sound(
                                    gui, window, notification_sound_path_gadget,
                                    notification_sound_gadget, dialog_status);
                                break;

                            case GID_ACCOUNT_SAVE:
                            {
                                AmgAccount candidate;
                                char *days_end = NULL;
                                char *imap_port_end = NULL;
                                char *smtp_port_end = NULL;
                                unsigned long fetch_days;
                                unsigned long imap_port;
                                unsigned long smtp_port;
                                ULONG fetch_on_start = 0;
                                ULONG periodic_fetch = 0;
                                ULONG notification_sound = 0;
                                ULONG imap_starttls = 0;
                                ULONG smtp_starttls = 0;
                                const char *master =
                                    string_text(master_password_gadget);
                                if (!*master) {
                                    set_string(
                                        dialog_status, window,
                                        T("Master-Passwort zum Verschl\374sseln eingeben.",
                                          "Enter a master password for encryption."));
                                    break;
                                }

                                fetch_days = strtoul(
                                    string_text(fetch_days_gadget),
                                    &days_end, 10);
                                imap_port = strtoul(
                                    string_text(imap_port_gadget),
                                    &imap_port_end, 10);
                                smtp_port = strtoul(
                                    string_text(smtp_port_gadget),
                                    &smtp_port_end, 10);
                                while (days_end && *days_end == ' ') ++days_end;
                                while (imap_port_end && *imap_port_end == ' ')
                                    ++imap_port_end;
                                while (smtp_port_end && *smtp_port_end == ' ')
                                    ++smtp_port_end;
                                if (!string_text(fetch_days_gadget)[0] ||
                                    (days_end && *days_end) ||
                                    fetch_days < 1UL || fetch_days > 3650UL) {
                                    set_string(
                                        dialog_status, window,
                                        T("Abruf-Zeitraum: 1 bis 3650 Tage eingeben.",
                                          "Fetch period: enter 1 to 3650 days."));
                                    break;
                                }
                                if (!string_text(imap_port_gadget)[0] ||
                                    (imap_port_end && *imap_port_end) ||
                                    imap_port < 1UL || imap_port > 65535UL ||
                                    !string_text(smtp_port_gadget)[0] ||
                                    (smtp_port_end && *smtp_port_end) ||
                                    smtp_port < 1UL || smtp_port > 65535UL) {
                                    set_string(
                                        dialog_status, window,
                                        T("IMAP-/SMTP-Port: 1 bis 65535 eingeben.",
                                          "IMAP/SMTP port: enter 1 to 65535."));
                                    break;
                                }

                                amg_account_init(&candidate);
                                strncpy(candidate.display_name,
                                        string_text(name_gadget),
                                        sizeof(candidate.display_name) - 1U);
                                strncpy(candidate.email,
                                        string_text(email_gadget),
                                        sizeof(candidate.email) - 1U);
                                strncpy(candidate.imap_host,
                                        string_text(imap_host_gadget),
                                        sizeof(candidate.imap_host) - 1U);
                                strncpy(candidate.imap_username,
                                        string_text(imap_username_gadget),
                                        sizeof(candidate.imap_username) - 1U);
                                strncpy(candidate.smtp_host,
                                        string_text(smtp_host_gadget),
                                        sizeof(candidate.smtp_host) - 1U);
                                strncpy(candidate.smtp_username,
                                        string_text(smtp_username_gadget),
                                        sizeof(candidate.smtp_username) - 1U);
                                candidate.display_name[
                                    sizeof(candidate.display_name) - 1U] = 0;
                                candidate.email[
                                    sizeof(candidate.email) - 1U] = 0;
                                candidate.imap_host[
                                    sizeof(candidate.imap_host) - 1U] = 0;
                                candidate.imap_username[
                                    sizeof(candidate.imap_username) - 1U] = 0;
                                candidate.smtp_host[
                                    sizeof(candidate.smtp_host) - 1U] = 0;
                                candidate.smtp_username[
                                    sizeof(candidate.smtp_username) - 1U] = 0;
                                if (local_to_utf8(
                                        sent_mailbox,
                                        candidate.sent_mailbox,
                                        sizeof(candidate.sent_mailbox)) != AMG_OK ||
                                    local_to_utf8(
                                        drafts_mailbox,
                                        candidate.drafts_mailbox,
                                        sizeof(candidate.drafts_mailbox)) != AMG_OK ||
                                    local_to_utf8(
                                        all_mailbox,
                                        candidate.all_mailbox,
                                        sizeof(candidate.all_mailbox)) != AMG_OK ||
                                    local_to_utf8(
                                        spam_mailbox,
                                        candidate.spam_mailbox,
                                        sizeof(candidate.spam_mailbox)) != AMG_OK ||
                                    local_to_utf8(
                                        trash_mailbox,
                                        candidate.trash_mailbox,
                                        sizeof(candidate.trash_mailbox)) != AMG_OK) {
                                    set_string(
                                        dialog_status, window,
                                        T("Ein Systemordnername ist zu lang.",
                                          "A system folder name is too long."));
                                    amg_account_clear(&candidate);
                                    break;
                                }
                                candidate.save_sent_copy = save_sent_copy ? 1 : 0;
                                candidate.imap_port =
                                    (unsigned short)imap_port;
                                candidate.smtp_port =
                                    (unsigned short)smtp_port;
                                GetAttr(GA_Selected,
                                        (Object *)imap_starttls_gadget,
                                        &imap_starttls);
                                GetAttr(GA_Selected,
                                        (Object *)smtp_starttls_gadget,
                                        &smtp_starttls);
                                candidate.imap_starttls =
                                    imap_starttls ? 1 : 0;
                                candidate.smtp_starttls =
                                    smtp_starttls ? 1 : 0;
                                candidate.auth_mode = AMG_AUTH_PASSWORD;
                                candidate.fetch_days =
                                    (unsigned int)fetch_days;

                                GetAttr(GA_Selected,
                                        (Object *)fetch_on_start_gadget,
                                        &fetch_on_start);
                                candidate.fetch_on_start =
                                    fetch_on_start ? 1 : 0;
                                GetAttr(GA_Selected,
                                        (Object *)periodic_fetch_gadget,
                                        &periodic_fetch);
                                candidate.periodic_fetch =
                                    periodic_fetch ? 1 : 0;

                                GetAttr(GA_Selected,
                                        (Object *)notification_sound_gadget,
                                        &notification_sound);
                                candidate.notification_sound =
                                    notification_sound ? 1 : 0;
                                strncpy(candidate.notification_sound_path,
                                        string_text(notification_sound_path_gadget),
                                        sizeof(candidate.notification_sound_path) - 1U);
                                candidate.notification_sound_path[
                                    sizeof(candidate.notification_sound_path) - 1U] = 0;
                                if (candidate.notification_sound) {
                                    BPTR sound_lock;
                                    if (!candidate.notification_sound_path[0]) {
                                        amg_account_clear(&candidate);
                                        set_string(
                                            dialog_status, window,
                                            T("Bitte eine IFF/8SVX/WAV-Tondatei ausw\344hlen.",
                                              "Please select an IFF/8SVX/WAV sound file."));
                                        break;
                                    }
                                    sound_lock = Lock(
                                        (CONST_STRPTR)candidate.notification_sound_path,
                                        ACCESS_READ);
                                    if (!sound_lock) {
                                        amg_account_clear(&candidate);
                                        set_string(
                                            dialog_status, window,
                                            T("Die gew\344hlte Tondatei wurde nicht gefunden.",
                                              "The selected sound file was not found."));
                                        break;
                                    }
                                    UnLock(sound_lock);
                                }

                                if (amg_account_set_secret(
                                        &candidate.imap_password,
                                        string_text(imap_password_gadget)) !=
                                        AMG_OK ||
                                    amg_account_set_secret(
                                        &candidate.smtp_password,
                                        string_text(smtp_password_gadget)) !=
                                        AMG_OK) {
                                    amg_account_clear(&candidate);
                                    set_string(dialog_status, window,
                                               T("Nicht genug Speicher.",
                                                 "Not enough memory."));
                                    break;
                                }
                                amg_account_normalize(&candidate);
                                if (amg_account_validate(&candidate, error) !=
                                    AMG_OK) {
                                    set_utf8_string(dialog_status, window,
                                                    error->message);
                                    amg_account_clear(&candidate);
                                    break;
                                }
                                if (ensure_config_drawer(error) != AMG_OK ||
                                    amg_storage_save_account(
                                        ACCOUNT_PATH, &candidate, master,
                                        error) != AMG_OK) {
                                    set_utf8_string(dialog_status, window,
                                                    error->message);
                                    amg_account_clear(&candidate);
                                    break;
                                }
                                {
                                    AmgError session_error;
                                    memset(&session_error, 0,
                                           sizeof(session_error));
                                    if (amg_storage_cache_session_key(
                                            ACCOUNT_PATH, SESSION_KEY_PATH,
                                            master, &session_error) != AMG_OK)
                                        session_cache_warning = 1;
                                }
                                network_settings_changed =
                                    !account_network_settings_equal(
                                        gui->account, &candidate);
                                periodic_fetch_changed =
                                    gui->account->periodic_fetch !=
                                    candidate.periodic_fetch;
                                replace_account(gui, &candidate);
                                changed = 1;
                                done = 1;
                                break;
                            }
                        }
                        break;
                }
            }
        }
    }
    set_string(master_password_gadget, window, "");
    DisposeObject(dialog);
    if (changed) {
        if (periodic_fetch_changed)
            periodic_timer_restart(gui);
        if (session_cache_warning)
            status_local(gui,
                T("Konto ist entsperrt; Sitzungsschl\374ssel konnte nicht in ENV: gespeichert werden.",
                  "Account is unlocked; session key could not be stored in ENV:."));
        else
            status_local(gui,
                T("Konto ist f\374r diese Amiga-Sitzung entsperrt.",
                  "Account is unlocked for this Amiga session."));
    }
    if (changed && network_was_running && network_settings_changed &&
        !account_is_locked(gui->account)) {
        int result = amg_network_request_reconfigure(
            gui->network, gui->account, error);
        if (result == AMG_OK) {
            gui->network_reconfigure_pending = 1;
            status_local(gui,
                T("Verbinde erneut mit dem Mailserver...",
                  "Reconnecting to the mail server..."));
        } else {
            status_utf8(gui, error->message);
        }
    }
    return changed;
}

static void draw_about_banner(AmgGui *gui, struct Window *window,
                              Object *banner_slot)
{
    struct Gadget *gadget;
    if (!gui || !window || !banner_slot) return;
    gadget = (struct Gadget *)banner_slot;
    draw_embedded_banner_at(gui, window,
                            (LONG)gadget->LeftEdge,
                            (LONG)gadget->TopEdge,
                            (LONG)gadget->Width,
                            (LONG)gadget->Height);
}

 void about_dialog(AmgGui *gui)
{
    Object *dialog;
    Object *banner_slot;
    struct Window *window;
    ULONG signal_mask = 0;
    LONG font_height = 8L;
    LONG line_height;
    int done = 0;

    if (!gui || !gui->window || !gui->screen) return;
    if (gui->screen->Font && gui->screen->Font->ta_YSize)
        font_height = (LONG)gui->screen->Font->ta_YSize;
    line_height = font_height + 2L;

    banner_slot = HGroupObject,
        LAYOUT_SpaceOuter, FALSE,
        LAYOUT_SpaceInner, FALSE,
    EndObject;
    if (!banner_slot) return;

    /* Eigenes ReAction-Fenster statt EasyRequestArgs(): WINDOW_RefWindow in
     * Kombination mit WPOS_CENTERWINDOW bestimmt die Position bereits vor
     * RA_OpenWindow(). Dadurch erscheint der About-Requester sofort sauber
     * zentriert ueber dem AmiMail-Hauptfenster und springt nicht nachtraeglich. */
    dialog = WindowObject,
        WA_Title, T("\334ber AmiMail", "About AmiMail"),
        WA_PubScreen, gui->screen,
        WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                  WFLG_ACTIVATE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_RAWKEY |
                  IDCMP_REFRESHWINDOW,
        WINDOW_RefWindow, gui->window,
        WINDOW_Position, WPOS_CENTERWINDOW,
        WINDOW_ParentGroup, VGroupObject,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_SpaceInner, FALSE,
            LAYOUT_ShrinkWrap, TRUE,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, banner_slot,
                CHILD_MinWidth, 170,
                CHILD_MaxWidth, 170,
                CHILD_WeightedWidth, 0,
                CHILD_MinHeight, 28,
                CHILD_MaxHeight, 28,
                CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, VGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                    LAYOUT_AddChild, static_text_label("AmiMail " AMIGMAIL_VERSION),
                    CHILD_MinHeight, line_height,
                    CHILD_MaxHeight, line_height,
                    CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild,
                        static_text_label(T("IMAP/SMTP-Mailclient f\374r AmigaOS 3.2",
                                            "IMAP/SMTP mail client for AmigaOS 3.2")),
                    CHILD_MinHeight, line_height,
                    CHILD_MaxHeight, line_height,
                    CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild,
                        static_text_label(T("ReAction, IMAP, SMTP und AmiSSL",
                                            "ReAction, IMAP, SMTP and AmiSSL")),
                    CHILD_MinHeight, line_height,
                    CHILD_MaxHeight, line_height,
                    CHILD_WeightedHeight, 0,
                EndObject,
            EndObject,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
            EndObject,
            CHILD_MinHeight, font_height / 2L,
            CHILD_MaxHeight, font_height / 2L,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild,
                static_text_label("\251 Andreas 'Andiweli' St\374rmer"),
            CHILD_MinHeight, line_height,
            CHILD_MaxHeight, line_height,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
            EndObject,
            CHILD_MinHeight, font_height / 2L,
            CHILD_MaxHeight, font_height / 2L,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild,
                static_text_label(T("Rechtlicher Hinweis:", "Legal notice:")),
            CHILD_MinHeight, line_height,
            CHILD_MaxHeight, line_height,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,
                static_text_label(T(
                    "AmiMail ist ein unabh\344ngiges, nichtkommerzielles Freizeitprojekt.",
                    "AmiMail is an independent, non-commercial hobby project.")),
            CHILD_MinHeight, line_height,
            CHILD_MaxHeight, line_height,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,
                static_text_label(T(
                    "Es steht in keiner Verbindung zu einem E-Mail-Anbieter und wird von",
                    "It is not affiliated with any email provider and is not developed,")),
            CHILD_MinHeight, line_height,
            CHILD_MaxHeight, line_height,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,
                static_text_label(T(
                    "keinem Anbieter entwickelt, unterst\374tzt oder gesponsert.",
                    "supported or sponsored by any provider.")),
            CHILD_MinHeight, line_height,
            CHILD_MaxHeight, line_height,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,
                static_text_label(T("Produkt- und Dienstnamen sind Marken ihrer jeweiligen Inhaber.",
                                    "Product and service names are trademarks of their respective owners.")),
            CHILD_MinHeight, line_height,
            CHILD_MaxHeight, line_height,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
            EndObject,
            CHILD_MinHeight, font_height / 2L,
            CHILD_MaxHeight, font_height / 2L,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
                LAYOUT_AddChild, static_text_label(""),
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_ABOUT_OK,
                    GA_RelVerify, TRUE,
                    GA_Text, "OK",
                EndObject,
                CHILD_MinWidth, 80,
                CHILD_MaxWidth, 100,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, static_text_label(""),
            EndObject,
            CHILD_MinHeight, font_height + 8L,
            CHILD_MaxHeight, font_height + 8L,
            CHILD_WeightedHeight, 0,
        EndObject,
    EndWindow;
    if (!dialog) {
        DisposeObject(banner_slot);
        return;
    }

    window = RA_OpenWindow(dialog);
    if (!window) {
        DisposeObject(dialog);
        return;
    }

    draw_about_banner(gui, window, banner_slot);
    GetAttr(WINDOW_SigMask, dialog, &signal_mask);
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
                        if ((result & WMHI_KEYMASK) == GUI_RAWKEY_ESCAPE ||
                            requester_rawkey_accept(result & WMHI_KEYMASK))
                            done = 1;
                        break;
                    case WMHI_GADGETUP:
                        if ((result & WMHI_GADGETMASK) == GID_ABOUT_OK)
                            done = 1;
                        break;
                }
            }
            if (!done)
                draw_about_banner(gui, window, banner_slot);
        }
    }
    DisposeObject(dialog);
}

 int confirm_question_dialog_for_window(AmgGui *gui,
                                              struct Window *ref_window,
                                              const char *question,
                                              const char *note, LONG width)
{
    LONG font_height = 8L;
    LONG question_height, note_height, text_height, button_height;
    Object *dialog;
    struct Window *window;
    ULONG signal_mask = 0;
    int done = 0;
    int confirmed = 0;

    if (!gui || !ref_window || !gui->screen) return 0;
    if (gui->screen->Font && gui->screen->Font->ta_YSize)
        font_height = (LONG)gui->screen->Font->ta_YSize;
    question_height = font_height + 4L;
    note_height = font_height + 2L;
    text_height = question_height + (note && *note ? note_height : 0L);
    button_height = font_height + 8L;

    /* Positionierung relativ zum Hauptfenster wird bereits vor RA_OpenWindow()
     * festgelegt. Dadurch gibt es keinen sichtbaren Sprung von einer
     * Standardposition in die Fenstermitte. */
    dialog = WindowObject,
        WA_Title, "AmiMail",
        WA_Width, width,
        WA_MinWidth, width,
        WA_MaxWidth, width,
        WA_PubScreen, gui->screen,
        WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                  WFLG_ACTIVATE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_RAWKEY,
        WINDOW_RefWindow, ref_window,
        WINDOW_Position, WPOS_CENTERWINDOW,
        WINDOW_ParentGroup, VGroupObject,
            LAYOUT_SpaceOuter, TRUE,
            /* Den Abstand zwischen Fragetext und Buttons nicht vom Layout
             * verteilen lassen. Die Warnzeile bekommt nur noch minimale
             * Innenhoehe und schliesst direkt an die Buttonzeile an. */
            LAYOUT_SpaceInner, FALSE,
            /* Frage + optionale Warnzeile bilden EIN Layout-Kind. */
            LAYOUT_AddChild, VGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
                LAYOUT_AddChild, ButtonObject,
                    GA_ReadOnly, TRUE,
                    GA_Text, question ? question : "",
                    BUTTON_BevelStyle, BVS_NONE,
                    BUTTON_Transparent, TRUE,
                EndObject,
                CHILD_MinHeight, question_height,
                CHILD_MaxHeight, question_height,
                CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, ButtonObject,
                    GA_ReadOnly, TRUE,
                    GA_Text, note && *note ? note : "",
                    BUTTON_BevelStyle, BVS_NONE,
                    BUTTON_Transparent, TRUE,
                EndObject,
                CHILD_MinHeight, note && *note ? note_height : 0,
                CHILD_MaxHeight, note && *note ? note_height : 0,
                CHILD_WeightedHeight, 0,
            EndObject,
            CHILD_MinHeight, text_height,
            CHILD_MaxHeight, text_height,
            CHILD_WeightedHeight, 0,

            /* Genau eine Leerzeile zwischen Textblock und Ja/Nein.
             * Die Fensterhoehe wird vom Layout selbst bestimmt, damit hier
             * keine zusaetzlichen Leerzeilen durch eine feste WA_Height
             * entstehen. Das gilt fuer Loeschen und Papierkorb leeren. */
            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
            EndObject,
            CHILD_MinHeight, font_height,
            CHILD_MaxHeight, font_height,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_CONFIRM_YES,
                    GA_RelVerify, TRUE,
                    GA_Text, T("_Ja", "_Yes"),
                EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_CONFIRM_NO,
                    GA_RelVerify, TRUE,
                    GA_Text, T("_Nein", "_No"),
                EndObject,
            EndObject,
            CHILD_MinHeight, button_height,
            CHILD_MaxHeight, button_height,
            CHILD_WeightedHeight, 0,
        EndObject,
    EndWindow;
    if (!dialog) return 0;

    window = RA_OpenWindow(dialog);
    if (!window) {
        DisposeObject(dialog);
        return 0;
    }
    GetAttr(WINDOW_SigMask, dialog, &signal_mask);
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
                        if ((result & WMHI_KEYMASK) == GUI_RAWKEY_ESCAPE) {
                            done = 1;
                        } else if (requester_rawkey_accept(
                                       result & WMHI_KEYMASK)) {
                            confirmed = 1;
                            done = 1;
                        }
                        break;
                    case WMHI_GADGETUP:
                        if ((result & WMHI_GADGETMASK) == GID_CONFIRM_YES) {
                            confirmed = 1;
                            done = 1;
                        } else if ((result & WMHI_GADGETMASK) ==
                                   GID_CONFIRM_NO) {
                            done = 1;
                        }
                        break;
                }
            }
        }
    }
    DisposeObject(dialog);
    return confirmed;
}

static int confirm_question_dialog(AmgGui *gui, const char *question,
                                   const char *note, LONG width)
{
    return confirm_question_dialog_for_window(
        gui, gui ? gui->window : NULL, question, note, width);
}

 int confirm_delete_dialog(AmgGui *gui)
{
    return confirm_question_dialog(
        gui, T("Mail wirklich l\366schen?", "Really delete mail?"),
        T("Dieser Vorgang kann nicht widerrufen werden.",
          "This action cannot be undone."), 310L);
}

 int confirm_empty_trash_dialog(AmgGui *gui)
{
    return confirm_question_dialog(
        gui, T("Papierkorb wirklich leeren?", "Really empty Trash?"),
        NULL, 280L);
}

 int confirm_empty_spam_dialog(AmgGui *gui)
{
    return confirm_question_dialog(
        gui, T("Spam wirklich leeren?", "Really empty Spam?"),
        NULL, 280L);
}

#endif /* AMIGMAIL_AMIGA */
