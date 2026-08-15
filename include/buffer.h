#ifndef AMIGMAIL_BUFFER_H
#define AMIGMAIL_BUFFER_H

#include "amigmail.h"

typedef struct AmgBuffer {
    unsigned char *data;
    size_t length;
    size_t capacity;
} AmgBuffer;

void amg_buffer_init(AmgBuffer *buffer);
void amg_buffer_free(AmgBuffer *buffer);
int amg_buffer_reserve(AmgBuffer *buffer, size_t capacity);
int amg_buffer_append(AmgBuffer *buffer, const void *data, size_t length);
int amg_buffer_append_cstr(AmgBuffer *buffer, const char *text);
int amg_buffer_append_char(AmgBuffer *buffer, unsigned char value);
int amg_buffer_terminate(AmgBuffer *buffer);
void amg_buffer_consume(AmgBuffer *buffer, size_t length);

#endif
