#include "smtp.h"
#include "codec.h"
#include "tls.h"
#include "i18n.h"

#define T(id, en) amg_tr((id), (en))

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define SMTP_HEADER_RECOMMENDED_LINE 78U
#define SMTP_MAX_CONTENT_LINE 998U

static int header_safe(const char *value)
{
    return value && !strchr(value, '\r') && !strchr(value, '\n');
}

static unsigned long boundary_hash_text(unsigned long hash, const char *text)
{
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    while (*p) {
        hash ^= (unsigned long)*p++;
        hash *= 16777619UL;
    }
    return hash;
}

static void make_mime_boundary(const AmgMailDraft *draft, char boundary[96])
{
    static unsigned long sequence = 0UL;
    unsigned long hash = 2166136261UL;
    hash = boundary_hash_text(hash, draft->from);
    hash = boundary_hash_text(hash, draft->to);
    hash = boundary_hash_text(hash, draft->cc);
    hash = boundary_hash_text(hash, draft->bcc);
    hash = boundary_hash_text(hash, draft->subject);
    hash = boundary_hash_text(hash, draft->message_id);
    ++sequence;
    snprintf(boundary, 96U, "=_AmiMail_%08lx_%08lx_%lu",
             hash, sequence, (unsigned long)draft->attachment_count);
}

int amg_smtp_dot_stuff(const char *message, size_t length, AmgBuffer *output)
{
    size_t i;
    int line_start = 1;
    if ((!message && length) || !output) return AMG_ERR_ARGUMENT;
    for (i = 0; i < length; ++i) {
        if (line_start && message[i] == '.' &&
            amg_buffer_append_char(output, '.') != AMG_OK)
            return AMG_ERR_MEMORY;
        if (amg_buffer_append_char(output, (unsigned char)message[i]) != AMG_OK)
            return AMG_ERR_MEMORY;
        line_start = message[i] == '\n';
    }
    return AMG_OK;
}

int amg_smtp_reply_subject(const char *subject, AmgBuffer *output)
{
    const char *p = subject ? subject : "";
    while (*p && isspace((unsigned char)*p)) ++p;
    if (tolower((unsigned char)p[0]) == 'r' &&
        tolower((unsigned char)p[1]) == 'e' && p[2] == ':')
        return amg_buffer_append_cstr(output, p);
    if (amg_buffer_append_cstr(output, "Re: ") != AMG_OK)
        return AMG_ERR_MEMORY;
    return amg_buffer_append_cstr(output, p);
}

static int append_header(AmgBuffer *output, const char *name, const char *value)
{
    const char *p;
    size_t column;
    int result;

    if (!output || !name || !*name || !header_safe(value))
        return AMG_ERR_ARGUMENT;

    result = amg_buffer_append_cstr(output, name);
    if (result == AMG_OK) result = amg_buffer_append_cstr(output, ": ");
    if (result != AMG_OK) return AMG_ERR_MEMORY;
    column = strlen(name) + 2U;
    if (column > SMTP_MAX_CONTENT_LINE) return AMG_ERR_LIMIT;

    p = value;
    while (*p) {
        if (*p == ' ' || *p == '\t') {
            const char *word = p;
            const char *end;
            size_t upcoming;

            while (*word == ' ' || *word == '\t') ++word;
            end = word;
            while (*end && *end != ' ' && *end != '\t') ++end;
            upcoming = (size_t)(end - p);

            /* Replace the existing folding whitespace by CRLF + that same
             * whitespace.  Unfolding therefore reproduces the original
             * header value instead of inserting or deleting characters. */
            if (*word && column + upcoming > SMTP_HEADER_RECOMMENDED_LINE) {
                if (amg_buffer_append_cstr(output, "\r\n") != AMG_OK)
                    return AMG_ERR_MEMORY;
                column = 0;
            }
        }

        if (column + 1U > SMTP_MAX_CONTENT_LINE)
            return AMG_ERR_LIMIT;
        if (amg_buffer_append_char(output, (unsigned char)*p) != AMG_OK)
            return AMG_ERR_MEMORY;
        ++column;
        ++p;
    }

    return amg_buffer_append_cstr(output, "\r\n");
}

static int append_quoted_printable_body(const char *body, AmgBuffer *output)
{
    const char *text = body ? body : "";
    return amg_quoted_printable_encode(text, strlen(text), output);
}

int amg_smtp_build_reply(const AmgReplyDraft *draft, AmgBuffer *output,
                         AmgError *error)
{
    AmgBuffer subject, raw, stuffed;
    int result = AMG_OK;
    if (!draft || !output || !header_safe(draft->from) ||
        !header_safe(draft->to) || !header_safe(draft->subject) ||
        !header_safe(draft->date_rfc2822) || !header_safe(draft->message_id)) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T(MSG_INVALID_MAIL_ADDRESS_OR_HEADER_LINE, "Invalid mail address or header line."));
        return AMG_ERR_ARGUMENT;
    }
    amg_buffer_init(&subject);
    amg_buffer_init(&raw);
    amg_buffer_init(&stuffed);
    result = amg_smtp_reply_subject(draft->subject, &subject);
    if (result == AMG_OK) result = amg_buffer_terminate(&subject);
    if (result == AMG_OK) result = append_header(&raw, "From", draft->from);
    if (result == AMG_OK) result = append_header(&raw, "To", draft->to);
    if (result == AMG_OK)
        result = append_header(&raw, "Subject", (const char *)subject.data);
    if (result == AMG_OK)
        result = append_header(&raw, "Date", draft->date_rfc2822);
    if (result == AMG_OK)
        result = append_header(&raw, "Message-ID", draft->message_id);
    if (result == AMG_OK && draft->in_reply_to && *draft->in_reply_to)
        result = append_header(&raw, "In-Reply-To", draft->in_reply_to);
    if (result == AMG_OK && draft->references && *draft->references)
        result = append_header(&raw, "References", draft->references);
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(
            &raw,
            "MIME-Version: 1.0\r\n"
            "Content-Type: text/plain; charset=UTF-8\r\n"
            "Content-Transfer-Encoding: quoted-printable\r\n\r\n");
    if (result == AMG_OK)
        result = append_quoted_printable_body(draft->body_utf8, &raw);
    if (result == AMG_OK &&
        (raw.length < 2U || raw.data[raw.length - 2U] != '\r' ||
         raw.data[raw.length - 1U] != '\n'))
        result = amg_buffer_append_cstr(&raw, "\r\n");
    if (result == AMG_OK)
        result = amg_smtp_dot_stuff((const char *)raw.data, raw.length, &stuffed);
    if (result == AMG_OK)
        result = amg_buffer_append(output, stuffed.data, stuffed.length);
    amg_buffer_free(&subject);
    amg_buffer_free(&raw);
    amg_buffer_free(&stuffed);
    amg_error_set(error, result,
                  result == AMG_OK
                      ? ""
                      : T(MSG_REPLY_MESSAGE_COULD_NOT_BE_CREATED, "Reply message could not be created."));
    return result;
}

static int smtp_response_capture_code(AmgTlsConnection *connection,
                                      int expected_class,
                                      AmgBuffer *capture, int *code_out,
                                      AmgError *error)
{
    char line[1024];
    size_t used;
    int code = 0, continued;
    do {
        int saw_newline = 0;
        used = 0;
        while (used + 1U < sizeof(line)) {
            long count = amg_tls_read(connection, line + used, 1U, error);
            if (count <= 0) {
                if (error && error->code != AMG_OK) return error->code;
                amg_error_set(error, AMG_ERR_IO,
                    T(MSG_THE_SERVER_CLOSED_THE_TLS_CONNECTION,
                      "The server closed the TLS connection."));
                return AMG_ERR_IO;
            }
            if (line[used++] == '\n') {
                saw_newline = 1;
                break;
            }
        }
        if (!saw_newline) {
            amg_error_set(error, AMG_ERR_LIMIT,
                          T(MSG_SMTP_RESPONSE_LINE_IS_TOO_LONG, "SMTP response line is too long."));
            return AMG_ERR_LIMIT;
        }
        line[used] = 0;
        if (capture && amg_buffer_append(capture, line, used) != AMG_OK) {
            amg_error_set(error, AMG_ERR_MEMORY,
                          T(MSG_NOT_ENOUGH_MEMORY_FOR_THE_SMTP_RESPONSE, "Not enough memory for the SMTP response."));
            return AMG_ERR_MEMORY;
        }
        if (used < 4U || !isdigit((unsigned char)line[0]) ||
            !isdigit((unsigned char)line[1]) ||
            !isdigit((unsigned char)line[2]) ||
            (line[3] != '-' && line[3] != ' ')) {
            amg_error_set(error, AMG_ERR_PROTOCOL,
                          T(MSG_INVALID_SMTP_RESPONSE, "Invalid SMTP response."));
            return AMG_ERR_PROTOCOL;
        }
        code = (line[0] - '0') * 100 +
               (line[1] - '0') * 10 + line[2] - '0';
        continued = line[3] == '-';
    } while (continued);
    if (code_out) *code_out = code;
    if (expected_class && code / 100 != expected_class) {
        amg_error_set(error, AMG_ERR_PROTOCOL, line);
        return AMG_ERR_PROTOCOL;
    }
    return AMG_OK;
}

static int smtp_response_capture(AmgTlsConnection *connection,
                                 int expected_class, AmgBuffer *capture,
                                 AmgError *error)
{
    return smtp_response_capture_code(connection, expected_class, capture,
                                      NULL, error);
}

static int smtp_response(AmgTlsConnection *connection, int expected_class,
                         AmgError *error)
{
    return smtp_response_capture(connection, expected_class, NULL, error);
}

static int smtp_command_capture(AmgTlsConnection *connection,
                                const char *command, int expected_class,
                                AmgBuffer *capture, AmgError *error)
{
    int result = amg_tls_write_all(connection, command, strlen(command), error);
    return result == AMG_OK
               ? smtp_response_capture(connection, expected_class,
                                       capture, error)
               : result;
}

static int smtp_command(AmgTlsConnection *connection, const char *command,
                        int expected_class, AmgError *error)
{
    return smtp_command_capture(connection, command, expected_class,
                                NULL, error);
}

static int ascii_ci_equal_n(const char *a, size_t length, const char *b)
{
    size_t i;
    if (!a || !b || strlen(b) != length) return 0;
    for (i = 0; i < length; ++i) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return 1;
}

int amg_smtp_response_has_capability(const AmgBuffer *response,
                                     const char *capability)
{
    size_t position = 0;
    if (!response || !response->data || !capability) return 0;
    while (position < response->length) {
        size_t end = position;
        size_t start, token_end;
        while (end < response->length && response->data[end] != '\n') ++end;
        start = position;
        if (end - position >= 4U &&
            isdigit((unsigned char)response->data[position]) &&
            isdigit((unsigned char)response->data[position + 1U]) &&
            isdigit((unsigned char)response->data[position + 2U]) &&
            (response->data[position + 3U] == '-' ||
             response->data[position + 3U] == ' ')) {
            start = position + 4U;
            while (start < end &&
                   (response->data[start] == ' ' ||
                    response->data[start] == '\t')) ++start;
            token_end = start;
            while (token_end < end &&
                   response->data[token_end] != ' ' &&
                   response->data[token_end] != '\t' &&
                   response->data[token_end] != '\r') ++token_end;
            if (ascii_ci_equal_n((const char *)response->data + start,
                                 token_end - start, capability))
                return 1;
        }
        position = end < response->length ? end + 1U : end;
    }
    return 0;
}

int amg_smtp_response_auth_has_mechanism(const AmgBuffer *response,
                                         const char *mechanism)
{
    size_t position = 0;
    if (!response || !response->data || !mechanism || !*mechanism) return 0;
    while (position < response->length) {
        size_t end = position;
        size_t cursor, token_end;
        while (end < response->length && response->data[end] != '\n') ++end;
        if (end - position >= 4U &&
            isdigit((unsigned char)response->data[position]) &&
            isdigit((unsigned char)response->data[position + 1U]) &&
            isdigit((unsigned char)response->data[position + 2U]) &&
            (response->data[position + 3U] == '-' ||
             response->data[position + 3U] == ' ')) {
            cursor = position + 4U;
            while (cursor < end &&
                   (response->data[cursor] == ' ' ||
                    response->data[cursor] == '\t')) ++cursor;
            if (end - cursor >= 4U &&
                ascii_ci_equal_n((const char *)response->data + cursor,
                                 4U, "AUTH")) {
                cursor += 4U;
                if (cursor < end && response->data[cursor] == '=') ++cursor;
                while (cursor < end) {
                    while (cursor < end &&
                           (response->data[cursor] == ' ' ||
                            response->data[cursor] == '\t')) ++cursor;
                    token_end = cursor;
                    while (token_end < end &&
                           response->data[token_end] != ' ' &&
                           response->data[token_end] != '\t' &&
                           response->data[token_end] != '\r') ++token_end;
                    if (token_end > cursor &&
                        ascii_ci_equal_n(
                            (const char *)response->data + cursor,
                            token_end - cursor, mechanism))
                        return 1;
                    if (token_end == cursor) break;
                    cursor = token_end;
                }
            }
        }
        position = end < response->length ? end + 1U : end;
    }
    return 0;
}

static int smtp_ehlo(AmgTlsConnection *connection, AmgBuffer *response,
                     AmgError *error)
{
    if (response) response->length = 0;
    return smtp_command_capture(connection, "EHLO amimail.local\r\n", 2,
                                response, error);
}

static int smtp_auth_login(AmgTlsConnection *connection,
                           const AmgAccount *account, AmgError *error)
{
    AmgBuffer user64, password64, command;
    const char *password = amg_account_smtp_password(account);
    int result;
    amg_buffer_init(&user64);
    amg_buffer_init(&password64);
    amg_buffer_init(&command);

    result = amg_base64_encode(
        (const unsigned char *)amg_account_smtp_user(account),
        strlen(amg_account_smtp_user(account)), &user64);
    if (result == AMG_OK)
        result = amg_base64_encode(
            (const unsigned char *)(password ? password : ""),
            strlen(password ? password : ""), &password64);
    if (result == AMG_OK)
        result = smtp_command(connection, "AUTH LOGIN\r\n", 3, error);
    if (result == AMG_OK) {
        result = amg_buffer_append(&command, user64.data, user64.length);
        if (result == AMG_OK)
            result = amg_buffer_append_cstr(&command, "\r\n");
        if (result == AMG_OK) {
            amg_buffer_terminate(&command);
            result = smtp_command(connection, (const char *)command.data, 3,
                                  error);
        }
    }
    if (result == AMG_OK) {
        command.length = 0;
        result = amg_buffer_append(&command, password64.data,
                                   password64.length);
        if (result == AMG_OK)
            result = amg_buffer_append_cstr(&command, "\r\n");
        if (result == AMG_OK) {
            amg_buffer_terminate(&command);
            result = smtp_command(connection, (const char *)command.data, 2,
                                  error);
        }
    }

    if (result == AMG_ERR_PROTOCOL) {
        result = AMG_ERR_AUTH;
        if (error) error->code = AMG_ERR_AUTH;
    }
    amg_secure_clear(user64.data, user64.capacity);
    amg_secure_clear(password64.data, password64.capacity);
    amg_secure_clear(command.data, command.capacity);
    amg_buffer_free(&user64);
    amg_buffer_free(&password64);
    amg_buffer_free(&command);
    return result;
}

static int smtp_auth_plain(AmgTlsConnection *connection,
                           const AmgAccount *account, AmgError *error)
{
    AmgBuffer raw, encoded, command;
    const char *password = amg_account_smtp_password(account);
    int result = AMG_OK;
    int code = 0;
    amg_buffer_init(&raw);
    amg_buffer_init(&encoded);
    amg_buffer_init(&command);

    result = amg_buffer_append_char(&raw, 0);
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(&raw, amg_account_smtp_user(account));
    if (result == AMG_OK) result = amg_buffer_append_char(&raw, 0);
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(&raw, password ? password : "");
    if (result == AMG_OK)
        result = amg_base64_encode(raw.data, raw.length, &encoded);
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(&command, "AUTH PLAIN ");
    if (result == AMG_OK)
        result = amg_buffer_append(&command, encoded.data, encoded.length);
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(&command, "\r\n");
    if (result == AMG_OK)
        result = amg_tls_write_all(connection, command.data, command.length,
                                   error);
    if (result == AMG_OK)
        result = smtp_response_capture_code(connection, 0, NULL, &code,
                                            error);

    /* Most servers accept the initial response.  A few answer with 334 and
     * request the same PLAIN response as a continuation instead. */
    if (result == AMG_OK && code == 334) {
        command.length = 0;
        result = amg_buffer_append(&command, encoded.data, encoded.length);
        if (result == AMG_OK)
            result = amg_buffer_append_cstr(&command, "\r\n");
        if (result == AMG_OK)
            result = amg_tls_write_all(connection, command.data,
                                       command.length, error);
        if (result == AMG_OK)
            result = smtp_response_capture_code(connection, 0, NULL, &code,
                                                error);
    }

    if (result == AMG_OK && code / 100 != 2) {
        char message[192];
        amg_tr_snprintf(message, sizeof(message), MSG_SMTP_AUTHENTICATION_REJECTED_CODE_VALUE, "SMTP authentication rejected (code %d).", code);
        amg_error_set(error, AMG_ERR_AUTH, message);
        result = AMG_ERR_AUTH;
    }
    if (result != AMG_OK && error && error->code == AMG_ERR_PROTOCOL)
        error->code = AMG_ERR_AUTH;

    amg_secure_clear(raw.data, raw.capacity);
    amg_secure_clear(encoded.data, encoded.capacity);
    amg_secure_clear(command.data, command.capacity);
    amg_buffer_free(&raw);
    amg_buffer_free(&encoded);
    amg_buffer_free(&command);
    return result;
}

static int smtp_auth_xoauth2(AmgTlsConnection *connection,
                             const AmgAccount *account,
                             const char *access_token, AmgError *error)
{
    AmgBuffer raw, encoded, command;
    int result = AMG_OK;
    int code = 0;
    amg_buffer_init(&raw);
    amg_buffer_init(&encoded);
    amg_buffer_init(&command);
    if (!access_token || !*access_token) {
        amg_error_set(error, AMG_ERR_AUTH,
                      T(MSG_OAUTH_ACCESS_TOKEN_IS_MISSING, "OAuth access token is missing."));
        result = AMG_ERR_AUTH;
    }
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(&raw, "user=");
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(&raw, amg_account_smtp_user(account));
    if (result == AMG_OK) result = amg_buffer_append_char(&raw, 1);
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(&raw, "auth=Bearer ");
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(&raw, access_token);
    if (result == AMG_OK) result = amg_buffer_append_char(&raw, 1);
    if (result == AMG_OK) result = amg_buffer_append_char(&raw, 1);
    if (result == AMG_OK)
        result = amg_base64_encode(raw.data, raw.length, &encoded);
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(&command, "AUTH XOAUTH2 ");
    if (result == AMG_OK)
        result = amg_buffer_append(&command, encoded.data, encoded.length);
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(&command, "\r\n");
    if (result == AMG_OK)
        result = amg_tls_write_all(connection, command.data, command.length,
                                   error);
    if (result == AMG_OK)
        result = smtp_response_capture_code(connection, 0, NULL, &code,
                                            error);
    if (result == AMG_OK && code == 334) {
        /* XOAUTH2 error challenges require an empty client response so that
         * the server can finish the AUTH exchange instead of waiting forever. */
        result = amg_tls_write_all(connection, "\r\n", 2U, error);
        if (result == AMG_OK)
            result = smtp_response_capture_code(connection, 0, NULL, &code,
                                                error);
    }
    if (result == AMG_OK && code / 100 != 2) {
        char message[192];
        amg_tr_snprintf(message, sizeof(message), MSG_SMTP_OAUTH_AUTHENTICATION_REJECTED_CODE_VALUE, "SMTP OAuth authentication rejected (code %d).", code);
        amg_error_set(error, AMG_ERR_AUTH, message);
        result = AMG_ERR_AUTH;
    }
    amg_secure_clear(raw.data, raw.capacity);
    amg_secure_clear(encoded.data, encoded.capacity);
    amg_secure_clear(command.data, command.capacity);
    amg_buffer_free(&raw);
    amg_buffer_free(&encoded);
    amg_buffer_free(&command);
    return result;
}

static int smtp_authenticate(AmgTlsConnection *connection,
                             const AmgAccount *account,
                             const char *access_token,
                             const AmgBuffer *ehlo_response,
                             AmgError *error)
{
    int has_plain, has_login, has_xoauth2;
    int result;
    has_plain = amg_smtp_response_auth_has_mechanism(ehlo_response, "PLAIN");
    has_login = amg_smtp_response_auth_has_mechanism(ehlo_response, "LOGIN");
    has_xoauth2 =
        amg_smtp_response_auth_has_mechanism(ehlo_response, "XOAUTH2");

    if (account->auth_mode == AMG_AUTH_OAUTH2_GOOGLE) {
        if (!has_xoauth2) {
            amg_error_set(error, AMG_ERR_UNSUPPORTED,
                          T(MSG_THE_SMTP_SERVER_DOES_NOT_ADVERTISE_AUTH_XOAUTH2, "The SMTP server does not advertise AUTH XOAUTH2."));
            return AMG_ERR_UNSUPPORTED;
        }
        return smtp_auth_xoauth2(connection, account, access_token, error);
    }

    if (has_plain) {
        result = smtp_auth_plain(connection, account, error);
        if (result == AMG_OK) return AMG_OK;
        if (result == AMG_ERR_AUTH && has_login) {
            amg_error_set(error, AMG_OK, "");
            return smtp_auth_login(connection, account, error);
        }
        return result;
    }
    if (has_login)
        return smtp_auth_login(connection, account, error);

    amg_error_set(error, AMG_ERR_UNSUPPORTED,
                  T(MSG_THE_SMTP_SERVER_ADVERTISES_NEITHER_AUTH_PLAIN_NOR, "The SMTP server advertises neither AUTH PLAIN nor AUTH LOGIN."));
    return AMG_ERR_UNSUPPORTED;
}

static AmgTlsConnection *smtp_open(const AmgAccount *account,
                                   const char *access_token, AmgError *error)
{
    AmgTlsConnection *connection;
    AmgBuffer ehlo_response;
    int result;

    amg_buffer_init(&ehlo_response);
    if (account->smtp_starttls)
        connection = amg_tls_connect_plain(account->smtp_host,
                                           account->smtp_port, 30U, error);
    else
        connection = amg_tls_connect(account->smtp_host,
                                     account->smtp_port, 30U, error);
    if (!connection) {
        amg_buffer_free(&ehlo_response);
        return NULL;
    }

    /* SMTP greeting is always sent before EHLO, both for implicit TLS and
     * for the plaintext phase preceding STARTTLS. */
    result = smtp_response(connection, 2, error);
    if (result == AMG_OK)
        result = smtp_ehlo(connection, &ehlo_response, error);

    if (result == AMG_OK && account->smtp_starttls) {
        if (!amg_smtp_response_has_capability(&ehlo_response, "STARTTLS")) {
            amg_error_set(
                error, AMG_ERR_UNSUPPORTED,
                T(MSG_THE_SMTP_SERVER_DOES_NOT_ADVERTISE_STARTTLS, "The SMTP server does not advertise STARTTLS."));
            result = AMG_ERR_UNSUPPORTED;
        } else {
            result = smtp_command(connection, "STARTTLS\r\n", 2, error);
            if (result == AMG_OK)
                result = amg_tls_starttls(connection, account->smtp_host,
                                          error);
            if (result == AMG_OK) {
                /* RFC 3207: discard knowledge from the plaintext EHLO and
                 * issue EHLO again after TLS before authentication. */
                ehlo_response.length = 0;
                result = smtp_ehlo(connection, &ehlo_response, error);
            }
        }
    }

    if (result == AMG_OK)
        result = smtp_authenticate(connection, account, access_token,
                                   &ehlo_response, error);
    amg_buffer_free(&ehlo_response);
    if (result != AMG_OK) {
        amg_tls_close(connection);
        return NULL;
    }
    return connection;
}

static int smtp_mail_from(AmgTlsConnection *connection, const char *address,
                          AmgError *error)
{
    char command[640];
    int length;
    if (!header_safe(address) || !strchr(address, '@')) return AMG_ERR_ARGUMENT;
    length = snprintf(command, sizeof(command), "MAIL FROM:<%s>\r\n", address);
    if (length < 0 || (size_t)length >= sizeof(command)) return AMG_ERR_LIMIT;
    return smtp_command(connection, command, 2, error);
}

static int smtp_recipient(AmgTlsConnection *connection, const char *start,
                          size_t length, AmgError *error)
{
    char address[512], command[640];
    const char *left, *right;
    int count;
    while (length && isspace((unsigned char)*start)) {
        ++start;
        --length;
    }
    while (length && isspace((unsigned char)start[length - 1U])) --length;
    if (!length) return 0;
    left = memchr(start, '<', length);
    right = left ? memchr(left + 1, '>', length - (size_t)(left + 1 - start))
                 : NULL;
    if (left && right) {
        start = left + 1;
        length = (size_t)(right - start);
    }
    while (length && isspace((unsigned char)*start)) {
        ++start;
        --length;
    }
    while (length && isspace((unsigned char)start[length - 1U])) --length;
    if (!length || length >= sizeof(address)) return AMG_ERR_ARGUMENT;
    memcpy(address, start, length);
    address[length] = 0;
    if (!header_safe(address) || !strchr(address, '@') || strchr(address, ' '))
        return AMG_ERR_ARGUMENT;
    count = snprintf(command, sizeof(command), "RCPT TO:<%s>\r\n", address);
    if (count < 0 || (size_t)count >= sizeof(command)) return AMG_ERR_LIMIT;
    if (smtp_command(connection, command, 2, error) != AMG_OK)
        return error ? error->code : AMG_ERR_PROTOCOL;
    return 1;
}

static int smtp_recipient_list(AmgTlsConnection *connection, const char *list,
                               unsigned *recipient_count, AmgError *error)
{
    const char *p, *start;
    if (!list || !*list) return AMG_OK;
    if (!header_safe(list)) return AMG_ERR_ARGUMENT;
    p = start = list;
    for (;;) {
        if (*p == ',' || *p == ';' || *p == 0) {
            int result = smtp_recipient(connection, start,
                                        (size_t)(p - start), error);
            if (result < 0) return result;
            if (result > 0) ++*recipient_count;
            if (!*p) break;
            start = p + 1;
        }
        ++p;
    }
    return AMG_OK;
}

static int smtp_begin_data(AmgTlsConnection *connection, const char *from,
                           const char *to, const char *cc, const char *bcc,
                           AmgError *error)
{
    unsigned recipients = 0;
    int result = smtp_mail_from(connection, from, error);
    if (result == AMG_OK)
        result = smtp_recipient_list(connection, to, &recipients, error);
    if (result == AMG_OK)
        result = smtp_recipient_list(connection, cc, &recipients, error);
    if (result == AMG_OK)
        result = smtp_recipient_list(connection, bcc, &recipients, error);
    if (result == AMG_OK && !recipients) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T(MSG_AT_LEAST_ONE_RECIPIENT_IS_REQUIRED, "At least one recipient is required."));
        result = AMG_ERR_ARGUMENT;
    }
    if (result == AMG_OK)
        result = smtp_command(connection, "DATA\r\n", 3, error);
    return result;
}

int amg_smtp_send_reply(const AmgAccount *account, const char *access_token,
                        const AmgReplyDraft *draft, AmgError *error)
{
    AmgTlsConnection *connection;
    AmgBuffer message;
    int result;
    if (!account || !draft) return AMG_ERR_ARGUMENT;
    connection = smtp_open(account, access_token, error);
    if (!connection) return error ? error->code : AMG_ERR_TLS;
    amg_buffer_init(&message);
    result = smtp_begin_data(connection, account->email, draft->to, NULL, NULL,
                             error);
    if (result == AMG_OK) result = amg_smtp_build_reply(draft, &message, error);
    if (result == AMG_OK) {
        amg_buffer_append_cstr(&message, ".\r\n");
        result = amg_tls_write_all(connection, message.data, message.length,
                                   error);
    }
    if (result == AMG_OK) result = smtp_response(connection, 2, error);
    if (result == AMG_OK)
        (void)smtp_command(connection, "QUIT\r\n", 2, error);
    amg_buffer_free(&message);
    amg_tls_close(connection);
    return result;
}

static int attachment_file_size(const char *path, unsigned long *size)
{
    FILE *file;
    long length;
    file = fopen(path, "rb");
    if (!file) return AMG_ERR_IO;
    if (fseek(file, 0L, SEEK_END) != 0 || (length = ftell(file)) < 0) {
        fclose(file);
        return AMG_ERR_IO;
    }
    fclose(file);
    *size = (unsigned long)length;
    return AMG_OK;
}

static int validate_attachments(const AmgMailDraft *draft, AmgError *error)
{
    size_t i;
    unsigned long total = 0;
    if (draft->body_utf8 && strlen(draft->body_utf8) > AMIMAIL_MAX_TEXT_PART) {
        amg_error_set(error, AMG_ERR_LIMIT,
                      T(MSG_MAIL_TEXT_IS_TOO_LARGE, "Mail text is too large."));
        return AMG_ERR_LIMIT;
    }
    for (i = 0; i < draft->attachment_count; ++i) {
        unsigned long size;
        if (!draft->attachments || !draft->attachments[i].path ||
            !draft->attachments[i].name_utf8 ||
            attachment_file_size(draft->attachments[i].path, &size) != AMG_OK) {
            amg_error_set(error, AMG_ERR_IO,
                          T(MSG_AN_ATTACHMENT_COULD_NOT_BE_READ, "An attachment could not be read."));
            return AMG_ERR_IO;
        }
        if (size > AMG_MAIL_MAX_ATTACHMENT_TOTAL - total) {
            amg_error_set(error, AMG_ERR_LIMIT,
                          T(MSG_ATTACHMENTS_MAY_TOTAL_NO_MORE_THAN_10_MB_UTF8, "Attachments may total no more than 20 MB."));
            return AMG_ERR_LIMIT;
        }
        total += size;
    }
    return AMG_OK;
}

static void safe_filename(const char *source, char *destination,
                          size_t capacity)
{
    size_t used = 0;
    if (!source) source = "attachment.bin";
    while (*source && used + 1U < capacity) {
        unsigned char c = (unsigned char)*source++;
        if (c < 32U || c == 127U || c == '"' || c == '\\') c = '_';
        destination[used++] = (char)c;
    }
    destination[used] = 0;
    if (!used && capacity > 1U) strcpy(destination, "attachment.bin");
}

static int append_mail_headers_common(const AmgMailDraft *draft,
                                      const char *boundary, int include_bcc,
                                      AmgBuffer *output)
{
    int result = append_header(output, "From", draft->from);
    if (result == AMG_OK) result = append_header(output, "To", draft->to);
    if (result == AMG_OK && draft->cc && *draft->cc)
        result = append_header(output, "Cc", draft->cc);
    if (result == AMG_OK && include_bcc && draft->bcc && *draft->bcc)
        result = append_header(output, "Bcc", draft->bcc);
    if (result == AMG_OK) result = append_header(output, "Subject", draft->subject);
    if (result == AMG_OK)
        result = append_header(output, "Date", draft->date_rfc2822);
    if (result == AMG_OK)
        result = append_header(output, "Message-ID", draft->message_id);
    if (result == AMG_OK && draft->in_reply_to && *draft->in_reply_to)
        result = append_header(output, "In-Reply-To", draft->in_reply_to);
    if (result == AMG_OK && draft->references && *draft->references)
        result = append_header(output, "References", draft->references);
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(output, "MIME-Version: 1.0\r\n");
    if (result == AMG_OK && boundary) {
        result = amg_buffer_append_cstr(output,
                                        "Content-Type: multipart/mixed; boundary=\"");
        if (result == AMG_OK) result = amg_buffer_append_cstr(output, boundary);
        if (result == AMG_OK)
            result = amg_buffer_append_cstr(output, "\"\r\n\r\n--");
        if (result == AMG_OK) result = amg_buffer_append_cstr(output, boundary);
        if (result == AMG_OK)
            result = amg_buffer_append_cstr(
                output,
                "\r\nContent-Type: text/plain; charset=UTF-8\r\n"
                "Content-Transfer-Encoding: quoted-printable\r\n\r\n");
    } else if (result == AMG_OK) {
        result = amg_buffer_append_cstr(
            output,
            "Content-Type: text/plain; charset=UTF-8\r\n"
            "Content-Transfer-Encoding: quoted-printable\r\n\r\n");
    }
    if (result == AMG_OK)
        result = append_quoted_printable_body(draft->body_utf8, output);
    if (result == AMG_OK &&
        (output->length < 2U || output->data[output->length - 2U] != '\r' ||
         output->data[output->length - 1U] != '\n'))
        result = amg_buffer_append_cstr(output, "\r\n");
    return result;
}

static int append_mail_headers(const AmgMailDraft *draft, const char *boundary,
                               AmgBuffer *output)
{
    return append_mail_headers_common(draft, boundary, 0, output);
}

/* Count MIME bytes, not SMTP dot transparency or the final DATA terminator.
 * The same raw-message ceiling applies to buffered drafts and live sending. */
static int smtp_take_mime_bytes(size_t *total, size_t length, AmgError *error)
{
    if (!total || *total > AMIMAIL_MAX_MIME_MESSAGE ||
        length > AMIMAIL_MAX_MIME_MESSAGE - *total) {
        amg_error_set(error, AMG_ERR_LIMIT,
                      T(MSG_MIME_MESSAGE_TOO_LARGE,
                        "Message exceeds the 32 MB MIME limit."));
        return AMG_ERR_LIMIT;
    }
    *total += length;
    return AMG_OK;
}

static int smtp_write_mime_bytes(AmgTlsConnection *connection,
                                  const void *data, size_t length,
                                  size_t *total, AmgError *error)
{
    int result = smtp_take_mime_bytes(total, length, error);
    if (result == AMG_OK)
        result = amg_tls_write_all(connection, data, length, error);
    return result;
}

static int smtp_write_text(AmgTlsConnection *connection, const AmgBuffer *text,
                           size_t *mime_total, AmgError *error)
{
    AmgBuffer stuffed;
    int result;
    amg_buffer_init(&stuffed);
    result = smtp_take_mime_bytes(mime_total, text->length, error);
    if (result == AMG_OK)
        result = amg_smtp_dot_stuff((const char *)text->data, text->length, &stuffed);
    if (result == AMG_OK)
        result = amg_tls_write_all(connection, stuffed.data, stuffed.length,
                                   error);
    amg_buffer_free(&stuffed);
    return result;
}

static int smtp_write_attachment(AmgTlsConnection *connection,
                                 const AmgAttachmentInput *attachment,
                                 const char *boundary,
                                 unsigned long *attachment_total_bytes,
                                 size_t *mime_total, AmgError *error)
{
    FILE *file;
    unsigned char block[57];
    char filename[256];
    AmgBuffer header, encoded;
    size_t count;
    int result = AMG_OK;
    safe_filename(attachment->name_utf8, filename, sizeof(filename));
    file = fopen(attachment->path, "rb");
    if (!file) {
        amg_error_set(error, AMG_ERR_IO,
                      T(MSG_AN_ATTACHMENT_COULD_NOT_BE_OPENED, "An attachment could not be opened."));
        return AMG_ERR_IO;
    }
    amg_buffer_init(&header);
    amg_buffer_init(&encoded);
    result = amg_buffer_append_cstr(&header, "--");
    if (result == AMG_OK) result = amg_buffer_append_cstr(&header, boundary);
    if (result == AMG_OK) result = amg_buffer_append_cstr(
        &header, "\r\nContent-Type: application/octet-stream; name=\"");
    if (result == AMG_OK) result = amg_buffer_append_cstr(&header, filename);
    if (result == AMG_OK) result = amg_buffer_append_cstr(
        &header, "\"\r\nContent-Transfer-Encoding: base64\r\n"
                 "Content-Disposition: attachment; filename=\"");
    if (result == AMG_OK) result = amg_buffer_append_cstr(&header, filename);
    if (result == AMG_OK) result = amg_buffer_append_cstr(&header, "\"\r\n\r\n");
    if (result == AMG_OK)
        result = smtp_write_mime_bytes(connection, header.data, header.length,
                                        mime_total, error);
    while (result == AMG_OK && (count = fread(block, 1U, sizeof(block), file)) > 0) {
        if (count > AMG_MAIL_MAX_ATTACHMENT_TOTAL - *attachment_total_bytes) {
            result = AMG_ERR_LIMIT;
            amg_error_set(error, result,
                          T(MSG_ATTACHMENTS_MAY_TOTAL_NO_MORE_THAN_10_MB_UTF8,
                            "Attachments may total no more than 20 MB."));
            break;
        }
        *attachment_total_bytes += (unsigned long)count;
        encoded.length = 0;
        result = amg_base64_encode(block, count, &encoded);
        if (result == AMG_OK) result = amg_buffer_append_cstr(&encoded, "\r\n");
        if (result == AMG_OK)
            result = smtp_write_mime_bytes(connection, encoded.data, encoded.length,
                                            mime_total, error);
    }
    if (result == AMG_OK && ferror(file)) result = AMG_ERR_IO;
    fclose(file);
    amg_buffer_free(&header);
    amg_buffer_free(&encoded);
    if (result != AMG_OK && error && error->code == AMG_OK)
        amg_error_set(error, result,
                      T(MSG_AN_ATTACHMENT_COULD_NOT_BE_SENT, "An attachment could not be sent."));
    return result;
}

static int append_attachment_to_buffer(const AmgAttachmentInput *attachment,
                                       const char *boundary,
                                       AmgBuffer *output,
                                       unsigned long *attachment_total_bytes,
                                       AmgError *error)
{
    FILE *file;
    unsigned char block[57];
    char filename[256];
    AmgBuffer encoded;
    size_t count;
    int result = AMG_OK;

    if (!attachment || !boundary || !output) return AMG_ERR_ARGUMENT;
    safe_filename(attachment->name_utf8, filename, sizeof(filename));
    file = fopen(attachment->path, "rb");
    if (!file) {
        amg_error_set(error, AMG_ERR_IO,
                      T(MSG_AN_ATTACHMENT_COULD_NOT_BE_OPENED, "An attachment could not be opened."));
        return AMG_ERR_IO;
    }

    result = amg_buffer_append_cstr(output, "--");
    if (result == AMG_OK) result = amg_buffer_append_cstr(output, boundary);
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(
            output, "\r\nContent-Type: application/octet-stream; name=\"");
    if (result == AMG_OK) result = amg_buffer_append_cstr(output, filename);
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(
            output,
            "\"\r\nContent-Transfer-Encoding: base64\r\n"
            "Content-Disposition: attachment; filename=\"");
    if (result == AMG_OK) result = amg_buffer_append_cstr(output, filename);
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(output, "\"\r\n\r\n");

    amg_buffer_init(&encoded);
    while (result == AMG_OK &&
           (count = fread(block, 1U, sizeof(block), file)) > 0U) {
        if (count > AMG_MAIL_MAX_ATTACHMENT_TOTAL - *attachment_total_bytes) {
            result = AMG_ERR_LIMIT;
            amg_error_set(error, result,
                          T(MSG_ATTACHMENTS_MAY_TOTAL_NO_MORE_THAN_10_MB_UTF8,
                            "Attachments may total no more than 20 MB."));
            break;
        }
        *attachment_total_bytes += (unsigned long)count;
        encoded.length = 0;
        result = amg_base64_encode(block, count, &encoded);
        if (result == AMG_OK)
            result = amg_buffer_append_cstr(&encoded, "\r\n");
        if (result == AMG_OK)
            result = amg_buffer_append(output, encoded.data, encoded.length);
    }
    if (result == AMG_OK && ferror(file)) result = AMG_ERR_IO;
    fclose(file);
    amg_buffer_free(&encoded);

    if (result != AMG_OK && error && error->code == AMG_OK)
        amg_error_set(error, result,
                      T(MSG_AN_ATTACHMENT_COULD_NOT_BE_WRITTEN_TO_THE, "An attachment could not be written to the draft."));
    return result;
}

int amg_smtp_build_mail(const AmgMailDraft *draft, int include_bcc,
                        AmgBuffer *output, AmgError *error)
{
    char boundary[96];
    const char *used_boundary = NULL;
    size_t i;
    unsigned long attachment_total_bytes = 0UL;
    int result;

    if (!draft || !output || !header_safe(draft->from) ||
        !header_safe(draft->to ? draft->to : "") ||
        !header_safe(draft->cc ? draft->cc : "") ||
        !header_safe(draft->bcc ? draft->bcc : "") ||
        !header_safe(draft->subject ? draft->subject : "") ||
        !header_safe(draft->date_rfc2822) ||
        !header_safe(draft->message_id) ||
        !header_safe(draft->in_reply_to ? draft->in_reply_to : "") ||
        !header_safe(draft->references ? draft->references : "")) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T(MSG_INVALID_MAIL_ADDRESS_OR_HEADER_LINE, "Invalid mail address or header line."));
        return AMG_ERR_ARGUMENT;
    }

    if (!output->limit || output->limit > AMIMAIL_MAX_MIME_MESSAGE) {
        result = amg_buffer_set_limit(output, AMIMAIL_MAX_MIME_MESSAGE);
        if (result != AMG_OK) return result;
    }
    output->limit_hit = 0;
    result = validate_attachments(draft, error);
    if (result != AMG_OK) return result;
    if (draft->attachment_count) {
        make_mime_boundary(draft, boundary);
        used_boundary = boundary;
    }

    result = append_mail_headers_common(draft, used_boundary,
                                        include_bcc ? 1 : 0, output);
    for (i = 0; result == AMG_OK && i < draft->attachment_count; ++i)
        result = append_attachment_to_buffer(&draft->attachments[i],
                                             used_boundary, output,
                                             &attachment_total_bytes, error);
    if (result == AMG_OK && used_boundary) {
        result = amg_buffer_append_cstr(output, "--");
        if (result == AMG_OK)
            result = amg_buffer_append_cstr(output, used_boundary);
        if (result == AMG_OK)
            result = amg_buffer_append_cstr(output, "--\r\n");
    }
    if (output->limit_hit) {
        result = AMG_ERR_LIMIT;
        amg_error_set(error, result,
                      T(MSG_MIME_MESSAGE_TOO_LARGE,
                        "Message exceeds the 32 MB MIME limit."));
    }
    if (result != AMG_OK && error && error->code == AMG_OK)
        amg_error_set(error, result,
                      T(MSG_MAIL_DRAFT_COULD_NOT_BE_CREATED, "Mail draft could not be created."));
    return result;
}

int amg_smtp_send_mail(const AmgAccount *account, const char *access_token,
                       const AmgMailDraft *draft, AmgError *error)
{
    AmgTlsConnection *connection;
    AmgBuffer text, ending;
    char boundary[96];
    const char *used_boundary = NULL;
    size_t i, mime_total = 0U;
    unsigned long attachment_total_bytes = 0UL;
    int result;
    if (!account || !draft || !header_safe(draft->from) ||
        !header_safe(draft->to) || !header_safe(draft->cc ? draft->cc : "") ||
        !header_safe(draft->bcc ? draft->bcc : "") ||
        !header_safe(draft->subject) || !header_safe(draft->date_rfc2822) ||
        !header_safe(draft->message_id) ||
        !header_safe(draft->in_reply_to ? draft->in_reply_to : "") ||
        !header_safe(draft->references ? draft->references : "")) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T(MSG_INVALID_MAIL_ADDRESS_OR_HEADER_LINE, "Invalid mail address or header line."));
        return AMG_ERR_ARGUMENT;
    }
    result = validate_attachments(draft, error);
    if (result != AMG_OK) return result;
    if (draft->attachment_count) {
        make_mime_boundary(draft, boundary);
        used_boundary = boundary;
    }
    connection = smtp_open(account, access_token, error);
    if (!connection) return error ? error->code : AMG_ERR_TLS;
    result = smtp_begin_data(connection, account->email, draft->to,
                             draft->cc, draft->bcc, error);
    amg_buffer_init(&text);
    (void)amg_buffer_set_limit(&text, AMIMAIL_MAX_MIME_MESSAGE);
    amg_buffer_init(&ending);
    if (result == AMG_OK)
        result = append_mail_headers(draft, used_boundary, &text);
    if (result == AMG_OK) result = smtp_write_text(connection, &text, &mime_total, error);
    for (i = 0; result == AMG_OK && i < draft->attachment_count; ++i)
        result = smtp_write_attachment(connection, &draft->attachments[i],
                                       used_boundary, &attachment_total_bytes,
                                       &mime_total, error);
    if (result == AMG_OK && used_boundary) {
        result = amg_buffer_append_cstr(&ending, "--");
        if (result == AMG_OK) result = amg_buffer_append_cstr(&ending, used_boundary);
        if (result == AMG_OK) result = amg_buffer_append_cstr(&ending, "--\r\n");
        if (result == AMG_OK)
            result = smtp_write_mime_bytes(connection, ending.data, ending.length,
                                            &mime_total, error);
    }
    if (result == AMG_OK)
        result = amg_tls_write_all(connection, ".\r\n", 3U, error);
    if (result == AMG_OK) result = smtp_response(connection, 2, error);
    if (result == AMG_OK)
        (void)smtp_command(connection, "QUIT\r\n", 2, error);
    amg_buffer_free(&text);
    amg_buffer_free(&ending);
    amg_tls_close(connection);
    return result;
}

static int file_mime_write(FILE *file, const void *bytes, size_t length,
                            size_t *total, AmgError *error)
{
    int result = smtp_take_mime_bytes(total, length, error);
    if (result != AMG_OK) return result;
    if (length && fwrite(bytes, 1U, length, file) != length) {
        amg_error_set(error, AMG_ERR_IO,
            T(MSG_AN_ATTACHMENT_COULD_NOT_BE_WRITTEN_TO_THE,
              "An attachment could not be written to the draft."));
        return AMG_ERR_IO;
    }
    return AMG_OK;
}

static int file_reply_headers(const AmgMailDraft *draft, AmgBuffer *output)
{
    AmgBuffer mailbox;
    char number[48];
    int result = AMG_OK;
    if (!draft->reply_source_uid || !draft->reply_source_uid_validity ||
        !draft->reply_source_mailbox || !*draft->reply_source_mailbox) return AMG_OK;
    amg_buffer_init(&mailbox);
    result = amg_modified_utf7_encode(draft->reply_source_mailbox, &mailbox);
    if (result == AMG_OK) result = amg_buffer_terminate(&mailbox);
    snprintf(number, sizeof(number), "%lu", draft->reply_source_uid);
    if (result == AMG_OK) result = append_header(output, AMG_MAIL_REPLY_UID_HEADER, number);
    snprintf(number, sizeof(number), "%lu", draft->reply_source_uid_validity);
    if (result == AMG_OK) result = append_header(output, AMG_MAIL_REPLY_UIDVALIDITY_HEADER, number);
    if (result == AMG_OK) result = append_header(output, AMG_MAIL_REPLY_MAILBOX_HEADER,
                                                (const char *)mailbox.data);
    amg_buffer_free(&mailbox);
    return result;
}

int amg_smtp_build_mail_file(const AmgMailDraft *draft, int include_bcc,
                             int include_reply_context, FILE *file,
                             size_t *length, AmgTransfer *transfer,
                             AmgError *error)
{
    AmgBuffer buffer, encoded;
    unsigned char input[57U * 128U];
    char boundary[96], filename[256];
    const char *used_boundary = NULL;
    size_t i, total = 0U;
    unsigned long attached = 0UL, expected = 0UL;
    int result;
    if (length) *length = 0U;
    if (!draft || !file || !length || !header_safe(draft->from) ||
        !header_safe(draft->to ? draft->to : "") ||
        !header_safe(draft->cc ? draft->cc : "") ||
        !header_safe(draft->bcc ? draft->bcc : "") ||
        !header_safe(draft->subject ? draft->subject : "") ||
        !header_safe(draft->date_rfc2822) || !header_safe(draft->message_id) ||
        !header_safe(draft->in_reply_to ? draft->in_reply_to : "") ||
        !header_safe(draft->references ? draft->references : "")) return AMG_ERR_ARGUMENT;
    result = validate_attachments(draft, error);
    if (result != AMG_OK) return result;
    for (i = 0U; i < draft->attachment_count; ++i) {
        unsigned long size = 0UL;
        if (attachment_file_size(draft->attachments[i].path, &size) != AMG_OK ||
            size > AMG_MAIL_MAX_ATTACHMENT_TOTAL - expected) return AMG_ERR_LIMIT;
        expected += size;
    }
    if (draft->attachment_count) { make_mime_boundary(draft, boundary); used_boundary = boundary; }
    amg_buffer_init(&buffer); amg_buffer_init(&encoded);
    (void)amg_buffer_set_limit(&buffer, AMIMAIL_MAX_MIME_MESSAGE);
    (void)amg_buffer_set_limit(&encoded, 16384U);
    result = amg_transfer_report(transfer, AMG_TRANSFER_PREPARE, 0U, expected, error);
    if (result == AMG_OK && include_reply_context) result = file_reply_headers(draft, &buffer);
    if (result == AMG_OK)
        result = append_mail_headers_common(draft, used_boundary, include_bcc, &buffer);
    if (result == AMG_OK) result = file_mime_write(file, buffer.data, buffer.length, &total, error);
    for (i = 0U; result == AMG_OK && i < draft->attachment_count; ++i) {
        FILE *attachment;
        size_t got;
        safe_filename(draft->attachments[i].name_utf8, filename, sizeof(filename));
        attachment = fopen(draft->attachments[i].path, "rb");
        if (!attachment) { result = AMG_ERR_IO; break; }
        buffer.length = 0U;
        result = amg_buffer_append_cstr(&buffer, "--");
        if (result == AMG_OK) result = amg_buffer_append_cstr(&buffer, boundary);
        if (result == AMG_OK) result = amg_buffer_append_cstr(&buffer,
            "\r\nContent-Type: application/octet-stream; name=\"");
        if (result == AMG_OK) result = amg_buffer_append_cstr(&buffer, filename);
        if (result == AMG_OK) result = amg_buffer_append_cstr(&buffer,
            "\"\r\nContent-Transfer-Encoding: base64\r\nContent-Disposition: attachment; filename=\"");
        if (result == AMG_OK) result = amg_buffer_append_cstr(&buffer, filename);
        if (result == AMG_OK) result = amg_buffer_append_cstr(&buffer, "\"\r\n\r\n");
        if (result == AMG_OK) result = file_mime_write(file, buffer.data, buffer.length, &total, error);
        while (result == AMG_OK && (got = fread(input, 1U, sizeof(input), attachment)) > 0U) {
            size_t pos = 0U;
            if (got > AMG_MAIL_MAX_ATTACHMENT_TOTAL - attached) { result = AMG_ERR_LIMIT; break; }
            attached += (unsigned long)got;
            encoded.length = 0U;
            while (pos < got && result == AMG_OK) {
                size_t chunk = got - pos;
                if (chunk > 57U) chunk = 57U;
                result = amg_base64_encode(input + pos, chunk, &encoded);
                if (result == AMG_OK) result = amg_buffer_append_cstr(&encoded, "\r\n");
                pos += chunk;
            }
            if (result == AMG_OK) result = amg_transfer_report(transfer,
                AMG_TRANSFER_PREPARE, attached, expected, error);
            if (result == AMG_OK) result = file_mime_write(file, encoded.data, encoded.length, &total, error);
        }
        if (result == AMG_OK && ferror(attachment)) result = AMG_ERR_IO;
        fclose(attachment);
    }
    if (result == AMG_OK && used_boundary) {
        buffer.length = 0U;
        result = amg_buffer_append_cstr(&buffer, "--");
        if (result == AMG_OK) result = amg_buffer_append_cstr(&buffer, boundary);
        if (result == AMG_OK) result = amg_buffer_append_cstr(&buffer, "--\r\n");
        if (result == AMG_OK) result = file_mime_write(file, buffer.data, buffer.length, &total, error);
    }
    if (result == AMG_OK && fflush(file)) result = AMG_ERR_IO;
    if (result == AMG_OK) *length = total;
    else if (!error || error->code == AMG_OK)
        amg_error_set(error, result,
            T(MSG_MAIL_DRAFT_COULD_NOT_BE_CREATED, "Mail draft could not be created."));
    amg_buffer_free(&buffer); amg_buffer_free(&encoded);
    return result;
}

/* Private spool files include Bcc for Drafts/Sent/recovery. This reader removes
 * Bcc plus continuations from SMTP DATA and preserves dot-stuffing across
 * block boundaries. Envelope recipients were supplied separately. */
static int smtp_file_data(AmgTlsConnection *connection, FILE *file, size_t length,
                           AmgTransfer *transfer, AmgError *error)
{
    unsigned char block[8192], stuffed[16384];
    char line[1024];
    size_t read_bytes = 0U;
    int result = AMG_OK, skip = 0, in_headers = 1, line_start = 1;
    unsigned char last = 0U, previous = 0U;
    if (length > AMIMAIL_MAX_MIME_MESSAGE || fseek(file, 0L, SEEK_SET)) return AMG_ERR_IO;
    while (in_headers && result == AMG_OK) {
        size_t n;
        result = amg_transfer_report(transfer, AMG_TRANSFER_UPLOAD, read_bytes, length, error);
        if (result != AMG_OK) break;
        if (!fgets(line, sizeof(line), file)) { result = AMG_ERR_IO; break; }
        n = strlen(line);
        if (!n || line[n - 1U] != '\n' || n > length - read_bytes) { result = AMG_ERR_PARSE; break; }
        read_bytes += n;
        if (line[0] != ' ' && line[0] != '\t')
            skip = n >= 4U && tolower((unsigned char)line[0]) == 'b' &&
                tolower((unsigned char)line[1]) == 'c' &&
                tolower((unsigned char)line[2]) == 'c' && line[3] == ':';
        if ((n == 2U && line[0] == '\r') || n == 1U) { skip = 0; in_headers = 0; }
        if (!skip) result = amg_tls_write_all(connection, line, n, error);
    }
    while (result == AMG_OK && read_bytes < length) {
        size_t want = length - read_bytes, got, i, used = 0U;
        result = amg_transfer_report(transfer, AMG_TRANSFER_UPLOAD, read_bytes, length, error);
        if (result != AMG_OK) break;
        if (want > sizeof(block)) want = sizeof(block);
        got = fread(block, 1U, want, file);
        if (!got) { result = AMG_ERR_IO; break; }
        read_bytes += got;
        for (i = 0U; i < got; ++i) {
            unsigned char c = block[i];
            if (line_start && c == '.') stuffed[used++] = '.';
            stuffed[used++] = c;
            line_start = c == '\n'; previous = last; last = c;
        }
        result = amg_tls_write_all(connection, stuffed, used, error);
    }
    if (result == AMG_OK && (previous != '\r' || last != '\n'))
        result = amg_tls_write_all(connection, "\r\n", 2U, error);
    return result;
}

int amg_smtp_send_mail_file(const AmgAccount *account, const char *access_token,
                            const AmgMailDraft *envelope, FILE *file,
                            size_t length, AmgTransfer *transfer,
                            AmgError *error)
{
    AmgTlsConnection *connection;
    int result;
    if (!account || !envelope || !file || !header_safe(envelope->to ? envelope->to : "") ||
        !header_safe(envelope->cc ? envelope->cc : "") ||
        !header_safe(envelope->bcc ? envelope->bcc : "")) return AMG_ERR_ARGUMENT;
    result = amg_transfer_report(transfer, AMG_TRANSFER_UPLOAD, 0U, length, error);
    if (result != AMG_OK) return result;
    connection = smtp_open(account, access_token, error);
    if (!connection) return error ? error->code : AMG_ERR_TLS;
    result = smtp_begin_data(connection, account->email, envelope->to,
                             envelope->cc, envelope->bcc, error);
    if (result == AMG_OK) result = smtp_file_data(connection, file, length, transfer, error);
    if (result == AMG_OK)
        result = amg_transfer_report(transfer, AMG_TRANSFER_COMMIT, length, length, error);
    if (result == AMG_OK) {
        int reply_code = 0;
        result = amg_tls_write_all(connection, ".\r\n", 3U, error);
        if (result == AMG_OK)
            result = smtp_response_capture_code(connection, 0, NULL,
                                                 &reply_code, error);
        if (result == AMG_OK && reply_code / 100 != 2) {
            result = AMG_ERR_PROTOCOL;
            amg_error_set(error, result,
                T(MSG_INVALID_SMTP_RESPONSE, "Invalid SMTP response."));
        }
        /* Only an explicit final 4xx/5xx proves rejection. A disconnect,
         * malformed reply, allocation failure or lost write status after
         * DATA's terminator leaves the delivery outcome unknown. */
        if (result != AMG_OK && !(result == AMG_ERR_PROTOCOL &&
            (reply_code / 100 == 4 || reply_code / 100 == 5))) {
            result = AMG_ERR_UNCERTAIN;
            amg_error_set(error, result, T(MSG_DELIVERY_UNCERTAIN,
                "Server confirmation missing. Check delivery before retrying."));
        }
    }
    if (result == AMG_OK) {
        AmgError ignored;
        memset(&ignored, 0, sizeof(ignored));
        (void)smtp_command(connection, "QUIT\r\n", 2, &ignored);
        amg_error_set(error, AMG_OK, "");
    }
    amg_tls_close(connection);
    return result;
}
