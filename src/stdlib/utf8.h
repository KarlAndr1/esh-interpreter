#ifndef UTF8_H_INCLUDED
#define UTF8_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

unsigned utf8_next(char c);
bool utf8_prev(char c);

unsigned utf8_decode(const char *s, size_t at, size_t len, uint32_t *codepoint);

#endif
