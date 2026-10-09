#include "tls.h"
#include "i18n.h"

#define T(id, en) amg_tr((id), (en))

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if AMIGMAIL_AMIGA
#include <errno.h>
#include <exec/libraries.h>
#include <exec/semaphores.h>
#include <exec/tasks.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <libraries/bsdsocket.h>
#include <proto/exec.h>

/* bsdsocket.library keeps opener state per task.  The generated inline
 * stubs normally use one global SocketBase, which is not sufficient for the
 * three independent AmiMail network processes.  Resolve the base belonging
 * to the calling task for every direct socket call. */
static struct Library *amg_tls_current_socket_base(void);
#define BSDSOCKET_BASE_NAME amg_tls_current_socket_base()
#include <proto/bsdsocket.h>
#include <proto/amissl.h>
#include <proto/amisslmaster.h>
#include <amissl/amissl.h>
#include <libraries/amissl.h>
#include <libraries/amisslmaster.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>

struct Library *SocketBase = NULL;
struct Library *AmiSSLMasterBase = NULL;
struct Library *AmiSSLBase = NULL;
struct Library *AmiSSLExtBase = NULL;

#define AMG_TLS_MAX_TASKS 8U

typedef struct AmgTlsTaskState {
    struct Task *task;
    struct Library *socket_base;
    unsigned int references;
    volatile int *cancel_flag;
    int socket_errno;
} AmgTlsTaskState;

static AmgTlsTaskState tls_tasks[AMG_TLS_MAX_TASKS];
static struct SignalSemaphore tls_state_lock;
static int tls_state_lock_ready = 0;
static unsigned int tls_users = 0U;
static AmgTlsTaskState *tls_find_task_state(struct Task *task);

struct AmgTlsConnection {
    int socket_fd;
    unsigned long timeout_seconds;
    volatile int *cancel_flag;
    SSL_CTX *context;
    SSL *ssl;
    struct MsgPort *timer_port;
    struct timerequest *timer;
    int timer_open;
    unsigned int deadline_depth;
};

/* libnix errno is shared by subprocesses. Socket/AmiSSL errors must instead
 * use the opener's own storage, including during concurrent handshakes. */
static int *tls_errno_pointer(void)
{
    static int fallback;
    AmgTlsTaskState *state = tls_find_task_state(FindTask(NULL));
    return state ? &state->socket_errno : &fallback;
}
#undef errno
#define errno (*tls_errno_pointer())

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

static void tls_ensure_state_lock(void)
{
    if (tls_state_lock_ready) return;
    Forbid();
    if (!tls_state_lock_ready) {
        InitSemaphore(&tls_state_lock);
        tls_state_lock_ready = 1;
    }
    Permit();
}

static AmgTlsTaskState *tls_find_task_state(struct Task *task)
{
    size_t index;
    for (index = 0U; index < AMG_TLS_MAX_TASKS; ++index)
        if (tls_tasks[index].task == task)
            return &tls_tasks[index];
    return NULL;
}

static AmgTlsTaskState *tls_find_free_task_state(void)
{
    size_t index;
    for (index = 0U; index < AMG_TLS_MAX_TASKS; ++index)
        if (!tls_tasks[index].task)
            return &tls_tasks[index];
    return NULL;
}

static struct Library *amg_tls_current_socket_base(void)
{
    AmgTlsTaskState *state = tls_find_task_state(FindTask(NULL));
    return state && state->socket_base ? state->socket_base : SocketBase;
}

static void tls_close_shared_instance(void)
{
    if (AmiSSLBase) {
        CloseAmiSSL();
        AmiSSLBase = NULL;
        AmiSSLExtBase = NULL;
    }
    if (AmiSSLMasterBase) {
        CloseLibrary(AmiSSLMasterBase);
        AmiSSLMasterBase = NULL;
    }
}

int amg_tls_global_init(AmgError *error)
{
    struct Task *task = FindTask(NULL);
    AmgTlsTaskState *state;
    struct Library *task_socket_base = NULL;
    int shared_opened_here = 0;
    int initialized;
    struct TagItem task_tags[4];

    tls_ensure_state_lock();
    ObtainSemaphore(&tls_state_lock);
    state = tls_find_task_state(task);
    if (state) {
        ++state->references;
        ReleaseSemaphore(&tls_state_lock);
        return AMG_OK;
    }

    state = tls_find_free_task_state();
    if (!state) {
        ReleaseSemaphore(&tls_state_lock);
        amg_error_set(error, AMG_ERR_MEMORY,
                      T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory."));
        return AMG_ERR_MEMORY;
    }

    task_socket_base = OpenLibrary(
        (CONST_STRPTR)"bsdsocket.library", 4);
    if (!task_socket_base) {
        ReleaseSemaphore(&tls_state_lock);
        amg_error_set(error, AMG_ERR_TLS,
                      T(MSG_BSDSOCKET_LIBRARY_V4_IS_MISSING,
                        "bsdsocket.library V4 is missing."));
        return AMG_ERR_TLS;
    }

    if (!AmiSSLBase) {
        struct TagItem open_tags[] = {
            { AmiSSL_UsesOpenSSLStructs, FALSE },
            { AmiSSL_InitAmiSSL, FALSE },
            { AmiSSL_GetAmiSSLBase,
                (ULONG)(uintptr_t)&AmiSSLBase },
            { AmiSSL_GetAmiSSLExtBase,
                (ULONG)(uintptr_t)&AmiSSLExtBase },
            { TAG_DONE, 0 }
        };
        AmiSSLMasterBase = OpenLibrary(
            (CONST_STRPTR)"amisslmaster.library",
            AMISSLMASTER_MIN_VERSION);
        if (!AmiSSLMasterBase ||
            OpenAmiSSLTagList(AMISSL_CURRENT_VERSION, open_tags) != 0) {
            if (AmiSSLMasterBase) {
                CloseLibrary(AmiSSLMasterBase);
                AmiSSLMasterBase = NULL;
            }
            AmiSSLBase = NULL;
            AmiSSLExtBase = NULL;
            CloseLibrary(task_socket_base);
            ReleaseSemaphore(&tls_state_lock);
            amg_error_set(
                error, AMG_ERR_TLS,
                T(MSG_AMISSL_V5_OR_AMISSLMASTER_LIBRARY_IS_MISSING,
                  "AmiSSL v5 or amisslmaster.library is missing."));
            return AMG_ERR_TLS;
        }
        shared_opened_here = 1;
    }

    /* Register the context before InitAmiSSLA(): the task-local socket-base
     * resolver is then already valid if initialization reaches the network
     * library.  AmiSSL's own documentation requires this Init/Cleanup pair
     * in every subprocess sharing one AmiSSLBase. */
    memset(state, 0, sizeof(*state));
    state->task = task;
    state->socket_base = task_socket_base;
    state->references = 1U;
    SocketBase = task_socket_base;
    SetErrnoPtr(&state->socket_errno, sizeof(state->socket_errno));
    task_tags[0].ti_Tag = AmiSSL_SocketBase;
    task_tags[0].ti_Data = (ULONG)(uintptr_t)task_socket_base;
    task_tags[1].ti_Tag = AmiSSL_ErrNoPtr;
    task_tags[1].ti_Data = (ULONG)(uintptr_t)&errno;
    task_tags[2].ti_Tag = AmiSSL_GetAmiSSLExtBase;
    task_tags[2].ti_Data = (ULONG)(uintptr_t)&AmiSSLExtBase;
    task_tags[3].ti_Tag = TAG_DONE;
    task_tags[3].ti_Data = 0;

    initialized = InitAmiSSLA(task_tags) == 0;
    if (!initialized ||
        !OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS |
                          OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL)) {
        if (initialized) (void)CleanupAmiSSLA(NULL);
        memset(state, 0, sizeof(*state));
        CloseLibrary(task_socket_base);
        if (shared_opened_here) tls_close_shared_instance();
        ReleaseSemaphore(&tls_state_lock);
        amg_error_set(error, AMG_ERR_TLS,
                      T(MSG_AMISSL_COULD_NOT_BE_INITIALIZED,
                        "AmiSSL could not be initialized."));
        return AMG_ERR_TLS;
    }
    ++tls_users;
    ReleaseSemaphore(&tls_state_lock);
    return AMG_OK;
}

void amg_tls_set_cancel_flag(volatile int *cancel_flag)
{
    AmgTlsTaskState *state;
    if (!tls_state_lock_ready) return;
    ObtainSemaphore(&tls_state_lock);
    state = tls_find_task_state(FindTask(NULL));
    if (state) state->cancel_flag = cancel_flag;
    ReleaseSemaphore(&tls_state_lock);
}

void amg_tls_global_cleanup(void)
{
    AmgTlsTaskState *state;
    struct Library *task_socket_base;
    if (!tls_state_lock_ready) return;
    ObtainSemaphore(&tls_state_lock);
    state = tls_find_task_state(FindTask(NULL));
    if (!state) {
        ReleaseSemaphore(&tls_state_lock);
        return;
    }
    if (--state->references > 0U) {
        ReleaseSemaphore(&tls_state_lock);
        return;
    }
    task_socket_base = state->socket_base;
    (void)CleanupAmiSSLA(NULL);
    memset(state, 0, sizeof(*state));
    CloseLibrary(task_socket_base);
    if (tls_users > 0U) --tls_users;
    if (!tls_users) {
        SocketBase = NULL;
        tls_close_shared_instance();
    }
    ReleaseSemaphore(&tls_state_lock);
}

static void tls_deadline_end(AmgTlsConnection *connection)
{
    if (!connection->deadline_depth || --connection->deadline_depth) return;
    (void)SocketBaseTags(SBTM_SETVAL(SBTC_BREAKMASK), SIGBREAKF_CTRL_C, TAG_DONE);
    if (!CheckIO((struct IORequest *)connection->timer))
        AbortIO((struct IORequest *)connection->timer);
    WaitIO((struct IORequest *)connection->timer);
    SetSignal(0UL, 1UL << connection->timer_port->mp_SigBit);
}

static int tls_deadline_begin(AmgTlsConnection *connection, AmgError *error)
{
    if (connection->deadline_depth) {
        ++connection->deadline_depth;
        return 1;
    }
    if (!connection->timer_open) {
        if (!connection->timer_port) connection->timer_port = CreateMsgPort();
        if (connection->timer_port && !connection->timer)
            connection->timer = (struct timerequest *)CreateIORequest(
                connection->timer_port, sizeof(*connection->timer));
        if (!connection->timer || OpenDevice((CONST_STRPTR)TIMERNAME,
                UNIT_VBLANK, (struct IORequest *)connection->timer, 0UL)) {
            amg_error_set(error, AMG_ERR_IO, "Network deadline timer unavailable.");
            return 0;
        }
        connection->timer_open = 1;
    }
    /* One timer spans every WANT_READ/WANT_WRITE retry and every partial
     * write. Readiness notifications do not extend the operation's budget. */
    SetSignal(0UL, SIGBREAKF_CTRL_C);
    if (SocketBaseTags(SBTM_SETVAL(SBTC_BREAKMASK),
            SIGBREAKF_CTRL_C | (1UL << connection->timer_port->mp_SigBit),
            TAG_DONE) != 0) {
        amg_error_set(error, AMG_ERR_IO, "Socket interrupt configuration failed.");
        return 0;
    }
    connection->timer->tr_node.io_Command = TR_ADDREQUEST;
    connection->timer->tr_time.tv_secs = connection->timeout_seconds;
    connection->timer->tr_time.tv_micro = 0UL;
    SendIO((struct IORequest *)connection->timer);
    connection->deadline_depth = 1U;
    return 1;
}

static int tls_deadline_check(AmgTlsConnection *connection, AmgError *error)
{
    if (connection->cancel_flag && *connection->cancel_flag) {
        amg_error_set(error, AMG_ERR_IO,
                      T(MSG_NETWORK_ERROR, "Network operation cancelled."));
        return 0;
    }
    if (connection->deadline_depth && CheckIO((struct IORequest *)connection->timer)) {
        amg_error_set(error, AMG_ERR_IO,
                      T(MSG_TIMED_OUT_WAITING_FOR_NETWORK_DATA,
                        "Timed out waiting for network data."));
        return 0;
    }
    return 1;
}

static int wait_for_tls_socket(AmgTlsConnection *connection, int write_ready,
                               AmgError *error)
{
    fd_set read_set, write_set;
    struct timeval timeout;
    LONG ready;
    if (connection->socket_fd < 0 || connection->socket_fd >= FD_SETSIZE) {
        amg_error_set(error, AMG_ERR_IO, "Socket descriptor exceeds select capacity.");
        return 0;
    }
    while (tls_deadline_check(connection, error)) {
        FD_ZERO(&read_set);
        FD_ZERO(&write_set);
        if (write_ready) FD_SET(connection->socket_fd, &write_set);
        else FD_SET(connection->socket_fd, &read_set);
        timeout.tv_sec = 1L;
        timeout.tv_usec = 0L;
        ready = WaitSelect(connection->socket_fd + 1,
            write_ready ? NULL : &read_set, write_ready ? &write_set : NULL,
            NULL, &timeout, NULL);
        if (!tls_deadline_check(connection, error)) return 0;
        if (ready > 0) return 1;
        if (ready < 0 && errno != EINTR) {
            amg_error_set(error, AMG_ERR_IO,
                          T(MSG_SOCKET_WAIT_ERROR_WHILE_ACCESSING_THE_NETWORK,
                            "Socket wait error while accessing the network."));
            return 0;
        }
    }
    return 0;
}

static int socket_would_block(int value)
{
    return value == EAGAIN || value == EWOULDBLOCK || value == EINTR;
}

static int open_socket(AmgTlsConnection *connection, const char *host,
                       unsigned short port, AmgError *error)
{
    const struct hostent *entry;
    char **address_cursor;
    struct sockaddr_in address;
    int fd = -1;
    int last_error = 0;

    if (!tls_deadline_check(connection, error)) return -1;
    entry = gethostbyname((STRPTR)host);
    if (!tls_deadline_check(connection, error)) return -1;
    if (!entry || entry->h_addrtype != AF_INET ||
        entry->h_length != (int)sizeof(address.sin_addr) ||
        !entry->h_addr_list || !entry->h_addr_list[0]) {
        char message[256];
        amg_tr_snprintf(message, sizeof(message),
                        MSG_SERVER_NAME_VALUE_COULD_NOT_BE_RESOLVED,
                        "Server name %.190s could not be resolved.", host);
        amg_error_set(error, AMG_ERR_IO, message);
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    /* Try every IPv4 address returned by DNS instead of failing on the first
     * endpoint.  Gmail and many other providers publish multiple frontends,
     * and a single stale/unreachable address must not make the whole account
     * look offline. */
    for (address_cursor = entry->h_addr_list; *address_cursor;
         ++address_cursor) {
        LONG nonblocking = 1L, socket_status;
        socklen_t status_size = sizeof(socket_status);
        if (!tls_deadline_check(connection, error)) return -1;
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            char message[256];
            amg_tr_snprintf(message, sizeof(message),
                            MSG_SOCKET_COULD_NOT_BE_OPENED_ERRNO_VALUE,
                            "Socket could not be opened (errno %d).", errno);
            amg_error_set(error, AMG_ERR_IO, message);
            return -1;
        }

        if (fd >= FD_SETSIZE || IoctlSocket(fd, FIONBIO, &nonblocking) < 0) {
            CloseSocket(fd);
            amg_error_set(error, AMG_ERR_IO, "Nonblocking socket initialization failed.");
            return -1;
        }
        memcpy(&address.sin_addr, *address_cursor,
               (size_t)entry->h_length);

        errno = 0;
        if (connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0)
            return fd;
        last_error = errno;
        if (last_error == EINPROGRESS || last_error == EALREADY ||
            socket_would_block(last_error)) {
            connection->socket_fd = fd;
            if (!wait_for_tls_socket(connection, 1, error)) {
                CloseSocket(fd);
                connection->socket_fd = -1;
                return -1;
            }
            socket_status = 0;
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&socket_status,
                           &status_size) == 0 && socket_status == 0)
                return fd;
            last_error = socket_status ? (int)socket_status : errno;
        }
        CloseSocket(fd);
        connection->socket_fd = -1;
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
    {
        AmgTlsTaskState *state = tls_find_task_state(FindTask(NULL));
        connection->cancel_flag = state ? state->cancel_flag : NULL;
    }
    if (!tls_deadline_begin(connection, error)) {
        amg_tls_close(connection);
        return NULL;
    }
    connection->socket_fd = open_socket(connection, host, port, error);
    tls_deadline_end(connection);
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
    if (!tls_deadline_begin(connection, error)) return AMG_ERR_IO;

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
    while (tls_deadline_check(connection, error)) {
        int handshake_result, ssl_result, socket_error;
        ERR_clear_error();
        errno = 0;
        handshake_result = SSL_connect(connection->ssl);
        socket_error = errno;
        if (handshake_result == 1) break;
        {
            ssl_result = SSL_get_error(connection->ssl, handshake_result);
            if (ssl_result == SSL_ERROR_WANT_READ || ssl_result == SSL_ERROR_WANT_WRITE) {
                if (!wait_for_tls_socket(connection, ssl_result == SSL_ERROR_WANT_WRITE, error))
                    goto fail;
                continue;
            }
            ssl_operation_error(error, AMG_ERR_TLS, "TLS-Handshake",
                                connection->ssl, handshake_result,
                                ssl_result, socket_error);
            goto fail;
        }
    }
    if (!tls_deadline_check(connection, error)) goto fail;
    if (SSL_get_verify_result(connection->ssl) != X509_V_OK) {
        amg_error_set(error, AMG_ERR_TLS,
                      T(MSG_THE_SERVER_CERTIFICATE_IS_INVALID_PLEASE_ALSO_CHECK, "The server certificate is invalid. Please also check date and time."));
        goto fail;
    }
    tls_deadline_end(connection);
    return AMG_OK;

fail:
    tls_deadline_end(connection);
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

static long tls_read_internal(AmgTlsConnection *connection, void *data, size_t length,
                  AmgError *error)
{
    int result, ssl_result, socket_error;
    if (!connection || connection->socket_fd < 0 || !data || !length)
        return AMG_ERR_ARGUMENT;

    if (!connection->ssl) {
      for (;;) {
        if (!wait_for_tls_socket(connection, 0, error))
            return AMG_ERR_IO;
        result = recv(connection->socket_fd, data,
                      length > 0x7fffffffU ? 0x7fffffff : (int)length, 0);
        if (result > 0) return result;
        if (result < 0 && socket_would_block(errno)) continue;
        if (result == 0)
            amg_error_set(error, AMG_ERR_IO,
                          T(MSG_THE_SERVER_CLOSED_THE_TCP_CONNECTION, "The server closed the TCP connection."));
        else {
            char message[192];
            amg_tr_snprintf(message, sizeof(message), MSG_TCP_READ_ERROR_ERRNO_VALUE, "TCP read error (errno %d).", errno);
            amg_error_set(error, AMG_ERR_IO, message);
        }
        return AMG_ERR_IO;
      }
    }

    for (;;) {
        if (!tls_deadline_check(connection, error)) return AMG_ERR_IO;
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

static long tls_write_internal(AmgTlsConnection *connection, const void *data,
                   size_t length, AmgError *error)
{
    int result, ssl_result, socket_error;
    if (!connection || connection->socket_fd < 0 || (!data && length))
        return AMG_ERR_ARGUMENT;
    if (!length) return 0;

    if (!connection->ssl) {
      for (;;) {
        if (!wait_for_tls_socket(connection, 1, error))
            return AMG_ERR_IO;
        result = send(connection->socket_fd, (char *)data,
                      length > 0x7fffffffU ? 0x7fffffff : (int)length, 0);
        if (result > 0) return result;
        if (result < 0 && socket_would_block(errno)) continue;
        {
            char message[192];
            amg_tr_snprintf(message, sizeof(message), MSG_TCP_WRITE_ERROR_ERRNO_VALUE, "TCP write error (errno %d).", errno);
            amg_error_set(error, AMG_ERR_IO, message);
        }
        return AMG_ERR_IO;
      }
    }

    for (;;) {
        if (!tls_deadline_check(connection, error)) return AMG_ERR_IO;
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

long amg_tls_read(AmgTlsConnection *connection, void *data, size_t length,
                  AmgError *error)
{
    long result;
    if (!connection || !data || !length || connection->socket_fd < 0)
        return AMG_ERR_ARGUMENT;
    if (!tls_deadline_begin(connection, error)) return AMG_ERR_IO;
    result = tls_read_internal(connection, data, length, error);
    tls_deadline_end(connection);
    return result;
}

long amg_tls_write(AmgTlsConnection *connection, const void *data, size_t length,
                   AmgError *error)
{
    long result;
    if (!connection || (!data && length) || connection->socket_fd < 0)
        return AMG_ERR_ARGUMENT;
    if (!length) return 0;
    if (!tls_deadline_begin(connection, error)) return AMG_ERR_IO;
    result = tls_write_internal(connection, data, length, error);
    tls_deadline_end(connection);
    return result;
}

void amg_tls_close(AmgTlsConnection *connection)
{
    if (!connection) return;
    while (connection->deadline_depth) tls_deadline_end(connection);
    if (connection->timer_open) CloseDevice((struct IORequest *)connection->timer);
    if (connection->timer) DeleteIORequest((struct IORequest *)connection->timer);
    if (connection->timer_port) DeleteMsgPort(connection->timer_port);
    if (connection->ssl) {
        /* During application shutdown the peer may be unreachable.  A
         * graceful TLS close would then wait on the socket again, defeating
         * the worker cancellation path. */
        if (!connection->cancel_flag || !*connection->cancel_flag)
            SSL_shutdown(connection->ssl);
        SSL_free(connection->ssl);
    }
    if (connection->context) SSL_CTX_free(connection->context);
    if (connection->socket_fd >= 0) CloseSocket(connection->socket_fd);
    free(connection);
}

#else

struct AmgTlsConnection { int unavailable; };
int amg_tls_global_init(AmgError *error) { amg_error_set(error, AMG_ERR_UNSUPPORTED, T(MSG_AMISSL_IS_ONLY_AVAILABLE_IN_THE_AMIGAOS_BUILD, "AmiSSL is only available in the AmigaOS build.")); return AMG_ERR_UNSUPPORTED; }
void amg_tls_global_cleanup(void) {}
void amg_tls_set_cancel_flag(volatile int *cancel_flag) { (void)cancel_flag; }
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
    int result = AMG_OK;
    if (!connection || (!data && length)) return AMG_ERR_ARGUMENT;
#if AMIGMAIL_AMIGA
    if (!length) return AMG_OK;
    if (!tls_deadline_begin(connection, error)) return AMG_ERR_IO;
#endif
    while (length) {
        long written = amg_tls_write(connection, p, length, error);
        if (written <= 0) { result = written < 0 ? (int)written : AMG_ERR_IO; break; }
        p += written; length -= (size_t)written;
    }
#if AMIGMAIL_AMIGA
    tls_deadline_end(connection);
#endif
    return result;
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
