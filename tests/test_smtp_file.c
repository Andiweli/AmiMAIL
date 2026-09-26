/* Production SMTP file sender with a scripted TLS peer, no live delivery. */
#include "transfer_tls_double.h"
#include "mailfile.h"
#include "../src/smtp.c"

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)
typedef struct Progress {
    int cancel_phase;
    size_t cancel_after, last_upload;
    unsigned calls, commits;
} Progress;
static int report(void *context, AmgTransferPhase phase, size_t done, size_t total)
{
    Progress *p = (Progress *)context;
    ++p->calls; (void)total;
    if (phase == AMG_TRANSFER_UPLOAD) p->last_upload = done;
    if (phase == AMG_TRANSFER_COMMIT) ++p->commits;
    return (int)phase != p->cancel_phase || done < p->cancel_after;
}
static FILE *source_file(const void *text, size_t length)
{
    FILE *file = tmpfile();
    CHECK(file != NULL);
    if (!file) return NULL;
    CHECK(fwrite(text, 1U, length, file) == length);
    CHECK(fflush(file) == 0);
    return file;
}
static void setup(AmgAccount *account, AmgMailDraft *draft)
{
    amg_account_init(account);
    strcpy(account->email, "sender@example.test");
    strcpy(account->smtp_host, "smtp.example.test");
    CHECK(amg_account_set_secret(&account->imap_password, "not-a-real-password") == AMG_OK);
    memset(draft, 0, sizeof(*draft));
    draft->from = account->email;
    draft->to = "to@example.test";
    draft->cc = "cc@example.test";
    draft->bcc = "hidden@example.test";
    draft->subject = "Private draft";
    draft->date_rfc2822 = "Sat, 26 Sep 2026 10:00:00 +0200";
    draft->message_id = "<file-test@example.test>";
    draft->body_utf8 = "Hello\n.one\n..two\n";
}
static const char greeting[] =
    "220 ready\r\n250-smtp.example.test\r\n250 AUTH PLAIN\r\n235 authenticated\r\n"
    "250 sender\r\n250 to\r\n250 cc\r\n250 bcc\r\n354 send data\r\n";
static const char raw[] =
    "From: sender@example.test\r\nTo: to@example.test\r\n"
    "bCc: hidden@example.test\r\n\tother-secret@example.test\r\n"
    "Subject: visible\r\n\r\n.one\r\n..two\r\nBcc: body text is not a header\r\n";

static void test_delivery(void)
{
    const char *ending[] = {"250 accepted\r\n221 bye\r\n", "250 accepted\r\n", "", "550 rejected\r\n",
                            "", "garbled\r\n", "350 unexpected\r\n"};
    int expected[] = {AMG_OK, AMG_OK, AMG_ERR_UNCERTAIN, AMG_ERR_PROTOCOL,
                      AMG_ERR_UNCERTAIN, AMG_ERR_UNCERTAIN, AMG_ERR_UNCERTAIN};
    unsigned i;
    for (i = 0U; i < 7U; ++i) {
        AmgBuffer peer;
        AmgAccount account; AmgMailDraft draft;
        AmgError error = {0};
        Progress progress = {-1,0,0,0,0}; AmgTransfer t = {report, &progress};
        FILE *file = source_file(raw, sizeof(raw) - 1U);
        const char *wire, *data;
        setup(&account, &draft); amg_buffer_init(&peer);
        CHECK(amg_buffer_append_cstr(&peer, greeting) == AMG_OK);
        CHECK(amg_buffer_append_cstr(&peer, ending[i == 4U ? 0U : i]) == AMG_OK);
        td_reset(peer.data, peer.length, i + 1U);
        if (i == 4U) td_missing_ack = 1;
        if (file) {
            int result = amg_smtp_send_mail_file(&account, NULL, &draft, file,
                sizeof(raw) - 1U, &t, &error);
            CHECK(result == (i == 4U ? AMG_ERR_UNCERTAIN : expected[i]));
            CHECK(td_closes == 1U && progress.commits == 1U);
            if (result == AMG_OK) CHECK(error.code == AMG_OK);
            if (result == AMG_ERR_UNCERTAIN) CHECK(error.code == AMG_ERR_UNCERTAIN);
            CHECK(amg_buffer_terminate(&td_output) == AMG_OK);
            wire = (const char *)td_output.data;
            CHECK(strstr(wire, "RCPT TO:<hidden@example.test>\r\n") != NULL);
            data = strstr(wire, "DATA\r\n"); CHECK(data != NULL);
            if (data) {
                data += 6U;
                CHECK(strstr(data, "hidden@example.test") == NULL);
                CHECK(strstr(data, "other-secret@example.test") == NULL);
                CHECK(strstr(data, "Subject: visible\r\n\r\n..one\r\n...two\r\n") != NULL);
                CHECK(strstr(data, "Bcc: body text is not a header\r\n.\r\n") != NULL);
            }
            fclose(file);
        }
        amg_account_clear(&account); amg_buffer_free(&peer);
    }
}
static void test_cancellation_and_writes(void)
{
    unsigned i;
    for (i = 0U; i < 4U; ++i) {
        AmgAccount account; AmgMailDraft draft; AmgError error = {0};
        Progress p = {i == 0U ? AMG_TRANSFER_UPLOAD : AMG_TRANSFER_COMMIT,0,0,0,0};
        AmgTransfer t = {report, &p};
        FILE *file = source_file(raw, sizeof(raw) - 1U);
        setup(&account, &draft);
        td_reset(greeting, sizeof(greeting) - 1U, 3U);
        if (i >= 2U) { p.cancel_phase = -1; td_fail_write = i == 2U ? 2U : 10U; }
        if (file) {
            int result = amg_smtp_send_mail_file(&account, NULL, &draft, file,
                sizeof(raw) - 1U, &t, &error);
            CHECK(result == (i < 2U ? AMG_ERR_CANCELLED : AMG_ERR_IO));
            CHECK(amg_buffer_terminate(&td_output) == AMG_OK);
            CHECK(strstr((const char *)td_output.data, "\r\n.\r\n") == NULL);
            CHECK(td_closes == (i == 0U ? 0U : 1U));
            fclose(file);
        }
        amg_account_clear(&account);
    }
}
static void test_block_dot_stuffing(void)
{
    AmgBuffer input, expected;
    AmgError error = {0};
    unsigned char block[8192];
    FILE *file;
    memset(block, 'X', sizeof(block));
    block[0] = '.'; block[8190] = '\r'; block[8191] = '\n';
    amg_buffer_init(&input); amg_buffer_init(&expected);
    CHECK(amg_buffer_append_cstr(&input, "Subject: split\r\n\r\n") == AMG_OK);
    CHECK(amg_buffer_append(&input, block, sizeof(block)) == AMG_OK);
    CHECK(amg_buffer_append_cstr(&input, ".final\r\n") == AMG_OK);
    CHECK(amg_smtp_dot_stuff((const char *)input.data, input.length, &expected) == AMG_OK);
    file = source_file(input.data, input.length); td_reset("", 0U, 1U);
    if (file) {
        CHECK(smtp_file_data((AmgTlsConnection *)&td_dummy, file, input.length, NULL, &error) == AMG_OK);
        CHECK(td_output.length == expected.length);
        CHECK(!memcmp(td_output.data, expected.data, expected.length));
        fclose(file);
    }
    amg_buffer_free(&input); amg_buffer_free(&expected);
}
static void test_snapshot(void)
{
    AmgAccount account; AmgMailDraft draft; AmgAttachmentInput items[12];
    AmgError error = {0};
    const char *attachment_path = "build/snapshot-attachment.bin";
    const char *message_path = "build/snapshot-file.eml";
    FILE *file = fopen(attachment_path, "wb"), *spool;
    AmgMailFile *indexed = NULL;
    size_t length = 0U, i;
    setup(&account, &draft);
    CHECK(file != NULL); if (!file) { amg_account_clear(&account); return; }
    CHECK(fwrite("ABC", 1U, 3U, file) == 3U); CHECK(fclose(file) == 0);
    memset(items, 0, sizeof(items));
    for (i = 0U; i < 12U; ++i) { items[i].path = attachment_path; items[i].name_utf8 = "tiny.bin"; items[i].size = 3U; }
    draft.attachments = items; draft.attachment_count = 12U;
    draft.reply_source_uid = 42UL; draft.reply_source_uid_validity = 321UL;
    draft.reply_source_mailbox = "INBOX";
    spool = fopen(message_path, "wb"); CHECK(spool != NULL);
    if (spool) {
        CHECK(amg_smtp_build_mail_file(&draft, 1, 1, spool, &length, NULL, &error) == AMG_OK);
        CHECK(fclose(spool) == 0);
        CHECK(amg_mailfile_open(message_path, 0, NULL, &indexed, &error) == AMG_OK);
        if (indexed) {
            CHECK(indexed->attachment_count == 12U);
            CHECK(!strcmp(amg_mail_header_get(&indexed->headers, "Bcc"), draft.bcc));
            CHECK(!strcmp(amg_mail_header_get(&indexed->headers, AMG_MAIL_REPLY_UID_HEADER), "42"));
            CHECK(!strcmp(amg_mail_header_get(&indexed->headers, AMG_MAIL_REPLY_UIDVALIDITY_HEADER), "321"));
            amg_mailfile_close(indexed);
        }
        /* Altering an original attachment after snapshot creation cannot
         * change the bytes subsequently sent or appended to Sent. */
        file = fopen(attachment_path, "wb"); CHECK(file != NULL);
        if (file) { CHECK(fwrite("CHANGED", 1U, 7U, file) == 7U); fclose(file); }
        spool = fopen(message_path, "rb"); CHECK(spool != NULL);
        td_reset("", 0U, 1U);
        if (spool) {
            CHECK(smtp_file_data((AmgTlsConnection *)&td_dummy, spool, length, NULL, &error) == AMG_OK);
            CHECK(amg_buffer_terminate(&td_output) == AMG_OK);
            CHECK(strstr((char *)td_output.data, "QUJD\r\n") != NULL);
            CHECK(strstr((char *)td_output.data, "Q0hBTkdFRA==") == NULL);
            CHECK(strstr((char *)td_output.data, "Bcc:") == NULL);
            fclose(spool);
        }
    }
    amg_account_clear(&account); remove(attachment_path); remove(message_path);
}
int main(void)
{
    test_delivery(); test_cancellation_and_writes(); test_block_dot_stuffing(); test_snapshot();
    amg_buffer_free(&td_input); amg_buffer_free(&td_output);
    printf("smtp-file: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
