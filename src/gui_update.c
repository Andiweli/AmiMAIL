#include "gui_internal.h"
#include "i18n.h"
#include "update.h"

#include <stdio.h>
#include <string.h>

#if AMIGMAIL_AMIGA

#include <dos/var.h>
#include <gadgets/button.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <utility/tagitem.h>

#define UPDATE_TEST_VAR "AmiMAILUpdateTest"
#define T(id, en) amg_tr((id), (en))

static int update_test_enabled(void)
{
    char value[8];
    LONG length = GetVar((STRPTR)UPDATE_TEST_VAR, (STRPTR)value,
                         (LONG)sizeof(value) - 1L, GVF_GLOBAL_ONLY);
    if (length <= 0L) return 0;
    if ((size_t)length >= sizeof(value)) length = (LONG)sizeof(value) - 1L;
    value[length] = 0;
    return strcmp(value, "0") != 0;
}

void gui_update_refresh_gadget(AmgGui *gui)
{
    const char *text;
    LONG text_pen;
    if (!gui || !gui->update_gadget) return;
    if (gui->update_available) {
        text = T(MSG_NEW_UPDATE, "new Update");
        text_pen = gui->update_pen;
    } else {
        text = T(MSG_UP_TO_DATE, "Up to date");
        text_pen = gui->text_pen;
    }
    if (gui->window) {
        SetGadgetAttrs(gui->update_gadget, gui->window, NULL,
                       GA_Text, (ULONG)(uintptr_t)text,
                       GA_ReadOnly, gui->update_available ? FALSE : TRUE,
                       BUTTON_TextPen, text_pen,
                       BUTTON_FillTextPen, text_pen,
                       TAG_DONE);
        RefreshGList(gui->update_gadget, gui->window, NULL, 1);
    } else {
        SetAttrs((Object *)gui->update_gadget,
                 GA_Text, (ULONG)(uintptr_t)text,
                 GA_ReadOnly, gui->update_available ? FALSE : TRUE,
                 BUTTON_TextPen, text_pen,
                 BUTTON_FillTextPen, text_pen,
                 TAG_DONE);
    }
}

void gui_update_request_check(AmgGui *gui)
{
    AmgError error;
    int result;
    if (!gui || gui->update_check_started) return;
    gui->update_check_started = 1;
    memset(&error, 0, sizeof(error));
    if (!amg_network_is_running(gui->network)) {
        result = amg_network_start(gui->network, gui->account, &error);
        if (result != AMG_OK) return;
    }
    result = amg_network_request(gui->network, AMG_NET_CHECK_UPDATE,
                                 0UL, NULL, NULL, &error);
    if (result == AMG_OK) gui->update_check_pending = 1;
}

void gui_update_handle_check(AmgGui *gui, const AmgNetworkEvent *event)
{
    int available;
    if (!gui || !event) return;
    gui->update_check_pending = 0;
    if (event->result != AMG_OK) return;
    available = amg_update_is_newer(event->argument1, AMIMAIL_VERSION);
    if (!available && update_test_enabled()) available = 1;
    if (!available || !event->argument1[0] || !event->argument2[0]) return;

    strncpy(gui->update_tag, event->argument1,
            sizeof(gui->update_tag) - 1U);
    gui->update_tag[sizeof(gui->update_tag) - 1U] = 0;
    strncpy(gui->update_download_url, event->argument2,
            sizeof(gui->update_download_url) - 1U);
    gui->update_download_url[sizeof(gui->update_download_url) - 1U] = 0;
    gui->update_available = 1;
    gui_update_refresh_gadget(gui);
}

void gui_update_start_download(AmgGui *gui, AmgError *error)
{
    char destination[AMG_UPDATE_PATH_MAX];
    int written;
    int result;
    if (!gui || !gui->update_available || gui->update_download_pending ||
        !gui->update_tag[0] || !gui->update_download_url[0])
        return;
    written = snprintf(destination, sizeof(destination),
                       "RAM:AmiMAIL-%s.lha", gui->update_tag);
    if (written < 0 || (size_t)written >= sizeof(destination)) {
        status_local(gui, T(MSG_UPDATE_FILENAME_IS_TOO_LONG, "Update filename is too long."));
        return;
    }
    if (!amg_network_is_running(gui->network)) {
        result = amg_network_start(gui->network, gui->account, error);
        if (result != AMG_OK) {
            if (error && error->message[0]) status_utf8(gui, error->message);
            return;
        }
    }
    result = amg_network_request(gui->network, AMG_NET_DOWNLOAD_UPDATE,
                                 0UL, gui->update_download_url,
                                 destination, error);
    if (result == AMG_OK) {
        gui->update_download_pending = 1;
        status_local(gui, T(MSG_DOWNLOADING_UPDATE_TO_RAM, "Downloading update to RAM:..."));
    } else if (error && error->message[0]) {
        status_utf8(gui, error->message);
    }
}

void gui_update_handle_download(AmgGui *gui,
                                const AmgNetworkEvent *event)
{
    char message[384];
    if (!gui || !event) return;
    gui->update_download_pending = 0;
    if (event->result != AMG_OK) return;
    amg_tr_snprintf(message, sizeof(message), MSG_UPDATE_DOWNLOADED_TO_VALUE_PLEASE_EXTRACT_IT_MANUALLY, "Update downloaded to %s. Please extract it manually.", event->argument2[0] ? event->argument2 : "RAM:");
    status_local(gui, message);
}

#endif /* AMIGMAIL_AMIGA */
