#ifndef AMIGMAIL_CRYPTO_H
#define AMIGMAIL_CRYPTO_H

#include "amigmail.h"

int amg_random_bytes(unsigned char *output, size_t length);
void amg_sha256(const unsigned char *data, size_t length, unsigned char digest[32]);

#endif
