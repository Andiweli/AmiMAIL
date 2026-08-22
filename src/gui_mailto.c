#include "gui_internal.h"
#include "buffer.h"
#include "codec.h"
#include "i18n.h"
#include "mailto.h"

#include <stdlib.h>
#include <string.h>

#if AMIGMAIL_AMIGA
#include <proto/intuition.h>

#define T(id, en) amg_tr((id), (en))

static int mailto_utf8_to_local_field(const char *utf8, char *local,
                                      size_t capacity, AmgError *error)
{
    AmgBuffer converted;
    if (!local || !capacity) return AMG_ERR_ARGUMENT;
    local[0] = 0;
    if (!utf8 || !*utf8) return AMG_OK;
    amg_buffer_init(&converted);
    if (amg_utf8_to_local(utf8, &converted) != AMG_OK ||
        amg_buffer_terminate(&converted) != AMG_OK) {
        amg_buffer_free(&converted);
        amg_error_set(error, AMG_ERR_PARSE,
                      T(MSG_MAILTO_TEXT_COULD_NOT_BE_DECODED, "mailto: text could not be decoded."));
        return AMG_ERR_PARSE;
    }
    if (converted.length >= capacity) {
        amg_buffer_free(&converted);
        amg_error_set(error, AMG_ERR_LIMIT,
                      T(MSG_A_FIELD_IN_THE_MAILTO_LINK_IS_TOO, "A field in the mailto: link is too long."));
        return AMG_ERR_LIMIT;
    }
    memcpy(local, converted.data, converted.length + 1U);
    amg_buffer_free(&converted);
    return AMG_OK;
}

int open_mailto_compose(AmgGui *gui, const char *url, AmgError *error)
{
    AmgMailtoRequest request;
    DraftEditData *seed;
    int result = AMG_OK;

    if (!gui || !url) return AMG_ERR_ARGUMENT;
    if (gui->iconified && !gui_uniconify(gui)) {
        amg_error_set(error, AMG_ERR_IO,
                      T(MSG_AMIMAIL_WINDOW_COULD_NOT_BE_RESTORED, "AmiMail window could not be restored."));
        return AMG_ERR_IO;
    }
    if (account_is_locked(gui->account)) {
        status_local(gui,
            T(MSG_MAILTO_LINK_REQUIRES_AN_UNLOCKED_MAIL_ACCOUNT, "mailto: link requires an unlocked mail account."));
        return AMG_ERR_AUTH;
    }

    amg_mailto_request_init(&request);
    result = amg_mailto_parse(url, &request, error);
    if (result != AMG_OK) {
        status_utf8(gui, error ? error->message : T(MSG_INVALID_MAILTO_URL, "Invalid mailto: URL."));
        amg_mailto_request_clear(&request);
        return result;
    }

    seed = (DraftEditData *)calloc(1, sizeof(*seed));
    if (!seed) {
        amg_mailto_request_clear(&request);
        amg_error_set(error, AMG_ERR_MEMORY,
                      T(MSG_NOT_ENOUGH_MEMORY_FOR_MAILTO_LINK, "Not enough memory for mailto: link."));
        status_utf8(gui, error->message);
        return AMG_ERR_MEMORY;
    }

    if ((result = mailto_utf8_to_local_field(
             request.to_utf8, seed->to_local, sizeof(seed->to_local), error)) == AMG_OK &&
        (result = mailto_utf8_to_local_field(
             request.cc_utf8, seed->cc_local, sizeof(seed->cc_local), error)) == AMG_OK &&
        (result = mailto_utf8_to_local_field(
             request.bcc_utf8, seed->bcc_local, sizeof(seed->bcc_local), error)) == AMG_OK &&
        (result = mailto_utf8_to_local_field(
             request.subject_utf8, seed->subject_local,
             sizeof(seed->subject_local), error)) == AMG_OK &&
        (result = mailto_utf8_to_local_field(
             request.body_utf8, seed->body_local,
             sizeof(seed->body_local), error)) == AMG_OK) {
        /* Do not bring the main window to the front here.  On classic
         * Intuition/ReAction the WindowToFront() request can race with the
         * subsequently opened compose window and leave the main window above
         * it.  compose_dialog() brings its own window to the front. */
        (void)compose_dialog(gui, COMPOSE_MODE_NEW, seed, error);
    } else {
        status_utf8(gui, error ? error->message : T(MSG_INVALID_MAILTO_URL, "Invalid mailto: URL."));
    }

    free(seed);
    amg_mailto_request_clear(&request);
    return result;
}

void handle_mailto_requests(AmgGui *gui, AmgMailtoServer *server,
                            AmgError *error)
{
    char *url = NULL;
    if (!gui || !server) return;
    while (amg_mailto_server_receive(server, &url)) {
        if (gui->iconified && !gui_uniconify(gui)) {
            free(url);
            url = NULL;
            continue;
        }
        if (account_is_locked(gui->account)) {
            (void)account_dialog(gui, error);
            draw_window_overlays(gui);
        }
        if (!account_is_locked(gui->account))
            (void)open_mailto_compose(gui, url, error);
        else
            status_local(gui,
                T(MSG_MAILTO_LINK_CANCELLED_MAIL_ACCOUNT_IS_LOCKED, "mailto: link cancelled: mail account is locked."));
        free(url);
        url = NULL;
    }
}

#endif /* AMIGMAIL_AMIGA */
