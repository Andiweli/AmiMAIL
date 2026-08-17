#ifndef AMIGMAIL_H
#define AMIGMAIL_H

#include <stddef.h>
#include <stdint.h>

#define AMIMAIL_NAME "AmiMail"
#define AMIMAIL_VERSION "1.0"
/* Legacy internal names are kept as aliases while the codebase is gradually
 * renamed; new AmiMail-specific code should use AMIMAIL_* directly. */
#define AMIGMAIL_NAME AMIMAIL_NAME
#define AMIGMAIL_VERSION AMIMAIL_VERSION
#define AMIGMAIL_PAGE_SIZE 50U
#define AMIGMAIL_MAX_LINE (256UL * 1024UL)
#define AMIGMAIL_MAX_MESSAGE (8UL * 1024UL * 1024UL)
#define AMIGMAIL_MAX_LABELS 256U
#define AMIGMAIL_MAX_HEADERS 2048U

#if defined(__amigaos__) || defined(__AMIGA__)
#define AMIGMAIL_AMIGA 1
#else
#define AMIGMAIL_AMIGA 0
#endif

typedef enum AmgResult {
    AMG_OK = 0,
    AMG_ERR_ARGUMENT = -1,
    AMG_ERR_MEMORY = -2,
    AMG_ERR_IO = -3,
    AMG_ERR_PROTOCOL = -4,
    AMG_ERR_TLS = -5,
    AMG_ERR_AUTH = -6,
    AMG_ERR_PARSE = -7,
    AMG_ERR_LIMIT = -8,
    AMG_ERR_UNSUPPORTED = -9,
    AMG_ERR_CANCELLED = -10
} AmgResult;

typedef enum AmgAuthMode {
    AMG_AUTH_PASSWORD = 0,
    AMG_AUTH_OAUTH2_GOOGLE = 1
} AmgAuthMode;

typedef struct AmgError {
    int code;
    char message[256];
} AmgError;

void amg_error_set(AmgError *error, int code, const char *message);
void amg_secure_clear(void *data, size_t size);

#endif
