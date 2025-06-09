#include "pattern.h"

#include <assert.h>
#include <limits.h>
#include <ctype.h>

#define RECURSION_LIMIT 16

static unsigned rec = 0;
static size_t n_captures = 0;

#define MAX_CAPTURES 16
static size_t captures[MAX_CAPTURES];

static bool is_modifier(char c) {
	switch(c) {
		case '*':
		case '?':
		case '+':
		case '!':
			return true;
		
		default:
			return false;
	}
}

static void modifier_ranges(char c, size_t *min, size_t *max) {
	switch(c) {
		case '*':
			*min = 0;
			*max = SIZE_MAX;
			break;
		
		case '?':
			*min = 0;
			*max = 1;
			break;
		
		case '+':
			*min = 1;
			*max = SIZE_MAX;
			break;
		
		case '!':
			*min = 1;
			*max = 1;
			break;
		
		default:
			assert(false);
	}
}

static bool match_char_class(char class, char c) {
	switch(class) {
		case 0:
			return true;
		
		case 's':
			return isspace(c);
		
		case 'w':
			return !isspace(c);
		
		case 'u':
			return isupper(c);
		
		case 'l':
			return islower(c);
		
		case 'a':
			return isalpha(c);
		
		case 'c':
			return isalpha(c) || c == '_';
		
		case 'h':
			return isxdigit(c);
		
		case 'd':
			return isdigit(c);
		
		default:
			return c == class;
	}
}

static int pattern_match(esh_state *esh, const char *str, size_t strlen, size_t at, const char *pattern, const char *pattern_end, bool match_entire) {
	assert(at <= strlen);
	if(rec == RECURSION_LIMIT) {
		esh_err_printf(esh, "Pattern recursion limit reached");
		return -1;
	}
	
	rec++;

	while(true) {
		if(pattern == pattern_end) {
			if(!match_entire || at == strlen) goto MATCH;
			else goto NO_MATCH;
		}
		
		char c = *(pattern++);
		if(is_modifier(c) || c == '%') {
			char modifier = '+';
			if(is_modifier(c)) {
				modifier = c;
				c = 0;
			}
			
			if(c == '%' && pattern != pattern_end) {
				c = *(pattern++);
				if(pattern != pattern_end) {
					char n = *pattern;
					if(is_modifier(n)) {
						modifier = n;
						pattern++;
					}
				}
			}
			
			size_t min, max;
			modifier_ranges(modifier, &min, &max);
			size_t i = 0;
			for(; i < max && i < strlen - at; i++) {
				if(!match_char_class(c, str[at + i])) break;
			}
			
			for(; i >= min; i--) {
				size_t save_n_captures = n_captures;
				int res = pattern_match(esh, str, strlen, at + i, pattern, pattern_end, match_entire);
				if(res == -1) return -1;
				else if(res) goto MATCH;
				
				n_captures = save_n_captures;
				if(i == 0) break;
			}
			
			goto NO_MATCH;
		} else if(c == '(') {
			if(n_captures % 2 == 0) {
				if(n_captures == MAX_CAPTURES) {
					esh_err_printf(esh, "Pattern capture limit reached");
					return -1;
				}
				
				captures[n_captures++] = at;
			}
		} else if(c == ')') {
			if(n_captures % 2 == 1) {
				if(n_captures == MAX_CAPTURES) {
					esh_err_printf(esh, "Pattern capture limit reached");
					return -1;
				}
				
				captures[n_captures++] = at;
			}
		} else {
			if(at == strlen) goto NO_MATCH;
			if(str[at] != c) goto NO_MATCH;
			at++;
		}
	}
	
	NO_MATCH:
	rec--;
	return 0;
	
	MATCH:
	rec--;
	return 1;
}

int esh_pattern_match(esh_state *esh, const char *str, size_t strlen, const char *pattern, size_t pattern_len, bool match_entire) {
	rec = 0;
	n_captures = 0;
	
	esh_save_stack(esh);
	int res = pattern_match(esh, str, strlen, 0, pattern, pattern + pattern_len, match_entire);
	if(res == -1) {
		esh_restore_stack(esh);
		return -1;
	}
	
	return res;
}

size_t *esh_pattern_match_captures(size_t *out_n_captures) {
	*out_n_captures = n_captures;
	return captures;
}

int esh_pattern_escape(esh_state *esh, const char *str, size_t strlen) {
	esh_str_buff_begin(esh);
	for(size_t i = 0; i < strlen; i++) {
		switch(str[i]) {
			case '%':
			case '+':
			case '?':
			case '!':
			case '*':
			case ')':
			case '(':
				if(esh_str_buff_appendc(esh, '%')) return 1;
				if(esh_str_buff_appendc(esh, str[i])) return 1;
				if(esh_str_buff_appendc(esh, '!')) return 1;
				break;
			
			default:
				if(esh_str_buff_appendc(esh, str[i])) return 1;
				break;
		}
	}
	
	size_t res_len;
	const char *res = esh_str_buff(esh, &res_len);
	if(esh_new_string(esh, res, res_len)) return 1;
	
	return 0;
}
