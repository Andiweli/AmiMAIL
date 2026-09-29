/* Context-bound progress in the main status strip; no progress windows. */
#include "gui_internal.h"
#include "i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if AMIGMAIL_AMIGA
#include <clib/alib_protos.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/string.h>
#include <gadgets/fuelgauge.h>
#include <proto/button.h>
#include <proto/string.h>
#include <proto/fuelgauge.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <proto/window.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <utility/tagitem.h>
#define T(id, en) amg_tr((id), (en))

extern struct Library *FuelGaugeBase;

struct GuiFileProgress {
    AmgGui *gui;
    AmgMailFile *mail;
    struct Window *window;
    size_t account;
    unsigned long uid;
    char mailbox[512];
    AmgTransfer callback;
    char title[112];
    struct Gadget *locked[8];
    ULONG was_disabled[8];
    size_t lock_count;
    int cancelled;
};

/* On success window.class owns layout. On failure ownership stays with the
 * caller. Measure before opening; never move a visible window afterwards. */
Object *gui_aux_window(AmgGui *gui, Object *layout, const char *title,
                       LONG width, LONG height, ULONG flags)
{
    struct LayoutLimits limits;
    struct Window *ref;
    LONG left, top, border_x = 8L, border_y = 20L;
    if (!gui || !gui->screen || !layout) return NULL;
    ref = gui->window;
    if (ref) {
        border_x = (LONG)ref->BorderLeft + ref->BorderRight;
        border_y = (LONG)ref->BorderTop + ref->BorderBottom;
    }
    memset(&limits, 0, sizeof(limits));
    LayoutLimits((struct Gadget *)layout, &limits,
                 gui->screen->RastPort.Font, gui->screen);
    if (width < (LONG)limits.MinWidth) width = (LONG)limits.MinWidth;
    if (height < (LONG)limits.MinHeight) height = (LONG)limits.MinHeight;
    width += border_x; height += border_y;
    left = ref ? ref->LeftEdge + ((LONG)ref->Width - width) / 2L :
                 ((LONG)gui->screen->Width - width) / 2L;
    top = ref ? ref->TopEdge + ((LONG)ref->Height - height) / 2L :
                ((LONG)gui->screen->Height - height) / 2L;
    if (left + width > (LONG)gui->screen->Width)
        left = (LONG)gui->screen->Width - width;
    if (top + height > (LONG)gui->screen->Height)
        top = (LONG)gui->screen->Height - height;
    if (left < 0L) left = 0L;
    if (top < 0L) top = 0L;
    {
        Object *object;
        struct TagItem tags[] = {
            { WA_Title, (ULONG)(uintptr_t)title },
            { WA_Left, (ULONG)left }, { WA_Top, (ULONG)top },
            { WA_Width, (ULONG)width }, { WA_Height, (ULONG)height },
            { WA_PubScreen, (ULONG)(uintptr_t)gui->screen },
            { WA_Flags, flags },
            { WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_RAWKEY },
            { TAG_DONE, 0UL }
        };
        /* WINDOW_GetClass() is valid after open_classes(). No public class
         * registration or varargs constructor is needed here. Attach the
         * layout only after construction succeeds, keeping failure ownership
         * with the caller even if window.class aborts its OM_NEW method. */
        object = NewObjectA(WINDOW_GetClass(), NULL, tags);
        if (object)
            SetAttrs(object, WINDOW_Layout, (ULONG)(uintptr_t)layout, TAG_DONE);
        return object;
    }
}

/* Divide without overflowing done*100 on a 32-bit Amiga (or size_t on the
 * host). Each threshold is ceil(total*p/100); the binary search is 7 steps. */
static unsigned int progress_percent(size_t done, size_t total)
{
    unsigned int low = 0U, high = 100U;
    size_t quotient, remainder;
    if (!total) return 0U;
    if (done >= total) return 100U;
    quotient = total / 100U;
    remainder = total % 100U;
    while (low < high) {
        unsigned int middle = (low + high + 1U) / 2U;
        size_t threshold = quotient * middle +
            (remainder * middle + 99U) / 100U;
        if (done >= threshold) low = middle;
        else high = middle - 1U;
    }
    return low;
}

Object *gui_transfer_create_gauge(AmgGui *gui)
{
    Object *gauge = NULL;
    if (!gui) return NULL;
    gui->transfer_native_gauge = 0;
    if (FuelGaugeBase) {
        /* Use the classic OS3 orientation constant and the class pointer
         * from the opened library, not a public class-name lookup. */
        struct TagItem tags[] = {
            { GA_ReadOnly, TRUE },
            { FUELGAUGE_Orientation, FGORIENT_HORIZ },
            { FUELGAUGE_Min, 0UL }, { FUELGAUGE_Max, 100UL },
            { FUELGAUGE_Level, 0UL }, { FUELGAUGE_Percent, FALSE },
            { FUELGAUGE_Ticks, 0UL }, { FUELGAUGE_ShortTicks, 0UL },
            { TAG_DONE, 0UL }
        };
        gauge = NewObjectA(FUELGAUGE_GetClass(), NULL, tags);
        if (gauge) gui->transfer_native_gauge = 1;
    }
    if (!gauge) {
        struct TagItem tags[] = {
            { GA_ReadOnly, TRUE }, { GA_Text, (ULONG)(uintptr_t)"" },
            { BUTTON_DomainString, (ULONG)(uintptr_t)"100%" },
            { BUTTON_TextPadding, FALSE },
            { BUTTON_Justification, BCJ_CENTER }, { TAG_DONE, 0UL }
        };
        gauge = NewObjectA(BUTTON_GetClass(), NULL, tags);
    }
    return gauge;
}

/* Query the actual status String gadget's font/theme-dependent domain, then
 * give all three children exactly that height. In particular, do not allow
 * fuelgauge.gadget's larger nominal domain or the X button's padding to make
 * the progress region taller than the neighbouring status field.
 * No geometry is changed after opening or on individual progress updates. */
Object *gui_transfer_create_status_row(AmgGui *gui)
{
    Object *status = NULL, *gauge = NULL, *cancel = NULL, *row = NULL;
    struct LayoutLimits limits;
    ULONG height, cancel_width = GUI_SCROLLBAR_WIDTH;
    struct TagItem status_tags[] = {
        { GA_ID, GID_STATUS }, { GA_ReadOnly, TRUE },
        { STRINGA_TextVal, (ULONG)(uintptr_t)T(MSG_READY, "Ready") },
        { TAG_DONE, 0UL }
    };
    struct TagItem cancel_tags[] = {
        { GA_ID, GID_TRANSFER_CANCEL }, { GA_RelVerify, TRUE },
        { GA_Disabled, TRUE }, { GA_Text, (ULONG)(uintptr_t)"X" },
        { BUTTON_TextPadding, FALSE }, { TAG_DONE, 0UL }
    };
    if (!gui || !gui->screen) return NULL;
    gui->status_gadget = NULL;
    gui->transfer_gadget = gui->transfer_cancel_gadget = NULL;
    status = NewObjectA(STRING_GetClass(), NULL, status_tags);
    if (!status) goto failed;
    memset(&limits, 0, sizeof(limits));
    LayoutLimits((struct Gadget *)status, &limits,
                 gui->screen->RastPort.Font, gui->screen);
    height = limits.MinHeight;
    if (!height)
        height = (gui->screen->RastPort.TxHeight
                  ? (ULONG)gui->screen->RastPort.TxHeight : 8UL) + 6UL;
    /* The preview scroller directly above is a zero-weight layout child:
     * its natural minimum domain is its displayed width. Use that same
     * font/theme-dependent width, not a hard-coded 18-pixel X button.
     * Fix both limits so extra horizontal space goes to the status/gauge.
     * Query before opening, exactly like the common status-row height. */
    if (gui->preview_scroller) {
        memset(&limits, 0, sizeof(limits));
        LayoutLimits(gui->preview_scroller, &limits,
                     gui->screen->RastPort.Font, gui->screen);
        if (limits.MinWidth > 0U)
            cancel_width = (ULONG)limits.MinWidth;
        else if (gui->preview_scroller->Width > 0)
            cancel_width = (ULONG)gui->preview_scroller->Width;
    }
    gauge = gui_transfer_create_gauge(gui);
    if (!gauge) goto failed;
    cancel = NewObjectA(BUTTON_GetClass(), NULL, cancel_tags);
    if (!cancel) goto failed;
    {
        struct TagItem tags[] = {
            { LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ },
            { LAYOUT_SpaceOuter, FALSE }, { LAYOUT_SpaceInner, TRUE },
            { LAYOUT_AddChild, (ULONG)(uintptr_t)status },
            { CHILD_NoDispose, TRUE },
            { CHILD_MinHeight, height }, { CHILD_MaxHeight, height },
            { CHILD_WeightedHeight, 0UL }, { CHILD_WeightedWidth, 80UL },
            { LAYOUT_AddChild, (ULONG)(uintptr_t)gauge },
            { CHILD_NoDispose, TRUE },
            { CHILD_MinHeight, height }, { CHILD_MaxHeight, height },
            { CHILD_WeightedHeight, 0UL },
            { CHILD_MinWidth, 104UL }, { CHILD_WeightedWidth, 20UL },
            { LAYOUT_AddChild, (ULONG)(uintptr_t)cancel },
            { CHILD_NoDispose, TRUE },
            { CHILD_MinHeight, height }, { CHILD_MaxHeight, height },
            { CHILD_WeightedHeight, 0UL },
            { CHILD_MinWidth, cancel_width },
            { CHILD_MaxWidth, cancel_width }, { CHILD_WeightedWidth, 0UL },
            { TAG_DONE, 0UL }
        };
        row = NewObjectA(LAYOUT_GetClass(), NULL, tags);
    }
    if (!row) goto failed;
    /* Children remain caller-owned even if the constructor fails part-way.
     * Once construction has succeeded, transfer their ownership to the row.
     * The row is not yet attached to a window. */
    SetGadgetAttrs((struct Gadget *)row, NULL, NULL,
        LAYOUT_ModifyChild, (ULONG)(uintptr_t)status, CHILD_NoDispose, FALSE,
        LAYOUT_ModifyChild, (ULONG)(uintptr_t)gauge, CHILD_NoDispose, FALSE,
        LAYOUT_ModifyChild, (ULONG)(uintptr_t)cancel, CHILD_NoDispose, FALSE,
        TAG_DONE);
    gui->status_gadget = (struct Gadget *)status;
    gui->transfer_gadget = (struct Gadget *)gauge;
    gui->transfer_cancel_gadget = (struct Gadget *)cancel;
    return row;
failed:
    if (cancel) DisposeObject(cancel);
    if (gauge) DisposeObject(gauge);
    if (status) DisposeObject(status);
    gui->transfer_native_gauge = 0;
    return NULL;
}

const char *gui_transfer_mailbox(const AmgGui *gui)
{
    size_t i;
    const char *current;
    if (!gui) return "";
    current = gui->current_mailbox_utf8;
    if (!*current) return "";
    for (i = 0U; i < gui->label_count && i < AMIGMAIL_MAX_LABELS; ++i) {
        const GuiLabel *label = &gui->labels[i];
        if (!strcmp(current, label->mailbox_utf8) ||
            (label->server_mailbox_utf8[0] &&
             !strcmp(current, label->server_mailbox_utf8)))
            return label->server_mailbox_utf8[0]
                ? label->server_mailbox_utf8 : current;
    }
    return current;
}

int gui_transfer_mailbox_matches(const AmgGui *gui, const char *mailbox)
{
    const char *current;
    size_t i;
    if (!gui || !mailbox || !*mailbox || !gui->current_mailbox_utf8[0])
        return 0;
    current = gui_transfer_mailbox(gui);
    if (!strcmp(current, mailbox) ||
        !strcmp(gui->current_mailbox_utf8, mailbox)) return 1;
    /* A virtual/special-use label may have a different server wire name.
     * Only aliases of this selected folder may match, never another folder. */
    for (i = 0U; i < gui->label_count && i < AMIGMAIL_MAX_LABELS; ++i) {
        const GuiLabel *label = &gui->labels[i];
        if ((!strcmp(gui->current_mailbox_utf8, label->mailbox_utf8) ||
             (label->server_mailbox_utf8[0] &&
              !strcmp(current, label->server_mailbox_utf8))) &&
            (!strcmp(mailbox, label->mailbox_utf8) ||
             (label->server_mailbox_utf8[0] &&
              !strcmp(mailbox, label->server_mailbox_utf8))))
            return 1;
    }
    return 0;
}

int gui_transfer_message_matches(const AmgGui *gui, unsigned long uid,
                                  const char *mailbox)
{
    return gui && uid && uid == gui->active_message_uid &&
           gui_transfer_mailbox_matches(gui, mailbox);
}

static int progress_matches_view(const AmgGui *gui,
                                  const AmgTransferProgress *progress)
{
    if (!gui || !progress || !progress->active ||
        !gui_transfer_mailbox_matches(gui, progress->mailbox))
        return 0;
    /* A folder-list job belongs to the selected mailbox, not to a single
     * message. Its immutable origin has UID zero even if an older message is
     * still selected while that folder is being refreshed. Background Inbox
     * checks (AMG_NET_CHECK_INBOX) deliberately remain invisible. */
    if (progress->type == AMG_NET_FETCH_INBOX) return progress->uid == 0UL;
    if (progress->uid != gui->active_message_uid) return 0;
    if (progress->type == AMG_NET_FETCH_MESSAGE) return progress->uid != 0UL;
    /* Outgoing mail carries the view from which it was submitted. A new
     * message may originate from an empty folder (UID zero). It must still
     * match this exact account and folder; an unscoped job stays silent. */
    return progress->type == AMG_NET_SEND_MAIL ||
           progress->type == AMG_NET_SEND_REPLY ||
           progress->type == AMG_NET_SAVE_DRAFT;
}

static void show_progress(AmgGui *gui, int visible, size_t done, size_t total,
                           int cancellable)
{
    int known = visible && total != 0U;
    unsigned int percent = known ? progress_percent(done, total) : 0U;
    int update;
    if (!gui) return;
    visible = visible != 0;
    cancellable = visible && cancellable;
    update = !gui->transfer_initialized || gui->transfer_visible != visible ||
        gui->transfer_known != known || gui->transfer_percent != percent;
    if (!gui->window || gui->iconified) {
        gui->transfer_initialized = 0;
        return;
    }
    if (update && gui->transfer_gadget) {
        if (gui->transfer_native_gauge) {
            SetGadgetAttrs(gui->transfer_gadget, gui->window, NULL,
                FUELGAUGE_Level, (ULONG)percent,
                FUELGAUGE_Percent, (ULONG)known, TAG_DONE);
        } else {
            char *text;
            /* Keep the label alive and change its address for classic
             * gadgets which skip updates for an unchanged GA_Text pointer. */
            gui->transfer_text_index ^= 1U;
            text = gui->transfer_text[gui->transfer_text_index];
            if (known) snprintf(text, sizeof(gui->transfer_text[0]), "%u%%", percent);
            else snprintf(text, sizeof(gui->transfer_text[0]), "%s", visible ? "..." : "");
            SetGadgetAttrs(gui->transfer_gadget, gui->window, NULL,
                GA_Text, (ULONG)(uintptr_t)text, TAG_DONE);
        }
        RefreshGList(gui->transfer_gadget, gui->window, NULL, 1);
    }
    if (gui->transfer_cancel_gadget && (!gui->transfer_initialized ||
        gui->transfer_cancellable != cancellable)) {
        SetGadgetAttrs(gui->transfer_cancel_gadget, gui->window, NULL,
                       GA_Disabled, (ULONG)!cancellable, TAG_DONE);
        RefreshGList(gui->transfer_cancel_gadget, gui->window, NULL, 1);
    }
    gui->transfer_visible = visible;
    gui->transfer_known = known;
    gui->transfer_percent = percent;
    gui->transfer_cancellable = cancellable;
    gui->transfer_initialized = 1;
}

void gui_transfer_update(AmgGui *gui)
{
    AmgTransferProgress progress;
    size_t account;
    if (!gui || gui->file_progress) return;
    account = gui->active_account;
    memset(&progress, 0, sizeof(progress));
    if (gui->running && !gui->iconified && gui->window &&
        gui->account_set && account < AMG_MAX_ACCOUNTS &&
        gui->account_set->accounts[account].enabled &&
        amg_network_transfer_progress(gui->networks[account], &progress) &&
        progress_matches_view(gui, &progress)) {
        gui->transfer_account = account;
        gui->transfer_serial = progress.serial;
        show_progress(gui, 1, progress.done, progress.total, progress.cancellable);
    } else {
        gui->transfer_serial = 0UL;
        show_progress(gui, 0, 0U, 0U, 0);
    }
}

void gui_transfer_cancel(AmgGui *gui)
{
    AmgTransferProgress current;
    if (!gui) return;
    if (gui->file_progress) {
        gui->file_progress->cancelled = 1;
        return;
    }
    memset(&current, 0, sizeof(current));
    /* Validate the displayed job again. A queued button release from the old
     * account or from an already finished job must not cancel a new transfer. */
    if (gui->active_account < AMG_MAX_ACCOUNTS &&
        gui->transfer_visible && gui->transfer_cancellable &&
        gui->active_account == gui->transfer_account &&
        amg_network_transfer_progress(gui->networks[gui->active_account], &current) &&
        current.serial == gui->transfer_serial &&
        progress_matches_view(gui, &current))
        (void)amg_network_cancel_transfer(gui->networks[gui->active_account],
                                          current.serial);
    gui_transfer_update(gui);
}

void gui_transfer_cleanup(AmgGui *gui)
{
    if (!gui) return;
    if (gui->file_progress) gui_file_progress_close(gui->file_progress);
    gui->transfer_serial = 0UL;
    gui->transfer_initialized = 0;
    gui->transfer_visible = 0;
    /* Both status gadgets are owned and disposed by the main WindowObject. */
}

static void file_progress_pump(GuiFileProgress *progress)
{
    AmgGui *gui = progress->gui;
    ULONG result;
    UWORD code = 0;
    if (!gui || gui->window != progress->window || !gui->window_object) {
        progress->cancelled = 1;
        return;
    }
    /* No gui_runtime_process_signals(), handle_main_gadget(), handle_menu()
     * or network polling here: they could replace/free the borrowed mail.
     * window.class still services real refresh messages in RA_HandleInput(). */
    while ((result = RA_HandleInput(gui->window_object, &code)) != WMHI_LASTMSG) {
        if (result == (ULONG)WMHI_IGNORE) continue;
        switch (result & WMHI_CLASSMASK) {
            case WMHI_CLOSEWINDOW:
                progress->cancelled = 1;
                gui->running = 0;
                break;
            case WMHI_GADGETUP:
                if ((result & WMHI_GADGETMASK) == GID_TRANSFER_CANCEL)
                    progress->cancelled = 1;
                break;
            case WMHI_RAWKEY:
                if (rawkey_is_cancel(result)) progress->cancelled = 1;
                break;
            case WMHI_NEWSIZE:
                gui_mail_split_update_limits(gui, 1);
                draw_window_overlays(gui);
                gui->transfer_initialized = 0;
                break;
            default:
                break;
        }
    }
    if ((SetSignal(0UL, 0UL) & SIGBREAKF_CTRL_C) != 0UL)
        progress->cancelled = 1;
}

static int file_progress_report(void *context, AmgTransferPhase phase,
                                 size_t done, size_t total)
{
    GuiFileProgress *progress = (GuiFileProgress *)context;
    AmgGui *gui;
    (void)phase;
    if (!progress || !(gui = progress->gui)) return 0;
    file_progress_pump(progress);
    if (gui->active_account != progress->account ||
        gui->active_message_uid != progress->uid ||
        strcmp(gui_transfer_mailbox(gui), progress->mailbox) ||
        gui->current_mail_file != progress->mail)
        progress->cancelled = 1;
    show_progress(gui, 1, done, total, !progress->cancelled);
    return !progress->cancelled;
}

static void file_progress_lock(GuiFileProgress *progress, struct Gadget *gadget)
{
    size_t i;
    if (!gadget || progress->lock_count >= 8U) return;
    i = progress->lock_count++;
    progress->locked[i] = gadget;
    GetAttr(GA_Disabled, (Object *)gadget, &progress->was_disabled[i]);
    SetGadgetAttrs(gadget, progress->window, NULL, GA_Disabled, TRUE, TAG_DONE);
    RefreshGList(gadget, progress->window, NULL, 1);
}

GuiFileProgress *gui_file_progress_open(AmgGui *gui, const char *title)
{
    GuiFileProgress *progress;
    if (!gui || !gui->window || !gui->window_object || gui->file_progress)
        return NULL;
    progress = (GuiFileProgress *)calloc(1U, sizeof(*progress));
    if (!progress) return NULL;
    progress->gui = gui;
    progress->window = gui->window;
    progress->mail = gui->current_mail_file;
    progress->account = gui->active_account;
    progress->uid = gui->active_message_uid;
    snprintf(progress->mailbox, sizeof(progress->mailbox), "%s", gui_transfer_mailbox(gui));
    snprintf(progress->title, sizeof(progress->title), "%s", title ? title : "");
    progress->callback.report = file_progress_report;
    progress->callback.context = progress;
    gui->file_progress = progress;
    gui->transfer_serial = 0UL;
    /* Local I/O remains synchronous. Lock the navigation gadgets so their
     * native internal state cannot switch accounts or mail under this job.
     * Unlike Request(), this leaves the inline cancel button usable. */
    file_progress_lock(progress, gui->account_tabs_gadget);
    file_progress_lock(progress, gui->system_labels_gadget);
    file_progress_lock(progress, gui->labels_gadget);
    file_progress_lock(progress, gui->messages_gadget);
    file_progress_lock(progress, gui->preview_gadget);
    file_progress_lock(progress, gui->new_mail_gadget);
    file_progress_lock(progress, gui->reply_gadget);
    file_progress_lock(progress, gui->reply_menu_gadget);
    status_local(gui, progress->title);
    show_progress(gui, 1, 0U, 0U, 1);
    return progress;
}

AmgTransfer *gui_file_progress_callback(GuiFileProgress *progress)
{
    return progress ? &progress->callback : NULL;
}

void gui_file_progress_item(GuiFileProgress *progress, const char *name)
{
    char text[192];
    if (!progress) return;
    snprintf(text, sizeof(text), "%.100s: %.85s", progress->title, name ? name : "");
    status_local(progress->gui, text);
    show_progress(progress->gui, 1, 0U, 0U, !progress->cancelled);
}

void gui_file_progress_close(GuiFileProgress *progress)
{
    size_t i;
    AmgGui *gui;
    if (!progress) return;
    gui = progress->gui;
    /* Discard delayed input before unlocking navigation, rather than letting
     * clicks made during export start unrelated actions afterwards. */
    file_progress_pump(progress);
    if (gui && gui->window == progress->window) {
        for (i = 0U; i < progress->lock_count; ++i) {
            SetGadgetAttrs(progress->locked[i], gui->window, NULL,
                GA_Disabled, progress->was_disabled[i], TAG_DONE);
            RefreshGList(progress->locked[i], gui->window, NULL, 1);
        }
    }
    if (gui && gui->file_progress == progress) {
        gui->file_progress = NULL;
        show_progress(gui, 0, 0U, 0U, 0);
    }
    free(progress);
}
#endif
