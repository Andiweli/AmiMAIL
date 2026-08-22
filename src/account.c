#include "account.h"
#include "i18n.h"

#define T(id, en) amg_tr((id), (en))

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void amg_account_init(AmgAccount *account)
{
    if (!account) return;
    memset(account, 0, sizeof(*account));
    account->imap_port = 993;
    account->smtp_port = 465;
    account->imap_starttls = 0;
    account->smtp_starttls = 0;
    account->smtp_same_credentials = 1;
    account->save_sent_copy = 1;
    account->fetch_on_start = 0;
    account->periodic_fetch = 0;
    account->fetch_days = 180U;
    account->notification_sound = 0;
    account->notification_sound_path[0] = 0;
    account->auth_mode = AMG_AUTH_PASSWORD;
}

void amg_account_clear(AmgAccount *account)
{
    if (!account) return;
    if (account->imap_password) {
        amg_secure_clear(account->imap_password, strlen(account->imap_password));
        free(account->imap_password);
    }
    if (account->smtp_password) {
        amg_secure_clear(account->smtp_password, strlen(account->smtp_password));
        free(account->smtp_password);
    }
    if (account->refresh_token) {
        amg_secure_clear(account->refresh_token, strlen(account->refresh_token));
        free(account->refresh_token);
    }
    amg_secure_clear(account, sizeof(*account));
}

int amg_account_set_secret(char **destination, const char *value)
{
    char *copy;
    if (!destination) return AMG_ERR_ARGUMENT;
    copy = NULL;
    if (value) {
        copy = (char *)malloc(strlen(value) + 1U);
        if (!copy) return AMG_ERR_MEMORY;
        strcpy(copy, value);
    }
    if (*destination) {
        amg_secure_clear(*destination, strlen(*destination));
        free(*destination);
    }
    *destination = copy;
    return AMG_OK;
}

const char *amg_account_imap_user(const AmgAccount *account)
{
    if (!account) return "";
    return account->imap_username[0] ? account->imap_username : account->email;
}

const char *amg_account_smtp_user(const AmgAccount *account)
{
    if (!account) return "";
    if (account->smtp_same_credentials)
        return amg_account_imap_user(account);
    return account->smtp_username[0] ? account->smtp_username : account->email;
}

const char *amg_account_smtp_password(const AmgAccount *account)
{
    if (!account) return NULL;
    if (account->smtp_same_credentials)
        return account->imap_password;
    return account->smtp_password && account->smtp_password[0]
        ? account->smtp_password : account->imap_password;
}

static void trim_ascii(char *text)
{
    size_t length;
    size_t start = 0U;
    size_t end;
    if (!text || !text[0]) return;
    length = strlen(text);
    while (start < length && isspace((unsigned char)text[start])) ++start;
    end = length;
    while (end > start && isspace((unsigned char)text[end - 1U])) --end;
    if (start > 0U) memmove(text, text + start, end - start);
    text[end - start] = 0;
}

static int ascii_ci_equal_char(char a, char b)
{
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    return a == b;
}

static int ascii_ci_ends_with(const char *text, const char *suffix)
{
    size_t text_length, suffix_length, i;
    if (!text || !suffix) return 0;
    text_length = strlen(text);
    suffix_length = strlen(suffix);
    if (suffix_length > text_length) return 0;
    text += text_length - suffix_length;
    for (i = 0; i < suffix_length; ++i)
        if (!ascii_ci_equal_char(text[i], suffix[i])) return 0;
    return 1;
}

static int ascii_ci_equal(const char *a, const char *b)
{
    size_t i = 0U;
    if (!a || !b) return 0;
    while (a[i] && b[i]) {
        if (!ascii_ci_equal_char(a[i], b[i])) return 0;
        ++i;
    }
    return a[i] == b[i];
}

int amg_account_is_google_host(const char *host)
{
    return host && *host &&
        (ascii_ci_equal(host, "gmail.com") ||
         ascii_ci_ends_with(host, ".gmail.com") ||
         ascii_ci_equal(host, "googlemail.com") ||
         ascii_ci_ends_with(host, ".googlemail.com"));
}

int amg_account_should_append_sent(const AmgAccount *account)
{
    if (!account || !account->save_sent_copy) return 0;
    /* Gmail/Google Workspace already files messages submitted through its
     * SMTP service in Sent Mail. An additional IMAP APPEND would duplicate
     * the message. */
    if (amg_account_is_google_host(account->imap_host) ||
        amg_account_is_google_host(account->smtp_host))
        return 0;
    return 1;
}

static void normalize_google_app_password(char **secret)
{
    char compact[128];
    size_t i, out = 0U;
    int had_whitespace = 0;
    if (!secret || !*secret || !(*secret)[0]) return;
    for (i = 0; (*secret)[i] && out + 1U < sizeof(compact); ++i) {
        unsigned char c = (unsigned char)(*secret)[i];
        if (isspace(c)) {
            had_whitespace = 1;
            continue;
        }
        if (!isalnum(c)) return;
        compact[out++] = (char)c;
    }
    if ((*secret)[i]) return;
    compact[out] = 0;
    if (had_whitespace && out == 16U)
        amg_account_set_secret(secret, compact);
}

void amg_account_normalize(AmgAccount *account)
{
    if (!account) return;
    trim_ascii(account->display_name);
    trim_ascii(account->email);
    trim_ascii(account->imap_host);
    trim_ascii(account->imap_username);
    trim_ascii(account->smtp_host);
    trim_ascii(account->smtp_username);
    trim_ascii(account->sent_mailbox);
    trim_ascii(account->drafts_mailbox);
    trim_ascii(account->all_mailbox);
    trim_ascii(account->spam_mailbox);
    trim_ascii(account->trash_mailbox);
    if (amg_account_is_google_host(account->imap_host))
        normalize_google_app_password(&account->imap_password);
    if (amg_account_is_google_host(account->smtp_host))
        normalize_google_app_password(&account->smtp_password);
}

static int valid_email(const char *email)
{
    const char *at;
    if (!email || !*email || strchr(email, ' ') || strchr(email, '\r') ||
        strchr(email, '\n'))
        return 0;
    at = strchr(email, '@');
    return at && at != email && strchr(at + 1, '.') != NULL;
}

static int valid_login(const char *value)
{
    if (!value || !*value) return 0;
    return !strchr(value, '\r') && !strchr(value, '\n');
}

static int valid_optional_mailbox(const char *value)
{
    return !value || (!strchr(value, '\r') && !strchr(value, '\n'));
}

int amg_account_validate(const AmgAccount *account, AmgError *error)
{
    if (!account || !valid_email(account->email)) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T(MSG_PLEASE_ENTER_A_VALID_EMAIL_ADDRESS, "Please enter a valid email address."));
        return AMG_ERR_ARGUMENT;
    }
    if (!account->imap_host[0] || !account->smtp_host[0] ||
        !account->imap_port || !account->smtp_port) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T(MSG_IMAP_SMTP_SERVER_OR_PORT_IS_MISSING, "IMAP/SMTP server or port is missing."));
        return AMG_ERR_ARGUMENT;
    }
    if (!valid_login(amg_account_imap_user(account)) ||
        !valid_login(amg_account_smtp_user(account))) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T(MSG_IMAP_SMTP_USER_NAME_IS_INVALID, "IMAP/SMTP user name is invalid."));
        return AMG_ERR_ARGUMENT;
    }
    if (!valid_optional_mailbox(account->sent_mailbox) ||
        !valid_optional_mailbox(account->drafts_mailbox) ||
        !valid_optional_mailbox(account->all_mailbox) ||
        !valid_optional_mailbox(account->spam_mailbox) ||
        !valid_optional_mailbox(account->trash_mailbox)) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T(MSG_A_MANUAL_SYSTEM_FOLDER_MAPPING_IS_INVALID, "A manual system-folder mapping is invalid."));
        return AMG_ERR_ARGUMENT;
    }
    if (account->fetch_days < 1U || account->fetch_days > 3650U) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T(MSG_THE_FETCH_PERIOD_MUST_BE_BETWEEN_1_AND, "The fetch period must be between 1 and 3650 days."));
        return AMG_ERR_ARGUMENT;
    }
    if (amg_account_is_google_host(account->imap_host) &&
        (account->imap_starttls || account->imap_port != 993U)) {
        amg_error_set(
            error, AMG_ERR_UNSUPPORTED,
            T(MSG_GMAIL_IMAP_USES_DIRECT_TLS_ON_PORT_993, "Gmail IMAP uses direct TLS on port 993. Disable IMAP STARTTLS and use port 993."));
        return AMG_ERR_UNSUPPORTED;
    }
    if (amg_account_is_google_host(account->smtp_host)) {
        if (account->smtp_starttls && account->smtp_port != 587U) {
            amg_error_set(
                error, AMG_ERR_UNSUPPORTED,
                T(MSG_GMAIL_SMTP_WITH_STARTTLS_USES_PORT_587, "Gmail SMTP with STARTTLS uses port 587."));
            return AMG_ERR_UNSUPPORTED;
        }
        if (!account->smtp_starttls && account->smtp_port != 465U) {
            amg_error_set(
                error, AMG_ERR_UNSUPPORTED,
                T(MSG_GMAIL_SMTP_WITH_DIRECT_TLS_USES_PORT_465, "Gmail SMTP with direct TLS uses port 465; for STARTTLS use port 587 and enable STARTTLS."));
            return AMG_ERR_UNSUPPORTED;
        }
    }
    if (account->auth_mode == AMG_AUTH_PASSWORD) {
        if (!account->imap_password || !account->imap_password[0]) {
            amg_error_set(error, AMG_ERR_AUTH,
                          T(MSG_THE_IMAP_PASSWORD_IS_MISSING, "The IMAP password is missing."));
            return AMG_ERR_AUTH;
        }
        if (!amg_account_smtp_password(account) ||
            !amg_account_smtp_password(account)[0]) {
            amg_error_set(error, AMG_ERR_AUTH,
                          T(MSG_THE_SMTP_PASSWORD_IS_MISSING, "The SMTP password is missing."));
            return AMG_ERR_AUTH;
        }
    } else if (account->auth_mode == AMG_AUTH_OAUTH2_GOOGLE) {
        if (!account->refresh_token) {
            amg_error_set(error, AMG_ERR_AUTH,
                          T(MSG_GOOGLE_OAUTH_AUTHORIZATION_HAS_NOT_BEEN_COMPLETED_YET, "Google OAuth authorization has not been completed yet."));
            return AMG_ERR_AUTH;
        }
    } else {
        amg_error_set(error, AMG_ERR_UNSUPPORTED,
                      T(MSG_THE_SELECTED_AUTHENTICATION_METHOD_IS_NOT_SUPPORTED, "The selected authentication method is not supported."));
        return AMG_ERR_UNSUPPORTED;
    }
    amg_error_set(error, AMG_OK, "");
    return AMG_OK;
}
