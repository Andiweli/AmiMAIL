#ifndef AMIGMAIL_CODEC_H
#define AMIGMAIL_CODEC_H

#include "buffer.h"

int amg_base64_encode(const unsigned char *input, size_t length, AmgBuffer *output);
int amg_base64_decode(const char *input, size_t length, AmgBuffer *output);
int amg_base64url_encode(const unsigned char *input, size_t length, AmgBuffer *output);
int amg_quoted_printable_decode(const char *input, size_t length, AmgBuffer *output);
int amg_percent_encode(const char *input, AmgBuffer *output);
int amg_modified_utf7_encode(const char *utf8, AmgBuffer *output);
int amg_modified_utf7_decode(const char *input, AmgBuffer *output);
int amg_utf8_to_local(const char *utf8, AmgBuffer *output);

#endif
