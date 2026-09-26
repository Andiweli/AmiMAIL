#ifndef AMIMAIL_TEST_TRANSFER_TLS_DOUBLE_H
#define AMIMAIL_TEST_TRANSFER_TLS_DOUBLE_H
#include "tls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AmgBuffer td_input, td_output;
static size_t td_position, td_chunk, td_reads, td_writes, td_closes;
static size_t td_fail_write;
static int td_missing_ack, td_dummy;

AmgTlsConnection *td_connect(const char *host, unsigned short port,
                             unsigned long timeout, AmgError *error);
int td_starttls(AmgTlsConnection *connection, const char *host, AmgError *error);
long td_read(AmgTlsConnection *connection, void *data, size_t length, AmgError *error);
int td_write_all(AmgTlsConnection *connection, const void *data, size_t length, AmgError *error);
void td_close(AmgTlsConnection *connection);

AmgTlsConnection *td_connect(const char *host, unsigned short port,
                             unsigned long timeout, AmgError *error)
{
    (void)host; (void)port; (void)timeout; (void)error;
    return (AmgTlsConnection *)&td_dummy;
}
int td_starttls(AmgTlsConnection *connection, const char *host, AmgError *error)
{
    (void)connection; (void)host; (void)error; return AMG_OK;
}
long td_read(AmgTlsConnection *connection, void *data, size_t length, AmgError *error)
{
    size_t n = td_input.length - td_position;
    (void)connection;
    ++td_reads;
    if (n > length) n = length;
    if (n > td_chunk) n = td_chunk;
    if (!n) {
        amg_error_set(error, AMG_ERR_TLS, "Injected disconnect.");
        return -1L; /* Real amg_tls_read() reports its reason in AmgError. */
    }
    memcpy(data, td_input.data + td_position, n);
    td_position += n;
    return (long)n;
}
int td_write_all(AmgTlsConnection *connection, const void *data, size_t length, AmgError *error)
{
    (void)connection;
    ++td_writes;
    if (td_fail_write && td_writes >= td_fail_write) {
        amg_error_set(error, AMG_ERR_IO, "Injected write failure."); return AMG_ERR_IO;
    }
    if (amg_buffer_append(&td_output, data, length) != AMG_OK) return AMG_ERR_MEMORY;
    if (td_missing_ack && length == 3U && !memcmp(data, ".\r\n", 3U)) {
        amg_error_set(error, AMG_ERR_IO, "Terminator write status unknown."); return AMG_ERR_IO;
    }
    return AMG_OK;
}
void td_close(AmgTlsConnection *connection)
{
    (void)connection; ++td_closes;
}
static void td_reset(const void *input, size_t length, size_t chunk)
{
    amg_buffer_free(&td_input); amg_buffer_free(&td_output);
    amg_buffer_init(&td_input); amg_buffer_init(&td_output);
    (void)amg_buffer_append(&td_input, input, length);
    td_position = td_reads = td_writes = td_closes = td_fail_write = 0U;
    td_chunk = chunk; td_missing_ack = 0;
}
#define amg_tls_connect td_connect
#define amg_tls_connect_tls12 td_connect
#define amg_tls_connect_plain td_connect
#define amg_tls_starttls td_starttls
#define amg_tls_read td_read
#define amg_tls_write_all td_write_all
#define amg_tls_close td_close
#endif
