#include "utf8.h"

#include <assert.h>

static unsigned char bcast(char c) {
	union {
		char s;
		unsigned char u;
	} b;
	
	b.s = c;
	return b.u;
}

unsigned utf8_next(char c) {
	unsigned char u = bcast(c);
	
	if((u & 128) == 0) return 1;
	if((u & 64) == 0) return 1;
	
	if((u & 32) == 0) return 2;
	if((u & 16) == 0) return 3;
	if((u & 8) == 0) return 4;
	
	return 1;
}

bool utf8_prev(char c) {
	unsigned char u = bcast(c);
	
	return (u & 128) && !(u & 64);
}

unsigned utf8_decode(const char *s, size_t at, size_t len, uint32_t *codepoint) {
	assert(at <= len);
	len -= at;
	s += at;
	
	*codepoint = 0;
	if(len == 0) return 0;
	
	unsigned char_len = utf8_next(s[0]);
	if(char_len > len) {
		*codepoint = 0;
		return 1;
	}
	
	unsigned char u = bcast(s[0]);
	
	switch(char_len) {
		case 1:
			if(u & 128) {
				*codepoint = 0;
				return 1;
			}
			
			*codepoint = u;
			return 1;
		
		case 2:
			*codepoint = u & 0x1F;
			break;
		case 3:
			*codepoint = u & 0xF;
			break;
		case 4:
			*codepoint = u & 0x7;
			break;
	}
	
	for(size_t i = 1; i < char_len; i++) {
		unsigned char u = bcast(s[1]);
		if((u >> 6) != 2) {
			*codepoint = 0;
			return 1;
		}
		
		*codepoint <<= 6;
		*codepoint |= (u & 0x3F);
	}
	
	return char_len;
}
