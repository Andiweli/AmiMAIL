#include "tls.h"
#include "i18n.h"

#define T(id, en) amg_tr((id), (en))

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if AMIGMAIL_AMIGA
#include <errno.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/bsdsocket.h>
#include <proto/amissl.h>
#include <proto/amisslmaster.h>
#include <amissl/amissl.h>
#include <libraries/amissl.h>
#include <libraries/amisslmaster.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>

struct Library *SocketBase = NULL;
struct Library *AmiSSLMasterBase = NULL;
struct Library *AmiSSLBase = NULL;
struct Library *AmiSSLExtBase = NULL;
static int tls_users = 0;

struct AmgTlsConnection {
    int socket_fd;
    unsigned long timeout_seconds;
    SSL_CTX *context;
    SSL *ssl;
};

static const char *socket_error_text(int value)
{
#ifdef ECONNRESET
    if (value == ECONNRESET) return "connection reset by peer";
#endif
#ifdef ETIMEDOUT
    if (value == ETIMEDOUT) return "connection timed out";
#endif
#ifdef ECONNREFUSED
    if (value == ECONNREFUSED) return "connection refused";
#endif
#ifdef EHOSTUNREACH
    if (value == EHOSTUNREACH) return "host unreachable";
#endif
#ifdef ENETUNREACH
    if (value == ENETUNREACH) return "network unreachable";
#endif
#ifdef EPIPE
    if (value == EPIPE) return "broken pipe";
#endif
#ifdef EAGAIN
    if (value == EAGAIN) return "operation would block or timed out";
#endif
#ifdef EWOULDBLOCK
#if !defined(EAGAIN) || EWOULDBLOCK != EAGAIN
    if (value == EWOULDBLOCK) return "operation would block or timed out";
#endif
#endif
    return "socket error";
}

static const char *ssl_error_text(int value)
{
    switch (value) {
        case SSL_ERROR_NONE: return "SSL_ERROR_NONE";
        case SSL_ERROR_ZERO_RETURN: return "SSL_ERROR_ZERO_RETURN";
        case SSL_ERROR_WANT_READ: return "SSL_ERROR_WANT_READ";
        case SSL_ERROR_WANT_WRITE: return "SSL_ERROR_WANT_WRITE";
        case SSL_ERROR_SYSCALL: return "SSL_ERROR_SYSCALL";
        case SSL_ERROR_SSL: return "SSL_ERROR_SSL";
        default: return "SSL_ERROR_UNKNOWN";
    }
}

static void ssl_error(AmgError *error, int code, const char *prefix)
{
    unsigned long value = ERR_get_error();
    char detail[160], combined[256];
    if (value)
        ERR_error_string_n(value, detail, sizeof(detail));
    else if (errno)
        snprintf(detail, sizeof(detail), "%s (socket errno %d)",
                 socket_error_text(errno), errno);
    else
        snprintf(detail, sizeof(detail), "no AmiSSL error text");
    snprintf(combined, sizeof(combined), "%s: %s", prefix, detail);
    amg_error_set(error, code, combined);
}

static void ssl_operation_error(AmgError *error, int code,
                                const char *prefix, SSL *ssl,
                                int io_result, int ssl_result,
                                int socket_error)
{
    unsigned long value = ERR_get_error();
    const char *tls_version = ssl ? SSL_get_version(ssl) : NULL;
    const char *cipher = ssl ? SSL_get_cipher_name(ssl) : NULL;
    char detail[176], combined[256];

    if (value) {
        char ami_detail[128];
        ERR_error_string_n(value, ami_detail, sizeof(ami_detail));
        snprintf(detail, sizeof(detail), "%s: %s",
                 ssl_error_text(ssl_result), ami_detail);
    } else if (ssl_result == SSL_ERROR_SYSCALL) {
        if (socket_error)
            snprintf(detail, sizeof(detail),
                     "%s: %s (socket errno %d)",
                     ssl_error_text(ssl_result),
                     socket_error_text(socket_error), socket_error);
        else if (io_result == 0)
            snprintf(detail, sizeof(detail),
                     "%s: peer closed the connection without TLS close_notify",
                     ssl_error_text(ssl_result));
        else
            snprintf(detail, sizeof(detail), "%s: no socket error reported",
                     ssl_error_text(ssl_result));
    } else if (ssl_result == SSL_ERROR_ZERO_RETURN) {
        snprintf(detail, sizeof(detail),
                 "%s: peer closed the TLS connection",
                 ssl_error_text(ssl_result));
    } else {
        snprintf(detail, sizeof(detail), "%s (socket errno %d)",
                 ssl_error_text(ssl_result), socket_error);
    }

    if (tls_version && *tls_version && cipher && *cipher)
        snprintf(combined, sizeof(combined), "%s: %s [TLS %s, %s]",
                 prefix, detail, tls_version, cipher);
    else
        snprintf(combined, sizeof(combined), "%s: %s", prefix, detail);
    amg_error_set(error, code, combined);
}

int amg_tls_global_init(AmgError *error)
{
    if (tls_users++ > 0) return AMG_OK;
    if (!(SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4))) {
        --tls_users; amg_error_set(error, AMG_ERR_TLS, T(MSG_BSDSOCKET_LIBRARY_V4_IS_MISSING, "bsdsocket.library V4 is missing.")); return AMG_ERR_TLS;
    }
    if (!(AmiSSLMasterBase = OpenLibrary((CONST_STRPTR)"amisslmaster.library", AMISSLMASTER_MIN_VERSION))) {
        CloseLibrary(SocketBase); SocketBase = NULL; --tls_users;
        amg_error_set(error, AMG_ERR_TLS, T(MSG_AMISSL_V5_OR_AMISSLMASTER_LIBRARY_IS_MISSING, "AmiSSL v5 or amisslmaster.library is missing.")); return AMG_ERR_TLS;
    }
    {
        struct TagItem ami_ssl_tags[] = {
            { AmiSSL_UsesOpenSSLStructs, FALSE },
            { AmiSSL_GetAmiSSLBase, (ULONG)(uintptr_t)&AmiSSLBase },
            { AmiSSL_GetAmiSSLExtBase, (ULONG)(uintptr_t)&AmiSSLExtBase },
            { AmiSSL_SocketBase, (ULONG)(uintptr_t)SocketBase },
            { AmiSSL_ErrNoPtr, (ULONG)(uintptr_t)&errno },
            { TAG_DONE, 0 }
        };
        if (OpenAmiSSLTagList(AMISSL_CURRENT_VERSION, ami_ssl_tags) != 0) {
            CloseLibrary(AmiSSLMasterBase); AmiSSLMasterBase = NULL;
            CloseLibrary(SocketBase); SocketBase = NULL; --tls_users;
            amg_error_set(error, AMG_ERR_TLS, T(MSG_AMISSL_COULD_NOT_BE_INITIALIZED, "AmiSSL could not be initialized.")); return AMG_ERR_TLS;
        }
    }
    if (!OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL)) {
        CloseAmiSSL(); AmiSSLBase = NULL; AmiSSLExtBase = NULL;
        CloseLibrary(AmiSSLMasterBase); AmiSSLMasterBase = NULL;
        CloseLibrary(SocketBase); SocketBase = NULL; --tls_users;
        amg_error_set(error, AMG_ERR_TLS, T(MSG_OPENSSL_INITIALIZATION_FAILED, "OpenSSL initialization failed.")); return AMG_ERR_TLS;
    }
    return AMG_OK;
}

void amg_tls_global_cleanup(void)
{
    if (tls_users <= 0 || --tls_users > 0) return;
    if (AmiSSLBase) { CloseAmiSSL(); AmiSSLBase = NULL; AmiSSLExtBase = NULL; }
    if (AmiSSLMasterBase) { CloseLibrary(AmiSSLMasterBase); AmiSSLMasterBase = NULL; }
    if (SocketBase) { CloseLibrary(SocketBase); SocketBase = NULL; }
}

static int open_socket(const char *host, unsigned short port,
                       unsigned long timeout, AmgError *error)
{
    const struct hostent *entry;
    char **address_cursor;
    struct sockaddr_in address;
    struct timeval tv;
    int fd = -1;
    int last_error = 0;

    entry = gethostbyname((STRPTR)host);
    if (!entry || !entry->h_addr_list || !entry->h_addr_list[0]) {
        char message[256];
        amg_tr_snprintf(message, sizeof(message),
                        MSG_SERVER_NAME_VALUE_COULD_NOT_BE_RESOLVED,
                        "Server name %.190s could not be resolved.", host);
        amg_error_set(error, AMG_ERR_IO, message);
        return -1;
    }

    tv.tv_sec = (long)(timeout ? timeout : 30U);
    tv.tv_usec = 0;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    /* Try every IPv4 address returned by DNS instead of failing on the first
     * endpoint.  Gmail and many other providers publish multiple frontends,
     * and a single stale/unreachable address must not make the whole account
     * look offline. */
    for (address_cursor = entry->h_addr_list; *address_cursor;
         ++address_cursor) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            char message[256];
            amg_tr_snprintf(message, sizeof(message),
                            MSG_SOCKET_COULD_NOT_BE_OPENED_ERRNO_VALUE,
                            "Socket could not be opened (errno %d).", errno);
            amg_error_set(error, AMG_ERR_IO, message);
            return -1;
        }

        (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                         (char *)&tv, sizeof(tv));
        (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
                         (char *)&tv, sizeof(tv));
        memcpy(&address.sin_addr, *address_cursor,
               (size_t)entry->h_length);

        errno = 0;
        if (connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0)
            return fd;

        last_error = errno;
        CloseSocket(fd);
        fd = -1;
    }

    {
        char message[256];
        amg_tr_snprintf(message, sizeof(message),
                        MSG_TCP_CONNECTION_TO_VALUE_VALUE_FAILED_ERRNO_VALUE,
                        "TCP connection to %.175s:%u failed (errno %d).",
                        host, (unsigned)port, last_error);
        amg_error_set(error, AMG_ERR_IO, message);
    }
    return -1;
}

AmgTlsConnection *amg_tls_connect_plain(const char *host, unsigned short port,
                                        unsigned long timeout_seconds,
                                        AmgError *error)
{
    AmgTlsConnection *connection;
    if (!host || !*host) return NULL;
    connection = (AmgTlsConnection *)calloc(1, sizeof(*connection));
    if (!connection) {
        amg_error_set(error, AMG_ERR_MEMORY,
                      T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory."));
        return NULL;
    }
    connection->socket_fd = -1;
    connection->timeout_seconds = timeout_seconds ? timeout_seconds : 30U;
    connection->socket_fd = open_socket(host, port,
                                        connection->timeout_seconds, error);
    if (connection->socket_fd < 0) {
        amg_tls_close(connection);
        return NULL;
    }
    return connection;
}

static int amg_tls_starttls_internal(AmgTlsConnection *connection, const char *host,
                                      int force_tls12, AmgError *error)
{
    if (!connection || connection->socket_fd < 0 || !host || !*host)
        return AMG_ERR_ARGUMENT;
    if (connection->ssl) return AMG_OK;

    connection->context = SSL_CTX_new(TLS_client_method());
    if (!connection->context) {
        ssl_error(error, AMG_ERR_TLS, T(MSG_TLS_CONTEXT, "TLS context"));
        goto fail;
    }
    if (force_tls12) {
#ifdef TLS1_2_VERSION
        if (SSL_CTX_set_min_proto_version(connection->context, TLS1_2_VERSION) != 1 ||
            SSL_CTX_set_max_proto_version(connection->context, TLS1_2_VERSION) != 1) {
            ssl_error(error, AMG_ERR_TLS, "TLS 1.2 compatibility mode");
            goto fail;
        }
#else
        amg_error_set(error, AMG_ERR_TLS,
                      "TLS 1.2 compatibility mode is unavailable in this AmiSSL build.");
        goto fail;
#endif
    }
    SSL_CTX_set_mode(connection->context, SSL_MODE_AUTO_RETRY);
    SSL_CTX_set_verify(connection->context, SSL_VERIFY_PEER, NULL);
    if (SSL_CTX_load_verify_locations(connection->context,
                                      "AmiSSL:Certs/ca-bundle.crt", NULL) != 1 &&
        SSL_CTX_set_default_verify_paths(connection->context) != 1) {
        ssl_error(error, AMG_ERR_TLS,
                  T(MSG_CA_CERTIFICATES_COULD_NOT_BE_LOADED, "CA certificates could not be loaded"));
        goto fail;
    }
    connection->ssl = SSL_new(connection->context);
    if (!connection->ssl) {
        ssl_error(error, AMG_ERR_TLS, T(MSG_TLS_SESSION, "TLS session"));
        goto fail;
    }
    if (SSL_set_tlsext_host_name(connection->ssl, host) != 1 ||
        SSL_set1_host(connection->ssl, host) != 1) {
        ssl_error(error, AMG_ERR_TLS, "TLS-Hostname");
        goto fail;
    }
    if (SSL_set_fd(connection->ssl, connection->socket_fd) != 1) {
        ssl_error(error, AMG_ERR_TLS, "TLS socket binding");
        goto fail;
    }
    {
        int handshake_result, ssl_result, socket_error;
        ERR_clear_error();
        errno = 0;
        handshake_result = SSL_connect(connection->ssl);
        socket_error = errno;
        if (handshake_result != 1) {
            ssl_result = SSL_get_error(connection->ssl, handshake_result);
            ssl_operation_error(error, AMG_ERR_TLS, "TLS-Handshake",
                                connection->ssl, handshake_result,
                                ssl_result, socket_error);
            goto fail;
        }
    }
    if (SSL_get_verify_result(connection->ssl) != X509_V_OK) {
        amg_error_set(error, AMG_ERR_TLS,
                      T(MSG_THE_SERVER_CERTIFICATE_IS_INVALID_PLEASE_ALSO_CHECK, "The server certificate is invalid. Please also check date and time."));
        goto fail;
    }
    return AMG_OK;

fail:
    if (connection->ssl) {
        SSL_free(connection->ssl);
        connection->ssl = NULL;
    }
    if (connection->context) {
        SSL_CTX_free(connection->context);
        connection->context = NULL;
    }
    return error && error->code != AMG_OK ? error->code : AMG_ERR_TLS;
}

int amg_tls_starttls(AmgTlsConnection *connection, const char *host,
                     AmgError *error)
{
    return amg_tls_starttls_internal(connection, host, 0, error);
}

static AmgTlsConnection *amg_tls_connect_internal(const char *host,
                                                   unsigned short port,
                                                   unsigned long timeout_seconds,
                                                   int force_tls12,
                                                   AmgError *error)
{
    AmgTlsConnection *connection =
        amg_tls_connect_plain(host, port, timeout_seconds, error);
    if (!connection) return NULL;
    if (amg_tls_starttls_internal(connection, host, force_tls12, error) != AMG_OK) {
        amg_tls_close(connection);
        return NULL;
    }
    return connection;
}

AmgTlsConnection *amg_tls_connect(const char *host, unsigned short port,
                                  unsigned long timeout_seconds, AmgError *error)
{
    return amg_tls_connect_internal(host, port, timeout_seconds, 0, error);
}

AmgTlsConnection *amg_tls_connect_tls12(const char *host, unsigned short port,
                                        unsigned long timeout_seconds, AmgError *error)
{
    return amg_tls_connect_internal(host, port, timeout_seconds, 1, error);
}

static int wait_for_tls_socket(AmgTlsConnection *connection, int write_ready,
                               AmgError *error)
{
    fd_set read_set, write_set;
    struct timeval timeout;
    LONG ready;
    FD_ZERO(&read_set);
    FD_ZERO(&write_set);
    if (write_ready)
        FD_SET(connection->socket_fd, &write_set);
    else
        FD_SET(connection->socket_fd, &read_set);
    timeout.tv_sec = (long)(connection->timeout_seconds
                                ? connection->timeout_seconds : 30U);
    timeout.tv_usec = 0L;
    ready = WaitSelect(
        connection->socket_fd + 1,
        write_ready ? NULL : &read_set,
        write_ready ? &write_set : NULL,
        NULL, &timeout, NULL);
    if (ready > 0) return 1;
    if (ready == 0)
        amg_error_set(error, AMG_ERR_IO,
                      T(MSG_TIMED_OUT_WAITING_FOR_NETWORK_DATA, "Timed out waiting for network data."));
    else
        amg_error_set(error, AMG_ERR_IO,
                      T(MSG_SOCKET_WAIT_ERROR_WHILE_ACCESSING_THE_NETWORK, "Socket wait error while accessing the network."));
    return 0;
}

static int socket_errno_is_timeout(int value)
{
#ifdef ETIMEDOUT
    if (value == ETIMEDOUT) return 1;
#endif
#ifdef EAGAIN
    if (value == EAGAIN) return 1;
#endif
#ifdef EWOULDBLOCK
    if (value == EWOULDBLOCK) return 1;
#endif
    return 0;
}

long amg_tls_read(AmgTlsConnection *connection, void *data, size_t length,
                  AmgError *error)
{
    int result, ssl_result, socket_error;
    if (!connection || connection->socket_fd < 0 || !data || !length)
        return AMG_ERR_ARGUMENT;

    if (!connection->ssl) {
        if (!wait_for_tls_socket(connection, 0, error))
            return AMG_ERR_IO;
        result = recv(connection->socket_fd, data,
                      length > 0x7fffffffU ? 0x7fffffff : (int)length, 0);
        if (result > 0) return result;
        if (result == 0)
            amg_error_set(error, AMG_ERR_IO,
                          T(MSG_THE_SERVER_CLOSED_THE_TCP_CONNECTION, "The server closed the TCP connection."));
        else if (socket_errno_is_timeout(errno))
            amg_error_set(error, AMG_ERR_IO,
                          T(MSG_TIMED_OUT_WHILE_READING_FROM_THE_MAIL_SERVER, "Timed out while reading from the mail server."));
        else {
            char message[192];
            amg_tr_snprintf(message, sizeof(message), MSG_TCP_READ_ERROR_ERRNO_VALUE, "TCP read error (errno %d).", errno);
            amg_error_set(error, AMG_ERR_IO, message);
        }
        return AMG_ERR_IO;
    }

    for (;;) {
        if (SSL_pending(connection->ssl) <= 0 &&
            !wait_for_tls_socket(connection, 0, error))
            return AMG_ERR_IO;
        ERR_clear_error();
        errno = 0;
        result = SSL_read(connection->ssl, data,
                          length > 0x7fffffffU ? 0x7fffffff : (int)length);
        if (result > 0) return result;
        socket_error = errno;
        ssl_result = SSL_get_error(connection->ssl, result);
        if (ssl_result == SSL_ERROR_WANT_READ) {
            if (!wait_for_tls_socket(connection, 0, error))
                return AMG_ERR_IO;
            continue;
        }
        if (ssl_result == SSL_ERROR_WANT_WRITE) {
            if (!wait_for_tls_socket(connection, 1, error))
                return AMG_ERR_IO;
            continue;
        }
        if (ssl_result == SSL_ERROR_ZERO_RETURN)
            amg_error_set(error, AMG_ERR_IO,
                          T(MSG_THE_SERVER_CLOSED_THE_TLS_CONNECTION, "The server closed the TLS connection."));
        else
            ssl_operation_error(error, AMG_ERR_IO,
                                T(MSG_TLS_READ_ERROR, "TLS read error"),
                                connection->ssl, result, ssl_result,
                                socket_error);
        return AMG_ERR_IO;
    }
}

long amg_tls_write(AmgTlsConnection *connection, const void *data,
                   size_t length, AmgError *error)
{
    int result, ssl_result, socket_error;
    if (!connection || connection->socket_fd < 0 || (!data && length))
        return AMG_ERR_ARGUMENT;
    if (!length) return 0;

    if (!connection->ssl) {
        if (!wait_for_tls_socket(connection, 1, error))
            return AMG_ERR_IO;
        result = send(connection->socket_fd, (char *)data,
                      length > 0x7fffffffU ? 0x7fffffff : (int)length, 0);
        if (result > 0) return result;
        if (socket_errno_is_timeout(errno))
            amg_error_set(error, AMG_ERR_IO,
                          T(MSG_TIMED_OUT_WHILE_WRITING_TO_THE_MAIL_SERVER, "Timed out while writing to the mail server."));
        else {
            char message[192];
            amg_tr_snprintf(message, sizeof(message), MSG_TCP_WRITE_ERROR_ERRNO_VALUE, "TCP write error (errno %d).", errno);
            amg_error_set(error, AMG_ERR_IO, message);
        }
        return AMG_ERR_IO;
    }

    for (;;) {
        if (!wait_for_tls_socket(connection, 1, error))
            return AMG_ERR_IO;
        ERR_clear_error();
        errno = 0;
        result = SSL_write(connection->ssl, data,
                           length > 0x7fffffffU ? 0x7fffffff : (int)length);
        if (result > 0) return result;
        socket_error = errno;
        ssl_result = SSL_get_error(connection->ssl, result);
        if (ssl_result == SSL_ERROR_WANT_READ) {
            if (!wait_for_tls_socket(connection, 0, error))
                return AMG_ERR_IO;
            continue;
        }
        if (ssl_result == SSL_ERROR_WANT_WRITE) {
            if (!wait_for_tls_socket(connection, 1, error))
                return AMG_ERR_IO;
            continue;
        }
        ssl_operation_error(error, AMG_ERR_IO,
                            T(MSG_TLS_WRITE_ERROR, "TLS write error"),
                            connection->ssl, result, ssl_result,
                            socket_error);
        return AMG_ERR_IO;
    }
}

void amg_tls_close(AmgTlsConnection *connection)
{
    if (!connection) return;
    if (connection->ssl) { SSL_shutdown(connection->ssl); SSL_free(connection->ssl); }
    if (connection->context) SSL_CTX_free(connection->context);
    if (connection->socket_fd >= 0) CloseSocket(connection->socket_fd);
    free(connection);
}

#else

struct AmgTlsConnection { int unavailable; };
int amg_tls_global_init(AmgError *error) { amg_error_set(error, AMG_ERR_UNSUPPORTED, T(MSG_AMISSL_IS_ONLY_AVAILABLE_IN_THE_AMIGAOS_BUILD, "AmiSSL is only available in the AmigaOS build.")); return AMG_ERR_UNSUPPORTED; }
void amg_tls_global_cleanup(void) {}
AmgTlsConnection *amg_tls_connect_plain(const char *host, unsigned short port, unsigned long timeout, AmgError *error)
{ (void)host; (void)port; (void)timeout; amg_error_set(error, AMG_ERR_UNSUPPORTED, T(MSG_HOST_BUILD_WITHOUT_AMISSL_NETWORKING, "Host build without AmiSSL networking.")); return NULL; }
int amg_tls_starttls(AmgTlsConnection *connection, const char *host, AmgError *error)
{ (void)connection; (void)host; amg_error_set(error, AMG_ERR_UNSUPPORTED, T(MSG_HOST_BUILD_WITHOUT_AMISSL_NETWORKING, "Host build without AmiSSL networking.")); return AMG_ERR_UNSUPPORTED; }
AmgTlsConnection *amg_tls_connect(const char *host, unsigned short port, unsigned long timeout, AmgError *error)
{ (void)host; (void)port; (void)timeout; amg_error_set(error, AMG_ERR_UNSUPPORTED, T(MSG_HOST_BUILD_WITHOUT_AMISSL_NETWORKING, "Host build without AmiSSL networking.")); return NULL; }
AmgTlsConnection *amg_tls_connect_tls12(const char *host, unsigned short port, unsigned long timeout, AmgError *error)
{ (void)host; (void)port; (void)timeout; amg_error_set(error, AMG_ERR_UNSUPPORTED, T(MSG_HOST_BUILD_WITHOUT_AMISSL_NETWORKING, "Host build without AmiSSL networking.")); return NULL; }
long amg_tls_read(AmgTlsConnection *c, void *d, size_t l, AmgError *e)
{ (void)c;(void)d;(void)l;amg_error_set(e,AMG_ERR_UNSUPPORTED,T(MSG_HOST_BUILD_WITHOUT_TLS, "Host build without TLS."));return AMG_ERR_UNSUPPORTED; }
long amg_tls_write(AmgTlsConnection *c, const void *d, size_t l, AmgError *e)
{ (void)c;(void)d;(void)l;amg_error_set(e,AMG_ERR_UNSUPPORTED,T(MSG_HOST_BUILD_WITHOUT_TLS, "Host build without TLS."));return AMG_ERR_UNSUPPORTED; }
void amg_tls_close(AmgTlsConnection *connection) { (void)connection; }
#endif

int amg_tls_write_all(AmgTlsConnection *connection, const void *data, size_t length, AmgError *error)
{
    const unsigned char *p = (const unsigned char *)data;
    while (length) {
        long written = amg_tls_write(connection, p, length, error);
        if (written <= 0) return written < 0 ? (int)written : AMG_ERR_IO;
        p += written; length -= (size_t)written;
    }
    return AMG_OK;
}

static int decode_chunked(const unsigned char *data, size_t length, AmgBuffer *output)
{
    size_t position = 0;
    while (position < length) {
        char number[32], *endptr; size_t line_end = position, digits, chunk;
        while (line_end + 1U < length && !(data[line_end] == '\r' && data[line_end + 1U] == '\n')) ++line_end;
        if (line_end + 1U >= length) return AMG_ERR_PARSE;
        digits = line_end - position; if (digits >= sizeof(number)) return AMG_ERR_PARSE;
        memcpy(number, data + position, digits); number[digits] = 0; chunk = strtoul(number, &endptr, 16);
        if (endptr == number) return AMG_ERR_PARSE;
        position = line_end + 2U; if (!chunk) return AMG_OK;
        if (chunk > length - position || amg_buffer_append(output, data + position, chunk) != AMG_OK) return AMG_ERR_PARSE;
        position += chunk; if (position + 2U > length || data[position] != '\r' || data[position + 1U] != '\n') return AMG_ERR_PARSE;
        position += 2U;
    }
    return AMG_ERR_PARSE;
}

int amg_https_post_form(const char *host, const char *path, const char *form,
                        AmgBuffer *response_body, AmgError *error)
{
    AmgTlsConnection *connection;
    AmgBuffer request, response;
    unsigned char block[2048];
    const unsigned char *header_end;
    size_t header_length;
    long count;
    int status = 0, result = AMG_OK;
    if (!host || !path || !form || !response_body) return AMG_ERR_ARGUMENT;
    connection = amg_tls_connect(host, 443, 30, error); if (!connection) return error ? error->code : AMG_ERR_TLS;
    amg_buffer_init(&request); amg_buffer_init(&response);
    {
        char length_text[32]; snprintf(length_text, sizeof(length_text), "%lu", (unsigned long)strlen(form));
        amg_buffer_append_cstr(&request, "POST "); amg_buffer_append_cstr(&request, path); amg_buffer_append_cstr(&request, " HTTP/1.1\r\nHost: ");
        amg_buffer_append_cstr(&request, host); amg_buffer_append_cstr(&request, "\r\nUser-Agent: AmiMail/" AMIGMAIL_VERSION
            "\r\nContent-Type: application/x-www-form-urlencoded\r\nAccept: application/json\r\nConnection: close\r\nContent-Length: ");
        amg_buffer_append_cstr(&request, length_text); amg_buffer_append_cstr(&request, "\r\n\r\n"); amg_buffer_append_cstr(&request, form);
    }
    result = amg_tls_write_all(connection, request.data, request.length, error);
    while (result == AMG_OK && response.length <= 1024U * 1024U) {
        count = amg_tls_read(connection, block, sizeof(block), error);
        if (count <= 0) break;
        result = amg_buffer_append(&response, block, (size_t)count);
    }
    amg_tls_close(connection); amg_buffer_free(&request);
    if (result != AMG_OK || response.length > 1024U * 1024U) { amg_buffer_free(&response); return result != AMG_OK ? result : AMG_ERR_LIMIT; }
    amg_buffer_terminate(&response); sscanf((const char *)response.data, "HTTP/%*u.%*u %d", &status);
    header_end = (const unsigned char *)strstr((const char *)response.data, "\r\n\r\n");
    if (!header_end) { amg_buffer_free(&response); amg_error_set(error, AMG_ERR_PROTOCOL, T(MSG_INVALID_HTTPS_RESPONSE, "Invalid HTTPS response.")); return AMG_ERR_PROTOCOL; }
    header_length = (size_t)(header_end - response.data) + 4U;
    if (strstr((const char *)response.data, "Transfer-Encoding: chunked") || strstr((const char *)response.data, "transfer-encoding: chunked"))
        result = decode_chunked(response.data + header_length, response.length - header_length, response_body);
    else result = amg_buffer_append(response_body, response.data + header_length, response.length - header_length);
    amg_buffer_free(&response);
    if (status < 200 || status >= 300) { amg_error_set(error, AMG_ERR_AUTH, T(MSG_GOOGLE_REJECTED_THE_OAUTH_REQUEST, "Google rejected the OAuth request.")); return AMG_ERR_AUTH; }
    return result;
}
