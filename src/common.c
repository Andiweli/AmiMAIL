#include "amigmail.h"

#include <string.h>

void amg_error_set(AmgError *error, int code, const char *message)
{
    if (!error) return;
    error->code = code;
    if (!message) message = "";
    strncpy(error->message, message, sizeof(error->message) - 1U);
    error->message[sizeof(error->message) - 1U] = '\0';
}

void amg_secure_clear(void *data, size_t size)
{
    volatile unsigned char *p = (volatile unsigned char *)data;
    while (p && size--) *p++ = 0;
}
