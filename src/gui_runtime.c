#include "gui_internal.h"
#include "i18n.h"

#include <stdint.h>

#if AMIGMAIL_AMIGA

#include <clib/alib_protos.h>
#include <classes/window.h>
#include <devices/timer.h>
#include <exec/io.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>

#define GUI_PERIODIC_FETCH_SECONDS 300UL
#define T(id, en) amg_tr((id), (en))

static void periodic_timer_clear_signal(AmgGui *gui)
{
    if (!gui || !gui->periodic_timer_port) return;
    SetSignal(0UL, 1UL << gui->periodic_timer_port->mp_SigBit);
}

static void periodic_timer_disarm(AmgGui *gui)
{
    if (!gui || !gui->periodic_timer_request || !gui->periodic_timer_pending)
        return;
    if (!CheckIO((struct IORequest *)gui->periodic_timer_request))
        AbortIO((struct IORequest *)gui->periodic_timer_request);
    WaitIO((struct IORequest *)gui->periodic_timer_request);
    gui->periodic_timer_pending = 0;
    periodic_timer_clear_signal(gui);
}

static void periodic_timer_arm(AmgGui *gui)
{
    if (!gui || !gui->periodic_timer_request ||
        !gui->periodic_timer_device_open || gui->periodic_timer_pending ||
        !gui->account || !gui->account->periodic_fetch)
        return;
    gui->periodic_timer_request->tr_node.io_Command = TR_ADDREQUEST;
    gui->periodic_timer_request->tr_time.tv_secs = GUI_PERIODIC_FETCH_SECONDS;
    gui->periodic_timer_request->tr_time.tv_micro = 0UL;
    SendIO((struct IORequest *)gui->periodic_timer_request);
    gui->periodic_timer_pending = 1;
}

static int periodic_timer_init(AmgGui *gui)
{
    if (!gui) return 0;
    if (gui->periodic_timer_device_open && gui->periodic_timer_request) {
        periodic_timer_restart(gui);
        return 1;
    }
    gui->periodic_timer_port = CreateMsgPort();
    if (!gui->periodic_timer_port) return 0;
    gui->periodic_timer_request = (struct timerequest *)CreateIORequest(
        gui->periodic_timer_port, sizeof(struct timerequest));
    if (!gui->periodic_timer_request) {
        DeleteMsgPort(gui->periodic_timer_port);
        gui->periodic_timer_port = NULL;
        return 0;
    }
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK,
                   (struct IORequest *)gui->periodic_timer_request, 0UL) != 0) {
        DeleteIORequest((struct IORequest *)gui->periodic_timer_request);
        DeleteMsgPort(gui->periodic_timer_port);
        gui->periodic_timer_request = NULL;
        gui->periodic_timer_port = NULL;
        return 0;
    }
    gui->periodic_timer_device_open = 1;
    gui->periodic_timer_pending = 0;
    periodic_timer_arm(gui);
    return 1;
}

void periodic_timer_restart(AmgGui *gui)
{
    if (!gui || !gui->periodic_timer_request) return;
    periodic_timer_disarm(gui);
    periodic_timer_arm(gui);
}

void periodic_timer_cleanup(AmgGui *gui)
{
    if (!gui) return;
    periodic_timer_disarm(gui);
    if (gui->periodic_timer_device_open && gui->periodic_timer_request) {
        CloseDevice((struct IORequest *)gui->periodic_timer_request);
        gui->periodic_timer_device_open = 0;
    }
    if (gui->periodic_timer_request) {
        DeleteIORequest((struct IORequest *)gui->periodic_timer_request);
        gui->periodic_timer_request = NULL;
    }
    if (gui->periodic_timer_port) {
        DeleteMsgPort(gui->periodic_timer_port);
        gui->periodic_timer_port = NULL;
    }
    gui->periodic_timer_pending = 0;
}

static ULONG periodic_timer_signal_mask(const AmgGui *gui)
{
    if (!gui || !gui->periodic_timer_port) return 0UL;
    return 1UL << gui->periodic_timer_port->mp_SigBit;
}

static ULONG app_port_signal_mask(const AmgGui *gui)
{
    if (!gui || !gui->app_port) return 0UL;
    return 1UL << gui->app_port->mp_SigBit;
}

void gui_iconify(AmgGui *gui)
{
    ULONG window_value = 0UL;
    if (!gui || !gui->window_object || !gui->window || gui->iconified)
        return;
    gui_state_save_window(gui);
    (void)DoMethod(gui->window_object, WM_ICONIFY);
    GetAttr(WINDOW_Window, gui->window_object, &window_value);
    gui->window = (struct Window *)(uintptr_t)window_value;
    gui->iconified = gui->window ? 0 : 1;
}

int gui_uniconify(AmgGui *gui)
{
    struct Window *window;
    if (!gui || !gui->window_object) return 0;
    if (gui->window && !gui->iconified) return 1;
    SetAttrs(gui->window_object, WA_Hidden, FALSE, TAG_DONE);
    window = (struct Window *)(uintptr_t)DoMethod(gui->window_object, WM_OPEN);
    if (!window) return 0;
    gui->window = window;
    gui->iconified = 0;
    gui_mail_split_update_limits(gui, 0);
    draw_window_overlays(gui);
    return 1;
}


ULONG gui_runtime_signal_mask(AmgGui *gui)
{
    ULONG window_signal = 0UL;
    if (!gui) return 0UL;
    if (gui->window_object)
        GetAttr(WINDOW_SigMask, gui->window_object, &window_signal);
    return window_signal |
           app_port_signal_mask(gui) |
           amg_network_signal_mask(gui->network) |
           periodic_timer_signal_mask(gui) |
           gui->preview_url_signal_mask |
           gui_notify_signal_mask(gui);
}

void gui_runtime_process_signals(AmgGui *gui, ULONG signals,
                                 AmgError *error)
{
    ULONG window_signal = 0UL;
    ULONG app_signal;
    ULONG network_signal;
    ULONG timer_signal;
    ULONG notify_signal;

    if (!gui) return;
    app_signal = app_port_signal_mask(gui);
    network_signal = amg_network_signal_mask(gui->network);
    timer_signal = periodic_timer_signal_mask(gui);
    notify_signal = gui_notify_signal_mask(gui);
    if (gui->window_object)
        GetAttr(WINDOW_SigMask, gui->window_object, &window_signal);

    /* A completed sound is handled before network events.  Otherwise an old
     * completion signal could dispose a newly-started notification object. */
    if (notify_signal && (signals & notify_signal))
        gui_notify_handle_signal(gui);

    if (network_signal && (signals & network_signal)) {
        handle_network(gui);
        draw_window_overlays(gui);
    }

    if (timer_signal && (signals & timer_signal)) {
        if (gui->periodic_timer_pending &&
            CheckIO((struct IORequest *)gui->periodic_timer_request)) {
            WaitIO((struct IORequest *)gui->periodic_timer_request);
            gui->periodic_timer_pending = 0;
            periodic_timer_clear_signal(gui);
            periodic_timer_arm(gui);
            periodic_fetch_mail(gui, error);
        } else {
            periodic_timer_clear_signal(gui);
        }
    }

    if ((window_signal && (signals & window_signal)) ||
        (app_signal && (signals & app_signal))) {
        ULONG result;
        int redraw_overlays = 0;
        UWORD code = 0U;

        while ((result = RA_HandleInput(gui->window_object, &code)) !=
               WMHI_LASTMSG) {
            /* window.class handles IDCMP_REFRESHWINDOW itself.  WMHI_IGNORE
             * can also represent unrelated ignored input and must not cause
             * an application-level redraw. */
            if (result == (ULONG)WMHI_IGNORE)
                continue;

            switch (result & WMHI_CLASSMASK) {
                case WMHI_CLOSEWINDOW:
                    gui->running = 0;
                    break;
                case WMHI_ICONIFY:
                    gui_iconify(gui);
                    break;
                case WMHI_UNICONIFY:
                    (void)gui_uniconify(gui);
                    break;
                case WMHI_GADGETUP:
                    handle_main_gadget(gui, result & WMHI_GADGETMASK, error);
                    break;
                case WMHI_NEWSIZE:
                    /* ReAction has updated the ListBrowser domain at this
                     * point. Refresh once, then recalculate the external
                     * hierarchical folder scroller for the new height. */
                    if (gui->window && gui->labels_gadget)
                        RefreshGList(gui->labels_gadget, gui->window, NULL, 1);
                    sync_labels_scroller(gui);
                    gui_mail_split_update_limits(gui, 1);
                    redraw_overlays = 1;
                    break;
                case WMHI_MENUPICK:
                    handle_menu(gui, result & 0xffffUL, error);
                    break;
                case WMHI_RAWKEY:
                    if (rawkey_is_rcommand_letter(
                            gui->window_object, result, 'A')) {
                        handle_main_gadget(gui, GID_FETCH, error);
                    } else if (rawkey_is_rcommand_letter(
                                   gui->window_object, result, 'W')) {
                        handle_main_shortcut(gui, 'W', error);
                    } else if (rawkey_is_delete(result)) {
                        handle_main_gadget(gui, GID_DELETE, error);
                    } else if (rawkey_is_help(result)) {
                        about_dialog(gui);
                    } else if (gui->move_pending && rawkey_is_cancel(result)) {
                        cancel_pending_move(gui);
                    }
                    break;
            }
        }

        if (redraw_overlays && gui->window)
            draw_window_overlays(gui);
    }

    if (gui->pending_preview_url_ready)
        open_pending_preview_url(gui);
}

int amg_gui_run(AmgGui *gui, AmgMailtoServer *mailto_server,
                const char *startup_mailto, AmgError *error)
{
    ULONG mailto_signal;
    if (!gui) return AMG_ERR_ARGUMENT;
    gui->window = RA_OpenWindow(gui->window_object);
    if (!gui->window) {
        amg_error_set(error, AMG_ERR_IO,
                      T(MSG_WORKBENCH_WINDOW_COULD_NOT_BE_OPENED, "Workbench window could not be opened."));
        return AMG_ERR_IO;
    }
    gui->iconified = 0;
    gui_mail_split_update_limits(gui, 0);
    draw_window_overlays(gui);
    mailto_signal = amg_mailto_server_signal_mask(mailto_server);
    gui->running = 1;

    gui->preview_url_signal_task = FindTask(NULL);
    gui->preview_url_signal_bit = AllocSignal(-1);
    if (gui->preview_url_signal_bit >= 0)
        gui->preview_url_signal_mask =
            1UL << (ULONG)gui->preview_url_signal_bit;
    else
        gui->preview_url_signal_mask = 0UL;
    (void)gui_notify_init(gui);

    if (account_is_locked(gui->account)) {
        if (gui->account->email[0]) {
            int unlock_result = unlock_account_dialog(gui, error);
            if (unlock_result == 1)
                status_local(gui,
                    T(MSG_ACCOUNT_IS_UNLOCKED_FOR_THIS_AMIGA_SESSION, "Account is unlocked for this Amiga session."));
            else if (unlock_result == 2)
                status_local(gui,
                    T(MSG_ACCOUNT_IS_UNLOCKED_SESSION_KEY_COULD_NOT_BE, "Account is unlocked; session key could not be stored in ENV:."));
            else
                status_local(gui,
                    T(MSG_ACCOUNT_IS_LOCKED_OPEN_ACCOUNT_SETTINGS_TO_UNLOCK, "Account is locked. Open Account settings to unlock."));
        } else {
            account_dialog(gui, error);
        }
        draw_window_overlays(gui);
    }

    if (!periodic_timer_init(gui) && gui->account->periodic_fetch)
        status_local(gui,
            T(MSG_PERIODIC_FETCH_IS_UNAVAILABLE_TIMER_DEVICE, "Periodic fetch is unavailable (timer.device)."));

    if (!account_is_locked(gui->account) && gui->account->fetch_on_start) {
        /* Do not let the GitHub check jump ahead of the initial mail/folder
         * sequence in the serial network worker. */
        gui->update_check_deferred = 1;
        fetch_mail(gui, error);
    } else {
        gui_update_request_check(gui);
    }

    if (startup_mailto && !account_is_locked(gui->account))
        (void)open_mailto_compose(gui, startup_mailto, error);
    else if (startup_mailto)
        status_local(gui,
            T(MSG_MAILTO_LINK_CANCELLED_MAIL_ACCOUNT_IS_LOCKED, "mailto: link cancelled: mail account is locked."));

    while (gui->running) {
        ULONG runtime_signal = gui_runtime_signal_mask(gui);
        ULONG signals = Wait(runtime_signal | mailto_signal |
                             SIGBREAKF_CTRL_C);

        if (signals & SIGBREAKF_CTRL_C)
            gui->running = 0;

        if (runtime_signal && (signals & runtime_signal))
            gui_runtime_process_signals(gui, signals & runtime_signal, error);

        if (mailto_signal && (signals & mailto_signal)) {
            handle_mailto_requests(gui, mailto_server, error);
            draw_window_overlays(gui);
        }
    }

    if (gui->preview_url_signal_bit >= 0) {
        FreeSignal(gui->preview_url_signal_bit);
        gui->preview_url_signal_bit = -1;
        gui->preview_url_signal_mask = 0UL;
    }
    gui->preview_url_signal_task = NULL;
    gui_notify_cleanup(gui);
    periodic_timer_cleanup(gui);
    gui_state_save_window(gui);
    gui_state_set_mail_status_inactive();
    amg_network_stop(gui->network);
    gui->window = NULL;
    return AMG_OK;
}

#endif /* AMIGMAIL_AMIGA */
