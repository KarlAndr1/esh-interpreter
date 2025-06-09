#include "utf16.h"

unsigned utf16_encode(uint16_t dst[static 2], uint32_t codepoint) {
	if(codepoint <= 0xFFFF) {
		dst[0] = codepoint;
		return 1;
	} else {
		codepoint -= 0x10000;
		dst[0] = (codepoint >> 10) + 0xD800;
		dst[1] = (codepoint & 0x3FF) + 0xDC00;
		return 2;
	}
}
