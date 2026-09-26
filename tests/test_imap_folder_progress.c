/* Real IMAP parser/command code, a scripted command-gated TLS peer, and
 * fragmented reads. No real mailserver, Amiga ABI or GUI is exercised. */
#include "transfer_tls_double.h"
#include "transfer.h"
#include "imap_parser.h"

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static size_t reply_end[8], reply_count, reply_index, read_limit;
static int peer_finished;
long folder_read(AmgTlsConnection *connection, void *data, size_t length, AmgError *error);
int folder_write(AmgTlsConnection *connection, const void *data, size_t length, AmgError *error);
long folder_read(AmgTlsConnection *connection, void *data, size_t length, AmgError *error)
{
    size_t saved_chunk = td_chunk;
    long result;
    CHECK(td_position <= read_limit);
    if (td_position == read_limit) {
        amg_error_set(error, AMG_ERR_IO, "Injected missing tagged response.");
        return -1L;
    }
    if (td_chunk > read_limit - td_position) td_chunk = read_limit - td_position;
    result = td_read(connection, data, length, error);
    td_chunk = saved_chunk;
    if (reply_index == reply_count && td_position == read_limit) peer_finished = 1;
    return result;
}
int folder_write(AmgTlsConnection *connection, const void *data, size_t length, AmgError *error)
{
    CHECK(reply_index < reply_count);
    CHECK(td_position == read_limit);
    if (reply_index >= reply_count) return AMG_ERR_PROTOCOL;
    read_limit = reply_end[reply_index++];
    return td_write_all(connection, data, length, error);
}
#undef amg_tls_read
#undef amg_tls_write_all
#define amg_tls_read folder_read
#define amg_tls_write_all folder_write
#include "../src/imap.c"

typedef struct FolderTrace {
    size_t total, last, calls, intermediate, completions;
    size_t cancel_at;
    int cancel_unknown;
} FolderTrace;
static int report_folder(void *context, AmgTransferPhase phase, size_t done, size_t total)
{
    FolderTrace *trace = (FolderTrace *)context;
    ++trace->calls;
    CHECK(phase == AMG_TRANSFER_RECEIVE);
    CHECK(done <= total);
    if (!total) {
        CHECK(done == 0U);
        return !trace->cancel_unknown;
    }
    if (trace->total) CHECK(total == trace->total);
    CHECK(done >= trace->last);
    trace->total = total;
    trace->last = done;
    if (done > 0U && done < total) ++trace->intermediate;
    if (done == total) {
        ++trace->completions;
        CHECK(peer_finished); /* never 100% before the final server response */
    }
    return !trace->cancel_at || done < trace->cancel_at;
}

static void add_reply(AmgBuffer *wire)
{
    CHECK(reply_count < sizeof(reply_end) / sizeof(reply_end[0]));
    reply_end[reply_count++] = wire->length;
}

static void append_header(AmgBuffer *wire, unsigned long uid, int uid_after)
{
    char prefix[160], trailer[160];
    /* Fake tokens inside the literal must never be interpreted as metadata. */
    const char *header = "From: sender@example.com\r\nSubject: Test UID 9999\r\n"
                         "X-Text: * 1 FETCH (UID 9999)\r\n\r\n";
    if (uid_after) {
        snprintf(prefix, sizeof(prefix), "* %lu FETCH (BODY[HEADER] {%lu}\r\n",
                 uid, (unsigned long)strlen(header));
        snprintf(trailer, sizeof(trailer), " UID %lu FLAGS (\\Seen))\r\n", uid);
    } else {
        snprintf(prefix, sizeof(prefix), "* %lu FETCH (UID %lu BODY[HEADER] {%lu}\r\n",
                 uid, uid, (unsigned long)strlen(header));
        snprintf(trailer, sizeof(trailer), ")\r\n");
    }
    CHECK(amg_buffer_append_cstr(wire, prefix) == AMG_OK);
    CHECK(amg_buffer_append_cstr(wire, header) == AMG_OK);
    CHECK(amg_buffer_append_cstr(wire, trailer) == AMG_OK);
}

/* Total is 205: exercise three batches, with a short final batch. */
static void prepare_wire(AmgBuffer *wire, size_t count, int special, int last_reply)
{
    size_t i, start, tag = 2U;
    char text[128];
    reply_count = reply_index = read_limit = 0U;
    peer_finished = 0;
    amg_buffer_init(wire);
    CHECK(amg_buffer_append_cstr(wire, "* SEARCH") == AMG_OK);
    for (i = 1U; i <= count; ++i) {
        snprintf(text, sizeof(text), " %lu", (unsigned long)i);
        CHECK(amg_buffer_append_cstr(wire, text) == AMG_OK);
    }
    CHECK(amg_buffer_append_cstr(wire, "\r\nA000001 OK searched\r\n") == AMG_OK);
    add_reply(wire);
    for (start = 1U; start <= count; start += IMAP_UID_BATCH) {
        size_t stop = start + IMAP_UID_BATCH;
        if (stop > count + 1U) stop = count + 1U;
        if (special) {
            /* An unsolicited flags-only FETCH and an unrelated literal. */
            CHECK(amg_buffer_append_cstr(wire, "* 9000 FETCH (UID 9000 FLAGS (\\Seen))\r\n") == AMG_OK);
            append_header(wire, 9999UL, 0);
        }
        for (i = start; i < stop; ++i) {
            if (special && i == 3U) continue; /* expunged between SEARCH/FETCH */
            append_header(wire, (unsigned long)i, special && (i % 2U == 0U));
            if (special && i == 2U) append_header(wire, 2UL, 0); /* duplicate */
        }
        if (stop == count + 1U && last_reply == 1)
            snprintf(text, sizeof(text), "A%06lu NO rejected\r\n", (unsigned long)tag);
        else if (stop == count + 1U && last_reply == 2)
            text[0] = 0; /* drop connection without final acknowledgement */
        else
            snprintf(text, sizeof(text), "A%06lu OK fetched\r\n", (unsigned long)tag);
        CHECK(amg_buffer_append_cstr(wire, text) == AMG_OK);
        add_reply(wire); ++tag;
    }
}
static void start_run(AmgImapSession *session, const AmgBuffer *wire, size_t chunk, size_t count)
{
    td_reset(wire->data, wire->length, chunk);
    reply_index = read_limit = 0U; peer_finished = 0;
    amg_imap_session_init(session);
    session->connection = (AmgTlsConnection *)&td_dummy;
    session->selected_exists = (unsigned long)count;
    strcpy(session->selected_mailbox, "[Gmail]/All Mail");
}
static void test_success(void)
{
    const size_t chunks[] = {1U, 2U, 7U, 127U, 4096U, 65536U};
    size_t c;
    AmgBuffer wire;
    prepare_wire(&wire, 205U, 0, 0);
    for (c = 0U; c < sizeof(chunks)/sizeof(chunks[0]); ++c) {
        AmgImapSession session;
        AmgBuffer output, legacy;
        AmgError error = {0};
        FolderTrace trace = {0};
        AmgTransfer transfer = {report_folder, &trace};
        start_run(&session, &wire, chunks[c], 205U);
        amg_buffer_init(&output); amg_buffer_init(&legacy);
        CHECK(amg_imap_fetch_recent_progress(&session, 180U, &output, &transfer, &error) == AMG_OK);
        CHECK(trace.total == 205U && trace.last == 205U && trace.completions == 1U);
        CHECK(trace.intermediate >= 202U);
        CHECK(reply_index == 4U && td_closes == 0U);
        CHECK(amg_buffer_terminate(&td_output) == AMG_OK);
        CHECK(strstr((char *)td_output.data, "UID SEARCH NOT DELETED SINCE") != NULL);
        CHECK(strstr((char *)td_output.data, "UID FETCH 201,202,203,204,205 ") != NULL);
        /* With progress disabled, bytes and fetch count must stay identical. */
        start_run(&session, &wire, chunks[c], 205U);
        CHECK(amg_imap_fetch_recent(&session, 180U, &legacy, &error) == AMG_OK);
        CHECK(output.length == legacy.length);
        CHECK(!memcmp(output.data, legacy.data, output.length));
        CHECK(reply_index == 4U && td_closes == 0U);
        amg_buffer_free(&output); amg_buffer_free(&legacy);
    }
    amg_buffer_free(&wire);
}
static void test_edge_cases(void)
{
    AmgBuffer wire, output;
    AmgImapSession session;
    AmgError error = {0};
    FolderTrace trace = {0};
    AmgTransfer transfer = {report_folder, &trace};
    int mode;
    prepare_wire(&wire, 8U, 1, 0);
    start_run(&session, &wire, 17U, 8U); amg_buffer_init(&output);
    CHECK(amg_imap_fetch_recent_progress(&session, 180U, &output, &transfer, &error) == AMG_OK);
    CHECK(trace.total == 8U && trace.last == 8U && trace.completions == 1U);
    CHECK(trace.intermediate == 7U); /* no duplicate/unrelated UID counted */
    amg_buffer_free(&output); amg_buffer_free(&wire);

    prepare_wire(&wire, 0U, 0, 0);
    for (mode = 0; mode <= 1; ++mode) {
        memset(&trace, 0, sizeof(trace));
        start_run(&session, &wire, 3U, mode ? 10U : 0U);
        amg_buffer_init(&output);
        CHECK(amg_imap_fetch_recent_progress(&session, 180U, &output, &transfer, &error) == AMG_OK);
        CHECK(trace.total == 0U && trace.completions == 0U && output.length == 0U);
        CHECK(reply_index == (mode ? 1U : 0U));
        amg_buffer_free(&output);
    }
    amg_buffer_free(&wire);

    for (mode = 1; mode <= 2; ++mode) {
        prepare_wire(&wire, 205U, 0, mode);
        memset(&trace, 0, sizeof(trace));
        start_run(&session, &wire, 17U, 205U); amg_buffer_init(&output);
        CHECK(amg_imap_fetch_recent_progress(&session, 180U, &output, &transfer, &error) != AMG_OK);
        CHECK(trace.completions == 0U && trace.last < trace.total);
        CHECK(td_closes == 1U && session.connection == NULL);
        amg_buffer_free(&output); amg_buffer_free(&wire);
    }
    /* Output-capacity failure must not publish successful completion. */
    prepare_wire(&wire, 8U, 0, 0);
    memset(&trace, 0, sizeof(trace));
    start_run(&session, &wire, 127U, 8U); amg_buffer_init(&output);
    CHECK(amg_buffer_set_limit(&output, 1U) == AMG_OK);
    CHECK(amg_imap_fetch_recent_progress(&session, 180U, &output, &transfer, &error) == AMG_ERR_LIMIT);
    CHECK(trace.completions == 0U && td_closes == 1U);
    amg_buffer_free(&output); amg_buffer_free(&wire);
}
static void test_cancel(void)
{
    AmgBuffer wire, output;
    AmgImapSession session;
    AmgError error = {0};
    FolderTrace trace = {0};
    AmgTransfer transfer = {report_folder, &trace};
    prepare_wire(&wire, 205U, 0, 0);
    start_run(&session, &wire, 3U, 205U);
    amg_buffer_init(&output); trace.cancel_at = 7U;
    CHECK(amg_imap_fetch_recent_progress(&session, 180U, &output, &transfer, &error) == AMG_ERR_CANCELLED);
    CHECK(reply_index == 2U && trace.last == 7U && trace.completions == 0U);
    CHECK(session.connection == NULL && td_closes == 1U);
    CHECK(amg_buffer_terminate(&td_output) == AMG_OK);
    CHECK(strstr((char *)td_output.data, "LOGOUT") == NULL);
    amg_buffer_free(&output);
    /* Cancel before SEARCH: no command should be issued. */
    start_run(&session, &wire, 17U, 205U);
    memset(&trace, 0, sizeof(trace)); trace.cancel_unknown = 1;
    amg_buffer_init(&output);
    CHECK(amg_imap_fetch_recent_progress(&session, 180U, &output, &transfer, &error) == AMG_ERR_CANCELLED);
    CHECK(reply_index == 0U && td_writes == 0U);
    amg_buffer_free(&output); amg_buffer_free(&wire);
}
int main(void)
{
    test_success(); test_edge_cases(); test_cancel();
    amg_buffer_free(&td_input); amg_buffer_free(&td_output);
    printf("imap-folder-progress: %u checks, %u failures (scripted TLS peer).\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
