/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef CMF_SHIM_STRING_H
#define CMF_SHIM_STRING_H
#include <stddef.h>

void *memcpy(void *restrict destination, const void *restrict source, size_t length);
void *memmove(void *destination, const void *source, size_t length);
void *memset(void *destination, int byte, size_t length);
int memcmp(const void *left, const void *right, size_t length);
void *memchr(const void *source, int byte, size_t length);
char *strchr(const char *string, int byte);
char *strrchr(const char *string, int byte);
size_t strlen(const char *string);
size_t strnlen(const char *string, size_t limit);
#endif
