/* Actual LIST/FETCH paths, with a byte-fragmented scripted TLS peer. */
#include "transfer_tls_double.h"
#include "../src/imap.c"

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static void test_list(void)
{
    static const char *responses[] = {
        "* LIST () \"/\" {5}\r\nhello\r\nA000001 OK done\r\n",
        "* list () NIL \"hello\"\r\nA000001 OK done\r\n",
        "* LIST () \"\\\\\" hello\r\nA000001 OK done\r\n"
    };
    size_t i, chunk;
    for (i = 0U; i < sizeof(responses) / sizeof(responses[0]); ++i) {
        for (chunk = 1U; chunk <= 19U; ++chunk) {
            AmgImapSession session; AmgImapLabel labels[4];
            AmgError error = {0}; size_t count = 0U;
            amg_imap_session_init(&session);
            session.connection = (AmgTlsConnection *)&td_dummy;
            td_reset(responses[i], strlen(responses[i]), chunk);
            CHECK(amg_imap_list_labels(&session, labels, 4U, &count, &error) == AMG_OK);
            CHECK(count == 1U);
            CHECK(count && !strcmp(labels[0].name_utf8, "hello"));
            CHECK(count && labels[0].delimiter == (i == 0U ? '/' : i == 1U ? 0 : '\\'));
        }
    }
    CHECK(special_flag("\\SentOther \\AllOther") == 0UL);
    CHECK(special_flag("\\Sent \\All") == (AMG_LABEL_SENT | AMG_LABEL_ALL));
    {
        AmgBuffer command;
        amg_buffer_init(&command);
        CHECK(quote_mailbox("Inbox\r\nA2 LOGOUT", &command) == AMG_ERR_ARGUMENT);
        CHECK(command.length == 0U);
        CHECK(amg_buffer_set_limit(&command, 3U) == AMG_OK);
        CHECK(quote_mailbox("hello", &command) == AMG_ERR_LIMIT);
        amg_buffer_free(&command);
    }
    {
        char response[1024];
        AmgImapSession session; AmgImapLabel labels[4];
        AmgError error = {0}; size_t count = 0U, n = 0U;
        n += (size_t)snprintf(response + n, sizeof(response) - n, "* LIST () NIL {800}\r\n");
        memset(response + n, 'x', 800U); n += 800U;
        n += (size_t)snprintf(response + n, sizeof(response) - n, "\r\nA000001 OK done\r\n");
        amg_imap_session_init(&session); session.connection = (AmgTlsConnection *)&td_dummy;
        td_reset(response, n, 17U);
        CHECK(amg_imap_list_labels(&session, labels, 4U, &count, &error) == AMG_ERR_LIMIT);
        CHECK(count == 0U);
    }
}

static void test_fetch_order(void)
{
    const char *header = "Subject: hello\r\n\r\n";
    const char *prefixes[] = {
        "UID 42 RFC822.SIZE 100 BODY[HEADER]",
        "BODY[HEADER]",
        "BODY[HEADER.FIELDS (UID RFC822.SIZE)]"
    };
    size_t i;
    for (i = 0U; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        char response[512]; size_t position = 0U;
        AmgImapFetchRecord record;
        snprintf(response, sizeof(response), "* 1 FETCH (%s {%lu}\r\n%s UID 42 RFC822.SIZE 100 FLAGS (\\Seen \\Answered))\r\n",
                 prefixes[i], (unsigned long)strlen(header), header);
        CHECK(amg_imap_fetch_record_next((const unsigned char *)response,
              strlen(response), &position, &record) == 1);
        CHECK(record.uid == 42UL && record.rfc822_size == 100UL);
        CHECK(record.seen && record.answered && !record.deleted);
        CHECK(record.literal_length == strlen(header));
        CHECK(!memcmp(record.literal, header, strlen(header)));
        CHECK(amg_imap_fetch_record_next((const unsigned char *)response,
              strlen(response), &position, &record) == 0);
    }
    {
        char response[512]; size_t position = 0U;
        AmgImapFetchRecord record;
        const char *fake = "* 2 FETCH (UID 99 BODY[] {1}\r\nx)\r\n";
        snprintf(response, sizeof(response), "* 1 FETCH (BODY[] {%lu}\r\n%s)\r\n",
                 (unsigned long)strlen(fake), fake);
        CHECK(amg_imap_fetch_record_next((const unsigned char *)response,
              strlen(response), &position, &record) == 0);
    }
    {
        char response[256]; size_t position = 0U;
        AmgImapFetchRecord record;
        snprintf(response, sizeof(response), "* 1 FETCH (UID 42 FLAGS (\\SeenOther) BODY[] {%lu}\r\n%s)\r\n",
                 (unsigned long)strlen(header), header);
        CHECK(amg_imap_fetch_record_next((const unsigned char *)response,
              strlen(response), &position, &record) == 1);
        CHECK(!record.seen);
    }
}

int main(void)
{
    test_list(); test_fetch_order();
    amg_buffer_free(&td_input); amg_buffer_free(&td_output);
    printf("imap-compat: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
