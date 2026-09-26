#include "buffer.h"
#include "charset.h"
#include "codec.h"
#include "mime.h"
#include "smtp.h"
#include "imap_parser.h"
#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static const char *text(AmgBuffer *b)
{
    CHECK(amg_buffer_terminate(b) == AMG_OK);
    return b->data ? (const char *)b->data : "";
}
static void check_charset(const char *charset, const unsigned char *bytes,
                            size_t length, const char *expected)
{
    AmgBuffer b;
    amg_buffer_init(&b);
    CHECK(amg_charset_to_utf8(charset, bytes, length, &b) == AMG_OK);
    CHECK(!strcmp(text(&b), expected));
    amg_buffer_free(&b);
}
static void charset_tests(void)
{
    const unsigned char latin1[] = {'G','r',0xfc,0xdf,'e'};
    const unsigned char cp1252[] = {0x93,'E','u','r','o',' ',0x80,0x94};
    const unsigned char latin9[] = {0xa4,0xbc,0xbd,0xbe};
    const unsigned char utf16[] = {0xff,0xfe,0x47,0x00,0xfc,0x00,0x3d,0xd8,0x00,0xde};
    const unsigned char utf16be[] = {0x00,0x47,0x00,0xfc};
    const unsigned char broken[] = {'A',0xf0,0x9f};
    const unsigned char with_nul[] = {'A',0,'B'};
    AmgBuffer b;
    check_charset("ISO-8859-1", latin1, sizeof(latin1), "Gr\303\274\303\237e");
    check_charset("latin1", latin1, sizeof(latin1), "Gr\303\274\303\237e");
    check_charset("windows-1252", cp1252, sizeof(cp1252), "\342\200\234Euro \342\202\254\342\200\235");
    check_charset("ISO-8859-15", latin9, sizeof(latin9), "\342\202\254\305\222\305\223\305\270");
    check_charset("UTF-16", utf16, sizeof(utf16), "G\303\274\360\237\230\200");
    check_charset("UTF-16BE", utf16be, sizeof(utf16be), "G\303\274");
    check_charset(NULL, cp1252, sizeof(cp1252), "\342\200\234Euro \342\202\254\342\200\235");
    check_charset("UTF-8", broken, sizeof(broken), "A\357\277\275\357\277\275");
    check_charset("UTF-8", with_nul, sizeof(with_nul), "A\357\277\275B");
    amg_buffer_init(&b);
    CHECK(amg_charset_to_utf8("unsupported-charset", latin1, sizeof(latin1), &b) == AMG_ERR_UNSUPPORTED);
    CHECK(b.length == 0U);
    CHECK(amg_rfc2047_decode("=?windows-1252?Q?Preis_=80?=", &b) == AMG_OK);
    CHECK(!strcmp(text(&b), "Preis \342\202\254"));
    amg_buffer_free(&b);
}
static void mime_text(const char *mail, const char *expected)
{
    AmgBuffer b;
    AmgError error;
    amg_buffer_init(&b);
    CHECK(amg_mime_extract_text(mail, strlen(mail), &b, &error) == AMG_OK);
    CHECK(!strcmp(text(&b), expected));
    amg_buffer_free(&b);
}
static void mime_tests(void)
{
    const char *mail = "Content-Type: multipart/mixed; boundary=bound\r\n\r\n"
        "preamble --bound invalid\r\n--bound\r\nContent-Type: text/plain\r\n\r\n"
        "before --bound inside text\r\n--bound-extra\r\nlast\r\n--bound--\r\n";
    const char *binary_header = "Content-Type: multipart/mixed; boundary=x\r\n\r\n"
        "--x\r\nContent-Type: application/octet-stream\r\n"
        "Content-Disposition: attachment; filename=\"O'Reilly's.bin\"\r\n\r\n";
    const unsigned char payload[] = {'A',0,'B','\r','\n','\r','\n'};
    const char *tail = "\r\n--x--\r\n";
    const char *nested = "Content-Type: multipart/mixed; boundary=outer\r\n\r\n"
        "--outer\r\nContent-Type: multipart/related; boundary=outer-more\r\n\r\n"
        "--outer-more\r\nContent-Type: text/plain\r\n\r\nhello\r\n"
        "--outer-more\r\nContent-Type: image/png\r\nContent-ID: <image>\r\n"
        "Content-Transfer-Encoding: base64\r\n\r\nQUJD\r\n--outer-more--\r\n"
        "--outer\r\nContent-Type: image/jpeg\r\nContent-ID: <explicit>\r\n"
        "Content-Disposition: attachment; filename=\"photo.jpg\"\r\n"
        "Content-Transfer-Encoding: base64\r\n\r\nQUJD\r\n--outer--\r\n";
    const char *extended = "Content-Type: application/octet-stream; x\r\n"
        "Content-Disposition: attachment; filename=\"fallback.txt\"; "
        "filename* = ISO-8859-1'de'gr%FCsse.txt\r\n\r\nABC";
    AmgBuffer b, name, data, regular, embedded;
    AmgError error;
    size_t count = 0U, files = 0U, images = 0U;
    unsigned char *exact;
    size_t exact_size;
    mime_text(mail, "before --bound inside text\r\n--bound-extra\r\nlast");
    mime_text("Content-Type: text/plain\r\n\r\n", "");
    mime_text("Content-Type: text/html\r\n\r\n", "");
    mime_text("Content-Type: text/html; charset=UTF-8\r\n\r\n\357\273\277", "");
    mime_text("Content-Type: multipart/alternative; boundary=z\n\n"
              "--z\nContent-Type: text/plain\n\n\n"
              "--z\nContent-Type: text/html\n\n<p>fallback</p>\n--z--", "fallback");
    mime_text("Content-Type: text/plain; charset=ISO-8859-1\r\n"
              "Content-Transfer-Encoding: quoted-printable\r\n\r\nGr=FC=DFe", "Gr\303\274\303\237e");
    mime_text("Content-Type: text/html; charset = \"windows-1252\"\r\n\r\n<p>Preis \200</p>", "Preis \342\202\254");
    mime_text("Content-Type: text/plain; charset=ISO-8859-1\r\n"
              "Content-Transfer-Encoding: base64\r\n\r\nR3L832U=", "Gr\303\274\303\237e");
    mime_text(nested, "hello");
    /* Exact-sized input, not NUL-terminated: ASan detects any strstr overrun. */
    exact_size = strlen(binary_header) + sizeof(payload) + strlen(tail);
    exact = (unsigned char *)malloc(exact_size);
    CHECK(exact != NULL);
    if (!exact) return;
    memcpy(exact, binary_header, strlen(binary_header));
    memcpy(exact + strlen(binary_header), payload, sizeof(payload));
    memcpy(exact + strlen(binary_header) + sizeof(payload), tail, strlen(tail));
    amg_buffer_init(&name); amg_buffer_init(&data);
    CHECK(amg_mime_attachment_count((const char *)exact, exact_size, &count, &error) == AMG_OK);
    CHECK(count == 1U);
    CHECK(amg_mime_extract_attachment((const char *)exact, exact_size, 0U, &name, &data, &error) == AMG_OK);
    CHECK(!strcmp(text(&name), "O'Reilly's.bin"));
    CHECK(data.length == sizeof(payload)); CHECK(!memcmp(data.data, payload, sizeof(payload)));
    amg_buffer_free(&name); amg_buffer_free(&data); free(exact);
    amg_buffer_init(&regular); amg_buffer_init(&embedded);
    CHECK(amg_mime_attachment_grouped_summary(nested, strlen(nested), &regular, &files,
                                              &embedded, &images, &error) == AMG_OK);
    CHECK(files == 1U); CHECK(images == 1U);
    CHECK(strstr(text(&regular), "photo.jpg") != NULL);
    CHECK(strstr(text(&embedded), "embedded-image.png") != NULL);
    amg_buffer_free(&regular); amg_buffer_free(&embedded);
    amg_buffer_init(&name); amg_buffer_init(&data);
    CHECK(amg_mime_extract_attachment(extended, strlen(extended), 0U, &name, &data, &error) == AMG_OK);
    CHECK(!strcmp(text(&name), "gr\303\274sse.txt"));
    amg_buffer_free(&name); amg_buffer_free(&data);
    count = 999U;
    CHECK(amg_mime_attachment_count("Content-Type: multipart/mixed; boundary=x\r\n\r\n--x\r\n\r\nmissing close",
        strlen("Content-Type: multipart/mixed; boundary=x\r\n\r\n--x\r\n\r\nmissing close"), &count, &error) == AMG_ERR_PARSE);
    CHECK(count == 0U);
    amg_buffer_init(&b);
    CHECK(amg_buffer_set_limit(&b, 8U) == AMG_OK);
    CHECK(amg_mime_extract_text("Content-Type: text/plain\r\n\r\n0123456789ABCDEF",
        strlen("Content-Type: text/plain\r\n\r\n0123456789ABCDEF"), &b, &error) == AMG_ERR_LIMIT);
    CHECK(b.limit_hit); CHECK(b.length == 8U);
    amg_buffer_free(&b);
}
static void buffer_and_parser_tests(void)
{
    AmgBuffer b;
    AmgImapParser parser;
    AmgImapEvent event;
    const char *response = "* 1 FETCH (BODY[] {5}\r\nA\0BCD)\r\nA000001 OK done\r\n";
    const unsigned char *owned;
    amg_buffer_init(&b);
    b.length = SIZE_MAX;
    CHECK(amg_buffer_append_char(&b, 'A') == AMG_ERR_LIMIT);
    CHECK(amg_buffer_terminate(&b) == AMG_ERR_LIMIT);
    b.length = 0;
    CHECK(amg_buffer_set_limit(&b, 4U) == AMG_OK);
    CHECK(amg_buffer_append_cstr(&b, "ABCD") == AMG_OK);
    CHECK(amg_buffer_append_char(&b, 'E') == AMG_ERR_LIMIT);
    CHECK(!strcmp(text(&b), "ABCD")); CHECK(b.capacity <= 5U);
    amg_buffer_free(&b);
    /* Feed exact full bytes, including an embedded NUL in the literal. */
    amg_imap_parser_init(&parser);
    CHECK(amg_imap_parser_feed(&parser, response,
        sizeof("* 1 FETCH (BODY[] {5}\r\nA\0BCD)\r\nA000001 OK done\r\n")-1U) == AMG_OK);
    CHECK(amg_imap_parser_next(&parser, &event) == 1);
    CHECK(event.type == AMG_IMAP_EVENT_LINE);
    owned = parser.pending.data;
    CHECK(amg_imap_parser_next(&parser, &event) == 1);
    CHECK(event.type == AMG_IMAP_EVENT_LITERAL); CHECK(event.length == 5U);
    CHECK(event.data == owned); CHECK(!memcmp(event.data, "A\0BCD", 5U));
    CHECK(amg_imap_parser_next(&parser, &event) == 1);
    CHECK(event.length == 3U); CHECK(!memcmp(event.data, ")\r\n", 3U));
    CHECK(amg_imap_parser_next(&parser, &event) == 1);
    CHECK(!memcmp(event.data, "A000001 OK", 10U));
    CHECK(amg_imap_parser_next(&parser, &event) == 0);
    amg_imap_parser_free(&parser);
    {
        size_t length = AMIGMAIL_MAX_LINE + 1U;
        unsigned char *line = (unsigned char *)malloc(length);
        CHECK(line != NULL);
        if (line) {
            memset(line, 'x', length);
            line[length - 2U] = '\r'; line[length - 1U] = '\n';
            amg_imap_parser_init(&parser);
            CHECK(amg_imap_parser_feed(&parser, line, length) == AMG_OK);
            CHECK(amg_imap_parser_next(&parser, &event) == AMG_ERR_LIMIT);
            CHECK(parser.failure == AMG_IMAP_PARSER_FAILURE_LINE_LIMIT);
            amg_imap_parser_free(&parser);
            free(line);
        }
    }
}
static void size_tests(void)
{
    const char *path = "build/review-20mb.bin", *extra = "build/review-extra.bin";
    unsigned char block[8192];
    unsigned long remaining = AMG_MAIL_MAX_ATTACHMENT_TOTAL;
    AmgAttachmentInput inputs[2];
    AmgMailDraft draft;
    AmgBuffer wire, body, name, data;
    AmgError error;
    FILE *file;
    size_t i, count;
    for (i = 0U; i < sizeof(block); ++i) block[i] = (unsigned char)(i & 255U);
    file = fopen(path, "wb"); CHECK(file != NULL); if (!file) return;
    while (remaining) {
        size_t n = remaining < sizeof(block) ? (size_t)remaining : sizeof(block);
        CHECK(fwrite(block, 1U, n, file) == n); remaining -= (unsigned long)n;
    }
    CHECK(fclose(file) == 0);
    file = fopen(extra, "wb"); CHECK(file != NULL); if (!file) return;
    CHECK(fputc('X', file) != EOF); CHECK(fclose(file) == 0);
    memset(inputs, 0, sizeof(inputs)); memset(&draft, 0, sizeof(draft));
    inputs[0].path = path; inputs[0].name_utf8 = "large.bin";
    inputs[0].size = 1U; /* must NOT trust stale attachment metadata */
    inputs[1].path = extra; inputs[1].name_utf8 = "extra.bin"; inputs[1].size = 1U;
    draft.from = "me@example.com"; draft.to = "you@example.com";
    draft.subject = "Limit test"; draft.body_utf8 = "Small body";
    draft.date_rfc2822 = "Sat, 26 Sep 2026 09:00:00 +0200";
    draft.message_id = "<limit@example.com>"; draft.attachments = inputs;
    draft.attachment_count = 1U;
    amg_buffer_init(&wire);
    CHECK(amg_smtp_build_mail(&draft, 0, &wire, &error) == AMG_OK);
    CHECK(wire.length > AMG_MAIL_MAX_ATTACHMENT_TOTAL);
    CHECK(wire.length < AMIMAIL_MAX_MIME_MESSAGE);
    CHECK(wire.capacity <= AMIMAIL_MAX_MIME_MESSAGE + 1U);
    amg_buffer_init(&body);
    CHECK(amg_mime_extract_text((const char *)wire.data, wire.length, &body, &error) == AMG_OK);
    CHECK(!strcmp(text(&body), "Small body"));
    CHECK(amg_mime_attachment_count((const char *)wire.data, wire.length, &count, &error) == AMG_OK);
    CHECK(count == 1U);
    amg_buffer_init(&name); amg_buffer_init(&data);
    CHECK(amg_mime_extract_attachment((const char *)wire.data, wire.length, 0U, &name, &data, &error) == AMG_OK);
    CHECK(data.length == AMG_MAIL_MAX_ATTACHMENT_TOTAL);
    CHECK(!memcmp(data.data, block, sizeof(block)));
    CHECK(!memcmp(data.data + data.length-sizeof(block), block, sizeof(block)));
    amg_buffer_free(&wire); amg_buffer_free(&body); amg_buffer_free(&name); amg_buffer_free(&data);
    draft.attachment_count = 2U;
    amg_buffer_init(&wire);
    CHECK(amg_smtp_build_mail(&draft, 0, &wire, &error) == AMG_ERR_LIMIT);
    CHECK(wire.length == 0U);
    amg_buffer_free(&wire);
    (void)remove(path); (void)remove(extra);
}
static void bounded_input_tests(void)
{
    unsigned iteration;
    unsigned long seed = 0x12345678UL;
    static const char prefix[] = "Content-Type: multipart/mixed; boundary=x\r\n\r\n";
    for (iteration = 0U; iteration < 3000U; ++iteration) {
        size_t length = 1U + iteration % 257U, i;
        unsigned char *bytes = (unsigned char *)malloc(length);
        AmgBuffer body, name, data;
        size_t count = 0;
        CHECK(bytes != NULL); if (!bytes) return;
        for (i = 0U; i < length; ++i) {
            seed = (seed * 1664525UL + 1013904223UL) & 0xffffffffUL;
            bytes[i] = (unsigned char)(seed >> 24);
        }
        if (iteration & 1U) {
            size_t n = length < sizeof(prefix)-1U ? length : sizeof(prefix)-1U;
            memcpy(bytes, prefix, n);
        }
        amg_buffer_init(&body); amg_buffer_init(&name); amg_buffer_init(&data);
        (void)amg_mime_extract_text((const char *)bytes, length, &body, NULL);
        (void)amg_mime_attachment_count((const char *)bytes, length, &count, NULL);
        (void)amg_mime_extract_attachment((const char *)bytes, length, 0U, &name, &data, NULL);
        amg_buffer_free(&body); amg_buffer_free(&name); amg_buffer_free(&data); free(bytes);
    }
}
int main(void)
{
    charset_tests(); mime_tests(); buffer_and_parser_tests(); size_tests(); bounded_input_tests();
    printf("review: %u checks, %u failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
