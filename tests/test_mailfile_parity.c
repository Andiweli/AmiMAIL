/* Run the existing message fixtures through both text extraction paths.
 * The original suite still executes all its own assertions unchanged. */
#include "mailfile.h"
#include <stdio.h>
#include <string.h>
static unsigned parity_cases, parity_failures;
static int parity_extract(const char *message, size_t length, AmgBuffer *output, AmgError *error);
#define amg_mime_extract_text parity_extract
#define main existing_fixture_suite
int existing_fixture_suite(void);
#include "test_main.c"
#undef main
#undef amg_mime_extract_text

static int parity_extract(const char *message, size_t length, AmgBuffer *output, AmgError *error)
{
    const char *path = "build/mailfile-parity.eml";
    AmgMailFile *mail = NULL;
    AmgError disk_error = {0};
    size_t before = output->length;
    int result = amg_mime_extract_text(message, length, output, error);
    FILE *file = fopen(path, "wb");
    int disk_result;
    ++parity_cases;
    if (!file) { ++parity_failures; return result; }
    if (fwrite(message, 1U, length, file) != length) ++parity_failures;
    if (fclose(file) != 0) ++parity_failures;
    disk_result = amg_mailfile_open(path, 0, NULL, &mail, &disk_error);
    if (disk_result == AMG_OK) disk_result = mail->text_result;
    if (disk_result != result ||
        (result == AMG_OK && (!mail || mail->text.length != output->length - before ||
         (mail->text.length && memcmp(mail->text.data, output->data + before, mail->text.length))))) {
        fprintf(stderr, "MIME parity case %u: RAM=%d/%lu, disk=%d/%lu\n",
            parity_cases, result, (unsigned long)(output->length - before),
            disk_result, (unsigned long)(mail ? mail->text.length : 0U));
        ++parity_failures;
    }
    amg_mailfile_close(mail); remove(path);
    return result;
}
int main(void)
{
    int result = existing_fixture_suite();
    printf("mailfile parity: %u fixtures, %u differences\n", parity_cases, parity_failures);
    return result || parity_failures ? 1 : 0;
}
