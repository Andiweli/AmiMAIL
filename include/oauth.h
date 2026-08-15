#ifndef AMIGMAIL_OAUTH_H
#define AMIGMAIL_OAUTH_H

#include "buffer.h"

typedef struct AmgOAuthConfig {
    const char *client_id;
    const char *client_secret;
    const char *redirect_uri;
    const char *scope;
} AmgOAuthConfig;

typedef struct AmgOAuthTokens {
    char *access_token;
    char *refresh_token;
    unsigned long expires_in;
} AmgOAuthTokens;

void amg_oauth_tokens_clear(AmgOAuthTokens *tokens);
int amg_oauth_generate_pkce(char verifier[129], char challenge[129], char state[65], AmgError *error);
int amg_oauth_build_authorize_url(const AmgOAuthConfig *config, const char *challenge,
                                  const char *state, AmgBuffer *output);
int amg_oauth_parse_token_json(const char *json, AmgOAuthTokens *tokens, AmgError *error);
int amg_oauth_exchange_code(const AmgOAuthConfig *config, const char *code,
                            const char *verifier, AmgOAuthTokens *tokens, AmgError *error);
int amg_oauth_refresh(const AmgOAuthConfig *config, const char *refresh_token,
                      AmgOAuthTokens *tokens, AmgError *error);

#endif
