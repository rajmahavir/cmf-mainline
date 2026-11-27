/* SPDX-License-Identifier: GPL-2.0-only */
#include <stddef.h>

void *memcpy(void *restrict destination, const void *restrict source, size_t length)
{
    unsigned char *out = destination;
    const unsigned char *in = source;

    for (size_t i = 0; i < length; i++)
        out[i] = in[i];
    return destination;
}

void *memset(void *destination, int byte, size_t length)
{
    unsigned char *out = destination;

    for (size_t i = 0; i < length; i++)
        out[i] = (unsigned char)byte;
    return destination;
}

int memcmp(const void *left, const void *right, size_t length)
{
    const unsigned char *a = left, *b = right;

    for (size_t i = 0; i < length; i++)
        if (a[i] != b[i])
            return a[i] - b[i];
    return 0;
}

void *memchr(const void *source, int byte, size_t length)
{
    const unsigned char *in = source;

    for (size_t i = 0; i < length; i++)
        if (in[i] == (unsigned char)byte)
            return (void *)(in + i);
    return NULL;
}

size_t strnlen(const char *string, size_t limit)
{
    size_t length = 0;

    while (length < limit && string[length])
        length++;
    return length;
}

size_t strlen(const char *string)
{
    size_t length = 0;

    while (string[length])
        length++;
    return length;
}

char *strrchr(const char *string, int byte)
{
    const char *last = NULL;

    do {
        if (*string == (char)byte)
            last = string;
    } while (*string++);
    return (char *)last;
}
