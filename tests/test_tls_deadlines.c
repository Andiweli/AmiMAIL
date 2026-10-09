/* OS doubles for native production functions selected by test_tls_deadlines.py. */
#include "tls.h"
#include "i18n.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/ioctl.h>

typedef long LONG;
typedef unsigned long ULONG;
typedef const char *CONST_STRPTR;
typedef char *STRPTR;
struct IORequest { int io_Command; };
struct timerequest { struct IORequest tr_node; struct { ULONG tv_secs, tv_micro; } tr_time; };
struct MsgPort { unsigned char mp_SigBit; };
typedef struct { int unused; } SSL;
typedef struct { int unused; } SSL_CTX;
typedef struct { volatile int *cancel_flag; } AmgTlsTaskState;
static int cancel_flag, ticks, expires_at, arms, waits, aborts, ports, requests, devices;
static int closes, next_fd, endpoint, bad_address, fail_ioctl, fail_timer, tag_failure;
static int peer_error, connect_mode, recv_mode, send_mode, ssl_mode, ssl_step, last_wanted;
static int dns_calls, recv_calls, send_calls, handshake_calls, cancel_at;
static ULONG break_mask;
static AmgTlsTaskState task_state = { &cancel_flag };
#define T(id, text) (text)
int amg_tr_snprintf(char *output, size_t capacity, long id,
                    const char *format, ...)
{
    int result;
    va_list args;
    (void)id;
    va_start(args, format);
    result = vsnprintf(output, capacity, format, args);
    va_end(args);
    return result;
}
#define SIGBREAKF_CTRL_C (1UL << 12)
#define TIMERNAME "timer.device"
#define UNIT_VBLANK 1
#define TR_ADDREQUEST 1
#define SBTC_BREAKMASK 1
#define SBTM_SETVAL(value) (value)
#define TAG_DONE 0
#define SSL_ERROR_WANT_READ 2
#define SSL_ERROR_WANT_WRITE 3
#define SSL_ERROR_ZERO_RETURN 6
#define SSL_VERIFY_PEER 1
#define SSL_MODE_AUTO_RETRY 4
#define TLS1_2_VERSION 0x303
#define X509_V_OK 0
static struct MsgPort *CreateMsgPort(void) { struct MsgPort *p = calloc(1, sizeof(*p)); assert(p); p->mp_SigBit = 5; ++ports; return p; }
static void DeleteMsgPort(struct MsgPort *p) { --ports; free(p); }
static void *CreateIORequest(struct MsgPort *p, size_t n) { (void)p; ++requests; return calloc(1, n); }
static void DeleteIORequest(struct IORequest *p) { --requests; free(p); }
static int OpenDevice(const char *n, int unit, struct IORequest *p, ULONG flags) { (void)n; (void)unit; (void)p; (void)flags; if (fail_timer) return 1; ++devices; return 0; }
static void CloseDevice(struct IORequest *p) { (void)p; --devices; }
static void SendIO(struct IORequest *p) { struct timerequest *r = (struct timerequest *)p; ++arms; expires_at = ticks + (int)r->tr_time.tv_secs; }
static int CheckIO(struct IORequest *p) { (void)p; return ticks >= expires_at; }
static void AbortIO(struct IORequest *p) { (void)p; ++aborts; }
static void WaitIO(struct IORequest *p) { (void)p; ++waits; }
static ULONG SetSignal(ULONG value, ULONG mask) { (void)value; (void)mask; return 0; }
static LONG SocketBaseTags(ULONG tag, ULONG mask, int end) { (void)tag; (void)end; break_mask = mask; return tag_failure; }
static void *FindTask(const char *name) { (void)name; return NULL; }
static AmgTlsTaskState *tls_find_task_state(void *task) { (void)task; return &task_state; }
static int mock_socket(int a, int b, int c) { (void)a; (void)b; (void)c; ++endpoint; return next_fd; }
static int CloseSocket(int fd) { (void)fd; ++closes; return 0; }
static int IoctlSocket(int fd, ULONG req, LONG *value) { (void)fd; assert(req == FIONBIO && *value == 1); if (fail_ioctl) { errno = EINVAL; return -1; } return 0; }
static int mock_connect(int fd, const struct sockaddr *a, socklen_t n) { (void)fd; (void)a; (void)n; if (connect_mode == 0) return 0; if (connect_mode == 2 && endpoint == 1) { errno = ECONNREFUSED; return -1; } errno = EINPROGRESS; return -1; }
static int mock_getsockopt(int fd, int level, int opt, void *p, socklen_t *n) { (void)fd; assert(level == SOL_SOCKET && opt == SO_ERROR && *n == sizeof(LONG)); *(LONG *)p = peer_error; return 0; }
static struct hostent *mock_dns(const char *host) { static struct hostent entry; static char address[4], *list[] = {address, address, NULL}; (void)host; ++dns_calls; entry.h_addrtype = AF_INET; entry.h_length = bad_address ? 16 : 4; entry.h_addr_list = list; return &entry; }
static LONG WaitSelect(int n, fd_set *r, fd_set *w, void *e, struct timeval *tv, void *sig) { (void)n; (void)e; (void)sig; assert(tv->tv_sec == 1); last_wanted = w ? 1 : 0; assert(r || w); ++ticks; if (cancel_at && ticks >= cancel_at) cancel_flag = 1; return connect_mode == 3 ? 0 : 1; }
static int mock_recv(int fd, void *p, int n, int flags) { (void)fd; (void)n; (void)flags; ++recv_calls; if (recv_mode == 1 && recv_calls == 1) { errno = EAGAIN; return -1; } if (recv_mode == 2) { errno = ECONNRESET; return -1; } if (recv_mode == 3) return 0; *(char *)p = 'x'; return 1; }
static int mock_send(int fd, const void *p, int n, int flags) { (void)fd; (void)p; (void)n; (void)flags; ++send_calls; if (send_mode == 1 && send_calls == 1) { errno = EAGAIN; return -1; } return 1; }
static SSL_CTX *SSL_CTX_new(int method) { (void)method; return calloc(1, sizeof(SSL_CTX)); }
static int TLS_client_method(void) { return 0; }
static int SSL_CTX_set_min_proto_version(SSL_CTX *p, int v) { (void)p; (void)v; return 1; }
static int SSL_CTX_set_max_proto_version(SSL_CTX *p, int v) { (void)p; (void)v; return 1; }
static void SSL_CTX_set_mode(SSL_CTX *p, int mode) { (void)p; (void)mode; }
static void SSL_CTX_set_verify(SSL_CTX *p, int mode, void *callback) { (void)p; (void)mode; (void)callback; }
static int SSL_CTX_load_verify_locations(SSL_CTX *p, const char *path, void *dir) { (void)p; (void)path; (void)dir; return 1; }
static int SSL_CTX_set_default_verify_paths(SSL_CTX *p) { (void)p; return 1; }
static SSL *SSL_new(SSL_CTX *p) { (void)p; return calloc(1, sizeof(SSL)); }
static int SSL_set_tlsext_host_name(SSL *p, const char *host) { (void)p; (void)host; return 1; }
static int SSL_set1_host(SSL *p, const char *host) { (void)p; (void)host; return 1; }
static int SSL_set_fd(SSL *p, int fd) { (void)p; (void)fd; return 1; }
static void ERR_clear_error(void) {}
static int SSL_connect(SSL *p) { (void)p; ++handshake_calls; return ssl_mode == 1 && handshake_calls > 2 ? 1 : -1; }
static int SSL_get_error(SSL *p, int result) { (void)p; (void)result; return ++ssl_step % 2 ? SSL_ERROR_WANT_WRITE : SSL_ERROR_WANT_READ; }
static int SSL_get_verify_result(SSL *p) { (void)p; return 0; }
static int SSL_read(SSL *p, void *data, int n) { (void)p; (void)n; ++recv_calls; if (ssl_mode == 1 && recv_calls > 2) { *(char *)data = 'x'; return 1; } return -1; }
static int SSL_write(SSL *p, const void *data, int n) { (void)p; (void)data; (void)n; ++send_calls; return ssl_mode == 1 && send_calls > 2 ? 1 : -1; }
static void SSL_free(SSL *p) { free(p); }
static void SSL_CTX_free(SSL_CTX *p) { free(p); }
static int SSL_shutdown(SSL *p) { (void)p; return 0; }
static void ssl_error(AmgError *e, int code, const char *prefix) { amg_error_set(e, code, prefix); }
static void ssl_operation_error(AmgError *e, int code, const char *prefix, SSL *p, int a, int b, int c) { (void)p; (void)a; (void)b; (void)c; amg_error_set(e, code, prefix); }
#define socket mock_socket
#define connect mock_connect
#define gethostbyname mock_dns
#define getsockopt mock_getsockopt
#define recv mock_recv
#define send mock_send
#include SUT_FILE

static void reset(void)
{
    assert(ports == 0 && requests == 0 && devices == 0);
    cancel_flag = ticks = arms = waits = aborts = closes = endpoint = 0;
    bad_address = fail_ioctl = fail_timer = tag_failure = peer_error = 0;
    connect_mode = recv_mode = send_mode = ssl_mode = ssl_step = 0;
    dns_calls = recv_calls = send_calls = handshake_calls = cancel_at = 0;
    next_fd = 3;
}

int main(void)
{
    AmgError error = {0};
    AmgTlsConnection *c;
    char byte;
    reset(); cancel_flag = 1;
    assert(!amg_tls_connect_plain("mail", 993, 3, &error) && dns_calls == 0);
    reset(); bad_address = 1;
    assert(!amg_tls_connect_plain("mail", 993, 3, &error) && endpoint == 0);
    reset(); next_fd = FD_SETSIZE;
    assert(!amg_tls_connect_plain("mail", 993, 3, &error) && closes == 1);
    reset(); fail_ioctl = 1;
    assert(!amg_tls_connect_plain("mail", 993, 3, &error) && closes == 1);
    reset(); fail_timer = 1;
    assert(!amg_tls_connect_plain("mail", 993, 3, &error) && dns_calls == 0);
    reset(); tag_failure = 1;
    assert(!amg_tls_connect_plain("mail", 993, 3, &error) && arms == 0);
    reset(); connect_mode = 2;
    c = amg_tls_connect_plain("mail", 993, 3, &error);
    assert(c && endpoint == 2 && closes == 1 && arms == 1 && waits == 1);
    recv_mode = send_mode = 1;
    assert(amg_tls_read(c, &byte, 1, &error) == 1 && recv_calls == 2);
    assert(amg_tls_write(c, &byte, 1, &error) == 1 && send_calls == 2);
    assert(arms == 3 && waits == 3 && break_mask == SIGBREAKF_CTRL_C);
    amg_tls_close(c);
    reset(); connect_mode = 3;
    assert(!amg_tls_connect_plain("mail", 993, 3, &error) && ticks == 3 && closes == 1);
    reset(); connect_mode = 3; cancel_at = 1;
    assert(!amg_tls_connect_plain("mail", 993, 30, &error) && ticks == 1 && closes == 1);
    reset(); connect_mode = 1; peer_error = ECONNREFUSED;
    assert(!amg_tls_connect_plain("mail", 993, 3, &error) && closes == 2);
    reset(); c = amg_tls_connect_plain("mail", 993, 3, &error);
    assert(c); ssl_mode = 1;
    assert(amg_tls_starttls_internal(c, "mail", 1, &error) == AMG_OK && handshake_calls == 3);
    assert(amg_tls_read(c, &byte, 1, &error) == 1 && recv_calls == 3);
    assert(amg_tls_write(c, &byte, 1, &error) == 1 && send_calls == 3);
    assert(arms == 4 && waits == 4);
    amg_tls_close(c);
    reset(); c = amg_tls_connect_plain("mail", 993, 3, &error); assert(c);
    assert(amg_tls_starttls_internal(c, "mail", 0, &error) == AMG_ERR_IO);
    assert(ticks == 3 && !c->ssl && !c->context && arms == 2 && waits == 2);
    amg_tls_close(c);
    reset(); c = amg_tls_connect_plain("mail", 993, 3, &error); assert(c);
    assert(amg_tls_write_all(c, "abcdef", 6, &error) == AMG_ERR_IO);
    assert(arms == 2 && send_calls == 2 && ticks == 3 && waits == 2);
    amg_tls_close(c);
    reset(); c = amg_tls_connect_plain("mail", 993, 3, &error); assert(c);
    recv_mode = 2; assert(amg_tls_read(c, &byte, 1, &error) == AMG_ERR_IO);
    recv_mode = 3; assert(amg_tls_read(c, &byte, 1, &error) == 0);
    cancel_flag = 1; assert(amg_tls_write(c, &byte, 1, &error) == AMG_ERR_IO);
    amg_tls_close(c); reset();
    puts("TLS deadlines: connect fallback/errors, cancellation, descriptor bounds, WANT retries, partial writes and resource cleanup passed.");
    return 0;
}
