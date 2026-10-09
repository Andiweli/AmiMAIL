#include "tls.h"
#include "oauth.h"
#include "i18n.h"
#define T(id, en) amg_tr((id), (en))
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned allocation, fail_allocation;
static void *fault_realloc(void *data, size_t size)
{
    if (++allocation == fail_allocation) return NULL;
    return realloc(data, size);
}
#define realloc fault_realloc
#include "../src/buffer.c"
#undef realloc
static const char *response;
static size_t position, chunk = 1U;
static int eof_error, connected, writes;
struct AmgTlsConnection { int unused; };
static struct AmgTlsConnection connection;
AmgTlsConnection *amg_tls_connect(const char *host, unsigned short port,
                                  unsigned long timeout, AmgError *error)
{ (void)host; (void)port; (void)timeout; (void)error; ++connected; return &connection; }
long amg_tls_read(AmgTlsConnection *c, void *data, size_t length, AmgError *error)
{
    size_t left = strlen(response) - position;
    (void)c;
    if (!left) { if (eof_error) amg_error_set(error, AMG_ERR_IO, "Injected disconnect"); return eof_error ? -3 : 0; }
    if (left > chunk) left = chunk;
    if (left > length) left = length;
    memcpy(data, response + position, left); position += left; return (long)left;
}
int amg_tls_write_all(AmgTlsConnection *c, const void *data, size_t length, AmgError *error)
{ (void)c; (void)error; assert(data && length); ++writes; return AMG_OK; }
void amg_tls_close(AmgTlsConnection *c) { if (c) --connected; }
#include SUT_FILE
#include "../src/oauth.c"

/* update.c's native HTTP branch itself uses only portable C and the TLS API. */
#undef AMIGMAIL_AMIGA
#define AMIGMAIL_AMIGA 1
int amg_tls_global_init(AmgError *error) { (void)error; return AMG_OK; }
void amg_tls_global_cleanup(void) {}
#include "../src/update.c"

static int post(const char *wire, AmgBuffer *body, AmgError *error)
{
    response = wire; position = 0U;
    return amg_https_post_form("mail", "/token", "code=abc", body, error);
}

int main(void)
{
    const char *good = "HTTP/1.1 200 OK\r\nCoNtEnT-LeNgTh: 5\r\n\r\nhello";
    const char *bad[] = {
        "HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\nshort",
        "HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\nx",
        "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 0\r\n\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5bad!\r\nhello\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nffffffffffffffffffff\r\nx\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\nabc", ""
    };
    const char *bad_json[] = {
        "{\"access_token\":\"abc\",\"expires_in\":-1}",
        "{\"access_token\":\"abc\",\"expires_in\":3600garbage}",
        "{\"access_token\":\"abc\",\"expires_in\":999999999999999999999999999}",
        "{\"access_token\":\"ab\ncd\",\"expires_in\":3600}"
    };
    AmgBuffer body;
    AmgError error = {0};
    AmgOAuthTokens tokens;
    unsigned count, i;
    amg_buffer_init(&body); allocation = 0;
    assert(post(good, &body, &error) == AMG_OK && body.length == 5 && !memcmp(body.data, "hello", 5));
    count = allocation; amg_buffer_free(&body);
    for (i = 1; i <= count; ++i) {
        allocation = 0; fail_allocation = i; writes = 0;
        assert(post(good, &body, &error) != AMG_OK && body.length == 0 && connected == 0);
        amg_buffer_free(&body);
    }
    fail_allocation = 0;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(post(bad[i], &body, &error) != AMG_OK && body.length == 0 && connected == 0);
        amg_buffer_free(&body);
    }
    assert(post("HTTP/1.1 200 OK\r\nTRANSFER-ENCODING: ChUnKeD\r\n\r\n5;extension=x\r\nhello\r\n0\r\nTrailer: value\r\n\r\n", &body, &error) == AMG_OK && body.length == 5);
    amg_buffer_free(&body); eof_error = 1;
    assert(post("HTTP/1.1 200 OK\r\n\r\npartial", &body, &error) == AMG_ERR_IO && !body.length);
    assert(post(good, &body, &error) == AMG_OK && error.code == AMG_OK);
    amg_buffer_free(&body); eof_error = 0;
    memset(&tokens, 0, sizeof(tokens));
    allocation = 0;
    assert(amg_oauth_parse_token_json("{\"access_token\":\"abc\",\"expires_in\":3600,\"refresh_token\":\"def\"}", &tokens, &error) == AMG_OK);
    count = allocation;
    for (i = 1; i <= count; ++i) {
        allocation = 0; fail_allocation = i;
        assert(amg_oauth_parse_token_json("{\"access_token\":\"new\",\"expires_in\":3600,\"refresh_token\":\"new\"}", &tokens, &error) != AMG_OK);
        assert(!strcmp(tokens.access_token, "abc") && !strcmp(tokens.refresh_token, "def"));
    }
    fail_allocation = 0;
    for (i = 0; i < sizeof(bad_json) / sizeof(bad_json[0]); ++i)
        assert(amg_oauth_parse_token_json(bad_json[i], &tokens, &error) == AMG_ERR_PARSE);
    amg_oauth_tokens_clear(&tokens);
    {
        char location[128];
        int status;
        response = good; position = 0;
        assert(https_get_once("https://mail/file", 100, &body, &status,
               location, sizeof(location), &error) == AMG_OK && body.length == 5);
        amg_buffer_free(&body);
        for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
            response = bad[i]; position = 0;
            assert(https_get_once("https://mail/file", 100, &body, &status,
                   location, sizeof(location), &error) != AMG_OK);
            amg_buffer_free(&body);
        }
        assert(https_get_once("https://mail/file\r\nInjected: header", 100,
               &body, &status, location, sizeof(location), &error) == AMG_ERR_ARGUMENT);
    }
    puts("HTTPS/OAuth: every allocation failure, mixed-case headers, framing/truncation, chunk trailers/overflow and invalid token lifetimes passed.");
    return 0;
}
