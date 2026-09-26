/* Disk-backed MIME regression tests. Runs on the build host without Amiga
 * libraries, mail credentials or network access. */
#include "mailfile.h"
#include "smtp.h"
#include "codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)
static const char *const path = "build/mailfile-test.eml";

static int make_dir(const char *name)
{
#ifdef _WIN32
    return mkdir(name) == 0 || errno == EEXIST;
#else
    return mkdir(name, 0700) == 0 || errno == EEXIST;
#endif
}

static int put_file(const char *name, const void *data, size_t length)
{
    FILE *file = fopen(name, "wb");
    int ok;
    if (!file) return 0;
    ok = !length || fwrite(data, 1U, length, file) == length;
    if (fclose(file)) ok = 0;
    return ok;
}

static int read_file(const char *name, AmgBuffer *out)
{
    FILE *file = fopen(name, "rb");
    unsigned char block[8192];
    size_t n;
    int result = AMG_OK;
    if (!file) return AMG_ERR_IO;
    while ((n = fread(block, 1U, sizeof(block), file)) != 0U && result == AMG_OK)
        result = amg_buffer_append(out, block, n);
    if (ferror(file)) result = AMG_ERR_IO;
    fclose(file);
    return result;
}

static int extract(const AmgMailFile *mail, size_t index, size_t limit,
                     AmgBuffer *out, AmgTransfer *transfer)
{
    FILE *file = tmpfile();
    unsigned char block[8192];
    AmgError error = {0};
    size_t written = 0U, n;
    int result;
    if (!file) return AMG_ERR_IO;
    result = amg_mailfile_extract(mail, index, file, limit, &written, transfer, &error);
    if (result == AMG_OK) {
        CHECK(fflush(file) == 0); CHECK(fseek(file, 0L, SEEK_SET) == 0);
        while ((n = fread(block, 1U, sizeof(block), file)) > 0U)
            CHECK(amg_buffer_append(out, block, n) == AMG_OK);
        CHECK(out->length == written);
    }
    fclose(file);
    return result;
}

typedef struct CancelState { AmgTransferPhase phase; size_t after; unsigned calls; } CancelState;
static int cancel_report(void *context, AmgTransferPhase phase, size_t done, size_t total)
{
    CancelState *state = (CancelState *)context;
    (void)total;
    ++state->calls;
    return phase != state->phase || done < state->after;
}

static int job_count(const char *directory)
{
    DIR *dir = opendir(directory);
    struct dirent *ent;
    int count = 0;
    if (!dir) return -1;
    while ((ent = readdir(dir)) != NULL) if (!strncmp(ent->d_name, "job-", 4U)) ++count;
    closedir(dir);
    return count;
}

static void test_classification(void)
{
    const char *message =
        "Subject: Subject\r\nMessage-ID: <root@example.test>\r\n"
        "Content-Type: multipart/mixed; boundary=m\r\n\r\n"
        "preamble\r\n--m\r\nContent-Type: text/plain; charset=ISO-8859-1\r\n\r\n"
        "Gr\374\337e. Inline --m marker and\r\n--m-prefix\r\nend.\r\n"
        "--m\r\nContent-Type: application/pdf; name=\"bill.pdf\"\r\n"
        "Content-Transfer-Encoding: base64\r\n\r\nQUJD\r\n"
        "--m\r\nContent-Type: multipart/related; boundary=r\r\n\r\n"
        "--r\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n<p>HTML</p>\r\n"
        "--r\r\nContent-Type: image/png\r\nContent-ID: <logo>\r\n"
        "Content-Transfer-Encoding: base64\r\n\r\niVBORw0KGgo=\r\n--r--\r\n"
        "--m\r\nContent-Type: image/jpeg; name=\"attached.jpg\"\r\n"
        "Content-ID: <file>\r\nContent-Disposition: attachment; filename=\"attached.jpg\"\r\n"
        "Content-Transfer-Encoding: base64\r\n\r\nRA==\r\n--m--\r\nepilogue";
    AmgMailFile *mail = NULL;
    AmgError error = {0};
    AmgBuffer body, attachments, graphics, data, payload;
    AmgImapFetchRecord record = {0}, parsed;
    size_t position = 0U;
    CHECK(put_file(path, message, strlen(message)));
    CHECK(amg_mailfile_open(path, 0, NULL, &mail, &error) == AMG_OK);
    if (!mail) return;
    CHECK(mail->attachment_count == 3U); CHECK(mail->text_result == AMG_OK);
    CHECK(mail->text.capacity <= AMIMAIL_MAX_PREVIEW_TEXT + 1U);
    amg_buffer_init(&body); amg_buffer_init(&attachments); amg_buffer_init(&graphics);
    amg_buffer_init(&data); amg_buffer_init(&payload);
    CHECK(amg_mime_extract_text(message, strlen(message), &body, &error) == AMG_OK);
    CHECK(mail->text.length == body.length);
    CHECK(!memcmp(mail->text.data, body.data, body.length));
    CHECK(amg_mailfile_summary(mail, &attachments, &graphics) == AMG_OK);
    CHECK(amg_buffer_terminate(&attachments) == AMG_OK);
    CHECK(amg_buffer_terminate(&graphics) == AMG_OK);
    CHECK(strstr((char *)attachments.data, "bill.pdf") != NULL);
    CHECK(strstr((char *)attachments.data, "attached.jpg") != NULL);
    CHECK(strstr((char *)attachments.data, "embedded-image") == NULL);
    CHECK(strstr((char *)graphics.data, "embedded-image.png") != NULL);
    CHECK(extract(mail, 0U, 3U, &data, NULL) == AMG_OK);
    CHECK(data.length == 3U && !memcmp(data.data, "ABC", 3U)); data.length = 0U;
    CHECK(extract(mail, 1U, 8U, &data, NULL) == AMG_OK);
    CHECK(data.length == 8U && !memcmp(data.data, "\x89PNG\r\n\x1a\n", 8U)); data.length = 0U;
    CHECK(extract(mail, 2U, 1U, &data, NULL) == AMG_OK);
    CHECK(data.length == 1U && data.data[0] == 'D');
    record.uid = 123UL; record.seen = 1; record.answered = 1; record.flagged = 1;
    CHECK(amg_mailfile_payload(mail, &record, &payload, &error) == AMG_OK);
    CHECK(amg_imap_fetch_record_next(payload.data, payload.length, &position, &parsed) == 1);
    CHECK(parsed.uid == 123UL && parsed.answered && parsed.seen && parsed.flagged);
    CHECK(parsed.literal_length < strlen(message)); CHECK(mail->uid == 123UL);
    amg_buffer_free(&body); amg_buffer_free(&attachments); amg_buffer_free(&graphics);
    amg_buffer_free(&data); amg_buffer_free(&payload); amg_mailfile_close(mail);
}

static void test_decoders(void)
{
    unsigned char data[12347];
    AmgBuffer encoded, raw, output;
    AmgMailFile *mail = NULL;
    AmgError error = {0};
    size_t i;
    const char *header = "Content-Type: application/octet-stream; name=data.bin\r\nContent-Transfer-Encoding: base64\r\n\r\n";
    for (i = 0U; i < sizeof(data); ++i) data[i] = (unsigned char)(i * 37U + i / 7U);
    amg_buffer_init(&encoded); amg_buffer_init(&raw); amg_buffer_init(&output);
    CHECK(amg_base64_encode(data, sizeof(data), &encoded) == AMG_OK);
    CHECK(amg_buffer_append_cstr(&raw, header) == AMG_OK);
    /* Change physical wrapping so padding and base64 quartets straddle 8K. */
    for (i = 0U; i < encoded.length; ++i) {
        CHECK(amg_buffer_append_char(&raw, encoded.data[i]) == AMG_OK);
        if (i % 53U == 2U) CHECK(amg_buffer_append_cstr(&raw, "\r\n\t") == AMG_OK);
    }
    CHECK(put_file(path, raw.data, raw.length));
    CHECK(amg_mailfile_open(path, 0, NULL, &mail, &error) == AMG_OK);
    if (mail) {
        CHECK(mail->text_result == AMG_ERR_UNSUPPORTED);
        CHECK(extract(mail, 0U, sizeof(data), &output, NULL) == AMG_OK);
        CHECK(output.length == sizeof(data) && !memcmp(output.data, data, sizeof(data)));
        output.length = 0U; CHECK(extract(mail, 0U, sizeof(data) - 1U, &output, NULL) == AMG_ERR_LIMIT);
        { CancelState state = {AMG_TRANSFER_EXPORT, 8192U, 0U}; AmgTransfer t = {cancel_report, &state};
          CHECK(extract(mail, 0U, sizeof(data), &output, &t) == AMG_ERR_CANCELLED); CHECK(state.calls >= 2U); }
        amg_mailfile_close(mail); mail = NULL;
    }
    /* Exhaust all alignments of quoted-printable escapes and soft breaks. */
    for (i = 8188U; i <= 8195U; ++i) {
        size_t j;
        raw.length = output.length = 0U;
        CHECK(amg_buffer_append_cstr(&raw,
            "Content-Type: application/octet-stream; name=q.bin\r\n"
            "Content-Transfer-Encoding: quoted-printable\r\n\r\n") == AMG_OK);
        for (j = 0U; j < i; ++j) CHECK(amg_buffer_append_char(&raw, 'A') == AMG_OK);
        CHECK(amg_buffer_append_cstr(&raw, "=00=FF=3D=\r\nX=\nY\r\n") == AMG_OK);
        CHECK(put_file(path, raw.data, raw.length));
        CHECK(amg_mailfile_open(path, 0, NULL, &mail, &error) == AMG_OK);
        if (mail) {
            CHECK(extract(mail, 0U, 20000U, &output, NULL) == AMG_OK);
            CHECK(output.length == i + 7U);
            CHECK(!memcmp(output.data + i, "\0\xff=XY\r\n", 7U));
            amg_mailfile_close(mail); mail = NULL;
        }
    }
    amg_buffer_free(&encoded); amg_buffer_free(&raw); amg_buffer_free(&output);
}

static void test_ranges_and_errors(void)
{
    const char *messages[] = {
        "Content-Type: application/octet-stream; name=e.bin\n\n",
        "Content-Type: application/octet-stream; name=e.bin\nContent-Transfer-Encoding: base64\n\nQQ=",
        "Content-Type: application/octet-stream; name=e.bin\nContent-Transfer-Encoding: base64\n\nQQ==BAD",
        "Content-Type: application/octet-stream; name=e.bin\nContent-Transfer-Encoding: base64\n\nA",
        "Content-Type: application/octet-stream; name=e.bin\nContent-Transfer-Encoding: quoted-printable\n\n=0",
        "Content-Type: application/octet-stream; name=e.bin\nContent-Transfer-Encoding: quoted-printable\n\n=QZ",
        "Content-Type: application/octet-stream; name=e.bin\nContent-Transfer-Encoding: x-unknown\n\nABC",
        "Content-Type: application/octet-stream; name=e.bin\nContent-Transfer-Encoding: base64\n\nQUI"
    };
    const int results[] = { AMG_OK, AMG_ERR_PARSE, AMG_ERR_PARSE, AMG_ERR_PARSE,
        AMG_ERR_PARSE, AMG_ERR_PARSE, AMG_ERR_UNSUPPORTED, AMG_OK };
    size_t i;
    AmgError error = {0};
    for (i = 0U; i < sizeof(messages) / sizeof(messages[0]); ++i) {
        AmgMailFile *mail = NULL;
        AmgBuffer output;
        amg_buffer_init(&output);
        CHECK(put_file(path, messages[i], strlen(messages[i])));
        CHECK(amg_mailfile_open(path, 0, NULL, &mail, &error) == AMG_OK);
        if (mail) {
            CHECK(extract(mail, 0U, 1024U, &output, NULL) == results[i]);
            if (i == 0U) CHECK(output.length == 0U);
            if (i == 7U) CHECK(output.length == 2U && !memcmp(output.data, "AB", 2U));
            amg_mailfile_close(mail);
        }
        amg_buffer_free(&output);
    }
    {
        const char *entity = "Content-Type: multipart/mixed; boundary=x\r\n\r\n"
            "--x\r\nContent-Type: application/octet-stream; name=r.bin\r\n\r\n"
            "A\0B\r\n\r\n--x--\r\n";
        size_t entity_length = 142U; /* compute explicitly below, including NUL */
        static const unsigned char bytes[] = "Content-Type: multipart/mixed; boundary=x\r\n\r\n"
            "--x\r\nContent-Type: application/octet-stream; name=r.bin\r\n\r\n"
            "A\0B\r\n\r\n--x--\r\n";
        FILE *file = fopen(path, "wb");
        AmgMailFile *mail = NULL;
        AmgBuffer output;
        (void)entity;
        entity_length = sizeof(bytes) - 1U;
        CHECK(file != NULL);
        if (file) {
            CHECK(fwrite("prefix", 1U, 6U, file) == 6U);
            CHECK(fwrite(bytes, 1U, entity_length, file) == entity_length);
            CHECK(fwrite("trailer", 1U, 7U, file) == 7U); CHECK(fclose(file) == 0);
        }
        CHECK(amg_mailfile_open_range(path, 6U, entity_length, 0, NULL, &mail, &error) == AMG_OK);
        amg_buffer_init(&output);
        if (mail) {
            CHECK(extract(mail, 0U, 1024U, &output, NULL) == AMG_OK);
            CHECK(output.length == 5U && !memcmp(output.data, "A\0B\r\n", 5U));
            amg_mailfile_close(mail); mail = NULL;
        }
        CHECK(amg_mailfile_open_range(path, SIZE_MAX - 1U, 20U, 0, NULL, &mail, &error) == AMG_ERR_LIMIT);
        CHECK(mail == NULL); amg_buffer_free(&output);
    }
}

static void test_export(void)
{
    const char *message = "Content-Type: multipart/mixed; boundary=x\n\n"
        "--x\nContent-Type: application/octet-stream; name=data.bin\n\nABC\n"
        "--x\nContent-Type: image/png\nContent-ID: <logo>\nContent-Transfer-Encoding: base64\n\nRA==\n--x--\n";
    AmgMailFile *mail = NULL;
    AmgError error = {0};
    AmgBuffer output;
    char destination[AMG_SPOOL_PATH_MAX];
    const char *dir = "build/export-test";
    CancelState state = {AMG_TRANSFER_EXPORT, 0U, 0U};
    AmgTransfer t = {cancel_report, &state};
    CHECK(make_dir(dir));
    (void)remove("build/export-test/data.bin"); (void)remove("build/export-test/data.bin.1");
    (void)remove("build/export-test/logo.png");
    CHECK(put_file(path, message, strlen(message)));
    CHECK(put_file("build/export-test/data.bin", "OLD", 3U));
    CHECK(amg_mailfile_open(path, 0, NULL, &mail, &error) == AMG_OK);
    if (!mail) return;
    CHECK(amg_mailfile_save_attachment(mail, 0U, dir, "data.bin", destination, NULL, &error) == AMG_OK);
    CHECK(!strcmp(destination, "build/export-test/data.bin.1"));
    amg_buffer_init(&output); CHECK(read_file("build/export-test/data.bin", &output) == AMG_OK);
    CHECK(output.length == 3U && !memcmp(output.data, "OLD", 3U)); output.length = 0U;
    CHECK(read_file(destination, &output) == AMG_OK);
    CHECK(output.length == 3U && !memcmp(output.data, "ABC", 3U)); output.length = 0U;
    CHECK(amg_mailfile_save_attachment(mail, 1U, dir, "logo.png", destination, &t, &error) == AMG_ERR_CANCELLED);
    CHECK(destination[0] == 0); CHECK(read_file("build/export-test/logo.png", &output) == AMG_ERR_IO);
    CHECK(job_count(dir) == 0);
    CHECK(amg_mailfile_save_attachment(mail, 1U, dir, "../escape", destination, NULL, &error) == AMG_ERR_ARGUMENT);
    CHECK(amg_mailfile_save_attachment(mail, 1U, dir, "DH0:escape", destination, NULL, &error) == AMG_ERR_ARGUMENT);
    CHECK(amg_mailfile_save_attachment(mail, 1U, dir, "logo.png", destination, NULL, &error) == AMG_OK);
    CHECK(read_file(destination, &output) == AMG_OK);
    CHECK(output.length == 1U && output.data[0] == 'D'); CHECK(job_count(dir) == 0);
    amg_buffer_free(&output); amg_mailfile_close(mail);
    (void)remove("build/export-test/data.bin"); (void)remove("build/export-test/data.bin.1");
    (void)remove("build/export-test/logo.png"); (void)remove(dir);
}

static void test_large_mail(void)
{
    const char *attachment_path = "build/mailfile-large.bin";
    AmgAttachmentInput attachment = {0};
    AmgMailDraft draft = {0};
    AmgMailFile *mail = NULL;
    AmgError error = {0};
    unsigned char block[8192];
    FILE *file;
    size_t i, length = 0U, written = 0U;
    memset(block, 0x53, sizeof(block));
    file = fopen(attachment_path, "wb"); CHECK(file != NULL); if (!file) return;
    for (i = 0U; i < AMG_MAIL_MAX_ATTACHMENT_TOTAL / sizeof(block); ++i)
        CHECK(fwrite(block, 1U, sizeof(block), file) == sizeof(block));
    CHECK(fclose(file) == 0);
    attachment.path = attachment_path; attachment.name_utf8 = "large.bin";
    attachment.size = AMG_MAIL_MAX_ATTACHMENT_TOTAL;
    draft.from = "sender@example.test"; draft.to = "recipient@example.test";
    draft.subject = "Large message"; draft.body_utf8 = "Twenty MiB attachment.";
    draft.date_rfc2822 = "Sat, 26 Sep 2026 10:00:00 +0200";
    draft.message_id = "<large@example.test>";
    draft.attachments = &attachment; draft.attachment_count = 1U;
    file = fopen(path, "wb"); CHECK(file != NULL); if (!file) return;
    CHECK(amg_smtp_build_mail_file(&draft, 1, 0, file, &length, NULL, &error) == AMG_OK);
    CHECK(fclose(file) == 0); CHECK(length > AMG_MAIL_MAX_ATTACHMENT_TOTAL);
    CHECK(length < AMIMAIL_MAX_MIME_MESSAGE);
    CHECK(amg_mailfile_open(path, 0, NULL, &mail, &error) == AMG_OK);
    if (mail) {
        CHECK(mail->attachment_count == 1U && mail->count == 3U);
        CHECK(mail->text_result == AMG_OK && mail->text.length < 128U);
        CHECK(mail->text.capacity < 4096U);
        file = fopen("build/mailfile-large-out.bin", "wb"); CHECK(file != NULL);
        if (file) {
            CHECK(amg_mailfile_extract(mail, 0U, file, AMG_MAIL_MAX_ATTACHMENT_TOTAL,
                                       &written, NULL, &error) == AMG_OK);
            CHECK(fclose(file) == 0); CHECK(written == AMG_MAIL_MAX_ATTACHMENT_TOTAL);
            file = fopen("build/mailfile-large-out.bin", "rb");
            CHECK(file != NULL);
            if (file) {
                unsigned char actual[8192];
                size_t verified = 0U, got;
                while ((got = fread(actual, 1U, sizeof(actual), file)) != 0U) {
                    CHECK(!memcmp(actual, block, got));
                    verified += got;
                }
                CHECK(!ferror(file)); CHECK(verified == AMG_MAIL_MAX_ATTACHMENT_TOTAL);
                CHECK(fclose(file) == 0);
            }
        }
        amg_mailfile_close(mail);
    }
    file = fopen(attachment_path, "ab"); CHECK(file != NULL);
    if (file) { CHECK(fputc('X', file) == 'X'); CHECK(fclose(file) == 0); }
    file = tmpfile(); CHECK(file != NULL);
    if (file) {
        CHECK(amg_smtp_build_mail_file(&draft, 1, 0, file, &length, NULL, &error) == AMG_ERR_LIMIT);
        CHECK(ftell(file) == 0L); CHECK(fclose(file) == 0);
    }
    (void)remove(attachment_path); (void)remove("build/mailfile-large-out.bin");
}

int main(void)
{
    test_classification(); test_decoders(); test_ranges_and_errors(); test_export(); test_large_mail();
    (void)remove(path);
    printf("mailfile: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
