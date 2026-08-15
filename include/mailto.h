#ifndef AMIMAIL_MAILTO_H
#define AMIMAIL_MAILTO_H

#include "amigmail.h"

typedef struct AmgMailtoRequest {
    char *to_utf8;
    char *cc_utf8;
    char *bcc_utf8;
    char *subject_utf8;
    char *body_utf8;
} AmgMailtoRequest;

typedef struct AmgMailtoServer AmgMailtoServer;

void amg_mailto_request_init(AmgMailtoRequest *request);
void amg_mailto_request_clear(AmgMailtoRequest *request);
int amg_mailto_parse(const char *url, AmgMailtoRequest *request,
                     AmgError *error);
const char *amg_mailto_find_argument(int argc, char **argv);
/* Return an owned mailto: URI from argv or the raw AmigaDOS argument string.
 * detached_child is set when the URI came from AmiMail's private temporary
 * hand-off file used for asynchronous browser launches. */
char *amg_mailto_startup_url(int argc, char **argv, const char *raw_args,
                             int *detached_child, AmgError *error);
/* Start a detached copy of AmiMail carrying url.  On non-Amiga hosts this
 * returns 0. */
int amg_mailto_spawn_detached(const char *url);

/* A lightweight public Exec port used only to hand mailto: requests to an
 * already running AmiMail instance.  The sender transfers ownership of the
 * message to the receiver, so the second process can terminate immediately. */
AmgMailtoServer *amg_mailto_server_create(void);
void amg_mailto_server_destroy(AmgMailtoServer *server);
unsigned long amg_mailto_server_signal_mask(const AmgMailtoServer *server);
int amg_mailto_server_receive(AmgMailtoServer *server, char **url_out);
int amg_mailto_forward_to_running(const char *url);

#endif
