/* Exercise the production streaming writer, substituting only its TLS sink.
 * No network, account credentials or actual SMTP delivery are used. */
#include "tls.h"
#include <stdio.h>
#include <string.h>

int review_tls_write_all(AmgTlsConnection *connection, const void *data,
                         size_t length, AmgError *error);
#define amg_tls_write_all review_tls_write_all
#include "../src/smtp.c"
#undef amg_tls_write_all

static unsigned checks, failures, writes;
static size_t sent;
static int fail_write;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

int review_tls_write_all(AmgTlsConnection *connection, const void *data,
                         size_t length, AmgError *error)
{
    (void)connection;
    ++writes;
    CHECK(data != NULL || length == 0U);
    if (fail_write) {
        amg_error_set(error, AMG_ERR_IO, "Injected write failure.");
        return AMG_ERR_IO;
    }
    sent += length;
    return AMG_OK;
}

static void reset(AmgError *error)
{
    memset(error, 0, sizeof(*error));
    sent = 0U; writes = 0U; fail_write = 0;
}

int main(void)
{
    const char *path = "build/review-stream.bin";
    AmgAttachmentInput attachment;
    AmgMailDraft draft;
    AmgBuffer buffer;
    AmgError error;
    size_t total;
    unsigned long attached;
    FILE *file;
    reset(&error);
    total = AMIMAIL_MAX_MIME_MESSAGE - 3U;
    CHECK(smtp_write_mime_bytes(NULL, "abc", 3U, &total, &error) == AMG_OK);
    CHECK(total == AMIMAIL_MAX_MIME_MESSAGE); CHECK(sent == 3U);
    CHECK(smtp_write_mime_bytes(NULL, "d", 1U, &total, &error) == AMG_ERR_LIMIT);
    CHECK(writes == 1U); CHECK(sent == 3U); CHECK(error.code == AMG_ERR_LIMIT);
    total = SIZE_MAX;
    CHECK(smtp_take_mime_bytes(&total, 1U, &error) == AMG_ERR_LIMIT);
    CHECK(total == SIZE_MAX);

    reset(&error); total = 0U;
    amg_buffer_init(&buffer);
    CHECK(amg_buffer_append_cstr(&buffer, ".one\r\n..two\r\n") == AMG_OK);
    CHECK(smtp_write_text(NULL, &buffer, &total, &error) == AMG_OK);
    CHECK(total == buffer.length); CHECK(sent == buffer.length + 2U);
    amg_buffer_free(&buffer);

    file = fopen(path, "wb"); CHECK(file != NULL); if (!file) return 1;
    CHECK(fwrite("ABC", 1U, 3U, file) == 3U); CHECK(fclose(file) == 0);
    memset(&attachment, 0, sizeof(attachment));
    attachment.path = path; attachment.name_utf8 = "stream.bin";
    memset(&draft, 0, sizeof(draft));
    draft.attachments = &attachment; draft.attachment_count = 1U;
    CHECK(validate_attachments(&draft, &error) == AMG_OK);
    reset(&error); total = 0U; attached = AMG_MAIL_MAX_ATTACHMENT_TOTAL - 3UL;
    CHECK(smtp_write_attachment(NULL, &attachment, "boundary", &attached, &total, &error) == AMG_OK);
    CHECK(attached == AMG_MAIL_MAX_ATTACHMENT_TOTAL); CHECK(sent == total);
    CHECK(writes == 2U);

    /* File contents changed after size validation: the actual read must
     * still stop at the remaining decoded-byte allowance. */
    file = fopen(path, "ab"); CHECK(file != NULL); if (!file) return 1;
    CHECK(fputc('D', file) != EOF); CHECK(fclose(file) == 0);
    reset(&error); total = 0U; attached = AMG_MAIL_MAX_ATTACHMENT_TOTAL - 3UL;
    CHECK(smtp_write_attachment(NULL, &attachment, "boundary", &attached, &total, &error) == AMG_ERR_LIMIT);
    CHECK(attached == AMG_MAIL_MAX_ATTACHMENT_TOTAL - 3UL);
    CHECK(writes == 1U); CHECK(error.code == AMG_ERR_LIMIT);

    reset(&error); total = AMIMAIL_MAX_MIME_MESSAGE; attached = 0UL;
    CHECK(smtp_write_attachment(NULL, &attachment, "boundary", &attached, &total, &error) == AMG_ERR_LIMIT);
    CHECK(writes == 0U); CHECK(attached == 0UL);

    reset(&error); total = 0U; attached = 0UL; fail_write = 1;
    CHECK(smtp_write_attachment(NULL, &attachment, "boundary", &attached, &total, &error) == AMG_ERR_IO);
    CHECK(writes == 1U); CHECK(attached == 0UL);

    reset(&error); attached = AMG_MAIL_MAX_ATTACHMENT_TOTAL - 3UL;
    amg_buffer_init(&buffer);
    CHECK(append_attachment_to_buffer(&attachment, "boundary", &buffer, &attached, &error) == AMG_ERR_LIMIT);
    CHECK(attached == AMG_MAIL_MAX_ATTACHMENT_TOTAL - 3UL);
    amg_buffer_free(&buffer);
    CHECK(remove(path) == 0);
    printf("smtp-stream: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
