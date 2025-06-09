#ifndef UTF16_H_INCLUDED
#define UTF16_H_INCLUDED

#include <stdint.h>

unsigned utf16_encode(uint16_t dst[static 2], uint32_t codepoint);

#endif
