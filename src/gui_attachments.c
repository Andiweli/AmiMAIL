/* Selective attachment export. Never exposes an editable mail preview. */
#include "gui_internal.h"
#include "fileio.h"
#include "i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if AMIGMAIL_AMIGA
#include <clib/alib_protos.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/listbrowser.h>
#include <gadgets/string.h>
#include <libraries/asl.h>
#include <dos/dosextens.h>
#include <proto/asl.h>
#include <proto/button.h>
#include <proto/layout.h>
#include <proto/string.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/listbrowser.h>
#include <proto/window.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <utility/tagitem.h>
#ifdef NewObject
#undef NewObject
#endif
#define T(id, en) amg_tr((id), (en))
enum { ATT_LIST = 900, ATT_ALL, ATT_NONE, ATT_REGULAR, ATT_GRAPHICS, ATT_SAVE, ATT_CANCEL };

#define ACCOUNT_DRAWER "ENVARC:AmiMail"
#define ATTACHMENT_DRAWER_STATE ACCOUNT_DRAWER "/attachment-drawer.cfg"
#define ATTACHMENT_DRAWER_TEMP ATTACHMENT_DRAWER_STATE ".new"
#define ATTACHMENT_DRAWER_HEADER "AMIMAIL-ATTACHMENT-DRAWER-1\n"
static char attachment_last_drawer[COMPOSE_PATH_MAX];
static int attachment_drawer_loaded, attachment_drawer_dirty;

/* Preference I/O must never open an Insert Disk requester. This code runs
 * only in the GUI process, and restores the previous requester window. */
static APTR attachment_suppress_dos_requesters(void)
{
    struct Process *process = (struct Process *)FindTask(NULL);
    APTR previous = process->pr_WindowPtr;
    process->pr_WindowPtr = (APTR)(intptr_t)-1;
    return previous;
}

static void attachment_restore_dos_requesters(APTR previous)
{
    ((struct Process *)FindTask(NULL))->pr_WindowPtr = previous;
}

static int attachment_drawer_path(const char *path, char *absolute)
{
    BPTR lock;
    struct FileInfoBlock *info = NULL;
    APTR previous;
    int valid = 0;
    if (!path || !*path || strlen(path) >= COMPOSE_PATH_MAX) return 0;
    previous = attachment_suppress_dos_requesters();
    lock = Lock((CONST_STRPTR)path, ACCESS_READ);
    if (lock) {
        info = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
        if (info && Examine(lock, info) && info->fib_DirEntryType > 0) {
            valid = 1;
            if (absolute) {
                if (strchr(path, ':')) memcpy(absolute, path, strlen(path) + 1U);
                else valid = NameFromLock(lock, (STRPTR)absolute, COMPOSE_PATH_MAX) != 0;
            }
        }
        if (info) FreeDosObject(DOS_FIB, info);
        UnLock(lock);
    }
    attachment_restore_dos_requesters(previous);
    return valid;
}

static const char *attachment_initial_drawer(void)
{
    if (!attachment_drawer_loaded) {
        char record[sizeof(ATTACHMENT_DRAWER_HEADER) + COMPOSE_PATH_MAX];
        size_t header = sizeof(ATTACHMENT_DRAWER_HEADER) - 1U, size = 0U;
        FILE *file = NULL;
        APTR previous = attachment_suppress_dos_requesters();
        attachment_drawer_loaded = 1;
        if (amg_file_recover(ATTACHMENT_DRAWER_STATE) == AMG_OK)
            file = fopen(ATTACHMENT_DRAWER_STATE, "rb");
        if (file) {
            size = fread(record, 1U, sizeof(record), file);
            if (ferror(file)) size = 0U;
            if (fclose(file) != 0) size = 0U;
        }
        attachment_restore_dos_requesters(previous);
        if (size > header + 1U && size <= header + COMPOSE_PATH_MAX &&
            !memcmp(record, ATTACHMENT_DRAWER_HEADER, header) &&
            memchr(record + header, 0, size - header) == record + size - 1U &&
            strchr(record + header, ':'))
            memcpy(attachment_last_drawer, record + header, size - header);
    }
    return attachment_drawer_path(attachment_last_drawer, NULL)
        ? attachment_last_drawer : "";
}

static void attachment_remember_drawer(const char *drawer)
{
    char absolute[COMPOSE_PATH_MAX];
    APTR previous;
    BPTR lock;
    FILE *file = NULL;
    int result = AMG_ERR_IO;
    if (!attachment_drawer_path(drawer, absolute)) return;
    if (strcmp(attachment_last_drawer, absolute)) {
        memcpy(attachment_last_drawer, absolute, strlen(absolute) + 1U);
        attachment_drawer_dirty = 1;
    }
    if (!attachment_drawer_dirty) return;
    previous = attachment_suppress_dos_requesters();
    lock = Lock((CONST_STRPTR)ACCOUNT_DRAWER, ACCESS_READ);
    if (!lock) lock = CreateDir((CONST_STRPTR)ACCOUNT_DRAWER);
    if (lock) {
        UnLock(lock);
        file = fopen(ATTACHMENT_DRAWER_TEMP, "wb");
    }
    if (file) {
        size_t length = strlen(attachment_last_drawer) + 1U;
        if (fwrite(ATTACHMENT_DRAWER_HEADER, 1U,
                   sizeof(ATTACHMENT_DRAWER_HEADER) - 1U, file) ==
                   sizeof(ATTACHMENT_DRAWER_HEADER) - 1U &&
            fwrite(attachment_last_drawer, 1U, length, file) == length)
            result = AMG_OK;
        if (fclose(file) != 0) result = AMG_ERR_IO;
        if (result == AMG_OK)
            result = amg_file_replace(ATTACHMENT_DRAWER_TEMP, ATTACHMENT_DRAWER_STATE);
        if (result != AMG_OK) (void)DeleteFile((CONST_STRPTR)ATTACHMENT_DRAWER_TEMP);
    }
    attachment_restore_dos_requesters(previous);
    if (result == AMG_OK) attachment_drawer_dirty = 0;
    /* Preference failure does not turn a successful export into a failure.
     * Keep the new session default and retry on the next successful export. */
}

static Object *choice_button(ULONG id, const char *text)
{
    struct TagItem tags[] = {
        { GA_ID, id }, { GA_RelVerify, TRUE },
        { GA_Text, (ULONG)(uintptr_t)text }, { TAG_DONE, 0UL }
    };
    return NewObjectA(BUTTON_GetClass(), NULL, tags);
}

static Object *button_row(Object **buttons, size_t count)
{
    /* Each child stays owned by choose_attachments(), even if OM_NEW fails
     * after accepting part of the tag list. Dispose parent groups first. */
    struct TagItem tags[3U + 4U * 3U];
    size_t i, used = 0U;
    if (count > 4U) return NULL;
    tags[used].ti_Tag = LAYOUT_Orientation; tags[used++].ti_Data = LAYOUT_ORIENT_HORIZ;
    tags[used].ti_Tag = LAYOUT_SpaceInner; tags[used++].ti_Data = TRUE;
    for (i = 0U; i < count; ++i) {
        tags[used].ti_Tag = LAYOUT_AddChild; tags[used++].ti_Data = (ULONG)(uintptr_t)buttons[i];
        tags[used].ti_Tag = CHILD_WeightedWidth; tags[used++].ti_Data = 100UL;
        tags[used].ti_Tag = CHILD_NoDispose; tags[used++].ti_Data = TRUE;
    }
    tags[used].ti_Tag = TAG_DONE; tags[used].ti_Data = 0UL;
    return NewObjectA(LAYOUT_GetClass(), NULL, tags);
}

static void select_category(AmgMailFile *mail, struct List *list,
                             struct Gadget *gadget, struct Window *window,
                             ULONG action)
{
    struct Node *node;
    ULONG top = 0UL;
    GetAttr(LISTBROWSER_Top, (Object *)gadget, &top);
    detach_listbrowser(gadget, window);
    for (node = list->lh_Head; node && node->ln_Succ; node = node->ln_Succ) {
        ULONG index = 0UL;
        const AmgMailFilePart *part;
        int selected;
        GetListBrowserNodeAttrs(node, LBNA_UserData, (ULONG)(uintptr_t)&index, TAG_DONE);
        part = amg_mailfile_attachment(mail, (size_t)index);
        selected = part && (action == ATT_ALL ||
            (action == ATT_REGULAR && !part->embedded) ||
            (action == ATT_GRAPHICS && part->embedded));
        SetListBrowserNodeAttrs(node, LBNA_Selected, selected ? TRUE : FALSE, TAG_DONE);
    }
    SetGadgetAttrs(gadget, window, NULL,
                   LISTBROWSER_Labels, (ULONG)(uintptr_t)list,
                   LISTBROWSER_Top, top, TAG_DONE);
    RefreshGList(gadget, window, NULL, 1);
}

static int choose_attachments(AmgGui *gui, unsigned char *selected)
{
    AmgMailFile *mail = gui->current_mail_file;
    struct List list;
    struct ColumnInfo *columns = NULL;
    struct Gadget *browser = NULL, *status = NULL;
    struct Window *window = NULL;
    struct Requester blocker;
    Object *dialog = NULL, *layout = NULL, *row1 = NULL, *row2 = NULL;
    Object *buttons[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
    size_t i;
    ULONG mask = 0UL;
    /* Negative results identify the failed setup step; only actual memory
     * allocation failures outside this dialog use "Not enough memory". */
    int result = -1, done = 0, blocked = 0;
    NewList(&list);
    columns = AllocLBColumnInfo(3,
        LBCIA_Column, 0, LBCIA_Title, (ULONG)(uintptr_t)T(MSG_ATTACHMENT_NAME, "File name"),
        LBCIA_Weight, 55U,
        LBCIA_Column, 1, LBCIA_Title, (ULONG)(uintptr_t)T(MSG_ATTACHMENT_KIND, "Type"),
        LBCIA_Weight, 25U,
        LBCIA_Column, 2, LBCIA_Title, (ULONG)(uintptr_t)T(MSG_ATTACHMENT_ENCODED_SIZE, "Encoded size"),
        LBCIA_Weight, 20U, TAG_DONE);
    if (!columns) goto cleanup;
    for (i = 0U; i < mail->attachment_count; ++i) {
        const AmgMailFilePart *part = amg_mailfile_attachment(mail, i);
        char name[COMPOSE_NAME_MAX], size[32];
        struct Node *node;
        result = -2;
        if (!part || part->end < part->body) goto cleanup;
        sanitize_attachment_name(part->name_utf8, name, sizeof(name));
        snprintf(size, sizeof(size), "%lu KB",
                 (unsigned long)((part->end - part->body + 1023U) / 1024U));
        node = AllocListBrowserNode(3,
            LBNA_UserData, (ULONG)i,
            LBNA_Selected, part->embedded ? FALSE : TRUE,
            LBNA_Column, 0, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)(uintptr_t)name,
            LBNA_Column, 1, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)(uintptr_t)
                (part->embedded ? T(MSG_EMBEDDED_GRAPHIC, "Embedded graphic") :
                                  T(MSG_REGULAR_ATTACHMENT, "Attachment")),
            LBNA_Column, 2, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)(uintptr_t)size,
            TAG_DONE);
        if (!node) goto cleanup;
        AddTail(&list, node);
    }
    {
        /* Use the already-open class libraries directly. Do not depend on
         * a ReAction class also being registered under a public name. */
        struct TagItem tags[] = {
            { GA_ID, ATT_LIST }, { GA_RelVerify, TRUE },
            { LISTBROWSER_Labels, (ULONG)(uintptr_t)&list },
            { LISTBROWSER_ColumnInfo, (ULONG)(uintptr_t)columns },
            { LISTBROWSER_ColumnTitles, TRUE },
            { LISTBROWSER_MultiSelect, TRUE },
            { LISTBROWSER_ShowSelected, TRUE },
            { LISTBROWSER_VerticalProp, TRUE },
            { LISTBROWSER_AutoWheel, TRUE },
            { LISTBROWSER_MinVisible, 8U }, { TAG_DONE, 0UL }
        };
        result = -3;
        browser = (struct Gadget *)NewObjectA(LISTBROWSER_GetClass(), NULL, tags);
        if (!browser) goto cleanup;
    }
    {
        struct TagItem tags[] = {
            { GA_ReadOnly, TRUE }, { STRINGA_MaxChars, 192U },
            { STRINGA_TextVal, (ULONG)(uintptr_t)T(MSG_ATTACHMENT_SELECT_HINT,
                "Select files. Existing files are never overwritten.") },
            { TAG_DONE, 0UL }
        };
        result = -4;
        status = (struct Gadget *)NewObjectA(STRING_GetClass(), NULL, tags);
        if (!status) goto cleanup;
    }
    result = -5;
    buttons[0] = choice_button(ATT_ALL, T(MSG_SELECT_ALL, "Select All"));
    buttons[1] = choice_button(ATT_NONE, T(MSG_SELECT_NONE, "Select none"));
    buttons[2] = choice_button(ATT_REGULAR, T(MSG_SELECT_REGULAR_ATTACHMENTS, "Attachments only"));
    buttons[3] = choice_button(ATT_GRAPHICS, T(MSG_SELECT_EMBEDDED_GRAPHICS, "Graphics only"));
    buttons[4] = choice_button(ATT_SAVE, T(MSG_SAVE_SELECTED, "Save selected"));
    buttons[5] = choice_button(ATT_CANCEL, T(MSG_CANCEL, "_Cancel"));
    for (i = 0U; i < 6U; ++i) if (!buttons[i]) goto cleanup;
    result = -6;
    row1 = button_row(buttons, 4U);
    if (!row1) goto cleanup;
    result = -7;
    row2 = button_row(buttons + 4U, 2U);
    if (!row2) goto cleanup;
    {
        struct TagItem tags[] = {
            { LAYOUT_Orientation, LAYOUT_ORIENT_VERT },
            { LAYOUT_SpaceOuter, TRUE }, { LAYOUT_SpaceInner, TRUE },
            { LAYOUT_AddChild, (ULONG)(uintptr_t)browser },
            { CHILD_WeightedHeight, 100U }, { CHILD_NoDispose, TRUE },
            { LAYOUT_AddChild, (ULONG)(uintptr_t)row1 },
            { CHILD_WeightedHeight, 0U }, { CHILD_NoDispose, TRUE },
            { LAYOUT_AddChild, (ULONG)(uintptr_t)status },
            { CHILD_WeightedHeight, 0U }, { CHILD_NoDispose, TRUE },
            { LAYOUT_AddChild, (ULONG)(uintptr_t)row2 },
            { CHILD_WeightedHeight, 0U }, { CHILD_NoDispose, TRUE },
            { TAG_DONE, 0UL }
        };
        result = -8;
        layout = NewObjectA(LAYOUT_GetClass(), NULL, tags);
        if (!layout) goto cleanup;
    }
    result = -9;
    dialog = gui_aux_window(gui, layout, T(MSG_ATTACHMENT_SELECTION, "Select attachments to save"),
        580L, 240L, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_ACTIVATE);
    if (!dialog) goto cleanup;
    layout = NULL; /* now owned by window.class */
    result = -10;
    window = RA_OpenWindow(dialog);
    if (!window) goto cleanup;
    result = -11;
    InitRequester(&blocker);
    blocked = Request(&blocker, gui->window);
    if (!blocked) goto cleanup;
    result = 0;
    GetAttr(WINDOW_SigMask, dialog, &mask);
    while (!done) {
        ULONG signals = Wait(mask | SIGBREAKF_CTRL_C);
        ULONG input;
        UWORD code = 0;
        if (signals & SIGBREAKF_CTRL_C) { done = 1; break; }
        while ((input = RA_HandleInput(dialog, &code)) != WMHI_LASTMSG) {
            if ((input & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW ||
                ((input & WMHI_CLASSMASK) == WMHI_RAWKEY && rawkey_is_cancel(input))) {
                done = 1; break;
            }
            if ((input & WMHI_CLASSMASK) == WMHI_GADGETUP) {
                ULONG action = input & WMHI_GADGETMASK;
                if (action == ATT_CANCEL) { done = 1; break; }
                if (action >= ATT_ALL && action <= ATT_GRAPHICS)
                    select_category(mail, &list, browser, window, action);
                if (action == ATT_SAVE) {
                    struct Node *node;
                    size_t count = 0U;
                    memset(selected, 0, mail->attachment_count);
                    for (node = list.lh_Head; node && node->ln_Succ; node = node->ln_Succ) {
                        ULONG index = 0UL, mark = FALSE;
                        GetListBrowserNodeAttrs(node,
                            LBNA_UserData, (ULONG)(uintptr_t)&index,
                            LBNA_Selected, (ULONG)(uintptr_t)&mark, TAG_DONE);
                        if (mark && index < mail->attachment_count) {
                            selected[index] = 1U; ++count;
                        }
                    }
                    if (count) { result = 1; done = 1; break; }
                    set_string(status, window, T(MSG_PLEASE_SELECT_AN_ATTACHMENT_FIRST,
                                                 "Please select an attachment first."));
                }
            }
        }
    }
cleanup:
    if (dialog) DisposeObject(dialog);
    else if (layout) DisposeObject(layout);
    /* CHILD_NoDispose makes ownership explicit on every failure path. */
    if (row1) DisposeObject(row1);
    if (row2) DisposeObject(row2);
    if (browser) DisposeObject((Object *)browser);
    if (status) DisposeObject((Object *)status);
    if (blocked) EndRequest(&blocker, gui->window);
    for (i = 0U; i < 6U; ++i) if (buttons[i]) DisposeObject(buttons[i]);
    FreeListBrowserList(&list);
    if (columns) FreeLBColumnInfo(columns);
    return result;
}

void save_current_attachments(AmgGui *gui)
{
    struct FileRequester *request;
    unsigned char *selected;
    char drawer[COMPOSE_PATH_MAX], message[192];
    size_t i, count, saved = 0U, failed = 0U;
    int cancelled = 0, choice;
    GuiFileProgress *progress;
    AmgError error;
    if (!gui || !gui->current_attachment_count) {
        status_local(gui, T(MSG_THIS_MESSAGE_HAS_NO_SAVABLE_ATTACHMENTS,
                            "This message has no savable attachments."));
        return;
    }
    memset(&error, 0, sizeof(error));
    if (gui_prepare_attachment_file(gui, &error) != AMG_OK) {
        status_utf8(gui, error.message); return;
    }
    count = gui->current_mail_file ? gui->current_mail_file->attachment_count : 0U;
    if (!count) {
        status_local(gui, T(MSG_THIS_MESSAGE_HAS_NO_SAVABLE_ATTACHMENTS,
                            "This message has no savable attachments."));
        return;
    }
    selected = (unsigned char *)calloc(count, 1U);
    if (!selected) { status_local(gui, T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory.")); return; }
    choice = choose_attachments(gui, selected);
    if (choice <= 0) {
        if (choice < 0) {
            amg_tr_snprintf(message, sizeof(message), MSG_VALUE_FAILED_CODE_VALUE,
                "%s failed (code %d).",
                T(MSG_ATTACHMENT_SELECTION, "Select attachments to save"), -choice);
            status_local(gui, message);
        }
        free(selected); return;
    }
    request = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (ULONG)(uintptr_t)T(MSG_SAVE_ATTACHMENTS, "Save attachments"),
        ASLFR_Window, (ULONG)(uintptr_t)gui->window,
        ASLFR_InitialDrawer, (ULONG)(uintptr_t)attachment_initial_drawer(),
        ASLFR_SleepWindow, TRUE, ASLFR_DrawersOnly, TRUE, ASLFR_RejectIcons, TRUE, TAG_DONE);
    if (!request) {
        status_local(gui, T(MSG_FILE_REQUESTER_COULD_NOT_BE_OPENED,
                            "File requester could not be opened."));
        free(selected); return;
    }
    if (!AslRequest(request, NULL)) { FreeAslRequest(request); free(selected); return; }
    if (request->rf_Dir && strlen((const char *)request->rf_Dir) >= sizeof(drawer)) {
        FreeAslRequest(request); free(selected);
        status_local(gui, T(MSG_FILE_PATH_IS_TOO_LONG, "File path is too long."));
        return;
    }
    snprintf(drawer, sizeof(drawer), "%s", request->rf_Dir ? (const char *)request->rf_Dir : "");
    FreeAslRequest(request);
    if (!drawer[0]) { free(selected); return; }
    progress = gui_file_progress_open(gui, T(MSG_SAVE_SELECTED, "Save selected"));
    if (!progress) {
        amg_tr_snprintf(message, sizeof(message), MSG_VALUE_FAILED_CODE_VALUE,
            "%s failed (code %d).", T(MSG_SAVE_SELECTED, "Save selected"), 12);
        status_local(gui, message);
        free(selected); return;
    }
    for (i = 0U; i < count; ++i) {
        const AmgMailFilePart *part;
        char name[COMPOSE_NAME_MAX], path[AMG_SPOOL_PATH_MAX];
        int result;
        if (!selected[i]) continue;
        part = amg_mailfile_attachment(gui->current_mail_file, i);
        if (!part) { ++failed; continue; }
        sanitize_attachment_name(part->name_utf8, name, sizeof(name));
        memset(&error, 0, sizeof(error));
        gui_file_progress_item(progress, name);
        result = amg_mailfile_save_attachment(gui->current_mail_file, i, drawer,
            name, path, gui_file_progress_callback(progress), &error);
        if (result == AMG_ERR_CANCELLED) { cancelled = 1; break; }
        if (result == AMG_OK) ++saved;
        else ++failed;
    }
    gui_file_progress_close(progress);
    free(selected);
    if (saved) attachment_remember_drawer(drawer);
    if (cancelled)
        amg_tr_snprintf(message, sizeof(message), MSG_ATTACHMENT_EXPORT_CANCELLED,
                        "Cancelled; %lu file(s) already saved.", (unsigned long)saved);
    else
        amg_tr_snprintf(message, sizeof(message), MSG_ATTACHMENT_EXPORT_RESULT,
                        "%lu saved, %lu failed.", (unsigned long)saved, (unsigned long)failed);
    status_local(gui, message);
}
#endif
