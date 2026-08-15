#ifndef AMIGMAIL_TLS_H
#define AMIGMAIL_TLS_H

#include "buffer.h"

typedef struct AmgTlsConnection AmgTlsConnection;

int amg_tls_global_init(AmgError *error);
void amg_tls_global_cleanup(void);
AmgTlsConnection *amg_tls_connect_plain(const char *host, unsigned short port,
                                        unsigned long timeout_seconds,
                                        AmgError *error);
int amg_tls_starttls(AmgTlsConnection *connection, const char *host,
                     AmgError *error);
AmgTlsConnection *amg_tls_connect(const char *host, unsigned short port,
                                  unsigned long timeout_seconds, AmgError *error);
long amg_tls_read(AmgTlsConnection *connection, void *data, size_t length, AmgError *error);
long amg_tls_write(AmgTlsConnection *connection, const void *data, size_t length, AmgError *error);
int amg_tls_write_all(AmgTlsConnection *connection, const void *data, size_t length, AmgError *error);
void amg_tls_close(AmgTlsConnection *connection);
int amg_https_post_form(const char *host, const char *path, const char *form,
                        AmgBuffer *response_body, AmgError *error);

#endif
