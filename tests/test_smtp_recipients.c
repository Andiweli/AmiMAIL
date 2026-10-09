#include "transfer_tls_double.h"
#include "../src/smtp.c"

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static void valid(const char *list, unsigned expected, const char *commands)
{
    const char *answers = "250 OK\r\n250 OK\r\n250 OK\r\n";
    AmgError error = {0};
    unsigned count = 0U;
    td_reset(answers, strlen(answers), 1U);
    CHECK(smtp_recipient_list((AmgTlsConnection *)&td_dummy,
                              list, &count, &error) == AMG_OK);
    CHECK(count == expected);
    CHECK(td_output.length == strlen(commands));
    CHECK(td_output.length == strlen(commands) &&
          !memcmp(td_output.data, commands, td_output.length));
}

int main(void)
{
    const char *invalid[] = {
        "\"Unclosed <a@example.com>", "A <a@example.com", "a@example.com>",
        "a@example.com (unclosed", "A <a@example.com> extra",
        "a b@example.com", "a@@example.com", "Friends: a@example.com",
        "a@example.com\r\nRCPT TO:<other@example.com>", "\"a\"b@example.com"
    };
    size_t i;
    valid("jane@example.com", 1U, "RCPT TO:<jane@example.com>\r\n");
    valid("\"Doe, Jane\" <jane@example.com>", 1U, "RCPT TO:<jane@example.com>\r\n");
    valid("\"Team; \\\"Sales\\\"\" <a@example.com>, b@example.com", 2U,
          "RCPT TO:<a@example.com>\r\nRCPT TO:<b@example.com>\r\n");
    valid("A (nested (comment, x)) <a@example.com>;b@example.com", 2U,
          "RCPT TO:<a@example.com>\r\nRCPT TO:<b@example.com>\r\n");
    valid("Friends: a@example.com, b@example.com;", 2U,
          "RCPT TO:<a@example.com>\r\nRCPT TO:<b@example.com>\r\n");
    valid("Empty:; a@example.com", 1U, "RCPT TO:<a@example.com>\r\n");
    valid("\"<Doe>\" <a@example.com>", 1U, "RCPT TO:<a@example.com>\r\n");
    valid("\"a b,c\"@example.com", 1U, "RCPT TO:<\"a b,c\"@example.com>\r\n");
    valid("(name) a@example.com (trailing)", 1U, "RCPT TO:<a@example.com>\r\n");
    valid("a@[IPv6:2001:db8::1]", 1U, "RCPT TO:<a@[IPv6:2001:db8::1]>\r\n");
    for (i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        AmgError error = {0}; unsigned count = 0U;
        td_reset("250 OK\r\n", 8U, 8U);
        CHECK(smtp_recipient_list((AmgTlsConnection *)&td_dummy,
                                  invalid[i], &count, &error) == AMG_ERR_ARGUMENT);
        CHECK(error.code == AMG_ERR_ARGUMENT);
    }
    amg_buffer_free(&td_input); amg_buffer_free(&td_output);
    printf("smtp-recipients: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
