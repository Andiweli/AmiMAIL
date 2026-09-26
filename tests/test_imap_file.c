#include "transfer_tls_double.h"
#include "mailfile.h"
#include "codec.h"
#include "imap_parser.h"
#include "../src/imap.c"

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)
typedef struct Progress { size_t calls, received; int cancel_phase; size_t cancel_after; } Progress;
static int report(void *context, AmgTransferPhase phase, size_t done, size_t total)
{
    Progress *p = (Progress *)context;
    ++p->calls; (void)total;
    if (phase == AMG_TRANSFER_RECEIVE && done > p->received) p->received = done;
    return (int)phase != p->cancel_phase || done < p->cancel_after;
}
static void reset_session(AmgImapSession *session)
{
    amg_imap_session_init(session);
    session->connection = (AmgTlsConnection *)&td_dummy;
    session->tag_counter = 1UL;
    strcpy(session->selected_mailbox, "INBOX");
    strcpy(session->special_mailboxes[2], "Sent");
}

static void test_fetch(void)
{
    AmgBuffer wire;
    const char *entity = "Subject: Example\r\nContent-Type: text/plain; charset=UTF-8\r\n\r\nHello World!";
    const size_t chunks[] = {1U,2U,3U,7U,17U,511U,8191U,8192U};
    size_t c;
    char start[160];
    amg_buffer_init(&wire);
    CHECK(amg_buffer_append_cstr(&wire, "* 2 FETCH (UID 5 BODY[] {3}\r\nabc)\r\n") == AMG_OK);
    snprintf(start, sizeof(start), "* 1 FETCH (BODY[] {%lu}\r\n", (unsigned long)strlen(entity));
    CHECK(amg_buffer_append_cstr(&wire, start) == AMG_OK);
    CHECK(amg_buffer_append_cstr(&wire, entity) == AMG_OK);
    /* UID/FLAGS after the literal must work as well as before it. */
    CHECK(amg_buffer_append_cstr(&wire,
        " UID 42 FLAGS (\\Seen \\Flagged \\Answered))\r\nA000001 OK fetched\r\n") == AMG_OK);
    for (c = 0U; c < sizeof(chunks) / sizeof(chunks[0]); ++c) {
        AmgImapSession session;
        AmgImapFetchRecord record;
        AmgError error = {0};
        FILE *file = tmpfile();
        size_t offset = 0U, length = 0U;
        char buffer[256];
        Progress p = {0U, 0U, -1, 0U}; AmgTransfer t = {report, &p};
        td_reset(wire.data, wire.length, chunks[c]); reset_session(&session);
        CHECK(file != NULL); if (!file) continue;
        CHECK(amg_imap_fetch_message_file(&session, 42UL, file,
            &offset, &length, &record, &t, &error) == AMG_OK);
        CHECK(length == strlen(entity) && offset > 0U);
        CHECK(record.uid == 42UL && record.seen && record.flagged && record.answered);
        CHECK(td_closes == 0U); CHECK(p.received == strlen(entity));
        CHECK(fseek(file, (long)offset, SEEK_SET) == 0);
        CHECK(fread(buffer, 1U, length, file) == length);
        CHECK(!memcmp(buffer, entity, length)); CHECK(fclose(file) == 0);
        CHECK(amg_buffer_terminate(&td_output) == AMG_OK);
        CHECK(strstr((char *)td_output.data, "UID FETCH 42 (UID FLAGS BODY.PEEK[])") != NULL);
    }
    {
        AmgImapSession session; AmgImapFetchRecord record; AmgError error = {0};
        size_t offset = 0, length = 0;
        FILE *file = tmpfile();
        Progress p = {0,0,(int)AMG_TRANSFER_RECEIVE,4U}; AmgTransfer t = {report, &p};
        td_reset(wire.data, wire.length, 7U); reset_session(&session);
        CHECK(file != NULL);
        if (file) {
            CHECK(amg_imap_fetch_message_file(&session, 42UL, file,
                &offset, &length, &record, &t, &error) == AMG_ERR_CANCELLED);
            CHECK(session.connection == NULL && td_closes == 1U);
            CHECK(amg_buffer_terminate(&td_output) == AMG_OK);
            CHECK(strstr((char *)td_output.data, "LOGOUT") == NULL);
            fclose(file);
        }
    }
    {
        const char *bad[] = {"* 1 FETCH (UID 42 BODY[] {100}\r\nshort", "A000001 NO rejected\r\n", "A000001 OK no message\r\n"};
        const int expect[] = {AMG_ERR_TLS, AMG_ERR_PROTOCOL, AMG_ERR_PROTOCOL};
        for (c = 0; c < 3U; ++c) {
            AmgImapSession session; AmgImapFetchRecord record; AmgError error = {0};
            size_t offset = 0, length = 0; FILE *file = tmpfile();
            td_reset(bad[c], strlen(bad[c]), 17U); reset_session(&session);
            CHECK(file != NULL);
            if (file) { CHECK(amg_imap_fetch_message_file(&session, 42UL, file,
                &offset, &length, &record, NULL, &error) == expect[c]);
                CHECK(session.connection == NULL); fclose(file); }
        }
    }
    amg_buffer_free(&wire);
}

static void test_append(void)
{
    const char *replies[] = {
        "+ continue\r\nA000001 OK appended\r\n", "+ continue\r\n",
        "A000001 NO quota\r\n", "+ continue\r\nA000001 NO quota\r\n",
        "+ continue\r\nA000001 UNKNOWN\r\n", "+ continue\r\n* 5 EXISTS\r\nA000001 OK [APPENDUID 1 99] done\r\n"};
    const int expect[] = {AMG_OK, AMG_ERR_UNCERTAIN, AMG_ERR_PROTOCOL, AMG_ERR_PROTOCOL,
                         AMG_ERR_UNCERTAIN, AMG_OK};
    unsigned i;
    for (i = 0U; i < 6U; ++i) {
        AmgImapSession session; AmgError error = {0};
        FILE *file = tmpfile();
        Progress p = {0,0,-1,0}; AmgTransfer t = {report, &p};
        CHECK(file != NULL); if (!file) continue;
        CHECK(fwrite("ABC", 1U, 3U, file) == 3U); CHECK(fflush(file) == 0);
        td_reset(replies[i], strlen(replies[i]), 7U); reset_session(&session);
        CHECK(amg_imap_append_draft_file(&session, "Drafts", file, 3U, &t, &error) == expect[i]);
        CHECK(amg_buffer_terminate(&td_output) == AMG_OK);
        if (i == 2U) CHECK(strstr((char *)td_output.data, "ABC") == NULL);
        else CHECK(strstr((char *)td_output.data, "ABC\r\n") != NULL);
        if (i == 0U) CHECK(session.connection != NULL);
        if (i == 1U) CHECK(session.connection == NULL && error.code == AMG_ERR_UNCERTAIN);
        fclose(file);
    }
    for (i = 0U; i < 2U; ++i) {
        AmgImapSession session; AmgError error = {0};
        Progress p = {0,0,(int)(i ? AMG_TRANSFER_COMMIT : AMG_TRANSFER_UPLOAD),0U};
        AmgTransfer t = {report, &p}; FILE *file = tmpfile();
        CHECK(file != NULL); if (!file) continue;
        CHECK(fwrite("ABC", 1U, 3U, file) == 3U); CHECK(fflush(file) == 0);
        td_reset(replies[0], strlen(replies[0]), 1U); reset_session(&session);
        CHECK(amg_imap_append_draft_file(&session, "Drafts", file, 3U, &t, &error) == AMG_ERR_CANCELLED);
        CHECK(session.connection == NULL);
        CHECK(amg_buffer_terminate(&td_output) == AMG_OK);
        CHECK(strstr((char *)td_output.data, "ABC\r\n") == NULL);
        fclose(file);
    }
}

static void test_parser_memory(void)
{
    AmgImapParser parser; AmgImapEvent event;
    unsigned char block[8192];
    size_t sent = 0U, received = 0U, size = 28U * 1024U * 1024U;
    char header[128];
    int step;
    amg_imap_parser_init(&parser); parser.stream_literals = 1;
    memset(block, 'X', sizeof(block));
    snprintf(header, sizeof(header), "* 1 FETCH (UID 42 BODY[] {%lu}\r\n", (unsigned long)size);
    CHECK(amg_imap_parser_feed(&parser, header, strlen(header)) == AMG_OK);
    CHECK(amg_imap_parser_next(&parser, &event) == 1 && event.type == AMG_IMAP_EVENT_LINE);
    while (sent < size) {
        size_t n = size - sent;
        if (n > sizeof(block)) n = sizeof(block);
        CHECK(amg_imap_parser_feed(&parser, block, n) == AMG_OK);
        sent += n;
        while ((step = amg_imap_parser_next(&parser, &event)) > 0) {
            CHECK(event.type == AMG_IMAP_EVENT_LITERAL);
            received += event.length;
        }
        CHECK(step == 0);
        CHECK(parser.pending.capacity <= 16384U && parser.event_data.capacity <= 16384U);
    }
    CHECK(received == size);
    CHECK(amg_imap_parser_feed(&parser, ")\r\nA000001 OK\r\n", sizeof(")\r\nA000001 OK\r\n") - 1U) == AMG_OK);
    CHECK(amg_imap_parser_next(&parser, &event) == 1 && event.type == AMG_IMAP_EVENT_LINE);
    amg_imap_parser_free(&parser);
}

int main(void)
{
    test_fetch(); test_append(); test_parser_memory();
    amg_buffer_free(&td_input); amg_buffer_free(&td_output);
    printf("imap-file: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
