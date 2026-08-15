#include "buffer.h"

#include <stdlib.h>
#include <string.h>

void amg_buffer_init(AmgBuffer *buffer)
{
    if (!buffer) return;
    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}

void amg_buffer_free(AmgBuffer *buffer)
{
    if (!buffer) return;
    free(buffer->data);
    amg_buffer_init(buffer);
}

int amg_buffer_reserve(AmgBuffer *buffer, size_t capacity)
{
    unsigned char *next;
    size_t grown;
    if (!buffer) return AMG_ERR_ARGUMENT;
    if (capacity <= buffer->capacity) return AMG_OK;
    grown = buffer->capacity ? buffer->capacity : 64U;
    while (grown < capacity) {
        if (grown > (SIZE_MAX / 2U)) {
            grown = capacity;
            break;
        }
        grown *= 2U;
    }
    next = (unsigned char *)realloc(buffer->data, grown);
    if (!next) return AMG_ERR_MEMORY;
    buffer->data = next;
    buffer->capacity = grown;
    return AMG_OK;
}

int amg_buffer_append(AmgBuffer *buffer, const void *data, size_t length)
{
    int result;
    if (!buffer || (!data && length)) return AMG_ERR_ARGUMENT;
    if (length > SIZE_MAX - buffer->length) return AMG_ERR_LIMIT;
    result = amg_buffer_reserve(buffer, buffer->length + length + 1U);
    if (result != AMG_OK) return result;
    if (length) memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    buffer->data[buffer->length] = 0;
    return AMG_OK;
}

int amg_buffer_append_cstr(AmgBuffer *buffer, const char *text)
{
    return text ? amg_buffer_append(buffer, text, strlen(text)) : AMG_ERR_ARGUMENT;
}

int amg_buffer_append_char(AmgBuffer *buffer, unsigned char value)
{
    return amg_buffer_append(buffer, &value, 1U);
}

int amg_buffer_terminate(AmgBuffer *buffer)
{
    int result;
    if (!buffer) return AMG_ERR_ARGUMENT;
    result = amg_buffer_reserve(buffer, buffer->length + 1U);
    if (result == AMG_OK) buffer->data[buffer->length] = 0;
    return result;
}

void amg_buffer_consume(AmgBuffer *buffer, size_t length)
{
    if (!buffer || !length) return;
    if (length >= buffer->length) {
        buffer->length = 0;
    } else {
        memmove(buffer->data, buffer->data + length, buffer->length - length);
        buffer->length -= length;
    }
    if (buffer->data) buffer->data[buffer->length] = 0;
}
