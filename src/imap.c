#include "imap.h"
#include "codec.h"
#include "imap_parser.h"
#include "i18n.h"

#define T(id, en) amg_tr((id), (en))

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if AMIGMAIL_AMIGA
#include <dos/dos.h>
#include <proto/dos.h>
#else
#include <time.h>
#endif

static const char imap_message_list_fetch_items[] =
    "(UID FLAGS RFC822.SIZE INTERNALDATE "
    "BODY.PEEK[HEADER.FIELDS (FROM TO SUBJECT DATE MESSAGE-ID REFERENCES "
    "IN-REPLY-TO CONTENT-TYPE)])";

static int ascii_ci_contains(const char *text, const char *needle)
{
    size_t length = strlen(needle);
    for (; *text; ++text) {
        size_t i;
        for (i = 0; i < length && text[i] && tolower((unsigned char)text[i]) == tolower((unsigned char)needle[i]); ++i) {}
        if (i == length) return 1;
    }
    return 0;
}

static int ascii_ci_equal(const char *left, const char *right)
{
    while (*left && *right) {
        if (tolower((unsigned char)*left) !=
            tolower((unsigned char)*right))
            return 0;
        ++left;
        ++right;
    }
    return !*left && !*right;
}

static void set_parser_error(AmgError *error, int result,
                             const AmgImapParser *parser)
{
    char message[256];
    if (result != AMG_ERR_LIMIT || !parser) {
        amg_error_set(error, result,
                      T(MSG_IMAP_RESPONSE_HAS_AN_INVALID_FORMAT, "IMAP response has an invalid format."));
        return;
    }
    switch (parser->failure) {
        case AMG_IMAP_PARSER_FAILURE_LINE_LIMIT:
            amg_tr_snprintf(message, sizeof(message), MSG_IMAP_LIMIT_LINE_VALUE_BYTES_ALLOWED_VALUE_BYTES, "IMAP limit: line %lu bytes, allowed %lu bytes.", (unsigned long)parser->failure_size, (unsigned long)parser->failure_limit);
            break;
        case AMG_IMAP_PARSER_FAILURE_LITERAL_LIMIT:
            if (parser->failure_size == SIZE_MAX)
                snprintf(message, sizeof(message), "%s",
                         T(MSG_IMAP_LIMIT_INVALID_LITERAL_SIZE, "IMAP limit: invalid literal size."));
            else
                amg_tr_snprintf(message, sizeof(message), MSG_IMAP_LIMIT_LITERAL_VALUE_BYTES_ALLOWED_VALUE_BYTES, "IMAP limit: literal %lu bytes, allowed %lu bytes.", (unsigned long)parser->failure_size, (unsigned long)parser->failure_limit);
            break;
        case AMG_IMAP_PARSER_FAILURE_BUFFER_LIMIT:
            amg_tr_snprintf(message, sizeof(message), MSG_IMAP_LIMIT_BUFFER_VALUE_BYTES_ALLOWED_VALUE_BYTES, "IMAP limit: buffer %lu bytes, allowed %lu bytes.", (unsigned long)parser->failure_size, (unsigned long)parser->failure_limit);
            break;
        default:
            snprintf(message, sizeof(message), "%s",
                     T(MSG_IMAP_LIMIT_WAS_EXCEEDED, "IMAP limit was exceeded."));
            break;
    }
    amg_error_set(error, result, message);
}

void amg_imap_session_init(AmgImapSession *session)
{
    if (!session) return;
    memset(session, 0, sizeof(*session));
    session->tag_counter = 1;
    strcpy(session->special_mailboxes[0], "INBOX");
    strcpy(session->configured_special_mailboxes[0], "INBOX");
}

void amg_imap_disconnect(AmgImapSession *session)
{
    if (!session) return;
    if (session->connection) {
        AmgError ignored;
        amg_tls_write_all(session->connection, "ZZZZ LOGOUT\r\n", 13U, &ignored);
        amg_tls_close(session->connection);
    }
    amg_imap_session_init(session);
}

static int imap_collect(AmgImapSession *session, const char *tag, AmgBuffer *response, AmgError *error)
{
    AmgImapParser parser;
    AmgImapEvent event;
    unsigned char input[4096];
    char rejection[192];
    int done = 0, ok = 0, result = AMG_OK;
    rejection[0] = 0;
    amg_imap_parser_init(&parser);
    while (!done) {
        long count = amg_tls_read(session->connection, input, sizeof(input), error);
        if (count <= 0) { result = AMG_ERR_IO; break; }
        result = amg_imap_parser_feed(&parser, input, (size_t)count);
        if (result != AMG_OK) {
            set_parser_error(error, result, &parser);
            break;
        }
        while ((result = amg_imap_parser_next(&parser, &event)) > 0) {
            if (response && amg_buffer_append(response, event.data, event.length) != AMG_OK) {
                result = AMG_ERR_MEMORY;
                amg_error_set(error, result,
                              T(MSG_NOT_ENOUGH_MEMORY_FOR_THE_IMAP_RESPONSE, "Not enough memory for the IMAP response."));
                done = 1;
                break;
            }
            if (event.type == AMG_IMAP_EVENT_LINE && event.length >= strlen(tag) + 4U &&
                !memcmp(event.data, tag, strlen(tag)) && event.data[strlen(tag)] == ' ') {
                const unsigned char *status = event.data + strlen(tag) + 1U;
                ok = event.length >= strlen(tag) + 4U && !memcmp(status, "OK", 2U);
                if (!ok) {
                    const unsigned char *detail = status + 2U;
                    size_t detail_length = event.length -
                        (size_t)(detail - event.data);
                    while (detail_length && (*detail == ' ' || *detail == '\t')) {
                        ++detail;
                        --detail_length;
                    }
                    while (detail_length &&
                           (detail[detail_length - 1U] == '\r' ||
                            detail[detail_length - 1U] == '\n'))
                        --detail_length;
                    if (detail_length >= sizeof(rejection))
                        detail_length = sizeof(rejection) - 1U;
                    if (detail_length)
                        memcpy(rejection, detail, detail_length);
                    rejection[detail_length] = 0;
                }
                done = 1; break;
            }
        }
        if (result < 0) {
            if (parser.failed)
                set_parser_error(error, result, &parser);
            else if (!error || error->code == AMG_OK)
                set_parser_error(error, result, NULL);
            break;
        }
        result = AMG_OK;
    }
    amg_imap_parser_free(&parser);
    if (result != AMG_OK) return result;
    if (!ok) {
        if (rejection[0]) {
            char message[256];
            amg_tr_snprintf(message, sizeof(message), MSG_IMAP_SERVER_VALUE, "IMAP server: %s", rejection);
            amg_error_set(error, AMG_ERR_PROTOCOL, message);
        } else {
            amg_error_set(error, AMG_ERR_PROTOCOL,
                          T(MSG_THE_IMAP_SERVER_REJECTED_THE_COMMAND, "The IMAP server rejected the command."));
        }
        return AMG_ERR_PROTOCOL;
    }
    return AMG_OK;
}

static int imap_command(AmgImapSession *session, const char *command, AmgBuffer *response, AmgError *error)
{
    char tag[16]; AmgBuffer wire; int result;
    if (!session || !session->connection || !command) return AMG_ERR_ARGUMENT;
    snprintf(tag, sizeof(tag), "A%06lu", session->tag_counter++);
    amg_buffer_init(&wire); amg_buffer_append_cstr(&wire, tag); amg_buffer_append_char(&wire, ' ');
    amg_buffer_append_cstr(&wire, command); amg_buffer_append_cstr(&wire, "\r\n");
    result = amg_tls_write_all(session->connection, wire.data, wire.length, error); amg_buffer_free(&wire);
    return result == AMG_OK ? imap_collect(session, tag, response, error) : result;
}

static int read_greeting(AmgImapSession *session, int *preauthenticated, AmgError *error)
{
    AmgBuffer greeting;
    unsigned char input[1024];
    int result = AMG_OK;
    if (preauthenticated) *preauthenticated = 0;
    amg_buffer_init(&greeting);
    for (;;) {
        long count = amg_tls_read(session->connection, input, sizeof(input), error);
        if (count <= 0) {
            result = count < 0 ? (int)count : AMG_ERR_IO;
            if (!error || error->code == AMG_OK)
                amg_error_set(
                    error, result,
                    T(MSG_THE_IMAP_SERVER_SENT_NO_DATA_AFTER_THE, "The IMAP server sent no data after the connection was established."));
            break;
        }
        result = amg_buffer_append(&greeting, input, (size_t)count);
        if (result != AMG_OK) {
            amg_error_set(error, result,
                          T(MSG_IMAP_GREETING_IS_TOO_LONG, "IMAP greeting is too long."));
            break;
        }
        if (amg_imap_greeting_status(greeting.data, greeting.length) > 0) {
            if (preauthenticated)
                *preauthenticated = amg_imap_greeting_is_preauth(
                    greeting.data, greeting.length);
            result = AMG_OK;
            break;
        }
        if (amg_imap_greeting_status(greeting.data, greeting.length) < 0) {
            size_t length = greeting.length;
            char detail[190], message[256];
            while (length && (greeting.data[length - 1U] == '\r' ||
                              greeting.data[length - 1U] == '\n'))
                --length;
            if (length >= sizeof(detail)) length = sizeof(detail) - 1U;
            memcpy(detail, greeting.data, length);
            detail[length] = 0;
            amg_tr_snprintf(message, sizeof(message), MSG_IMAP_SERVER_REPORTS_VALUE, "IMAP server reports: %s", detail);
            amg_error_set(error, AMG_ERR_PROTOCOL, message);
            result = AMG_ERR_PROTOCOL;
            break;
        }
        if (greeting.length > AMIGMAIL_MAX_LINE) {
            amg_error_set(error, AMG_ERR_PROTOCOL,
                          T(MSG_THE_IMAP_SERVER_DID_NOT_SEND_A_VALID, "The IMAP server did not send a valid greeting."));
            result = AMG_ERR_PROTOCOL;
            break;
        }
    }
    amg_buffer_free(&greeting);
    return result;
}

static int append_imap_quoted(const char *value, AmgBuffer *output)
{
    const unsigned char *p = (const unsigned char *)(value ? value : "");
    int result = amg_buffer_append_char(output, '"');
    if (result != AMG_OK) return result;
    while (*p) {
        if (*p == '\r' || *p == '\n') return AMG_ERR_ARGUMENT;
        if (*p == '"' || *p == '\\') {
            result = amg_buffer_append_char(output, '\\');
            if (result != AMG_OK) return result;
        }
        result = amg_buffer_append_char(output, *p++);
        if (result != AMG_OK) return result;
    }
    return amg_buffer_append_char(output, '"');
}

static void update_capabilities(AmgImapSession *session,
                                const AmgBuffer *response)
{
    const char *text;
    if (!session || !response) return;
    text = response->data ? (const char *)response->data : "";
    session->capability_move = ascii_ci_contains(text, " MOVE");
    session->capability_uidplus = ascii_ci_contains(text, " UIDPLUS");
    session->capability_special_use = ascii_ci_contains(text, " SPECIAL-USE");
    session->capability_starttls = ascii_ci_contains(text, " STARTTLS");
    session->capability_auth_plain = ascii_ci_contains(text, " AUTH=PLAIN");
    session->capability_sasl_ir = ascii_ci_contains(text, " SASL-IR");
    session->capability_login_disabled = ascii_ci_contains(text, " LOGINDISABLED");
}

static int build_plain_response(const AmgAccount *account,
                                AmgBuffer *encoded, AmgError *error)
{
    AmgBuffer raw;
    int result;
    const char *user;
    if (!account || !encoded) return AMG_ERR_ARGUMENT;
    user = amg_account_imap_user(account);
    if (!account->imap_password || !account->imap_password[0]) {
        amg_error_set(error, AMG_ERR_AUTH,
                      T(MSG_IMAP_PASSWORD_IS_MISSING, "IMAP password is missing."));
        return AMG_ERR_AUTH;
    }
    amg_buffer_init(&raw);
    result = amg_buffer_append_char(&raw, 0);
    if (result == AMG_OK) result = amg_buffer_append_cstr(&raw, user);
    if (result == AMG_OK) result = amg_buffer_append_char(&raw, 0);
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(&raw, account->imap_password);
    if (result == AMG_OK)
        result = amg_base64_encode(raw.data, raw.length, encoded);
    amg_secure_clear(raw.data, raw.capacity);
    amg_buffer_free(&raw);
    if (result != AMG_OK)
        amg_error_set(error, result,
                      T(MSG_IMAP_LOGIN_COULD_NOT_BE_PREPARED, "IMAP login could not be prepared."));
    return result;
}

static int build_xoauth2_response(const AmgAccount *account,
                                  const char *access_token,
                                  AmgBuffer *encoded, AmgError *error)
{
    AmgBuffer raw;
    int result;
    if (!account || !encoded) return AMG_ERR_ARGUMENT;
    if (!access_token || !*access_token) {
        amg_error_set(error, AMG_ERR_AUTH,
                      T(MSG_OAUTH_ACCESS_TOKEN_IS_MISSING, "OAuth access token is missing."));
        return AMG_ERR_AUTH;
    }
    amg_buffer_init(&raw);
    result = amg_buffer_append_cstr(&raw, "user=");
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(&raw, account->email);
    if (result == AMG_OK) result = amg_buffer_append_char(&raw, 1);
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(&raw, "auth=Bearer ");
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(&raw, access_token);
    if (result == AMG_OK) result = amg_buffer_append_char(&raw, 1);
    if (result == AMG_OK) result = amg_buffer_append_char(&raw, 1);
    if (result == AMG_OK)
        result = amg_base64_encode(raw.data, raw.length, encoded);
    amg_secure_clear(raw.data, raw.capacity);
    amg_buffer_free(&raw);
    if (result != AMG_OK)
        amg_error_set(error, result,
                      T(MSG_OAUTH_LOGIN_COULD_NOT_BE_PREPARED, "OAuth login could not be prepared."));
    return result;
}

static int imap_authenticate_sasl(AmgImapSession *session,
                                  const char *mechanism,
                                  const AmgBuffer *response,
                                  int send_initial_response,
                                  AmgError *error)
{
    AmgImapParser parser;
    AmgImapEvent event;
    AmgBuffer wire;
    unsigned char input[2048];
    char tag[16];
    char rejection[192];
    int result = AMG_OK;
    int done = 0;
    int ok = 0;
    int credential_sent = send_initial_response ? 1 : 0;
    unsigned continuation_after_credential = 0U;

    if (!session || !session->connection || !mechanism || !response)
        return AMG_ERR_ARGUMENT;
    rejection[0] = 0;
    snprintf(tag, sizeof(tag), "A%06lu", session->tag_counter++);
    amg_buffer_init(&wire);
    result = amg_buffer_append_cstr(&wire, tag);
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(&wire, " AUTHENTICATE ");
    if (result == AMG_OK) result = amg_buffer_append_cstr(&wire, mechanism);
    if (result == AMG_OK && send_initial_response) {
        result = amg_buffer_append_char(&wire, ' ');
        if (result == AMG_OK)
            result = amg_buffer_append(&wire, response->data,
                                       response->length);
    }
    if (result == AMG_OK) result = amg_buffer_append_cstr(&wire, "\r\n");
    if (result == AMG_OK)
        result = amg_tls_write_all(session->connection, wire.data,
                                   wire.length, error);
    amg_secure_clear(wire.data, wire.capacity);
    amg_buffer_free(&wire);
    if (result != AMG_OK) return result;

    amg_imap_parser_init(&parser);
    while (!done) {
        long count = amg_tls_read(session->connection, input, sizeof(input),
                                  error);
        if (count <= 0) {
            result = count < 0 ? (int)count : AMG_ERR_IO;
            break;
        }
        result = amg_imap_parser_feed(&parser, input, (size_t)count);
        if (result != AMG_OK) {
            set_parser_error(error, result, &parser);
            break;
        }
        while ((result = amg_imap_parser_next(&parser, &event)) > 0) {
            if (event.type != AMG_IMAP_EVENT_LINE) continue;
            if (event.length && event.data[0] == '+') {
                AmgBuffer answer;
                amg_buffer_init(&answer);
                if (!credential_sent) {
                    result = amg_buffer_append(&answer, response->data,
                                               response->length);
                    credential_sent = 1;
                } else if (continuation_after_credential++ == 0U) {
                    /* Some SASL servers (notably XOAUTH2 error paths) send
                     * one final challenge after an initial response and wait
                     * for an empty response before returning the tagged NO. */
                    result = AMG_OK;
                } else {
                    result = amg_buffer_append_char(&answer, '*');
                }
                if (result == AMG_OK)
                    result = amg_buffer_append_cstr(&answer, "\r\n");
                if (result == AMG_OK)
                    result = amg_tls_write_all(session->connection,
                                               answer.data, answer.length,
                                               error);
                amg_secure_clear(answer.data, answer.capacity);
                amg_buffer_free(&answer);
                if (result != AMG_OK) {
                    done = 1;
                    break;
                }
                continue;
            }
            if (event.length >= strlen(tag) + 4U &&
                !memcmp(event.data, tag, strlen(tag)) &&
                event.data[strlen(tag)] == ' ') {
                const unsigned char *status =
                    event.data + strlen(tag) + 1U;
                ok = event.length >= strlen(tag) + 4U &&
                     tolower((unsigned char)status[0]) == 'o' &&
                     tolower((unsigned char)status[1]) == 'k';
                if (!ok) {
                    const unsigned char *detail = status + 2U;
                    size_t detail_length = event.length -
                        (size_t)(detail - event.data);
                    while (detail_length &&
                           (*detail == ' ' || *detail == '\t')) {
                        ++detail;
                        --detail_length;
                    }
                    while (detail_length &&
                           (detail[detail_length - 1U] == '\r' ||
                            detail[detail_length - 1U] == '\n'))
                        --detail_length;
                    if (detail_length >= sizeof(rejection))
                        detail_length = sizeof(rejection) - 1U;
                    if (detail_length)
                        memcpy(rejection, detail, detail_length);
                    rejection[detail_length] = 0;
                }
                result = AMG_OK;
                done = 1;
                break;
            }
            if (event.length >= 5U && event.data[0] == '*' &&
                (event.data[1] == ' ' || event.data[1] == '\t') &&
                tolower((unsigned char)event.data[2]) == 'b' &&
                tolower((unsigned char)event.data[3]) == 'y' &&
                tolower((unsigned char)event.data[4]) == 'e') {
                amg_error_set(error, AMG_ERR_PROTOCOL,
                              T(MSG_THE_IMAP_SERVER_CLOSED_THE_CONNECTION_DURING_AUTHENTICATION, "The IMAP server closed the connection during authentication."));
                result = AMG_ERR_PROTOCOL;
                done = 1;
                break;
            }
        }
        if (result < 0) {
            if (parser.failed)
                set_parser_error(error, result, &parser);
            else if (!error || error->code == AMG_OK)
                set_parser_error(error, result, NULL);
            break;
        }
        if (!done) result = AMG_OK;
    }
    amg_imap_parser_free(&parser);
    if (result != AMG_OK) return result;
    if (!ok) {
        char message[256];
        if (rejection[0])
            amg_tr_snprintf(message, sizeof(message), MSG_IMAP_AUTHENTICATION_REJECTED_VALUE, "IMAP authentication rejected: %s", rejection);
        else
            snprintf(message, sizeof(message), "%s",
                     T(MSG_THE_IMAP_SERVER_REJECTED_AUTHENTICATION, "The IMAP server rejected authentication."));
        amg_error_set(error, AMG_ERR_AUTH, message);
        return AMG_ERR_AUTH;
    }
    amg_error_set(error, AMG_OK, "");
    return AMG_OK;
}

static int imap_authenticate(AmgImapSession *session,
                             const AmgAccount *account,
                             const char *access_token, AmgError *error)
{
    AmgBuffer encoded, command;
    int result = AMG_OK;
    amg_buffer_init(&encoded);
    amg_buffer_init(&command);

    if (account->auth_mode == AMG_AUTH_OAUTH2_GOOGLE) {
        result = build_xoauth2_response(account, access_token, &encoded,
                                        error);
        if (result == AMG_OK)
            result = imap_authenticate_sasl(session, "XOAUTH2", &encoded,
                                            session->capability_sasl_ir,
                                            error);
    } else {
        if (!account->imap_password || !account->imap_password[0]) {
            amg_error_set(error, AMG_ERR_AUTH,
                          T(MSG_IMAP_PASSWORD_IS_MISSING, "IMAP password is missing."));
            result = AMG_ERR_AUTH;
        } else if (!session->capability_login_disabled) {
            result = amg_buffer_append_cstr(&command, "LOGIN ");
            if (result == AMG_OK)
                result = append_imap_quoted(amg_account_imap_user(account),
                                            &command);
            if (result == AMG_OK)
                result = amg_buffer_append_char(&command, ' ');
            if (result == AMG_OK)
                result = append_imap_quoted(account->imap_password, &command);
            if (result == AMG_OK) {
                amg_buffer_terminate(&command);
                result = imap_command(session, (const char *)command.data,
                                      NULL, error);
                if (result == AMG_ERR_PROTOCOL) {
                    result = AMG_ERR_AUTH;
                    if (error) error->code = AMG_ERR_AUTH;
                }
            }
            /* A server can allow LOGIN syntactically but still reject that
             * command while offering SASL PLAIN.  Retry PLAIN when advertised
             * before giving up on otherwise valid credentials. */
            if (result == AMG_ERR_AUTH && session->capability_auth_plain) {
                amg_error_set(error, AMG_OK, "");
                encoded.length = 0;
                result = build_plain_response(account, &encoded, error);
                if (result == AMG_OK)
                    result = imap_authenticate_sasl(
                        session, "PLAIN", &encoded,
                        session->capability_sasl_ir, error);
            }
        } else if (session->capability_auth_plain) {
            result = build_plain_response(account, &encoded, error);
            if (result == AMG_OK)
                result = imap_authenticate_sasl(
                    session, "PLAIN", &encoded,
                    session->capability_sasl_ir, error);
        } else {
            result = AMG_ERR_UNSUPPORTED;
            amg_error_set(
                error, result,
                T(MSG_THE_IMAP_SERVER_DISABLES_LOGIN_AND_DOES_NOT, "The IMAP server disables LOGIN and does not advertise AUTH=PLAIN."));
        }
    }

    amg_secure_clear(encoded.data, encoded.capacity);
    amg_secure_clear(command.data, command.capacity);
    amg_buffer_free(&encoded);
    amg_buffer_free(&command);
    return result;
}

static void configure_special_mailboxes(AmgImapSession *session,
                                        const AmgAccount *account)
{
    static const char *empty = "";
    const char *configured[7];
    size_t i;
    if (!session || !account) return;
    configured[0] = "INBOX";
    configured[1] = account->sent_mailbox;
    configured[2] = account->drafts_mailbox;
    configured[3] = account->trash_mailbox;
    configured[4] = account->spam_mailbox;
    configured[5] = account->all_mailbox;
    configured[6] = empty;
    for (i = 0; i < 7U; ++i) {
        session->configured_special_mailboxes[i][0] = 0;
        if (configured[i] && configured[i][0]) {
            snprintf(session->configured_special_mailboxes[i],
                     sizeof(session->configured_special_mailboxes[i]), "%s",
                     configured[i]);
            snprintf(session->special_mailboxes[i],
                     sizeof(session->special_mailboxes[i]), "%s",
                     configured[i]);
        }
    }
}

int amg_imap_connect(AmgImapSession *session, const AmgAccount *account,
                     const char *access_token, AmgError *error)
{
    AmgBuffer response;
    int result;
    int preauthenticated = 0;
    if (!session || !account) return AMG_ERR_ARGUMENT;
    configure_special_mailboxes(session, account);

    if (account->imap_starttls)
        session->connection = amg_tls_connect_plain(
            account->imap_host, account->imap_port, 30U, error);
    else
        session->connection = amg_tls_connect(
            account->imap_host, account->imap_port, 30U, error);
    if (!session->connection)
        return error ? error->code : AMG_ERR_TLS;

    result = read_greeting(session, &preauthenticated, error);
    if (result != AMG_OK) goto fail;
    if (preauthenticated && account->imap_starttls) {
        amg_error_set(
            error, AMG_ERR_UNSUPPORTED,
            T(MSG_THE_IMAP_SERVER_REPORTS_PREAUTH_BEFORE_STARTTLS_PLEASE, "The IMAP server reports PREAUTH before STARTTLS. Please use implicit TLS instead."));
        result = AMG_ERR_UNSUPPORTED;
        goto fail;
    }

    amg_buffer_init(&response);
    result = imap_command(session, "CAPABILITY", &response, error);
    if (result == AMG_OK) {
        amg_buffer_terminate(&response);
        update_capabilities(session, &response);
    }

    if (result == AMG_OK && account->imap_starttls) {
        if (!session->capability_starttls) {
            amg_error_set(
                error, AMG_ERR_UNSUPPORTED,
                T(MSG_THE_IMAP_SERVER_DOES_NOT_ADVERTISE_STARTTLS, "The IMAP server does not advertise STARTTLS."));
            result = AMG_ERR_UNSUPPORTED;
        } else {
            response.length = 0;
            result = imap_command(session, "STARTTLS", &response, error);
            if (result == AMG_OK)
                result = amg_tls_starttls(
                    session->connection, account->imap_host, error);
            if (result == AMG_OK) {
                /* Capabilities learned before STARTTLS are no longer valid. */
                response.length = 0;
                result = imap_command(session, "CAPABILITY", &response,
                                      error);
                if (result == AMG_OK) {
                    amg_buffer_terminate(&response);
                    update_capabilities(session, &response);
                }
            }
        }
    }

    if (result == AMG_OK && !preauthenticated)
        result = imap_authenticate(session, account, access_token, error);
    amg_buffer_free(&response);
    if (result == AMG_OK) {
        session->authenticated = 1;
        amg_error_set(error, AMG_OK, "");
        return AMG_OK;
    }
fail:
    amg_tls_close(session->connection);
    session->connection = NULL;
    return result;
}

static unsigned long special_flag(const char *flags)
{
    unsigned long result = 0;
    if (ascii_ci_contains(flags,"\\Inbox")) result|=AMG_LABEL_INBOX;
    if (ascii_ci_contains(flags,"\\Sent")) result|=AMG_LABEL_SENT;
    if (ascii_ci_contains(flags,"\\Drafts")) result|=AMG_LABEL_DRAFTS;
    if (ascii_ci_contains(flags,"\\Trash")) result|=AMG_LABEL_TRASH;
    if (ascii_ci_contains(flags,"\\Junk") ||
        ascii_ci_contains(flags,"\\Spam")) result|=AMG_LABEL_SPAM;
    if (ascii_ci_contains(flags,"\\All") ||
        ascii_ci_contains(flags,"\\AllMail")) result|=AMG_LABEL_ALL;
    if (ascii_ci_contains(flags,"\\Flagged") ||
        ascii_ci_contains(flags,"\\Starred")) result|=AMG_LABEL_FLAGGED;
    return result;
}

static unsigned long special_flag_from_name(const char *name_utf8,
                                            char delimiter)
{
    const char *leaf = name_utf8 ? name_utf8 : "";
    const char *cursor = leaf;
    if (delimiter) {
        while (*cursor) {
            if (*cursor == delimiter) leaf = cursor + 1;
            ++cursor;
        }
    }
    if (ascii_ci_equal(leaf, "INBOX") ||
        ascii_ci_equal(leaf, "Posteingang")) return AMG_LABEL_INBOX;
    if (ascii_ci_equal(leaf, "Sent") ||
        ascii_ci_equal(leaf, "Sent Mail") ||
        ascii_ci_equal(leaf, "Gesendet")) return AMG_LABEL_SENT;
    if (ascii_ci_equal(leaf, "Drafts") ||
        !strcmp(leaf, "Entw\303\274rfe")) return AMG_LABEL_DRAFTS;
    if (ascii_ci_equal(leaf, "Trash") || ascii_ci_equal(leaf, "Bin") ||
        ascii_ci_equal(leaf, "Papierkorb")) return AMG_LABEL_TRASH;
    if (ascii_ci_equal(leaf, "Spam") ||
        ascii_ci_equal(leaf, "Junk")) return AMG_LABEL_SPAM;
    if (ascii_ci_equal(leaf, "All Mail") ||
        ascii_ci_equal(leaf, "Alle Nachrichten")) return AMG_LABEL_ALL;
    if (ascii_ci_equal(leaf, "Starred") ||
        ascii_ci_equal(leaf, "Flagged") ||
        ascii_ci_equal(leaf, "Markiert")) return AMG_LABEL_FLAGGED;
    return 0;
}

static unsigned long apply_configured_special_use(
    const AmgImapSession *session, const char *mailbox_utf8,
    unsigned long detected)
{
    static const unsigned long flags[7] = {
        AMG_LABEL_INBOX, AMG_LABEL_SENT, AMG_LABEL_DRAFTS,
        AMG_LABEL_TRASH, AMG_LABEL_SPAM, AMG_LABEL_ALL, AMG_LABEL_FLAGGED
    };
    size_t i;
    unsigned long result = detected;
    if (!session || !mailbox_utf8) return detected;
    for (i = 1U; i <= 5U; ++i) {
        const char *configured = session->configured_special_mailboxes[i];
        if (!configured[0]) continue;
        result &= ~flags[i];
        if (!strcmp(mailbox_utf8, configured)) result |= flags[i];
    }
    return result;
}

static void remember_special_mailboxes(AmgImapSession *session,
                                       const AmgImapLabel *label)
{
    static const unsigned long flags[7] = {
        AMG_LABEL_INBOX, AMG_LABEL_SENT, AMG_LABEL_DRAFTS,
        AMG_LABEL_TRASH, AMG_LABEL_SPAM, AMG_LABEL_ALL, AMG_LABEL_FLAGGED
    };
    size_t i;
    if (!session || !label || !label->name_utf8[0]) return;
    for (i = 0; i < 7U; ++i) {
        if (label->special_use & flags[i]) {
            snprintf(session->special_mailboxes[i],
                     sizeof(session->special_mailboxes[i]), "%s",
                     label->name_utf8);
        }
    }
}

static const char *resolve_special_mailbox(const AmgImapSession *session,
                                           const char *mailbox_utf8)
{
    static const char *aliases[7] = {
        "\\Inbox", "\\Sent", "\\Drafts", "\\Trash",
        "\\Junk", "\\All", "\\Flagged"
    };
    size_t i;
    if (!session || !mailbox_utf8) return mailbox_utf8;
    for (i = 0; i < 7U; ++i) {
        int matches = ascii_ci_equal(mailbox_utf8, aliases[i]);
        if (i == 4U && ascii_ci_equal(mailbox_utf8, "\\Spam")) matches = 1;
        if (i == 5U && ascii_ci_equal(mailbox_utf8, "\\AllMail")) matches = 1;
        if (i == 6U && ascii_ci_equal(mailbox_utf8, "\\Starred")) matches = 1;
        if (matches && session->special_mailboxes[i][0])
            return session->special_mailboxes[i];
    }
    return mailbox_utf8;
}

static const char *parse_quoted(const char *p, AmgBuffer *value)
{
    if (*p != '"') return NULL;
    ++p;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) ++p;
        if (amg_buffer_append_char(value,(unsigned char)*p++)!=AMG_OK) return NULL;
    }
    return *p == '"' ? p + 1 : NULL;
}

static int parse_list_line(const char *line, const AmgImapSession *session,
                           AmgImapLabel *label)
{
    const char *p, *end; AmgBuffer wire, decoded;
    if (strncmp(line,"* LIST ",7U) && strncmp(line,"* XLIST ",8U)) return 0;
    p = strchr(line,'('); if(!p) return 0; end=strchr(p,')'); if(!end)return 0;
    { char flags[256]; size_t n=(size_t)(end-p-1);if(n>=sizeof(flags))n=sizeof(flags)-1U;memcpy(flags,p+1,n);flags[n]=0;label->special_use=special_flag(flags);label->selectable=!ascii_ci_contains(flags,"\\Noselect"); }
    p=end+1;while(*p==' ')++p;
    if(*p=='"'){ if(!p[1]||p[2]!='"')return 0;label->delimiter=p[1];p+=3; } else if(!strncmp(p,"NIL",3U)){label->delimiter=0;p+=3;} else return 0;
    while (*p == ' ') ++p;
    amg_buffer_init(&wire);
    amg_buffer_init(&decoded);
    if(*p=='"'){if(!parse_quoted(p,&wire)){amg_buffer_free(&wire);amg_buffer_free(&decoded);return 0;}}
    else {const char *q=p;while(*q&&*q!='\r'&&*q!='\n'&&!isspace((unsigned char)*q))++q;amg_buffer_append(&wire,p,(size_t)(q-p));}
    amg_buffer_terminate(&wire);if(amg_modified_utf7_decode((const char*)wire.data,&decoded)!=AMG_OK){amg_buffer_free(&wire);amg_buffer_free(&decoded);return 0;}
    amg_buffer_terminate(&decoded);snprintf(label->wire_name,sizeof(label->wire_name),"%s",(const char*)wire.data);
    snprintf(label->name_utf8,sizeof(label->name_utf8),"%s",(const char*)decoded.data);
    if (!strcmp(label->wire_name,"INBOX")) label->special_use|=AMG_LABEL_INBOX;
    label->special_use |= special_flag_from_name(label->name_utf8,
                                                 label->delimiter);
    label->special_use = apply_configured_special_use(
        session, label->name_utf8, label->special_use);
    amg_buffer_free(&wire);amg_buffer_free(&decoded);return 1;
}

int amg_imap_list_labels(AmgImapSession *session, AmgImapLabel *labels,
                         size_t capacity, size_t *count, AmgError *error)
{
    AmgBuffer response; char *cursor; size_t found=0; int result;
    if (!session || !labels || !count) return AMG_ERR_ARGUMENT;
    amg_buffer_init(&response);
    /* Standardserver zuerst nach RFC-6154-Sonderordnern fragen, wenn die
     * Capability angeboten wird. Ohne SPECIAL-USE bleibt normales LIST der
     * portable Fallback. XLIST bleibt nur ein Kompatibilitaetsweg fuer alte
     * Gmail-Serverkonfigurationen. */
    if (session->capability_special_use)
        result=imap_command(session,"LIST \"\" \"*\" RETURN (SPECIAL-USE)",&response,error);
    else
        result=imap_command(session,"LIST \"\" \"*\"",&response,error);
    if(result!=AMG_OK && session->capability_special_use){response.length=0;result=imap_command(session,"LIST \"\" \"*\"",&response,error);}
    if(result!=AMG_OK){response.length=0;result=imap_command(session,"XLIST \"\" \"*\"",&response,error);}
    if(result==AMG_OK){amg_buffer_terminate(&response);cursor=(char*)response.data;
        while(*cursor&&found<capacity){char *next=strstr(cursor,"\r\n");if(!next)break;*next=0;if(parse_list_line(cursor,session,&labels[found])){remember_special_mailboxes(session,&labels[found]);++found;}cursor=next+2;}}
    amg_buffer_free(&response);*count=found;return result;
}

static int quote_mailbox(const char *utf8, AmgBuffer *output)
{
    AmgBuffer wire; size_t i; int result;
    amg_buffer_init(&wire);result=amg_modified_utf7_encode(utf8,&wire);if(result!=AMG_OK){amg_buffer_free(&wire);return result;}
    amg_buffer_append_char(output,'"');for(i=0;i<wire.length;++i){if(wire.data[i]=='"'||wire.data[i]=='\\')amg_buffer_append_char(output,'\\');amg_buffer_append_char(output,wire.data[i]);}
    result=amg_buffer_append_char(output,'"');amg_buffer_free(&wire);return result;
}

int amg_imap_select(AmgImapSession *session, const char *mailbox_utf8, AmgError *error)
{
    const char *resolved_mailbox;
    AmgBuffer command,response;unsigned long exists=0,uid_validity=0;int result;
    if (!session || !mailbox_utf8 || !*mailbox_utf8)
        return AMG_ERR_ARGUMENT;
    resolved_mailbox=resolve_special_mailbox(session,mailbox_utf8);amg_buffer_init(&command);amg_buffer_init(&response);amg_buffer_append_cstr(&command,"SELECT ");
    result=quote_mailbox(resolved_mailbox,&command);if(result==AMG_OK){amg_buffer_terminate(&command);result=imap_command(session,(char*)command.data,&response,error);}
    if(result==AMG_OK){amg_buffer_terminate(&response);if(!amg_imap_parse_exists(response.data,response.length,&exists)){result=AMG_ERR_PROTOCOL;amg_error_set(error,result,T(MSG_THE_IMAP_SERVER_DID_NOT_PROVIDE_A_MESSAGE, "The IMAP server did not provide a message count for the folder."));}}
    if(result==AMG_OK){
        /* UIDVALIDITY is the mailbox generation identifier.  It is used by
         * the GUI notification high-water mark to distinguish a genuinely
         * new UID from a recreated mailbox whose UID sequence restarted. */
        (void)amg_imap_parse_uidvalidity(response.data,response.length,
                                         &uid_validity);
        session->uid_validity=uid_validity;
        session->selected_exists=exists;
        snprintf(session->selected_mailbox,sizeof(session->selected_mailbox),
                 "%s",resolved_mailbox);
    }
    amg_buffer_free(&command);amg_buffer_free(&response);return result;
}

int amg_imap_fetch_page(AmgImapSession *session, unsigned long before_uid,
                        size_t limit, AmgBuffer *response, AmgError *error)
{
    AmgBuffer mapping;
    char command[512];
    unsigned long first, last;
    int result = AMG_OK;
    if (!session || !response || !limit) return AMG_ERR_ARGUMENT;
    last = session->selected_exists;
    if (before_uid) {
        unsigned long sequence = 0;
        amg_buffer_init(&mapping);
        snprintf(command, sizeof(command), "UID FETCH %lu (UID)", before_uid);
        result = imap_command(session, command, &mapping, error);
        if (result == AMG_OK) {
            amg_buffer_terminate(&mapping);
            if (!amg_imap_parse_fetch_sequence(mapping.data, mapping.length,
                                                before_uid, &sequence)) {
                result = AMG_ERR_PROTOCOL;
                amg_error_set(error, result,
                              T(MSG_POSITION_OF_THE_NEXT_MESSAGE_PAGE_IS_MISSING, "Position of the next message page is missing."));
            } else {
                last = sequence > 1UL ? sequence - 1UL : 0UL;
            }
        }
        amg_buffer_free(&mapping);
    }
    if (result != AMG_OK || !last) return result;
    first = (limit >= (size_t)last) ? 1UL : last - (unsigned long)limit + 1UL;
    snprintf(command, sizeof(command), "FETCH %lu:%lu %s",
             first, last, imap_message_list_fetch_items);
    return imap_command(session, command, response, error);
}

static int parse_uid_search_result(const unsigned char *data, size_t length,
                                   unsigned long **uids, size_t *count)
{
    size_t position = 0, found = 0, capacity = 0;
    unsigned long *values = NULL;
    if ((!data && length) || !uids || !count) return AMG_ERR_ARGUMENT;
    while (position < length) {
        size_t line_start = position, line_end;
        const unsigned char *cursor, *end;
        while (position < length && data[position] != '\n') ++position;
        line_end = position;
        if (position < length) ++position;
        if (line_end > line_start && data[line_end - 1U] == '\r') --line_end;
        if (line_end - line_start < 8U ||
            memcmp(data + line_start, "* SEARCH", 8U) != 0)
            continue;
        cursor = data + line_start + 8U;
        end = data + line_end;
        while (cursor < end) {
            unsigned long value = 0UL;
            while (cursor < end && isspace((unsigned char)*cursor)) ++cursor;
            if (cursor >= end) break;
            if (!isdigit((unsigned char)*cursor)) {
                while (cursor < end && !isspace((unsigned char)*cursor))
                    ++cursor;
                continue;
            }
            while (cursor < end && isdigit((unsigned char)*cursor)) {
                unsigned long digit = (unsigned long)(*cursor - '0');
                if (value > (0xffffffffUL - digit) / 10UL) {
                    free(values);
                    return AMG_ERR_LIMIT;
                }
                value = value * 10UL + digit;
                ++cursor;
            }
            if (!value) continue;
            if (found == capacity) {
                size_t next_capacity = capacity ? capacity * 2U : 128U;
                unsigned long *next;
                if (next_capacity < capacity ||
                    next_capacity > SIZE_MAX / sizeof(*values)) {
                    free(values);
                    return AMG_ERR_LIMIT;
                }
                next = (unsigned long *)realloc(
                    values, next_capacity * sizeof(*values));
                if (!next) {
                    free(values);
                    return AMG_ERR_MEMORY;
                }
                values = next;
                capacity = next_capacity;
            }
            values[found++] = value;
        }
    }
    *uids = values;
    *count = found;
    return AMG_OK;
}

static int append_uid_fetch_batch(AmgImapSession *session,
                                  const unsigned long *uids,
                                  size_t count, AmgBuffer *response,
                                  AmgError *error)
{
    AmgBuffer command, batch_response;
    size_t i;
    int result = AMG_OK;
    amg_buffer_init(&command);
    amg_buffer_init(&batch_response);
    result = amg_buffer_append_cstr(&command, "UID FETCH ");
    for (i = 0; result == AMG_OK && i < count; ++i) {
        char value[32];
        int written = snprintf(value, sizeof(value), "%s%lu",
                               i ? "," : "", uids[i]);
        if (written <= 0 || (size_t)written >= sizeof(value))
            result = AMG_ERR_LIMIT;
        else
            result = amg_buffer_append(&command, value, (size_t)written);
    }
    if (result == AMG_OK)
        result = amg_buffer_append_char(&command, ' ');
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(&command,
                                        imap_message_list_fetch_items);
    if (result == AMG_OK)
        result = amg_buffer_terminate(&command);
    if (result == AMG_OK)
        result = imap_command(session, (const char *)command.data,
                              &batch_response, error);
    if (result == AMG_OK && batch_response.length)
        result = amg_buffer_append(response, batch_response.data,
                                   batch_response.length);
    if (result != AMG_OK && (!error || error->code == AMG_OK))
        amg_error_set(error, result,
                      T(MSG_MESSAGE_LIST_COULD_NOT_BE_LOADED, "Message list could not be loaded."));
    amg_buffer_free(&command);
    amg_buffer_free(&batch_response);
    return result;
}

#if AMIGMAIL_AMIGA
static int imap_is_leap_year(unsigned long year)
{
    return (year % 4UL == 0UL &&
            (year % 100UL != 0UL || year % 400UL == 0UL));
}

static unsigned long imap_days_in_year(unsigned long year)
{
    return imap_is_leap_year(year) ? 366UL : 365UL;
}

static int imap_date_from_days_since_1978(unsigned long days_since_1978,
                                          unsigned long *day,
                                          unsigned long *month,
                                          unsigned long *year)
{
    static const unsigned char month_lengths[] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };
    unsigned long current_year = 1978UL;
    unsigned long current_month;

    if (!day || !month || !year) return AMG_ERR_ARGUMENT;

    while (days_since_1978 >= imap_days_in_year(current_year)) {
        days_since_1978 -= imap_days_in_year(current_year);
        ++current_year;
    }

    for (current_month = 0UL; current_month < 12UL; ++current_month) {
        unsigned long length = month_lengths[current_month];
        if (current_month == 1UL && imap_is_leap_year(current_year))
            ++length;
        if (days_since_1978 < length) break;
        days_since_1978 -= length;
    }
    if (current_month >= 12UL) return AMG_ERR_IO;

    *day = days_since_1978 + 1UL;
    *month = current_month;
    *year = current_year;
    return AMG_OK;
}

#endif

static int imap_recent_since_date(unsigned int days,
                                  unsigned long *day,
                                  unsigned long *month,
                                  unsigned long *year)
{
#if AMIGMAIL_AMIGA
    struct DateStamp stamp;
    unsigned long target_days;

    DateStamp(&stamp);
    if (stamp.ds_Days < 0) return AMG_ERR_IO;
    if ((unsigned long)(days - 1U) > (unsigned long)stamp.ds_Days)
        return AMG_ERR_IO;

    target_days = (unsigned long)stamp.ds_Days - (unsigned long)(days - 1U);
    return imap_date_from_days_since_1978(target_days, day, month, year);
#else
    time_t now, since_time;
    struct tm *since_tm;

    now = time(NULL);
    if (now == (time_t)-1) return AMG_ERR_IO;
    since_time = now - (time_t)(days - 1U) * (time_t)86400;
    since_tm = localtime(&since_time);
    if (!since_tm || since_tm->tm_mon < 0 || since_tm->tm_mon > 11)
        return AMG_ERR_IO;

    *day = (unsigned long)since_tm->tm_mday;
    *month = (unsigned long)since_tm->tm_mon;
    *year = (unsigned long)(since_tm->tm_year + 1900);
    return AMG_OK;
#endif
}

int amg_imap_fetch_recent(AmgImapSession *session, unsigned int days,
                          AmgBuffer *response, AmgError *error)
{
    static const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    enum { UID_BATCH = 100 };
    AmgBuffer search_response;
    unsigned long *uids = NULL;
    size_t uid_count = 0, offset;
    unsigned long since_day, since_month, since_year;
    char command[96];
    int result;

    if (!session || !response || days < 1U || days > 3650U)
        return AMG_ERR_ARGUMENT;
    if (!session->selected_exists) {
        amg_error_set(error, AMG_OK, "");
        return AMG_OK;
    }

    result = imap_recent_since_date(days, &since_day, &since_month,
                                    &since_year);
    if (result != AMG_OK || since_month > 11UL) {
        amg_error_set(error, AMG_ERR_IO,
                      T(MSG_FETCH_DATE_COULD_NOT_BE_CALCULATED, "Fetch date could not be calculated."));
        return AMG_ERR_IO;
    }

    snprintf(command, sizeof(command),
             "UID SEARCH NOT DELETED SINCE %02lu-%s-%04lu",
             since_day, months[since_month], since_year);
    amg_buffer_init(&search_response);
    result = imap_command(session, command, &search_response, error);
    if (result == AMG_OK)
        result = parse_uid_search_result(search_response.data,
                                         search_response.length,
                                         &uids, &uid_count);
    if (result != AMG_OK && (!error || error->code == AMG_OK))
        amg_error_set(error, result,
                      T(MSG_MESSAGES_IN_THE_FETCH_PERIOD_COULD_NOT_BE, "Messages in the fetch period could not be determined."));
    amg_buffer_free(&search_response);
    if (result != AMG_OK) {
        free(uids);
        return result;
    }

    for (offset = 0U; offset < uid_count; offset += UID_BATCH) {
        size_t batch = uid_count - offset;
        if (batch > UID_BATCH) batch = UID_BATCH;
        result = append_uid_fetch_batch(session, uids + offset, batch,
                                        response, error);
        if (result != AMG_OK) break;
    }
    free(uids);
    if (result == AMG_OK) amg_error_set(error, AMG_OK, "");
    return result;
}

int amg_imap_fetch_after_uid(AmgImapSession *session, unsigned long uid,
                             AmgBuffer *response, AmgError *error)
{
    enum { UID_BATCH = 100 };
    AmgBuffer search_response;
    unsigned long *uids = NULL;
    size_t uid_count = 0U, offset;
    char command[64];
    int result;

    if (!session || !response || uid == 0UL) return AMG_ERR_ARGUMENT;
    if (!session->selected_exists || uid == ~0UL) {
        amg_error_set(error, AMG_OK, "");
        return AMG_OK;
    }

    snprintf(command, sizeof(command),
             "UID SEARCH NOT DELETED UID %lu:*", uid + 1UL);
    amg_buffer_init(&search_response);
    result = imap_command(session, command, &search_response, error);
    if (result == AMG_OK)
        result = parse_uid_search_result(search_response.data,
                                         search_response.length,
                                         &uids, &uid_count);
    if (result != AMG_OK && (!error || error->code == AMG_OK))
        amg_error_set(error, result,
                      T(MSG_NEW_MESSAGES_COULD_NOT_BE_DETERMINED, "New messages could not be determined."));
    amg_buffer_free(&search_response);
    if (result != AMG_OK) {
        free(uids);
        return result;
    }

    /* In an IMAP sequence-set, a range whose first value is greater than
     * '*' can be interpreted as a reversed range.  Therefore SEARCH
     * UID <last+1>:* may legally include the previous highest UID when no
     * newer message exists.  Filter the search result explicitly so the
     * five-minute check never refetches an old message. */
    {
        size_t read_index, write_index = 0U;
        for (read_index = 0U; read_index < uid_count; ++read_index) {
            if (uids[read_index] > uid)
                uids[write_index++] = uids[read_index];
        }
        uid_count = write_index;
    }

    for (offset = 0U; offset < uid_count; offset += UID_BATCH) {
        size_t batch = uid_count - offset;
        if (batch > UID_BATCH) batch = UID_BATCH;
        result = append_uid_fetch_batch(session, uids + offset, batch,
                                        response, error);
        if (result != AMG_OK) break;
    }
    free(uids);
    if (result == AMG_OK) amg_error_set(error, AMG_OK, "");
    return result;
}

int amg_imap_fetch_message(AmgImapSession *session, unsigned long uid, AmgBuffer *message, AmgError *error)
{
    char command[112];if(!uid)return AMG_ERR_ARGUMENT;snprintf(command,sizeof(command),"UID FETCH %lu (UID FLAGS BODY.PEEK[])",uid);return imap_command(session,command,message,error);
}

int amg_imap_set_seen(AmgImapSession *session, unsigned long uid, int seen, AmgError *error)
{
    char command[128];snprintf(command,sizeof(command),"UID STORE %lu %sFLAGS.SILENT (\\Seen)",uid,seen?"+":"-");return imap_command(session,command,NULL,error);
}

int amg_imap_set_flagged(AmgImapSession *session, unsigned long uid,
                         int flagged, AmgError *error)
{
    char command[128];
    if (!session || !uid) return AMG_ERR_ARGUMENT;
    snprintf(command, sizeof(command),
             "UID STORE %lu %sFLAGS.SILENT (\\Flagged)",
             uid, flagged ? "+" : "-");
    return imap_command(session, command, NULL, error);
}

static int move_with_uid_move(AmgImapSession *session, unsigned long uid,
                              const char *destination_mailbox,
                              AmgError *error)
{
    AmgBuffer command;
    char prefix[64];
    int result;
    amg_buffer_init(&command);
    snprintf(prefix, sizeof(prefix), "UID MOVE %lu ", uid);
    result = amg_buffer_append_cstr(&command, prefix);
    if (result == AMG_OK)
        result = quote_mailbox(destination_mailbox, &command);
    if (result == AMG_OK)
        result = amg_buffer_terminate(&command);
    if (result == AMG_OK)
        result = imap_command(session, (const char *)command.data, NULL,
                              error);
    amg_buffer_free(&command);
    return result;
}

static int move_with_copy_delete(AmgImapSession *session, unsigned long uid,
                                 const char *destination_mailbox,
                                 AmgError *error)
{
    AmgBuffer command;
    char prefix[64];
    char flag_command[128];
    int result;

    amg_buffer_init(&command);
    snprintf(prefix, sizeof(prefix), "UID COPY %lu ", uid);
    result = amg_buffer_append_cstr(&command, prefix);
    if (result == AMG_OK)
        result = quote_mailbox(destination_mailbox, &command);
    if (result == AMG_OK)
        result = amg_buffer_terminate(&command);
    if (result == AMG_OK)
        result = imap_command(session, (const char *)command.data, NULL,
                              error);
    amg_buffer_free(&command);
    if (result != AMG_OK) return result;

    snprintf(flag_command, sizeof(flag_command),
             "UID STORE %lu +FLAGS.SILENT (\\Deleted)", uid);
    result = imap_command(session, flag_command, NULL, error);
    if (result != AMG_OK) return result;

    if (session->capability_uidplus) {
        snprintf(flag_command, sizeof(flag_command),
                 "UID EXPUNGE %lu", uid);
        result = imap_command(session, flag_command, NULL, error);
    } else {
        /* A plain EXPUNGE would permanently remove every message that is
         * already marked \Deleted in this mailbox, not just the UID moved
         * above.  On servers without UIDPLUS we deliberately leave this one
         * message marked \Deleted and hide it from subsequent list/search
         * results instead of risking unrelated mail. */
        result = AMG_OK;
        amg_error_set(error, AMG_OK, "");
    }
    return result;
}

int amg_imap_move_label(AmgImapSession *session, unsigned long uid,
                        const char *source_label, const char *destination_label,
                        AmgError *error)
{
    const char *source_mailbox;
    const char *destination_mailbox;
    int result;
    if (!session || !uid || !destination_label || !*destination_label)
        return AMG_ERR_ARGUMENT;

    source_mailbox = source_label && *source_label
        ? resolve_special_mailbox(session, source_label)
        : session->selected_mailbox;
    destination_mailbox =
        resolve_special_mailbox(session, destination_label);
    if (!source_mailbox || !*source_mailbox ||
        !destination_mailbox || !*destination_mailbox)
        return AMG_ERR_ARGUMENT;
    if (ascii_ci_equal(source_mailbox, destination_mailbox))
        return AMG_ERR_ARGUMENT;

    if (!ascii_ci_equal(session->selected_mailbox, source_mailbox)) {
        result = amg_imap_select(session, source_mailbox, error);
        if (result != AMG_OK) return result;
    }

    if (session->capability_move)
        result = move_with_uid_move(session, uid, destination_mailbox, error);
    else
        result = move_with_copy_delete(session, uid, destination_mailbox,
                                       error);
    /* MOVE and UID EXPUNGE really remove one message from the selected
     * mailbox.  The safe no-UIDPLUS fallback only marks it \Deleted, so the
     * server's EXISTS count must remain untouched in that case. */
    if (result == AMG_OK && session->selected_exists &&
        (session->capability_move || session->capability_uidplus))
        --session->selected_exists;
    return result;
}

int amg_imap_move_to_trash(AmgImapSession *session, unsigned long uid,
                           const char *trash_label, AmgError *error)
{
    if (!session || !uid || !trash_label) return AMG_ERR_ARGUMENT;
    return amg_imap_move_label(session, uid, session->selected_mailbox, trash_label, error);
}

int amg_imap_delete_uid(AmgImapSession *session, unsigned long uid,
                        const char *mailbox_utf8, AmgError *error)
{
    const char *mailbox;
    char previous_mailbox[sizeof(session->selected_mailbox)];
    char command[128];
    int restore_previous = 0;
    int result;

    if (!session || !uid || !mailbox_utf8 || !*mailbox_utf8)
        return AMG_ERR_ARGUMENT;
    mailbox = resolve_special_mailbox(session, mailbox_utf8);
    if (!mailbox || !*mailbox) return AMG_ERR_ARGUMENT;

    previous_mailbox[0] = 0;
    if (session->selected_mailbox[0] &&
        !ascii_ci_equal(session->selected_mailbox, mailbox)) {
        strncpy(previous_mailbox, session->selected_mailbox,
                sizeof(previous_mailbox) - 1U);
        previous_mailbox[sizeof(previous_mailbox) - 1U] = 0;
        restore_previous = 1;
    }

    if (!ascii_ci_equal(session->selected_mailbox, mailbox)) {
        result = amg_imap_select(session, mailbox, error);
        if (result != AMG_OK) return result;
    }

    snprintf(command, sizeof(command),
             "UID STORE %lu +FLAGS.SILENT (\\Deleted)", uid);
    result = imap_command(session, command, NULL, error);
    if (result == AMG_OK && session->capability_uidplus) {
        snprintf(command, sizeof(command), "UID EXPUNGE %lu", uid);
        result = imap_command(session, command, NULL, error);
        if (result == AMG_OK && session->selected_exists)
            --session->selected_exists;
    } else if (result == AMG_OK) {
        /* As with the MOVE fallback, never issue a global EXPUNGE merely to
         * remove one draft.  On servers without UIDPLUS the old draft stays
         * marked \Deleted and is hidden by our NOT DELETED searches. */
        amg_error_set(error, AMG_OK, "");
    }

    if (restore_previous) {
        AmgError restore_error;
        memset(&restore_error, 0, sizeof(restore_error));
        if (amg_imap_select(session, previous_mailbox, &restore_error) !=
                AMG_OK &&
            result == AMG_OK) {
            result = restore_error.code != AMG_OK
                ? restore_error.code : AMG_ERR_PROTOCOL;
            if (error) *error = restore_error;
        }
    }
    return result;
}

static int imap_wait_append_continuation(AmgImapSession *session,
                                         const char *tag, AmgError *error)
{
    char line[1024];
    size_t used = 0;
    if (!session || !session->connection || !tag) return AMG_ERR_ARGUMENT;

    for (;;) {
        long count;
        if (used + 1U >= sizeof(line)) {
            amg_error_set(error, AMG_ERR_PROTOCOL,
                          T(MSG_IMAP_APPEND_RESPONSE_IS_TOO_LONG, "IMAP APPEND response is too long."));
            return AMG_ERR_PROTOCOL;
        }
        count = amg_tls_read(session->connection, line + used, 1U, error);
        if (count <= 0) return AMG_ERR_IO;
        if (line[used++] != '\n') continue;
        line[used] = 0;

        if (line[0] == '+') return AMG_OK;
        if (!strncmp(line, tag, strlen(tag)) &&
            line[strlen(tag)] == ' ') {
            char message[256];
            size_t length = used;
            while (length && (line[length - 1U] == '\r' ||
                              line[length - 1U] == '\n'))
                --length;
            line[length] = 0;
            amg_tr_snprintf(message, sizeof(message), MSG_IMAP_SERVER_VALUE, "IMAP server: %s", line);
            amg_error_set(error, AMG_ERR_PROTOCOL, message);
            return AMG_ERR_PROTOCOL;
        }
        used = 0;
    }
}

static int imap_append_message(AmgImapSession *session,
                               const char *mailbox_utf8,
                               const char *flags,
                               const unsigned char *message, size_t length,
                               AmgError *error)
{
    const char *mailbox;
    AmgBuffer wire;
    char tag[16], literal[64];
    int result;

    if (!session || !session->connection || !mailbox_utf8 ||
        !*mailbox_utf8 || (!message && length))
        return AMG_ERR_ARGUMENT;

    mailbox = resolve_special_mailbox(session, mailbox_utf8);
    if ((ascii_ci_equal(mailbox_utf8, "\\Drafts") &&
         ascii_ci_equal(mailbox, "\\Drafts")) ||
        (ascii_ci_equal(mailbox_utf8, "\\Sent") &&
         ascii_ci_equal(mailbox, "\\Sent"))) {
        AmgImapLabel *labels = (AmgImapLabel *)calloc(
            AMIGMAIL_MAX_LABELS, sizeof(*labels));
        size_t count = 0;
        if (!labels) {
            amg_error_set(error, AMG_ERR_MEMORY,
                          T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory."));
            return AMG_ERR_MEMORY;
        }
        result = amg_imap_list_labels(session, labels, AMIGMAIL_MAX_LABELS,
                                      &count, error);
        free(labels);
        if (result != AMG_OK) return result;
        mailbox = resolve_special_mailbox(session, mailbox_utf8);
    }

    if (!mailbox || !*mailbox ||
        (ascii_ci_equal(mailbox_utf8, "\\Drafts") &&
         ascii_ci_equal(mailbox, "\\Drafts")) ||
        (ascii_ci_equal(mailbox_utf8, "\\Sent") &&
         ascii_ci_equal(mailbox, "\\Sent"))) {
        if (ascii_ci_equal(mailbox_utf8, "\\Sent"))
            amg_error_set(error, AMG_ERR_PROTOCOL,
                          T(MSG_THE_SENT_FOLDER_WAS_NOT_FOUND, "The Sent folder was not found."));
        else
            amg_error_set(error, AMG_ERR_PROTOCOL,
                          T(MSG_THE_DRAFTS_FOLDER_WAS_NOT_FOUND, "The Drafts folder was not found."));
        return AMG_ERR_PROTOCOL;
    }

    snprintf(tag, sizeof(tag), "A%06lu", session->tag_counter++);
    amg_buffer_init(&wire);
    result = amg_buffer_append_cstr(&wire, tag);
    if (result == AMG_OK) result = amg_buffer_append_cstr(&wire, " APPEND ");
    if (result == AMG_OK) result = quote_mailbox(mailbox, &wire);
    if (result == AMG_OK && flags && *flags) {
        result = amg_buffer_append_cstr(&wire, " (");
        if (result == AMG_OK) result = amg_buffer_append_cstr(&wire, flags);
        if (result == AMG_OK) result = amg_buffer_append_char(&wire, ')');
    }
    if (result == AMG_OK) result = amg_buffer_append_cstr(&wire, " {");
    if (result == AMG_OK) {
        snprintf(literal, sizeof(literal), "%lu}", (unsigned long)length);
        result = amg_buffer_append_cstr(&wire, literal);
    }
    if (result == AMG_OK) result = amg_buffer_append_cstr(&wire, "\r\n");
    if (result != AMG_OK) {
        amg_buffer_free(&wire);
        amg_error_set(error, result,
                      T(MSG_IMAP_APPEND_COMMAND_COULD_NOT_BE_CREATED, "IMAP APPEND command could not be created."));
        return result;
    }

    result = amg_tls_write_all(session->connection, wire.data, wire.length,
                               error);
    amg_buffer_free(&wire);
    if (result != AMG_OK) return result;

    result = imap_wait_append_continuation(session, tag, error);
    if (result != AMG_OK) return result;
    if (length) {
        result = amg_tls_write_all(session->connection, message, length, error);
        if (result != AMG_OK) return result;
    }
    result = amg_tls_write_all(session->connection, "\r\n", 2U, error);
    if (result != AMG_OK) return result;
    return imap_collect(session, tag, NULL, error);
}

int amg_imap_append_draft(AmgImapSession *session, const char *mailbox_utf8,
                          const unsigned char *message, size_t length,
                          AmgError *error)
{
    return imap_append_message(session, mailbox_utf8, "\\Draft",
                               message, length, error);
}

int amg_imap_append_sent(AmgImapSession *session, const unsigned char *message,
                         size_t length, AmgError *error)
{
    return imap_append_message(session, "\\Sent", "\\Seen",
                               message, length, error);
}

int amg_imap_empty_mailbox(AmgImapSession *session, const char *mailbox_utf8,
                           AmgError *error)
{
    const char *mailbox;
    char previous_mailbox[sizeof(session->selected_mailbox)];
    int restore_previous = 0;
    int result;
    if (!session || !mailbox_utf8 || !*mailbox_utf8)
        return AMG_ERR_ARGUMENT;
    mailbox = resolve_special_mailbox(session, mailbox_utf8);
    if (!mailbox || !*mailbox) return AMG_ERR_ARGUMENT;

    previous_mailbox[0] = 0;
    if (session->selected_mailbox[0] &&
        !ascii_ci_equal(session->selected_mailbox, mailbox)) {
        strncpy(previous_mailbox, session->selected_mailbox,
                sizeof(previous_mailbox) - 1U);
        previous_mailbox[sizeof(previous_mailbox) - 1U] = 0;
        restore_previous = 1;
    }

    result = amg_imap_select(session, mailbox, error);
    if (result == AMG_OK && session->selected_exists) {
        result = imap_command(
            session, "STORE 1:* +FLAGS.SILENT (\\Deleted)", NULL, error);
        /* Here a full EXPUNGE is intentional: the user explicitly requested
         * that this entire Trash/Junk mailbox be emptied. */
        if (result == AMG_OK)
            result = imap_command(session, "EXPUNGE", NULL, error);
        if (result == AMG_OK) session->selected_exists = 0;
    }

    if (restore_previous) {
        AmgError restore_error;
        memset(&restore_error, 0, sizeof(restore_error));
        if (amg_imap_select(session, previous_mailbox, &restore_error) !=
                AMG_OK &&
            result == AMG_OK) {
            result = restore_error.code != AMG_OK
                ? restore_error.code : AMG_ERR_PROTOCOL;
            if (error) *error = restore_error;
        }
    }
    return result;
}
