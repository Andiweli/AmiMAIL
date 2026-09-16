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
#include <gadgets/clicktab.h>
#include <gadgets/layout.h>
#include <gadgets/string.h>
#include <intuition/classes.h>
#include <intuition/intuition.h>
#include <libraries/asl.h>
#include <proto/asl.h>
#include <proto/button.h>
#include <proto/clicktab.h>
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
#define GUI_ACCOUNT_LABEL_WIDTH 150
#define GUI_ABOUT_BANNER_WIDTH 170L
#define GUI_ABOUT_BANNER_HEIGHT 28L
#define GUI_RAWKEY_NP_ENTER 0x43UL
#define GUI_RAWKEY_RETURN 0x44UL
#define GUI_RAWKEY_ESCAPE 0x45UL
#define T(id, en) amg_tr((id), (en))
enum AccountGadgetId { GID_ACCOUNT_CONFIG_TABS=100,GID_ACCOUNT_ADD,GID_ACCOUNT_DELETE,GID_ACCOUNT_MOVE_LEFT,GID_ACCOUNT_MOVE_RIGHT,GID_ACCOUNT_ENABLED,GID_ACCOUNT_ACCOUNT_NAME,GID_ACCOUNT_NAME,GID_ACCOUNT_EMAIL,GID_ACCOUNT_IMAP_HOST,GID_ACCOUNT_IMAP_PORT,GID_ACCOUNT_IMAP_STARTTLS,GID_ACCOUNT_IMAP_USERNAME,GID_ACCOUNT_IMAP_PASSWORD,GID_ACCOUNT_SMTP_HOST,GID_ACCOUNT_SMTP_PORT,GID_ACCOUNT_SMTP_STARTTLS,GID_ACCOUNT_SMTP_SAME_CREDENTIALS,GID_ACCOUNT_SMTP_USERNAME,GID_ACCOUNT_SMTP_PASSWORD,GID_ACCOUNT_FOLDER_MAPPING,GID_ACCOUNT_FETCH_DAYS,GID_ACCOUNT_FETCH_ON_START,GID_ACCOUNT_PERIODIC_FETCH,GID_ACCOUNT_NOTIFICATION_SOUND,GID_ACCOUNT_NOTIFICATION_PATH,GID_ACCOUNT_NOTIFICATION_CHOOSE,GID_ACCOUNT_STATUS,GID_ACCOUNT_SAVE,GID_ACCOUNT_CANCEL };
enum FolderMappingGadgetId { GID_FOLDER_SENT=140,GID_FOLDER_DRAFTS,GID_FOLDER_ALL,GID_FOLDER_SPAM,GID_FOLDER_TRASH,GID_FOLDER_SAVE_SENT,GID_FOLDER_OK,GID_FOLDER_CANCEL };
enum ConfirmGadgetId { GID_CONFIRM_YES=300,GID_CONFIRM_NO };
enum AboutGadgetId { GID_ABOUT_OK=400 };

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


static int folder_mapping_string_id(ULONG gadget_id)
{
    return gadget_id >= GID_FOLDER_SENT && gadget_id <= GID_FOLDER_TRASH;
}

static int account_string_id(ULONG gadget_id)
{
    switch (gadget_id) {
        case GID_ACCOUNT_ACCOUNT_NAME:
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
        case GID_ACCOUNT_FETCH_DAYS:
            return 1;
        default:
            return 0;
    }
}


static int account_file_exists(size_t account_index)
{
    const char *path = amg_storage_account_path(account_index);
    BPTR lock = path ? Lock((CONST_STRPTR)path, ACCESS_READ) : 0;
    if (!lock) return 0;
    UnLock(lock);
    return 1;
}

static void delete_account_auxiliary_files(size_t account_index)
{
    char path[96];
    if (account_index >= AMG_MAX_ACCOUNTS) return;

    if (account_index == 0U) {
        DeleteFile((CONST_STRPTR)"ENVARC:AmiMail/signature.txt");
        DeleteFile((CONST_STRPTR)"ENVARC:AmiMail/signature.txt.new");
        DeleteFile((CONST_STRPTR)"ENVARC:AmiMail/inbox-notify.state");
        DeleteFile((CONST_STRPTR)"ENVARC:AmiMail/inbox-notify.state.new");
        return;
    }

    snprintf(path, sizeof(path), "ENVARC:AmiMail/signature-%lu.txt",
             (unsigned long)(account_index + 1U));
    DeleteFile((CONST_STRPTR)path);
    snprintf(path, sizeof(path), "ENVARC:AmiMail/signature-%lu.txt.new",
             (unsigned long)(account_index + 1U));
    DeleteFile((CONST_STRPTR)path);
    snprintf(path, sizeof(path), "ENVARC:AmiMail/inbox-notify-%lu.state",
             (unsigned long)(account_index + 1U));
    DeleteFile((CONST_STRPTR)path);
    snprintf(path, sizeof(path), "ENVARC:AmiMail/inbox-notify-%lu.state.new",
             (unsigned long)(account_index + 1U));
    DeleteFile((CONST_STRPTR)path);
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
                      T(MSG_ENVARC_AMIMAIL_COULD_NOT_BE_CREATED, "ENVARC:AmiMail could not be created."));
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
           left->smtp_same_credentials == right->smtp_same_credentials &&
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

static int account_settings_equal(const AmgAccount *left,
                                  const AmgAccount *right)
{
    return account_network_settings_equal(left, right) &&
        !strcmp(left->account_name, right->account_name) &&
        left->enabled == right->enabled &&
        left->fetch_on_start == right->fetch_on_start &&
        left->periodic_fetch == right->periodic_fetch &&
        left->notification_sound == right->notification_sound &&
        !strcmp(left->notification_sound_path,
                right->notification_sound_path);
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
            (ULONG)(uintptr_t)T(MSG_SELECT_NOTIFICATION_SOUND, "Select notification sound"),
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
                               T(MSG_PLAYING_SOUND_PREVIEW, "Playing sound preview..."));
            } else if (status_gadget) {
                set_string(status_gadget, window,
                           T(MSG_SOUND_FILE_COULD_NOT_BE_LOADED_PLAYED, "Sound file could not be loaded/played."));
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
        WA_Title, T(MSG_AMIMAIL_SYSTEM_FOLDERS, "AmiMail - System folders"),
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
                T(MSG_EMPTY_AUTOMATIC_OTHERWISE_ENTER_THE_EXACT_IMAP_NAME, "Empty = automatic; otherwise enter the exact IMAP name.")),
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T(MSG_SENT, "Sent:")),
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
                LAYOUT_AddChild, static_text_label(T(MSG_DRAFTS, "Drafts:")),
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
                LAYOUT_AddChild, static_text_label(T(MSG_ALL_MAIL, "All Mail:")),
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
                LAYOUT_AddChild, static_text_label(T(MSG_SPAM, "Spam:")),
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
                LAYOUT_AddChild, static_text_label(T(MSG_TRASH, "Trash:")),
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
                    T(MSG_SAVE_SENT_MAIL, "Save sent mail:")),
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
                    T(MSG_VIA_IMAP_GMAIL_STORES_AUTOMATICALLY, "via IMAP (Gmail stores automatically)")),
            EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_FOLDER_OK,
                    GA_RelVerify, TRUE,
                    GA_Text, T(MSG_APPLY, "_Apply"),
                EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_FOLDER_CANCEL,
                    GA_RelVerify, TRUE,
                    GA_Text, T(MSG_CANCEL, "_Cancel"),
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

typedef struct AccountPageGadgets {
    struct Gadget *enabled;
    struct Gadget *account_name;
    struct Gadget *name;
    struct Gadget *email;
    struct Gadget *imap_host;
    struct Gadget *imap_port;
    struct Gadget *imap_starttls;
    struct Gadget *imap_username;
    struct Gadget *imap_password;
    struct Gadget *smtp_host;
    struct Gadget *smtp_port;
    struct Gadget *smtp_starttls;
    struct Gadget *smtp_same_credentials;
    struct Gadget *smtp_username;
    struct Gadget *smtp_password;
    struct Gadget *fetch_days;
    struct Gadget *fetch_on_start;
    struct Gadget *periodic_fetch;
    struct Gadget *notification_sound;
    struct Gadget *notification_path;
} AccountPageGadgets;

static int account_page_collect(
    const AccountPageGadgets *page, AmgAccount *candidate,
    char sent_mailbox[512], char drafts_mailbox[512],
    char all_mailbox[512], char spam_mailbox[512],
    char trash_mailbox[512], int save_sent_copy,
    AmgError *error)
{
    char *days_end = NULL, *imap_end = NULL, *smtp_end = NULL;
    unsigned long days, imap_port, smtp_port;
    ULONG selected = 0UL, enabled_selected = 0UL;
    int keep_locked_imap_secret, keep_locked_smtp_secret;
    if (!page || !candidate) return AMG_ERR_ARGUMENT;

    keep_locked_imap_secret =
        candidate->imap_password == NULL &&
        !string_text(page->imap_password)[0];
    keep_locked_smtp_secret =
        candidate->smtp_password == NULL &&
        !string_text(page->smtp_password)[0];
    GetAttr(GA_Selected, (Object *)page->enabled, &enabled_selected);
    days = strtoul(string_text(page->fetch_days), &days_end, 10);
    imap_port = strtoul(string_text(page->imap_port), &imap_end, 10);
    smtp_port = strtoul(string_text(page->smtp_port), &smtp_end, 10);
    while (days_end && *days_end == ' ') ++days_end;
    while (imap_end && *imap_end == ' ') ++imap_end;
    while (smtp_end && *smtp_end == ' ') ++smtp_end;
    if (!string_text(page->fetch_days)[0] || (days_end && *days_end) ||
        days < 1UL || days > 3650UL) {
        if (!enabled_selected) {
            days = 180UL;
        } else {
        amg_error_set(error, AMG_ERR_ARGUMENT,
            T(MSG_FETCH_PERIOD_ENTER_1_TO_3650_DAYS,
              "Fetch period: enter 1 to 3650 days."));
        return AMG_ERR_ARGUMENT;
        }
    }
    if (!string_text(page->imap_port)[0] || (imap_end && *imap_end) ||
        imap_port < 1UL || imap_port > 65535UL ||
        !string_text(page->smtp_port)[0] || (smtp_end && *smtp_end) ||
        smtp_port < 1UL || smtp_port > 65535UL) {
        if (!enabled_selected) {
            imap_port = 993UL;
            smtp_port = 465UL;
        } else {
        amg_error_set(error, AMG_ERR_ARGUMENT,
            T(MSG_IMAP_SMTP_PORT_ENTER_1_TO_65535,
              "IMAP/SMTP port: enter 1 to 65535."));
        return AMG_ERR_ARGUMENT;
        }
    }

    amg_account_clear(candidate);
    amg_account_init(candidate);
    candidate->enabled = enabled_selected ? 1 : 0;
    snprintf(candidate->account_name, sizeof(candidate->account_name), "%s",
             string_text(page->account_name));
    snprintf(candidate->display_name, sizeof(candidate->display_name), "%s",
             string_text(page->name));
    snprintf(candidate->email, sizeof(candidate->email), "%s",
             string_text(page->email));
    snprintf(candidate->imap_host, sizeof(candidate->imap_host), "%s",
             string_text(page->imap_host));
    snprintf(candidate->imap_username, sizeof(candidate->imap_username), "%s",
             string_text(page->imap_username));
    snprintf(candidate->smtp_host, sizeof(candidate->smtp_host), "%s",
             string_text(page->smtp_host));
    snprintf(candidate->smtp_username, sizeof(candidate->smtp_username), "%s",
             string_text(page->smtp_username));
    if (local_to_utf8(sent_mailbox, candidate->sent_mailbox,
                      sizeof(candidate->sent_mailbox)) != AMG_OK ||
        local_to_utf8(drafts_mailbox, candidate->drafts_mailbox,
                      sizeof(candidate->drafts_mailbox)) != AMG_OK ||
        local_to_utf8(all_mailbox, candidate->all_mailbox,
                      sizeof(candidate->all_mailbox)) != AMG_OK ||
        local_to_utf8(spam_mailbox, candidate->spam_mailbox,
                      sizeof(candidate->spam_mailbox)) != AMG_OK ||
        local_to_utf8(trash_mailbox, candidate->trash_mailbox,
                      sizeof(candidate->trash_mailbox)) != AMG_OK) {
        amg_error_set(error, AMG_ERR_LIMIT,
            T(MSG_A_SYSTEM_FOLDER_NAME_IS_TOO_LONG,
              "A system folder name is too long."));
        return AMG_ERR_LIMIT;
    }
    candidate->save_sent_copy = save_sent_copy ? 1 : 0;
    candidate->imap_port = (unsigned short)imap_port;
    candidate->smtp_port = (unsigned short)smtp_port;
    candidate->fetch_days = (unsigned int)days;
    candidate->auth_mode = AMG_AUTH_PASSWORD;
    GetAttr(GA_Selected, (Object *)page->imap_starttls, &selected);
    candidate->imap_starttls = selected ? 1 : 0;
    GetAttr(GA_Selected, (Object *)page->smtp_starttls, &selected);
    candidate->smtp_starttls = selected ? 1 : 0;
    GetAttr(GA_Selected, (Object *)page->smtp_same_credentials, &selected);
    candidate->smtp_same_credentials = selected ? 1 : 0;
    GetAttr(GA_Selected, (Object *)page->fetch_on_start, &selected);
    candidate->fetch_on_start = selected ? 1 : 0;
    GetAttr(GA_Selected, (Object *)page->periodic_fetch, &selected);
    candidate->periodic_fetch = selected ? 1 : 0;
    GetAttr(GA_Selected, (Object *)page->notification_sound, &selected);
    candidate->notification_sound = selected ? 1 : 0;
    snprintf(candidate->notification_sound_path,
             sizeof(candidate->notification_sound_path), "%s",
             string_text(page->notification_path));
    if ((!keep_locked_imap_secret &&
         amg_account_set_secret(&candidate->imap_password,
                                string_text(page->imap_password)) != AMG_OK) ||
        (!keep_locked_smtp_secret &&
         amg_account_set_secret(&candidate->smtp_password,
                                string_text(page->smtp_password)) != AMG_OK)) {
        amg_error_set(error, AMG_ERR_MEMORY,
                      T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory."));
        return AMG_ERR_MEMORY;
    }
    amg_account_normalize(candidate);
    return AMG_OK;
}

static void account_page_show(
    const AccountPageGadgets *page, struct Window *window,
    const AmgAccount *account, char sent_mailbox[512],
    char drafts_mailbox[512], char all_mailbox[512],
    char spam_mailbox[512], char trash_mailbox[512],
    int *save_sent_copy)
{
    char value[16];
    if (!page || !account || !save_sent_copy) return;
    set_string(page->account_name, window, account->account_name);
    set_string(page->name, window, account->display_name);
    set_string(page->email, window, account->email);
    set_string(page->imap_host, window, account->imap_host);
    snprintf(value, sizeof(value), "%u",
             (unsigned)(account->imap_port ? account->imap_port : 993U));
    set_string(page->imap_port, window, value);
    set_string(page->imap_username, window, account->imap_username);
    set_string(page->imap_password, window,
               account->imap_password ? account->imap_password : "");
    set_string(page->smtp_host, window, account->smtp_host);
    snprintf(value, sizeof(value), "%u",
             (unsigned)(account->smtp_port ? account->smtp_port : 465U));
    set_string(page->smtp_port, window, value);
    set_string(page->smtp_username, window, account->smtp_username);
    set_string(page->smtp_password, window,
               account->smtp_password ? account->smtp_password : "");
    snprintf(value, sizeof(value), "%u",
             account->fetch_days ? account->fetch_days : 180U);
    set_string(page->fetch_days, window, value);
    set_string(page->notification_path, window,
               account->notification_sound_path);
    SetGadgetAttrs(page->enabled, window, NULL, GA_Selected,
                   account->enabled ? TRUE : FALSE, TAG_DONE);
    SetGadgetAttrs(page->imap_starttls, window, NULL, GA_Selected,
                   account->imap_starttls ? TRUE : FALSE, TAG_DONE);
    SetGadgetAttrs(page->smtp_starttls, window, NULL, GA_Selected,
                   account->smtp_starttls ? TRUE : FALSE, TAG_DONE);
    SetGadgetAttrs(page->smtp_same_credentials, window, NULL, GA_Selected,
                   account->smtp_same_credentials ? TRUE : FALSE, TAG_DONE);
    SetGadgetAttrs(page->smtp_username, window, NULL, GA_Disabled,
                   account->smtp_same_credentials ? TRUE : FALSE, TAG_DONE);
    SetGadgetAttrs(page->smtp_password, window, NULL, GA_Disabled,
                   account->smtp_same_credentials ? TRUE : FALSE, TAG_DONE);
    SetGadgetAttrs(page->fetch_on_start, window, NULL, GA_Selected,
                   account->fetch_on_start ? TRUE : FALSE, TAG_DONE);
    SetGadgetAttrs(page->periodic_fetch, window, NULL, GA_Selected,
                   account->periodic_fetch ? TRUE : FALSE, TAG_DONE);
    SetGadgetAttrs(page->notification_sound, window, NULL, GA_Selected,
                   account->notification_sound ? TRUE : FALSE, TAG_DONE);
    utf8_to_local_copy(account->sent_mailbox, sent_mailbox, 512U);
    utf8_to_local_copy(account->drafts_mailbox, drafts_mailbox, 512U);
    utf8_to_local_copy(account->all_mailbox, all_mailbox, 512U);
    utf8_to_local_copy(account->spam_mailbox, spam_mailbox, 512U);
    utf8_to_local_copy(account->trash_mailbox, trash_mailbox, 512U);
    *save_sent_copy = account->save_sent_copy ? 1 : 0;
}

static void account_config_free_tab_nodes(struct List *list)
{
    struct Node *node;
    if (!list) return;
    while ((node = RemHead(list)) != NULL)
        FreeClickTabNode(node);
}

static int account_slot_has_configuration(const AmgAccount *account,
                                          size_t account_index)
{
    if (!account) return 0;
    if (account_file_exists(account_index)) return 1;
    return account->account_name[0] || account->display_name[0] ||
           account->email[0] || account->imap_host[0] ||
           account->smtp_host[0] || account->imap_username[0] ||
           account->smtp_username[0];
}

static size_t account_config_visible_for_slot(
    const size_t map[AMG_MAX_ACCOUNTS], size_t count, size_t slot)
{
    size_t visible;
    for (visible = 0U; visible < count; ++visible)
        if (map[visible] == slot) return visible;
    return 0U;
}

static int account_config_build_tab_nodes(
    struct List *list, const AmgAccount drafts[AMG_MAX_ACCOUNTS],
    const int configured[AMG_MAX_ACCOUNTS],
    const size_t order[AMG_MAX_ACCOUNTS],
    size_t map[AMG_MAX_ACCOUNTS],
    char labels[AMG_MAX_ACCOUNTS][128], size_t active_slot,
    size_t *count_out, size_t *selected_out)
{
    size_t position, count = 0U, selected = 0U;
    if (!list || !drafts || !configured || !order || !map || !labels)
        return AMG_ERR_ARGUMENT;

    for (position = 0U; position < AMG_MAX_ACCOUNTS; ++position) {
        size_t slot = order[position];
        const AmgAccount *account;
        const char *label;
        struct Node *node;
        if (slot >= AMG_MAX_ACCOUNTS || !configured[slot]) continue;
        account = &drafts[slot];
        label = account->account_name[0] ? account->account_name :
                (account->email[0] ? account->email : NULL);
        if (label)
            snprintf(labels[count], sizeof(labels[count]), "%s", label);
        else
            amg_tr_snprintf(labels[count], sizeof(labels[count]),
                            MSG_ACCOUNT_VALUE, "Account %lu",
                            (unsigned long)(slot + 1U));
        if (!labels[count][0])
            amg_tr_snprintf(labels[count], sizeof(labels[count]),
                            MSG_ACCOUNT_VALUE, "Account %lu",
                            (unsigned long)(slot + 1U));
        map[count] = slot;
        if (slot == active_slot) selected = count;
        node = AllocClickTabNode(
            TNA_Text, (ULONG)(uintptr_t)labels[count],
            TNA_Number, (ULONG)count, TAG_DONE);
        if (!node) {
            account_config_free_tab_nodes(list);
            return AMG_ERR_MEMORY;
        }
        AddTail(list, node);
        ++count;
    }
    if (count_out) *count_out = count;
    if (selected_out) *selected_out = selected;
    return AMG_OK;
}

static void account_config_update_order_buttons(
    struct Gadget *add_gadget, struct Gadget *delete_gadget,
    struct Gadget *move_left_gadget, struct Gadget *move_right_gadget,
    struct Window *window, size_t selected, size_t count)
{
    if (add_gadget)
        SetGadgetAttrs(add_gadget, window, NULL, GA_Disabled,
                       count >= AMG_MAX_ACCOUNTS ? TRUE : FALSE, TAG_DONE);
    if (delete_gadget)
        SetGadgetAttrs(delete_gadget, window, NULL, GA_Disabled,
                       count <= 1U ? TRUE : FALSE, TAG_DONE);
    if (move_left_gadget)
        SetGadgetAttrs(move_left_gadget, window, NULL, GA_Disabled,
                       selected == 0U ? TRUE : FALSE, TAG_DONE);
    if (move_right_gadget)
        SetGadgetAttrs(move_right_gadget, window, NULL, GA_Disabled,
                       selected + 1U >= count ? TRUE : FALSE, TAG_DONE);
}

static int account_config_rebuild_tabs(
    Object *dialog, struct Window *window, struct Gadget *tabs_gadget,
    struct Gadget *add_gadget, struct Gadget *delete_gadget,
    struct Gadget *move_left_gadget, struct Gadget *move_right_gadget,
    struct List *tabs_list,
    const AmgAccount drafts[AMG_MAX_ACCOUNTS],
    const int configured[AMG_MAX_ACCOUNTS],
    const size_t order[AMG_MAX_ACCOUNTS],
    size_t map[AMG_MAX_ACCOUNTS],
    char labels[AMG_MAX_ACCOUNTS][128], size_t active_slot,
    size_t *count_out)
{
    size_t count = 0U, selected = 0U;
    int result;
    if (tabs_gadget)
        SetAttrs((Object *)tabs_gadget, CLICKTAB_Labels, (ULONG)~0UL,
                 TAG_DONE);
    account_config_free_tab_nodes(tabs_list);
    result = account_config_build_tab_nodes(
        tabs_list, drafts, configured, order, map, labels, active_slot,
        &count, &selected);
    if (result != AMG_OK) return result;
    if (tabs_gadget)
        SetGadgetAttrs(tabs_gadget, window, NULL,
                       CLICKTAB_Labels, (ULONG)(uintptr_t)tabs_list,
                       CLICKTAB_Current, (ULONG)selected, TAG_DONE);
    account_config_update_order_buttons(
        add_gadget, delete_gadget, move_left_gadget, move_right_gadget,
        window, selected, count);
    if (dialog && window) {
        (void)DoMethod(dialog, WM_RETHINK);
        if (tabs_gadget) RefreshGList(tabs_gadget, window, NULL, 1);
    }
    if (count_out) *count_out = count;
    return AMG_OK;
}

static void account_order_append_configured_slot(
    size_t order[AMG_MAX_ACCOUNTS],
    const int configured[AMG_MAX_ACCOUNTS], size_t new_slot)
{
    size_t new_order[AMG_MAX_ACCOUNTS];
    size_t position, out = 0U;
    for (position = 0U; position < AMG_MAX_ACCOUNTS; ++position) {
        size_t slot = order[position];
        if (slot < AMG_MAX_ACCOUNTS && configured[slot] && slot != new_slot)
            new_order[out++] = slot;
    }
    if (new_slot < AMG_MAX_ACCOUNTS) new_order[out++] = new_slot;
    for (position = 0U; position < AMG_MAX_ACCOUNTS; ++position) {
        size_t slot = order[position];
        if (slot < AMG_MAX_ACCOUNTS && !configured[slot] && slot != new_slot)
            new_order[out++] = slot;
    }
    if (out == AMG_MAX_ACCOUNTS)
        memcpy(order, new_order, sizeof(new_order));
}

static void account_order_partition_configured_slots(
    size_t order[AMG_MAX_ACCOUNTS],
    const int configured[AMG_MAX_ACCOUNTS])
{
    size_t new_order[AMG_MAX_ACCOUNTS];
    size_t position, out = 0U;
    if (!order || !configured) return;
    for (position = 0U; position < AMG_MAX_ACCOUNTS; ++position) {
        size_t slot = order[position];
        if (slot < AMG_MAX_ACCOUNTS && configured[slot])
            new_order[out++] = slot;
    }
    for (position = 0U; position < AMG_MAX_ACCOUNTS; ++position) {
        size_t slot = order[position];
        if (slot < AMG_MAX_ACCOUNTS && !configured[slot])
            new_order[out++] = slot;
    }
    if (out == AMG_MAX_ACCOUNTS)
        memcpy(order, new_order, sizeof(new_order));
}

static int account_order_move_configured_slot(
    size_t order[AMG_MAX_ACCOUNTS],
    const int configured[AMG_MAX_ACCOUNTS], size_t slot, int direction)
{
    size_t position;
    if (!order || !configured || !direction) return 0;
    for (position = 0U; position < AMG_MAX_ACCOUNTS; ++position)
        if (order[position] == slot) break;
    if (position >= AMG_MAX_ACCOUNTS) return 0;
    if (direction < 0) {
        size_t other = position;
        while (other > 0U) {
            --other;
            if (order[other] < AMG_MAX_ACCOUNTS &&
                configured[order[other]]) {
                size_t temp = order[position];
                order[position] = order[other];
                order[other] = temp;
                return 1;
            }
        }
    } else {
        size_t other;
        for (other = position + 1U; other < AMG_MAX_ACCOUNTS; ++other) {
            if (order[other] < AMG_MAX_ACCOUNTS &&
                configured[order[other]]) {
                size_t temp = order[position];
                order[position] = order[other];
                order[other] = temp;
                return 1;
            }
        }
    }
    return 0;
}

 int account_dialog(AmgGui *gui, AmgError *error)
{
    Object *dialog;
    Object *account_layout = NULL;
    struct Window *window;
    struct Gadget *tabs_gadget, *add_account_gadget, *delete_account_gadget;
    struct Gadget *move_left_gadget, *move_right_gadget, *enabled_gadget;
    struct Gadget *account_name_gadget, *name_gadget, *email_gadget;
    struct Gadget *imap_host_gadget, *imap_port_gadget;
    struct Gadget *imap_starttls_gadget;
    struct Gadget *imap_username_gadget, *imap_password_gadget;
    struct Gadget *smtp_host_gadget, *smtp_port_gadget;
    struct Gadget *smtp_starttls_gadget, *smtp_same_credentials_gadget;
    struct Gadget *smtp_username_gadget, *smtp_password_gadget;
    struct Gadget *fetch_days_gadget;
    struct Gadget *fetch_on_start_gadget, *periodic_fetch_gadget;
    struct Gadget *notification_sound_gadget, *notification_sound_path_gadget;
    struct Gadget *dialog_status;
    AccountPageGadgets page;
    AmgAccount drafts[AMG_MAX_ACCOUNTS];
    int configured_slots[AMG_MAX_ACCOUNTS];
    int deleted_slots[AMG_MAX_ACCOUNTS];
    size_t draft_order[AMG_MAX_ACCOUNTS];
    size_t original_order[AMG_MAX_ACCOUNTS];
    size_t account_tab_map[AMG_MAX_ACCOUNTS];
    char account_tab_labels[AMG_MAX_ACCOUNTS][128];
    struct List account_tabs_list;
    size_t account_tab_count = 0U, initial_selected = 0U;
    size_t active_tab;
    size_t account_index;
    ULONG signal_mask;
    ULONG account_width = 470UL;
    ULONG hint_gap = 4UL;
    LONG account_outer_width, account_outer_height;
    LONG account_left, account_top, account_max_left, account_max_top;
    struct LayoutLimits account_limits;
    char fetch_days_text[16];
    char imap_port_text[8];
    char smtp_port_text[8];
    char sent_mailbox[512], drafts_mailbox[512], all_mailbox[512];
    char spam_mailbox[512], trash_mailbox[512];
    int save_sent_copy;
    int done = 0, changed = 0;
    int network_was_running[AMG_MAX_ACCOUNTS];

    memset(&page, 0, sizeof(page));
    memset(configured_slots, 0, sizeof(configured_slots));
    memset(deleted_slots, 0, sizeof(deleted_slots));
    memcpy(draft_order, gui->account_set->order, sizeof(draft_order));
    memcpy(original_order, gui->account_set->order, sizeof(original_order));
    NewList(&account_tabs_list);
    active_tab = gui->active_account < AMG_MAX_ACCOUNTS ?
        gui->active_account : 0U;
    for (account_index = 0U; account_index < AMG_MAX_ACCOUNTS;
         ++account_index) {
        amg_account_init(&drafts[account_index]);
        if (amg_account_copy(&drafts[account_index],
                             &gui->account_set->accounts[account_index]) !=
            AMG_OK) {
            while (account_index > 0U)
                amg_account_clear(&drafts[--account_index]);
            amg_error_set(error, AMG_ERR_MEMORY,
                          T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory."));
            return 0;
        }
        network_was_running[account_index] =
            amg_network_is_running(gui->networks[account_index]);
        configured_slots[account_index] =
            account_slot_has_configuration(&drafts[account_index],
                                           account_index);
    }
    /* The currently selected slot must always remain reachable, including on
     * a completely fresh first-run configuration. */
    configured_slots[active_tab] = 1;
    if (account_config_build_tab_nodes(
            &account_tabs_list, drafts, configured_slots, draft_order,
            account_tab_map, account_tab_labels, active_tab,
            &account_tab_count, &initial_selected) != AMG_OK) {
        for (account_index = 0U; account_index < AMG_MAX_ACCOUNTS;
             ++account_index)
            amg_account_clear(&drafts[account_index]);
        account_config_free_tab_nodes(&account_tabs_list);
        amg_error_set(error, AMG_ERR_MEMORY,
                      T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory."));
        return 0;
    }
    tabs_gadget = NULL;
    add_account_gadget = NULL;
    delete_account_gadget = NULL;
    move_left_gadget = NULL;
    move_right_gadget = NULL;
    enabled_gadget = NULL;
    account_name_gadget = NULL;
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
    smtp_same_credentials_gadget = NULL;
    smtp_username_gadget = NULL;
    smtp_password_gadget = NULL;
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
        WA_Title, T(MSG_AMIMAIL_ACCOUNT_SETTINGS, "AmiMail - Account settings"),
        WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                      WFLG_ACTIVATE,
        WA_PubScreen, gui->screen,
        WA_Width, account_width,
        WA_MinWidth, 440,
        WA_MaxWidth, 8192,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_RAWKEY,
        WINDOW_ParentGroup,
            account_layout = VGroupObject,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_SpaceInner, FALSE,

            LAYOUT_AddChild, VGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
                LAYOUT_ShrinkWrap, TRUE,

            LAYOUT_AddChild, VGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_ShrinkWrap, TRUE,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild,
                        tabs_gadget = (struct Gadget *)NewObject(
                            CLICKTAB_GetClass(), NULL,
                            GA_ID, GID_ACCOUNT_CONFIG_TABS,
                            GA_RelVerify, TRUE,
                            CLICKTAB_Labels,
                                (ULONG)(uintptr_t)&account_tabs_list,
                            CLICKTAB_Current, (ULONG)initial_selected,
                            CLICKTAB_AutoFit, TRUE,
                            CLICKTAB_PageGroupBorder, FALSE,
                            CLICKTAB_TabsOffsetAsLayoutSpacing, TRUE,
                            TAG_DONE),
                    LAYOUT_AddChild,
                        move_left_gadget = (struct Gadget *)ButtonObject,
                            GA_ID, GID_ACCOUNT_MOVE_LEFT,
                            GA_RelVerify, TRUE,
                            GA_Disabled, initial_selected == 0U ? TRUE : FALSE,
                            GA_Text, "<",
                        EndObject,
                    CHILD_MinWidth, 28,
                    CHILD_MaxWidth, 28,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        move_right_gadget = (struct Gadget *)ButtonObject,
                            GA_ID, GID_ACCOUNT_MOVE_RIGHT,
                            GA_RelVerify, TRUE,
                            GA_Disabled,
                                initial_selected + 1U >= account_tab_count
                                    ? TRUE : FALSE,
                            GA_Text, ">",
                        EndObject,
                    CHILD_MinWidth, 28,
                    CHILD_MaxWidth, 28,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        add_account_gadget = (struct Gadget *)ButtonObject,
                            GA_ID, GID_ACCOUNT_ADD,
                            GA_RelVerify, TRUE,
                            GA_Disabled,
                                account_tab_count >= AMG_MAX_ACCOUNTS
                                    ? TRUE : FALSE,
                            GA_Text, "+",
                        EndObject,
                    CHILD_MinWidth, 28,
                    CHILD_MaxWidth, 28,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        delete_account_gadget = (struct Gadget *)ButtonObject,
                            GA_ID, GID_ACCOUNT_DELETE,
                            GA_RelVerify, TRUE,
                            GA_Disabled,
                                account_tab_count <= 1U ? TRUE : FALSE,
                            GA_Text, "-",
                        EndObject,
                    CHILD_MinWidth, 28,
                    CHILD_MaxWidth, 28,
                    CHILD_WeightedWidth, 0,
                EndObject,
                CHILD_WeightedHeight, 0,

                /* Keep the first checkbox clear of the clicktab baseline.
                 * A tiny fixed spacer avoids the one-pixel clipping seen
                 * with classic fonts without visibly opening the layout. */
                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, 2,
                CHILD_MaxHeight, 2,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(
                        T(MSG_ACTIVE_ACCOUNT, "Active account:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        enabled_gadget = (struct Gadget *)ButtonObject,
                            GA_ID, GID_ACCOUNT_ENABLED,
                            GA_RelVerify, TRUE,
                            GA_Selected,
                                gui->account->enabled ? TRUE : FALSE,
                            BUTTON_AutoButton, BAG_CHECKBOX,
                            BUTTON_PushButton, TRUE,
                        EndObject,
                    CHILD_MinWidth, 24,
                    CHILD_MaxWidth, 24,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, static_text_label(
                        T(MSG_ACTIVATE_ACCOUNT,
                          "Activate account")),
                EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(
                        T(MSG_ACCOUNT_NAME, "Account name:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        account_name_gadget = (struct Gadget *)StringObject,
                            GA_ID, GID_ACCOUNT_ACCOUNT_NAME,
                            GA_RelVerify, TRUE,
                            GA_TabCycle, TRUE,
                            STRINGA_MaxChars, 95,
                            STRINGA_TextVal, gui->account->account_name,
                        EndObject,
                    EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(
                        T(MSG_SENDER_NAME, "Sender name:")),
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
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T(MSG_EMAIL_ADDRESS_F1D2, "Email address:")),
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
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T(MSG_IMAP_SERVER_PORT, "IMAP server / port:")),
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
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T(MSG_IMAP_SECURITY, "IMAP security:")),
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
                        T(MSG_STARTTLS_TYPICALLY_PORT_143, "STARTTLS (typically port 143)")),
                EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T(MSG_IMAP_USER, "IMAP user:")),
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
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T(MSG_IMAP_PASSWORD, "IMAP password:")),
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
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T(MSG_SMTP_SERVER_PORT, "SMTP server / port:")),
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
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T(MSG_SMTP_SECURITY, "SMTP security:")),
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
                        T(MSG_STARTTLS_TYPICALLY_PORT_587, "STARTTLS (typically port 587)")),
                EndObject,
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
                        smtp_same_credentials_gadget =
                            (struct Gadget *)ButtonObject,
                            GA_ID, GID_ACCOUNT_SMTP_SAME_CREDENTIALS,
                            GA_RelVerify, TRUE,
                            GA_Selected,
                                gui->account->smtp_same_credentials ? TRUE : FALSE,
                            BUTTON_AutoButton, BAG_CHECKBOX,
                            BUTTON_PushButton, TRUE,
                        EndObject,
                    CHILD_MinWidth, 24,
                    CHILD_MaxWidth, 24,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, static_text_label(
                        T(MSG_SMTP_USES_SAME_CREDENTIALS, "SMTP uses same credentials")),
                EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T(MSG_SMTP_USER, "SMTP user:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        smtp_username_gadget = (struct Gadget *)StringObject,
                            GA_ID, GID_ACCOUNT_SMTP_USERNAME,
                            GA_RelVerify, TRUE,
                            GA_TabCycle, TRUE,
                            GA_Disabled,
                                gui->account->smtp_same_credentials ? TRUE : FALSE,
                            STRINGA_MaxChars, 255,
                            STRINGA_TextVal, gui->account->smtp_username,
                        EndObject,
                    EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T(MSG_SMTP_PASSWORD, "SMTP password:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        smtp_password_gadget = (struct Gadget *)StringObject,
                            GA_ID, GID_ACCOUNT_SMTP_PASSWORD,
                            GA_RelVerify, TRUE,
                            GA_TabCycle, TRUE,
                            GA_Disabled,
                                gui->account->smtp_same_credentials ? TRUE : FALSE,
                            STRINGA_MaxChars, 255,
                            STRINGA_HookType, SHK_PASSWORD,
                            STRINGA_TextVal,
                                gui->account->smtp_password ?
                                    gui->account->smtp_password : "",
                        EndObject,
                    EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T(MSG_SYSTEM_FOLDERS, "System folders:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, ButtonObject,
                        GA_ID, GID_ACCOUNT_FOLDER_MAPPING,
                        GA_RelVerify, TRUE,
                        GA_Text, T(MSG_MAP, "_Map..."),
                    EndObject,
                EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T(MSG_FETCH_PERIOD_DAYS, "Fetch period (days):")),
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
                        T(MSG_FETCH_MAIL_AT_STARTUP, "Fetch mail at startup")),
                EndObject,
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
                        T(MSG_PERIODIC_FETCH_5_MIN, "Periodic fetch (5 min)")),
                EndObject,
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
                        T(MSG_NOTIFICATION_SOUND, "Notification Sound")),
                EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(
                        T(MSG_SOUND_FILE, "Sound file:")),
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
                        T(MSG_STARTTLS_ONLY_IF_SUPPORTED_GMAIL_IMAP_993_OFF, "STARTTLS only if supported. Gmail: IMAP 993 off; SMTP 587 on."),
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
                    GA_ID, GID_ACCOUNT_SAVE,
                    GA_RelVerify, TRUE,
                    GA_Text, T(MSG_SAVE, "_Save"),
                EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_ACCOUNT_CANCEL,
                    GA_RelVerify, TRUE,
                    GA_Text, T(MSG_CANCEL, "_Cancel"),
                EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,

            EndObject,
            CHILD_WeightedHeight, 0,
        EndObject,
    EndWindow;

    if (!dialog) {
        for (account_index = 0U; account_index < AMG_MAX_ACCOUNTS;
             ++account_index)
            amg_account_clear(&drafts[account_index]);
        account_config_free_tab_nodes(&account_tabs_list);
        amg_error_set(error, AMG_ERR_MEMORY,
                      T(MSG_ACCOUNT_DIALOG_COULD_NOT_BE_CREATED, "Account dialog could not be created."));
        return 0;
    }
    page.enabled = enabled_gadget;
    page.account_name = account_name_gadget;
    page.name = name_gadget;
    page.email = email_gadget;
    page.imap_host = imap_host_gadget;
    page.imap_port = imap_port_gadget;
    page.imap_starttls = imap_starttls_gadget;
    page.imap_username = imap_username_gadget;
    page.imap_password = imap_password_gadget;
    page.smtp_host = smtp_host_gadget;
    page.smtp_port = smtp_port_gadget;
    page.smtp_starttls = smtp_starttls_gadget;
    page.smtp_same_credentials = smtp_same_credentials_gadget;
    page.smtp_username = smtp_username_gadget;
    page.smtp_password = smtp_password_gadget;
    page.fetch_days = fetch_days_gadget;
    page.fetch_on_start = fetch_on_start_gadget;
    page.periodic_fetch = periodic_fetch_gadget;
    page.notification_sound = notification_sound_gadget;
    page.notification_path = notification_sound_path_gadget;

    /* Measure the finished account layout before the window becomes visible.
     * The old WPOS_CENTERWINDOW + post-open MoveWindow() sequence caused the
     * account window to appear briefly offset to the right and then jump to
     * the center.  Compute the final geometry up front, just like the stable
     * confirm requesters, and let RA_OpenWindow() show it only once. */
    memset(&account_limits, 0, sizeof(account_limits));
    if (account_layout && gui->window && gui->screen) {
        LayoutLimits((struct Gadget *)account_layout, &account_limits,
                     gui->screen->RastPort.Font, gui->screen);

        account_outer_width = (LONG)account_width;
        {
            LONG required_width = (LONG)account_limits.MinWidth +
                (LONG)gui->window->BorderLeft +
                (LONG)gui->window->BorderRight;
            if (account_outer_width < required_width)
                account_outer_width = required_width;
        }
        account_outer_height = (LONG)account_limits.MinHeight +
            (LONG)gui->window->BorderTop +
            (LONG)gui->window->BorderBottom;
        if (account_outer_height < 1L) account_outer_height = 1L;

        if (account_outer_width > (LONG)gui->screen->Width)
            account_outer_width = (LONG)gui->screen->Width;
        if (account_outer_height > (LONG)gui->screen->Height)
            account_outer_height = (LONG)gui->screen->Height;

        account_left = (LONG)gui->window->LeftEdge +
            ((LONG)gui->window->Width - account_outer_width) / 2L;
        account_top = (LONG)gui->window->TopEdge +
            ((LONG)gui->window->Height - account_outer_height) / 2L;
        account_max_left = (LONG)gui->screen->Width - account_outer_width;
        account_max_top = (LONG)gui->screen->Height - account_outer_height;
        if (account_max_left < 0L) account_max_left = 0L;
        if (account_max_top < 0L) account_max_top = 0L;
        if (account_left < 0L) account_left = 0L;
        if (account_top < 0L) account_top = 0L;
        if (account_left > account_max_left) account_left = account_max_left;
        if (account_top > account_max_top) account_top = account_max_top;

        SetAttrs(dialog,
                 WA_Left, account_left,
                 WA_Top, account_top,
                 WA_Width, account_outer_width,
                 WA_Height, account_outer_height,
                 TAG_DONE);
    }

    window = RA_OpenWindow(dialog);
    if (!window) {
        DisposeObject(dialog);
        for (account_index = 0U; account_index < AMG_MAX_ACCOUNTS;
             ++account_index)
            amg_account_clear(&drafts[account_index]);
        account_config_free_tab_nodes(&account_tabs_list);
        amg_error_set(error, AMG_ERR_IO,
                      T(MSG_ACCOUNT_DIALOG_COULD_NOT_BE_OPENED, "Account dialog could not be opened."));
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
                        result = WMHI_GADGETUP | GID_ACCOUNT_SAVE;

                    case WMHI_GADGETUP:
                        if (account_string_id(result & WMHI_GADGETMASK) &&
                            requester_string_accept_code(input_code))
                            result = WMHI_GADGETUP | GID_ACCOUNT_SAVE;
                        switch (result & WMHI_GADGETMASK) {
                            case GID_ACCOUNT_CONFIG_TABS:
                            {
                                ULONG next_visible = 0UL;
                                size_t next_slot;
                                size_t current_visible =
                                    account_config_visible_for_slot(
                                        account_tab_map, account_tab_count,
                                        active_tab);
                                GetAttr(CLICKTAB_Current,
                                        (Object *)tabs_gadget, &next_visible);
                                if (next_visible >= account_tab_count) {
                                    SetGadgetAttrs(
                                        tabs_gadget, window, NULL,
                                        CLICKTAB_Current,
                                        (ULONG)current_visible, TAG_DONE);
                                    break;
                                }
                                next_slot = account_tab_map[next_visible];
                                if (next_slot == active_tab) break;
                                if (account_page_collect(
                                        &page, &drafts[active_tab],
                                        sent_mailbox, drafts_mailbox,
                                        all_mailbox, spam_mailbox,
                                        trash_mailbox, save_sent_copy,
                                        error) != AMG_OK) {
                                    set_utf8_string(dialog_status, window,
                                                    error->message);
                                    SetGadgetAttrs(
                                        tabs_gadget, window, NULL,
                                        CLICKTAB_Current,
                                        (ULONG)current_visible, TAG_DONE);
                                    break;
                                }
                                active_tab = next_slot;
                                account_page_show(
                                    &page, window, &drafts[active_tab],
                                    sent_mailbox,
                                    drafts_mailbox, all_mailbox,
                                    spam_mailbox, trash_mailbox,
                                    &save_sent_copy);
                                if (account_config_rebuild_tabs(
                                        dialog, window, tabs_gadget,
                                        add_account_gadget, delete_account_gadget,
                                        move_left_gadget, move_right_gadget,
                                        &account_tabs_list,
                                        drafts, configured_slots, draft_order,
                                        account_tab_map, account_tab_labels,
                                        active_tab, &account_tab_count) !=
                                    AMG_OK)
                                    set_string(
                                        dialog_status, window,
                                        T(MSG_NOT_ENOUGH_MEMORY,
                                          "Not enough memory."));
                                else
                                    set_string(
                                        dialog_status, window,
                                        T(MSG_CONFIGURE_THIS_ACCOUNT_THEN_SAVE,
                                          "Configure this account, then Save."));
                                break;
                            }

                            case GID_ACCOUNT_ADD:
                            {
                                size_t new_slot;
                                if (account_page_collect(
                                        &page, &drafts[active_tab],
                                        sent_mailbox, drafts_mailbox,
                                        all_mailbox, spam_mailbox,
                                        trash_mailbox, save_sent_copy,
                                        error) != AMG_OK) {
                                    set_utf8_string(dialog_status, window,
                                                    error->message);
                                    break;
                                }
                                for (new_slot = 0U; new_slot < AMG_MAX_ACCOUNTS;
                                     ++new_slot)
                                    if (!configured_slots[new_slot] &&
                                        !deleted_slots[new_slot]) break;
                                if (new_slot >= AMG_MAX_ACCOUNTS) {
                                    for (new_slot = 0U;
                                         new_slot < AMG_MAX_ACCOUNTS;
                                         ++new_slot)
                                        if (!configured_slots[new_slot]) break;
                                }
                                if (new_slot >= AMG_MAX_ACCOUNTS) break;
                                if (deleted_slots[new_slot]) {
                                    /* With all three slots occupied, '-' and
                                     * then '+' acts as a safe undo until Save:
                                     * restore the original account instead of
                                     * accidentally overwriting a still-present
                                     * encrypted account file as a new account. */
                                    if (amg_account_copy(
                                            &drafts[new_slot],
                                            &gui->account_set->accounts[
                                                new_slot]) != AMG_OK) {
                                        set_string(
                                            dialog_status, window,
                                            T(MSG_NOT_ENOUGH_MEMORY,
                                              "Not enough memory."));
                                        break;
                                    }
                                }
                                deleted_slots[new_slot] = 0;
                                account_order_append_configured_slot(
                                    draft_order, configured_slots, new_slot);
                                configured_slots[new_slot] = 1;
                                active_tab = new_slot;
                                account_page_show(
                                    &page, window, &drafts[active_tab],
                                    sent_mailbox,
                                    drafts_mailbox, all_mailbox,
                                    spam_mailbox, trash_mailbox,
                                    &save_sent_copy);
                                if (account_config_rebuild_tabs(
                                        dialog, window, tabs_gadget,
                                        add_account_gadget, delete_account_gadget,
                                        move_left_gadget, move_right_gadget,
                                        &account_tabs_list,
                                        drafts, configured_slots, draft_order,
                                        account_tab_map, account_tab_labels,
                                        active_tab, &account_tab_count) !=
                                    AMG_OK)
                                    set_string(
                                        dialog_status, window,
                                        T(MSG_NOT_ENOUGH_MEMORY,
                                          "Not enough memory."));
                                else
                                    set_string(
                                        dialog_status, window,
                                        T(MSG_CONFIGURE_THIS_ACCOUNT_THEN_SAVE,
                                          "Configure this account, then Save."));
                                break;
                            }

                            case GID_ACCOUNT_DELETE:
                            {
                                size_t current_visible;
                                size_t next_slot;
                                if (account_tab_count <= 1U) break;
                                current_visible =
                                    account_config_visible_for_slot(
                                        account_tab_map, account_tab_count,
                                        active_tab);
                                if (current_visible + 1U < account_tab_count)
                                    next_slot = account_tab_map[
                                        current_visible + 1U];
                                else
                                    next_slot = account_tab_map[
                                        current_visible - 1U];

                                deleted_slots[active_tab] = 1;
                                configured_slots[active_tab] = 0;
                                amg_account_clear(&drafts[active_tab]);
                                amg_account_init(&drafts[active_tab]);
                                account_order_partition_configured_slots(
                                    draft_order, configured_slots);
                                active_tab = next_slot;
                                account_page_show(
                                    &page, window, &drafts[active_tab],
                                    sent_mailbox,
                                    drafts_mailbox, all_mailbox,
                                    spam_mailbox, trash_mailbox,
                                    &save_sent_copy);
                                if (account_config_rebuild_tabs(
                                        dialog, window, tabs_gadget,
                                        add_account_gadget, delete_account_gadget,
                                        move_left_gadget, move_right_gadget,
                                        &account_tabs_list, drafts,
                                        configured_slots, draft_order,
                                        account_tab_map, account_tab_labels,
                                        active_tab, &account_tab_count) !=
                                    AMG_OK)
                                    set_string(
                                        dialog_status, window,
                                        T(MSG_NOT_ENOUGH_MEMORY,
                                          "Not enough memory."));
                                else
                                    set_string(
                                        dialog_status, window,
                                        T(MSG_CONFIGURE_THIS_ACCOUNT_THEN_SAVE,
                                          "Configure this account, then Save."));
                                break;
                            }

                            case GID_ACCOUNT_MOVE_LEFT:
                            case GID_ACCOUNT_MOVE_RIGHT:
                            {
                                int direction =
                                    (result & WMHI_GADGETMASK) ==
                                            GID_ACCOUNT_MOVE_LEFT
                                        ? -1 : 1;
                                if (account_page_collect(
                                        &page, &drafts[active_tab],
                                        sent_mailbox, drafts_mailbox,
                                        all_mailbox, spam_mailbox,
                                        trash_mailbox, save_sent_copy,
                                        error) != AMG_OK) {
                                    set_utf8_string(dialog_status, window,
                                                    error->message);
                                    break;
                                }
                                if (account_order_move_configured_slot(
                                        draft_order, configured_slots,
                                        active_tab, direction)) {
                                    if (account_config_rebuild_tabs(
                                            dialog, window, tabs_gadget,
                                            add_account_gadget,
                                            delete_account_gadget,
                                            move_left_gadget,
                                            move_right_gadget,
                                            &account_tabs_list, drafts,
                                            configured_slots, draft_order,
                                            account_tab_map,
                                            account_tab_labels, active_tab,
                                            &account_tab_count) != AMG_OK)
                                        set_string(
                                            dialog_status, window,
                                            T(MSG_NOT_ENOUGH_MEMORY,
                                              "Not enough memory."));
                                    else
                                        set_string(
                                            dialog_status, window,
                                            T(MSG_CONFIGURE_THIS_ACCOUNT_THEN_SAVE,
                                              "Configure this account, then Save."));
                                }
                                break;
                            }

                            case GID_ACCOUNT_CANCEL:
                                done = 1;
                                break;


                            case GID_ACCOUNT_SMTP_SAME_CREDENTIALS:
                            {
                                ULONG same_credentials = 0;
                                GetAttr(GA_Selected,
                                        (Object *)smtp_same_credentials_gadget,
                                        &same_credentials);
                                SetGadgetAttrs(smtp_username_gadget, window,
                                               NULL, GA_Disabled,
                                               same_credentials ? TRUE : FALSE,
                                               TAG_DONE);
                                SetGadgetAttrs(smtp_password_gadget, window,
                                               NULL, GA_Disabled,
                                               same_credentials ? TRUE : FALSE,
                                               TAG_DONE);
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
                                size_t enabled_count = 0U;
                                size_t next_active;
                                if (account_page_collect(
                                        &page, &drafts[active_tab],
                                        sent_mailbox, drafts_mailbox,
                                        all_mailbox, spam_mailbox,
                                        trash_mailbox, save_sent_copy,
                                        error) != AMG_OK) {
                                    set_utf8_string(dialog_status, window,
                                                    error->message);
                                    break;
                                }

                                /* Validate every changed enabled page before
                                 * writing any account files. Pure tab-order
                                 * changes must not require decrypting an
                                 * otherwise untouched account. */
                                for (account_index = 0U;
                                     account_index < AMG_MAX_ACCOUNTS;
                                     ++account_index) {
                                    AmgAccount *candidate =
                                        &drafts[account_index];
                                    int candidate_changed;
                                    if (deleted_slots[account_index])
                                        continue;
                                    candidate_changed =
                                        !account_settings_equal(
                                            candidate,
                                            &gui->account_set->accounts[
                                                account_index]);
                                    if (candidate->enabled) {
                                        ++enabled_count;
                                        if (candidate_changed &&
                                            amg_account_validate(
                                                candidate, error) != AMG_OK) {
                                            char message[320];
                                            char detail[256];
                                            snprintf(detail, sizeof(detail),
                                                     "%s", error->message);
                                            amg_tr_snprintf(
                                                message, sizeof(message),
                                                MSG_ACCOUNT_VALUE_DETAIL,
                                                "Account %lu: %s",
                                                (unsigned long)
                                                    (account_index + 1U),
                                                detail);
                                            amg_error_set(error, error->code,
                                                          message);
                                            break;
                                        }
                                        if (candidate_changed &&
                                            candidate->notification_sound) {
                                            BPTR sound_lock;
                                            if (!candidate->notification_sound_path[0]) {
                                                amg_error_set(
                                                    error, AMG_ERR_ARGUMENT,
                                                    T(MSG_PLEASE_SELECT_AN_IFF_8SVX_WAV_SOUND_FILE,
                                                      "Please select an IFF/8SVX/WAV sound file."));
                                                break;
                                            }
                                            sound_lock = Lock(
                                                (CONST_STRPTR)
                                                    candidate->notification_sound_path,
                                                ACCESS_READ);
                                            if (!sound_lock) {
                                                amg_error_set(
                                                    error, AMG_ERR_IO,
                                                    T(MSG_THE_SELECTED_SOUND_FILE_WAS_NOT_FOUND,
                                                      "The selected sound file was not found."));
                                                break;
                                            }
                                            UnLock(sound_lock);
                                        }
                                    }
                                }
                                if (account_index < AMG_MAX_ACCOUNTS) {
                                    if (error->message[0])
                                        set_utf8_string(dialog_status, window,
                                                        error->message);
                                    break;
                                }
                                if (!enabled_count) {
                                    set_string(
                                        dialog_status, window,
                                        T(MSG_AT_LEAST_ONE_ACCOUNT_MUST_BE_ACTIVE,
                                          "At least one account must be active."));
                                    break;
                                }
                                if (ensure_config_drawer(error) != AMG_OK) {
                                    set_utf8_string(dialog_status, window,
                                                    error->message);
                                    break;
                                }

                                for (account_index = 0U;
                                     account_index < AMG_MAX_ACCOUNTS;
                                     ++account_index) {
                                    const char *account_path =
                                        amg_storage_account_path(account_index);
                                    const char *key_path =
                                        amg_storage_persistent_key_path(account_index);
                                    AmgAccount *candidate =
                                        &drafts[account_index];
                                    int settings_changed;
                                    if (deleted_slots[account_index])
                                        continue;
                                    settings_changed =
                                        !account_settings_equal(
                                            candidate,
                                            &gui->account_set->accounts[
                                                account_index]);
                                    if (!settings_changed)
                                        continue;
                                    if (!candidate->enabled &&
                                        !candidate->email[0] &&
                                        !account_file_exists(account_index))
                                        continue;
                                    if (amg_storage_save_account_auto(
                                            account_path, key_path, candidate,
                                            error) != AMG_OK) {
                                        set_utf8_string(
                                            dialog_status, window,
                                            error->message);
                                        break;
                                    }
                                }
                                if (account_index < AMG_MAX_ACCOUNTS)
                                    break;

                                if (memcmp(draft_order, original_order,
                                           sizeof(draft_order)) != 0 &&
                                    amg_storage_save_account_order(
                                        draft_order, error) != AMG_OK) {
                                    set_utf8_string(dialog_status, window,
                                                    error->message);
                                    break;
                                }

                                /* Only remove account files after every
                                 * remaining account and the order file have
                                 * been saved successfully. This keeps Cancel
                                 * and save errors non-destructive. */
                                for (account_index = 0U;
                                     account_index < AMG_MAX_ACCOUNTS;
                                     ++account_index) {
                                    if (!deleted_slots[account_index])
                                        continue;
                                    amg_network_stop(
                                        gui->networks[account_index]);
                                    amg_storage_delete_account_files(
                                        account_index);
                                    delete_account_auxiliary_files(
                                        account_index);
                                }

                                next_active = gui->active_account;
                                if (next_active >= AMG_MAX_ACCOUNTS ||
                                    !drafts[next_active].enabled) {
                                    size_t position;
                                    next_active = 0U;
                                    for (position = 0U;
                                         position < AMG_MAX_ACCOUNTS;
                                         ++position) {
                                        size_t slot = draft_order[position];
                                        if (slot < AMG_MAX_ACCOUNTS &&
                                            drafts[slot].enabled) {
                                            next_active = slot;
                                            break;
                                        }
                                    }
                                }
                                for (account_index = 0U;
                                     account_index < AMG_MAX_ACCOUNTS;
                                     ++account_index) {
                                    amg_network_stop(
                                        gui->networks[account_index]);
                                    amg_account_clear(
                                        &gui->account_set->accounts[
                                            account_index]);
                                    gui->account_set->accounts[account_index] =
                                        drafts[account_index];
                                    drafts[account_index].imap_password = NULL;
                                    drafts[account_index].smtp_password = NULL;
                                    drafts[account_index].refresh_token = NULL;
                                }
                                memcpy(gui->account_set->order, draft_order,
                                       sizeof(draft_order));
                                gui->active_account = next_active;
                                gui->account_set->current = next_active;
                                gui->account =
                                    &gui->account_set->accounts[next_active];
                                gui->network = gui->networks[next_active];
                                gui_reload_account_states(gui);
                                gui_rebuild_account_tabs(gui);
                                for (account_index = 0U;
                                     account_index < AMG_MAX_ACCOUNTS;
                                     ++account_index) {
                                    AmgAccount *saved_account =
                                        &gui->account_set->accounts[
                                            account_index];
                                    if (!network_was_running[account_index] ||
                                        !saved_account->enabled ||
                                        account_is_locked(saved_account))
                                        continue;
                                    if (amg_network_start(
                                            gui->networks[account_index],
                                            saved_account, error) == AMG_OK)
                                        (void)amg_network_request(
                                            gui->networks[account_index],
                                            AMG_NET_CONNECT, 0, NULL,
                                            account_index == next_active
                                                ? NULL : "background",
                                            NULL);
                                }
                                periodic_timer_restart(gui);
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
    DisposeObject(dialog);
    account_config_free_tab_nodes(&account_tabs_list);
    for (account_index = 0U; account_index < AMG_MAX_ACCOUNTS;
         ++account_index)
        amg_account_clear(&drafts[account_index]);
    if (changed)
        status_local(gui, T(MSG_MAIL_ACCOUNTS_SAVED, "Mail accounts saved."));
    return changed;
}

static UWORD about_header_fill_pattern[2] = {0xffffU, 0xffffU};

static void draw_about_banner(AmgGui *gui, struct Window *window,
                              Object *banner_slot)
{
    struct Gadget *gadget;
    LONG banner_top;
    if (!gui || !window || !banner_slot) return;
    gadget = (struct Gadget *)banner_slot;
    banner_top = (LONG)gadget->TopEdge;
    if ((LONG)gadget->Height > GUI_ABOUT_BANNER_HEIGHT)
        banner_top +=
            ((LONG)gadget->Height - GUI_ABOUT_BANNER_HEIGHT) / 2L;
    draw_embedded_banner_at(gui, window,
                            (LONG)gadget->LeftEdge,
                            banner_top,
                            GUI_ABOUT_BANNER_WIDTH,
                            GUI_ABOUT_BANNER_HEIGHT);
}

 void about_dialog(AmgGui *gui)
{
    Object *dialog;
    Object *banner_slot;
    struct Window *window;
    ULONG signal_mask = 0;
    LONG font_height = 8L;
    LONG line_height;
    LONG header_height;
    int done = 0;

    if (!gui || !gui->window || !gui->screen) return;
    if (gui->screen->Font && gui->screen->Font->ta_YSize)
        font_height = (LONG)gui->screen->Font->ta_YSize;
    line_height = font_height + 2L;
    header_height = line_height * 3L;
    if (header_height < GUI_ABOUT_BANNER_HEIGHT)
        header_height = GUI_ABOUT_BANNER_HEIGHT;

    banner_slot = HGroupObject,
        LAYOUT_SpaceOuter, FALSE,
        LAYOUT_SpaceInner, FALSE,
        LAYOUT_FillPen, gui->banner_pens[7],
        LAYOUT_FillPattern,
            (ULONG)(uintptr_t)about_header_fill_pattern,
    EndObject;
    if (!banner_slot) return;

    dialog = WindowObject,
        WA_Title, T(MSG_ABOUT_AMIMAIL, "About AmiMail"),
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
                LAYOUT_FillPen, gui->banner_pens[7],
                LAYOUT_FillPattern,
                    (ULONG)(uintptr_t)about_header_fill_pattern,
                LAYOUT_AddChild, banner_slot,
                CHILD_MinWidth, GUI_ABOUT_BANNER_WIDTH,
                CHILD_MaxWidth, GUI_ABOUT_BANNER_WIDTH,
                CHILD_WeightedWidth, 0,
                CHILD_MinHeight, header_height,
                CHILD_MaxHeight, header_height,
                CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, VGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                    LAYOUT_FillPen, gui->banner_pens[7],
                    LAYOUT_FillPattern,
                        (ULONG)(uintptr_t)about_header_fill_pattern,
                    LAYOUT_AddChild, static_text_label("AmiMail " AMIGMAIL_VERSION),
                    CHILD_MinHeight, line_height,
                    CHILD_MaxHeight, line_height,
                    CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild,
                        static_text_label(T(MSG_IMAP_SMTP_MAIL_CLIENT_FOR_AMIGAOS_3_2, "IMAP/SMTP mail client for AmigaOS 3.2")),
                    CHILD_MinHeight, line_height,
                    CHILD_MaxHeight, line_height,
                    CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild,
                        static_text_label("\251 Andreas St\374rmer"),
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

            /* Native ReAction layout bevel used as a simple horizontal
             * separator between the header and the legal text.  In a
             * vertical group BVS_SBAR_VERT is the horizontal bar style. */
            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
                LAYOUT_BevelStyle, BVS_SBAR_VERT,
            EndObject,
            CHILD_MinHeight, 2,
            CHILD_MaxHeight, 2,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
            EndObject,
            CHILD_MinHeight, font_height / 2L,
            CHILD_MaxHeight, font_height / 2L,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild,
                static_text_label(T(MSG_ICON_BY_MARTIN_MASON_MERZ,
                                    "Icon by Martin 'Mason' Merz")),
            CHILD_MinHeight, line_height,
            CHILD_MaxHeight, line_height,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,
                static_text_label(T(
                    MSG_THANKS_TO_ALL_TESTERS_WHO_HELP_TO_CONTINUOUSLY_IMPROVE_AMIMAIL,
                    "Thanks to all testers who help to continuously improve AmiMAIL.")),
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
                static_text_label(T(MSG_AMIMAIL_IS_AN_INDEPENDENT_NON_COMMERCIAL_HOBBY_PROJECT, "AmiMail is an independent, non-commercial hobby project.")),
            CHILD_MinHeight, line_height,
            CHILD_MaxHeight, line_height,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,
                static_text_label(T(MSG_IT_IS_NOT_AFFILIATED_WITH_ANY_EMAIL_PROVIDER, "It is not affiliated with any email provider and is not developed,")),
            CHILD_MinHeight, line_height,
            CHILD_MaxHeight, line_height,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,
                static_text_label(T(MSG_SUPPORTED_OR_SPONSORED_BY_ANY_PROVIDER, "supported or sponsored by any provider.")),
            CHILD_MinHeight, line_height,
            CHILD_MaxHeight, line_height,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,
                static_text_label(T(MSG_PRODUCT_AND_SERVICE_NAMES_ARE_TRADEMARKS_OF_THEIR, "Product and service names are trademarks of their respective owners.")),
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
    LONG question_height, note_height, button_height;
    LONG outer_width, outer_height, min_outer_width;
    LONG left, top, max_left, max_top;
    struct LayoutLimits limits;
    Object *layout;
    Object *dialog;
    struct Window *window;
    ULONG signal_mask = 0;
    int done = 0;
    int confirmed = 0;

    if (!gui || !ref_window || !gui->screen) return 0;

    /* The compose requester is called from gui_compose.c with no secondary
     * note.  Supply its explanatory line here so all four Yes/No requesters
     * keep using the same measured/centered layout without changing the
     * compose-window code or the centering calculation. */
    if ((!note || !*note) && question &&
        !strcmp(question,
                T(MSG_DO_YOU_WANT_TO_SAVE_THE_DRAFT,
                  "Do you want to save the draft?"))) {
        note = T(MSG_THE_MAIL_CAN_BE_EDITED_LATER,
                 "The mail can be edited later.");
    }

    if (gui->screen->Font && gui->screen->Font->ta_YSize)
        font_height = (LONG)gui->screen->Font->ta_YSize;
    /* Keep the message block to the actual text-line height.  The previous
     * extra vertical padding looked like an additional blank line below the
     * requester text and pushed the Yes/No row too far down. */
    question_height = font_height;
    note_height = font_height;
    button_height = font_height + 8L;

    /* Build and measure the complete requester layout before opening the
     * window.  WPOS_CENTERWINDOW is intentionally not used here: on classic
     * ReAction 3.2 the final layout domain can differ from the provisional
     * window size used for WPOS_CENTERWINDOW, which made short requesters
     * (Trash/Spam/Draft) visibly or permanently offset while the larger
     * delete requester happened to be centered. */
    layout = VGroupObject,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_SpaceInner, FALSE,

        /* Put the message lines directly into the outer group.  The previous
         * nested VGroup introduced an additional vertical layout gap before
         * the Yes/No row on classic ReAction.  Keeping the text objects as
         * direct children removes that apparent blank line while the window
         * is still measured and centered from the final LayoutLimits(). */
        LAYOUT_AddChild, ButtonObject,
            GA_ReadOnly, TRUE,
            GA_Text, question ? question : "",
            BUTTON_BevelStyle, BVS_NONE,
            BUTTON_Transparent, TRUE,
        EndObject,
        CHILD_MinHeight, question_height,
        CHILD_MaxHeight, question_height,
        CHILD_WeightedHeight, 0,

        note && *note ? LAYOUT_AddChild : TAG_IGNORE, ButtonObject,
            GA_ReadOnly, TRUE,
            GA_Text, note && *note ? note : "",
            BUTTON_BevelStyle, BVS_NONE,
            BUTTON_Transparent, TRUE,
        EndObject,
        note && *note ? CHILD_MinHeight : TAG_IGNORE, note_height,
        note && *note ? CHILD_MaxHeight : TAG_IGNORE, note_height,
        note && *note ? CHILD_WeightedHeight : TAG_IGNORE, 0,

        LAYOUT_AddChild, HGroupObject,
            LAYOUT_SpaceOuter, FALSE,
            LAYOUT_SpaceInner, TRUE,
            LAYOUT_EvenSize, TRUE,
            LAYOUT_AddChild, ButtonObject,
                GA_ID, GID_CONFIRM_YES,
                GA_RelVerify, TRUE,
                GA_Text, T(MSG_YES, "_Yes"),
            EndObject,
            LAYOUT_AddChild, ButtonObject,
                GA_ID, GID_CONFIRM_NO,
                GA_RelVerify, TRUE,
                GA_Text, T(MSG_NO, "_No"),
            EndObject,
        EndObject,
        CHILD_MinHeight, button_height,
        CHILD_MaxHeight, button_height,
        CHILD_WeightedHeight, 0,
    EndObject;
    if (!layout) return 0;

    memset(&limits, 0, sizeof(limits));
    LayoutLimits((struct Gadget *)layout, &limits,
                 gui->screen->RastPort.Font, gui->screen);

    /* LayoutLimits() reports the inner layout domain without window borders.
     * The reference window uses the same screen/title style, so its border
     * sizes are a safe classic-ReAction allowance.  Keep the historical
     * caller width as a minimum, but enlarge it before centering if the
     * translated text actually requires more room. */
    outer_width = width;
    min_outer_width = (LONG)limits.MinWidth +
                      (LONG)ref_window->BorderLeft +
                      (LONG)ref_window->BorderRight;
    if (outer_width < min_outer_width) outer_width = min_outer_width;
    outer_height = (LONG)limits.MinHeight +
                   (LONG)ref_window->BorderTop +
                   (LONG)ref_window->BorderBottom;
    if (outer_height < 1L) outer_height = 1L;

    if (outer_width > (LONG)gui->screen->Width)
        outer_width = (LONG)gui->screen->Width;
    if (outer_height > (LONG)gui->screen->Height)
        outer_height = (LONG)gui->screen->Height;

    left = (LONG)ref_window->LeftEdge +
           ((LONG)ref_window->Width - outer_width) / 2L;
    top = (LONG)ref_window->TopEdge +
          ((LONG)ref_window->Height - outer_height) / 2L;
    max_left = (LONG)gui->screen->Width - outer_width;
    max_top = (LONG)gui->screen->Height - outer_height;
    if (max_left < 0L) max_left = 0L;
    if (max_top < 0L) max_top = 0L;
    if (left < 0L) left = 0L;
    if (top < 0L) top = 0L;
    if (left > max_left) left = max_left;
    if (top > max_top) top = max_top;

    dialog = WindowObject,
        WA_Title, "AmiMail",
        WA_Left, left,
        WA_Top, top,
        WA_Width, outer_width,
        WA_Height, outer_height,
        WA_MinWidth, outer_width,
        WA_MaxWidth, outer_width,
        WA_MinHeight, outer_height,
        WA_MaxHeight, outer_height,
        WA_PubScreen, gui->screen,
        WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                  WFLG_ACTIVATE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_RAWKEY,
        WINDOW_Layout, (ULONG)(uintptr_t)layout,
    EndWindow;
    if (!dialog) {
        DisposeObject(layout);
        return 0;
    }

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
        gui, T(MSG_REALLY_DELETE_MAIL, "Really delete mail?"),
        T(MSG_THIS_ACTION_CANNOT_BE_UNDONE, "This action cannot be undone."), 310L);
}

 int confirm_empty_trash_dialog(AmgGui *gui)
{
    return confirm_question_dialog(
        gui, T(MSG_REALLY_EMPTY_TRASH, "Really empty Trash?"),
        T(MSG_THIS_ACTION_CANNOT_BE_UNDONE, "This action cannot be undone."), 310L);
}

 int confirm_empty_spam_dialog(AmgGui *gui)
{
    return confirm_question_dialog(
        gui, T(MSG_REALLY_EMPTY_SPAM, "Really empty Spam?"),
        T(MSG_THIS_ACTION_CANNOT_BE_UNDONE, "This action cannot be undone."), 310L);
}

#endif /* AMIGMAIL_AMIGA */
