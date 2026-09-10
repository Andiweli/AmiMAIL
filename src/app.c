#include "app.h"
#include "account.h"
#include "buffer.h"
#include "codec.h"
#include "gui.h"
#include "i18n.h"
#include "mailto.h"
#include "splash.h"
#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if AMIGMAIL_AMIGA
#include <proto/dos.h>
#endif

static void print_local_error(const char *message)
{
    AmgBuffer local;
    amg_buffer_init(&local);
    if (message && amg_utf8_to_local(message, &local) == AMG_OK &&
        amg_buffer_terminate(&local) == AMG_OK)
        fprintf(stderr, "AmiMail: %s\n", (const char *)local.data);
    else
        fprintf(stderr, "AmiMail: %s\n", amg_tr(MSG_ERROR, "Error"));
    amg_buffer_free(&local);
}

int amg_app_run(int argc, char **argv)
{
    AmgAccountSet accounts;
    AmgGui *gui;
    AmgMailtoServer *mailto_server = NULL;
    AmgMailtoRequest mailto_request;
    AmgError error;
    int result;
    int detached_mailto_child = 0;
    char *startup_mailto = NULL;
    const char *raw_arguments = NULL;
#if AMIGMAIL_AMIGA
    size_t account_index;
#endif

    memset(&error, 0, sizeof(error));
    amg_i18n_init();
    (void)atexit(amg_i18n_cleanup);
#if AMIGMAIL_AMIGA
    raw_arguments = (const char *)GetArgStr();
#endif
    startup_mailto = amg_mailto_startup_url(
        argc, argv, raw_arguments, &detached_mailto_child, &error);
    if (error.code != 0 && !startup_mailto) {
        print_local_error(error.message);
        return 20;
    }

    amg_mailto_request_init(&mailto_request);
    if (startup_mailto) {
        result = amg_mailto_parse(startup_mailto, &mailto_request, &error);
        amg_mailto_request_clear(&mailto_request);
        if (result != AMG_OK) {
            print_local_error(error.message);
            free(startup_mailto);
            return 20;
        }

        /* Fast path for every mailto: launch while AmiMail is already
         * running: transfer the request to the existing instance and exit. */
        if (amg_mailto_forward_to_running(startup_mailto)) {
            free(startup_mailto);
            return 0;
        }

        /* Browser external-command handlers may wait for the launched
         * process. Turn the first mailto: invocation into a short-lived
         * hand-off and start the real AmiMail instance asynchronously. */
        if (!detached_mailto_child &&
            amg_mailto_spawn_detached(startup_mailto)) {
            free(startup_mailto);
            return 0;
        }
    }

    /* Publish the hand-off port before account/config loading so later
     * browser clicks can already be queued while the primary instance starts. */
    mailto_server = amg_mailto_server_create();
    if (!mailto_server && startup_mailto &&
        amg_mailto_forward_to_running(startup_mailto)) {
        free(startup_mailto);
        return 0;
    }

    /* Only the real primary instance gets a splash. Short-lived mailto:
     * forwarding/detach helpers above have already returned at this point. */
    amg_splash_open();

    amg_account_set_init(&accounts);
#if AMIGMAIL_AMIGA
    for (account_index = 0U; account_index < AMG_MAX_ACCOUNTS;
         ++account_index) {
        AmgAccount *account = &accounts.accounts[account_index];
        const char *config = amg_storage_account_path(account_index);
        const char *key_path =
            amg_storage_persistent_key_path(account_index);
        int load_result;

        amg_account_clear(account);
        amg_account_init(account);
        load_result = amg_storage_load_account_auto(
            config, key_path, account, &error);

        /* If an old encrypted account has no usable automatic key,
         * amg_storage_load_account_auto() deliberately keeps its public
         * settings and returns AMG_ERR_AUTH. The account then remains
         * available in Account settings so only the mail password needs to
         * be entered again. No master-password prompt is involved. */
        if (load_result != AMG_OK && load_result != AMG_ERR_AUTH) {
            amg_account_clear(account);
            amg_account_init(account);
            if (account_index == 0U) account->enabled = 1;
        }
    }
    (void)amg_storage_load_account_order(accounts.order);
    accounts.current = amg_account_set_first_enabled(&accounts);
#else
    accounts.current = 0U;
#endif
    gui = amg_gui_create(&accounts, &error);
    if (!gui) {
        amg_splash_close();
        print_local_error(error.message);
        amg_mailto_server_destroy(mailto_server);
        amg_account_set_clear(&accounts);
        free(startup_mailto);
        return 20;
    }

    /* Keep the splash up through account/config loading and construction of
     * the ReAction GUI, then remove it immediately before the main event
     * loop takes over. */
    amg_splash_close();

    result = amg_gui_run(gui, mailto_server, startup_mailto, &error);
    if (result != AMG_OK) print_local_error(error.message);
    amg_gui_destroy(gui);
    amg_mailto_server_destroy(mailto_server);
    amg_account_set_clear(&accounts);
    free(startup_mailto);
    return result == AMG_OK ? 0 : 20;
}
