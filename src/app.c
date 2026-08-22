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
    AmgAccount account;
    AmgGui *gui;
    AmgMailtoServer *mailto_server = NULL;
    AmgMailtoRequest mailto_request;
    AmgError error;
    int result;
    int detached_mailto_child = 0;
    char *startup_mailto = NULL;
    const char *raw_arguments = NULL;
    const char *config = "ENVARC:AmiMail/account.cfg";
#if AMIGMAIL_AMIGA
    char legacy_master[128];
    const char *session_key = "ENV:AmiMail.session-key";
    const char *persistent_key = "ENVARC:AmiMail/account.key";
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

    /* Publish the hand-off port before account dialogs are opened so later
     * browser clicks can already be queued while the primary instance is
     * waiting for its master password. */
    mailto_server = amg_mailto_server_create();
    if (!mailto_server && startup_mailto &&
        amg_mailto_forward_to_running(startup_mailto)) {
        free(startup_mailto);
        return 0;
    }

    /* Only the real primary instance gets a splash. Short-lived mailto:
     * forwarding/detach helpers above have already returned at this point. */
    amg_splash_open();

    amg_account_init(&account);
#if AMIGMAIL_AMIGA
    legacy_master[0] = 0;
    /* First try the volatile ENV: session key. It contains only the PBKDF2
     * derived account key and disappears with the Amiga session. */
    result = amg_storage_load_account_session(
        config, session_key, &account, &error);
    if (result != AMG_OK) {
        amg_account_clear(&account);
        amg_account_init(&account);
        result = amg_storage_load_account_session(
            config, persistent_key, &account, &error);
    }
    if (result != AMG_OK) {
        amg_account_clear(&account);
        amg_account_init(&account);
        result = amg_storage_load_account(config, NULL, &account, &error);
    }
    /* AmiMail 0.1.10 and earlier stored the master password reversibly in
     * ACCOUNT-1. Use it exactly once to migrate the encrypted secrets to
     * ACCOUNT-2, which never persists the master password. The derived key
     * is cached only in ENV: for the remainder of this Amiga session. */
    if (result == AMG_ERR_AUTH &&
        amg_storage_load_legacy_master(
            config, legacy_master, sizeof(legacy_master)) == AMG_OK &&
        legacy_master[0]) {
        amg_account_clear(&account);
        amg_account_init(&account);
        result = amg_storage_load_account(
            config, legacy_master, &account, &error);
        if (result == AMG_OK) {
            result = amg_storage_save_account(
                config, &account, legacy_master, &error);
            if (result == AMG_OK) {
                AmgError session_error;
                memset(&session_error, 0, sizeof(session_error));
                (void)amg_storage_cache_session_key(
                    config, session_key, legacy_master, &session_error);
            }
        }
    }
    amg_secure_clear(legacy_master, sizeof(legacy_master));
    if (result != AMG_OK && result != AMG_ERR_AUTH) {
        amg_account_clear(&account);
        amg_account_init(&account);
    }
#else
    (void)config;
#endif
    gui = amg_gui_create(&account, &error);
    if (!gui) {
        amg_splash_close();
        print_local_error(error.message);
        amg_mailto_server_destroy(mailto_server);
        amg_account_clear(&account);
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
    amg_account_clear(&account);
    free(startup_mailto);
    return result == AMG_OK ? 0 : 20;
}
