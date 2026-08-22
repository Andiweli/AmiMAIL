#include "oauth.h"
#include "i18n.h"

#define T(id, en) amg_tr((id), (en))
#include "codec.h"
#include "crypto.h"
#include "tls.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void amg_oauth_tokens_clear(AmgOAuthTokens *tokens)
{
    if (!tokens) return;
    if (tokens->access_token) { amg_secure_clear(tokens->access_token, strlen(tokens->access_token)); free(tokens->access_token); }
    if (tokens->refresh_token) { amg_secure_clear(tokens->refresh_token, strlen(tokens->refresh_token)); free(tokens->refresh_token); }
    memset(tokens, 0, sizeof(*tokens));
}

int amg_oauth_generate_pkce(char verifier[129], char challenge[129], char state[65], AmgError *error)
{
    unsigned char random[64], digest[32];
    AmgBuffer encoded;
    int result;
    if (!verifier || !challenge || !state) return AMG_ERR_ARGUMENT;
    result = amg_random_bytes(random, sizeof(random));
    if (result != AMG_OK) { amg_error_set(error, result, T(MSG_SECURE_RANDOM_VALUES_COULD_NOT_BE_GENERATED, "Secure random values could not be generated.")); return result; }
    amg_buffer_init(&encoded);
    result = amg_base64url_encode(random, 48U, &encoded);
    if (result == AMG_OK && encoded.length < 129U) { memcpy(verifier, encoded.data, encoded.length); verifier[encoded.length] = 0; }
    else result = AMG_ERR_LIMIT;
    amg_buffer_free(&encoded);
    if (result != AMG_OK) return result;
    amg_sha256((const unsigned char *)verifier, strlen(verifier), digest);
    amg_buffer_init(&encoded); result = amg_base64url_encode(digest, sizeof(digest), &encoded);
    if (result == AMG_OK && encoded.length < 129U) { memcpy(challenge, encoded.data, encoded.length); challenge[encoded.length] = 0; }
    else result = AMG_ERR_LIMIT;
    amg_buffer_free(&encoded);
    if (result != AMG_OK) return result;
    amg_buffer_init(&encoded); result = amg_base64url_encode(random + 48U, 16U, &encoded);
    if (result == AMG_OK && encoded.length < 65U) { memcpy(state, encoded.data, encoded.length); state[encoded.length] = 0; }
    else result = AMG_ERR_LIMIT;
    amg_buffer_free(&encoded); amg_secure_clear(random, sizeof(random));
    amg_error_set(error, result, result == AMG_OK ? "" : T(MSG_PKCE_DATA_COULD_NOT_BE_GENERATED, "PKCE data could not be generated."));
    return result;
}

static int add_query(AmgBuffer *output, const char *name, const char *value, char separator)
{
    int result = amg_buffer_append_char(output, (unsigned char)separator);
    if (result == AMG_OK) result = amg_buffer_append_cstr(output, name);
    if (result == AMG_OK) result = amg_buffer_append_char(output, '=');
    if (result == AMG_OK) result = amg_percent_encode(value, output);
    return result;
}

int amg_oauth_build_authorize_url(const AmgOAuthConfig *config, const char *challenge,
                                  const char *state, AmgBuffer *output)
{
    int result;
    if (!config || !config->client_id || !config->redirect_uri || !config->scope || !challenge || !state || !output)
        return AMG_ERR_ARGUMENT;
    result = amg_buffer_append_cstr(output, "https://accounts.google.com/o/oauth2/v2/auth");
    if (result == AMG_OK) result = add_query(output, "client_id", config->client_id, '?');
    if (result == AMG_OK) result = add_query(output, "redirect_uri", config->redirect_uri, '&');
    if (result == AMG_OK) result = add_query(output, "response_type", "code", '&');
    if (result == AMG_OK) result = add_query(output, "scope", config->scope, '&');
    if (result == AMG_OK) result = add_query(output, "access_type", "offline", '&');
    if (result == AMG_OK) result = add_query(output, "prompt", "consent", '&');
    if (result == AMG_OK) result = add_query(output, "code_challenge", challenge, '&');
    if (result == AMG_OK) result = add_query(output, "code_challenge_method", "S256", '&');
    if (result == AMG_OK) result = add_query(output, "state", state, '&');
    return result;
}

static const char *skip_ws(const char *p) { while (*p && isspace((unsigned char)*p)) ++p; return p; }

static int json_string(const char *json, const char *key, char **value)
{
    AmgBuffer needle, decoded; const char *p;
    amg_buffer_init(&needle); amg_buffer_init(&decoded);
    amg_buffer_append_char(&needle, '"'); amg_buffer_append_cstr(&needle, key); amg_buffer_append_char(&needle, '"'); amg_buffer_terminate(&needle);
    p = strstr(json, (const char *)needle.data); amg_buffer_free(&needle); if (!p) return 0;
    p = strchr(p, ':'); if (!p) return -1; p = skip_ws(p + 1); if (*p++ != '"') return -1;
    while (*p && *p != '"') {
        unsigned char c = (unsigned char)*p++;
        if (c == '\\') {
            c = (unsigned char)*p++;
            if (c == 'n') c = '\n'; else if (c == 'r') c = '\r'; else if (c == 't') c = '\t';
            else if (c != '"' && c != '\\' && c != '/') { amg_buffer_free(&decoded); return -1; }
        }
        if (amg_buffer_append_char(&decoded, c) != AMG_OK) { amg_buffer_free(&decoded); return -1; }
    }
    if (*p != '"' || amg_buffer_terminate(&decoded) != AMG_OK) { amg_buffer_free(&decoded); return -1; }
    *value = (char *)decoded.data; return 1;
}

static int json_ulong(const char *json, const char *key, unsigned long *value)
{
    AmgBuffer needle; const char *p; char *end;
    amg_buffer_init(&needle); amg_buffer_append_char(&needle, '"'); amg_buffer_append_cstr(&needle, key); amg_buffer_append_char(&needle, '"'); amg_buffer_terminate(&needle);
    p = strstr(json, (const char *)needle.data); amg_buffer_free(&needle); if (!p) return 0;
    p = strchr(p, ':'); if (!p) return -1; p = skip_ws(p + 1); *value = strtoul(p, &end, 10); return end == p ? -1 : 1;
}

int amg_oauth_parse_token_json(const char *json, AmgOAuthTokens *tokens, AmgError *error)
{
    AmgOAuthTokens parsed; char *oauth_error = NULL; int found;
    if (!json || !tokens) return AMG_ERR_ARGUMENT;
    memset(&parsed, 0, sizeof(parsed));
    found = json_string(json, "error", &oauth_error);
    if (found > 0) { amg_error_set(error, AMG_ERR_AUTH, oauth_error); free(oauth_error); return AMG_ERR_AUTH; }
    if (found < 0 || json_string(json, "access_token", &parsed.access_token) <= 0 ||
        json_ulong(json, "expires_in", &parsed.expires_in) <= 0) {
        amg_oauth_tokens_clear(&parsed); amg_error_set(error, AMG_ERR_PARSE, T(MSG_INVALID_OAUTH_TOKEN_RESPONSE, "Invalid OAuth token response.")); return AMG_ERR_PARSE;
    }
    found = json_string(json, "refresh_token", &parsed.refresh_token);
    if (found < 0) { amg_oauth_tokens_clear(&parsed); return AMG_ERR_PARSE; }
    amg_oauth_tokens_clear(tokens); *tokens = parsed; amg_error_set(error, AMG_OK, ""); return AMG_OK;
}

static int token_request(const AmgOAuthConfig *config, const char *form, AmgOAuthTokens *tokens, AmgError *error)
{
    AmgBuffer response; int result;
    (void)config; amg_buffer_init(&response);
    result = amg_https_post_form("oauth2.googleapis.com", "/token", form, &response, error);
    if (result == AMG_OK) { amg_buffer_terminate(&response); result = amg_oauth_parse_token_json((const char *)response.data, tokens, error); }
    amg_buffer_free(&response); return result;
}

int amg_oauth_exchange_code(const AmgOAuthConfig *config, const char *code,
                            const char *verifier, AmgOAuthTokens *tokens, AmgError *error)
{
    AmgBuffer form; int result;
    if (!config || !config->client_id || !config->redirect_uri || !code || !verifier || !tokens) return AMG_ERR_ARGUMENT;
    amg_buffer_init(&form);
    result = add_query(&form, "client_id", config->client_id, 0); if (result == AMG_OK && form.length) amg_buffer_consume(&form, 1U);
    if (result == AMG_OK && config->client_secret && *config->client_secret) result = add_query(&form, "client_secret", config->client_secret, '&');
    if (result == AMG_OK) result = add_query(&form, "code", code, '&');
    if (result == AMG_OK) result = add_query(&form, "code_verifier", verifier, '&');
    if (result == AMG_OK) result = add_query(&form, "redirect_uri", config->redirect_uri, '&');
    if (result == AMG_OK) result = add_query(&form, "grant_type", "authorization_code", '&');
    if (result == AMG_OK) { amg_buffer_terminate(&form); result = token_request(config, (const char *)form.data, tokens, error); }
    amg_buffer_free(&form); return result;
}

int amg_oauth_refresh(const AmgOAuthConfig *config, const char *refresh_token,
                      AmgOAuthTokens *tokens, AmgError *error)
{
    AmgBuffer form; int result;
    if (!config || !config->client_id || !refresh_token || !tokens) return AMG_ERR_ARGUMENT;
    amg_buffer_init(&form);
    result = add_query(&form, "client_id", config->client_id, 0); if (result == AMG_OK && form.length) amg_buffer_consume(&form, 1U);
    if (result == AMG_OK && config->client_secret && *config->client_secret) result = add_query(&form, "client_secret", config->client_secret, '&');
    if (result == AMG_OK) result = add_query(&form, "refresh_token", refresh_token, '&');
    if (result == AMG_OK) result = add_query(&form, "grant_type", "refresh_token", '&');
    if (result == AMG_OK) { amg_buffer_terminate(&form); result = token_request(config, (const char *)form.data, tokens, error); }
    amg_buffer_free(&form); return result;
}
