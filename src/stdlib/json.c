#include "json.h"

#include <ctype.h>

typedef struct json_reader {
	const char *at, *end;
	size_t line, column;
	
	char *str_buff;
	size_t str_buff_len, str_buff_cap;
} json_reader;

static char peekc(json_reader *r) {
	if(r->at == r->end) return '\0';
	
	return *r->at;
}

static char popc(json_reader *r) {
	if(r->at == r->end) return '\0';
	
	char c = *r->at;
	if(c == '\n') {
		r->line++;
		r->column = 0;
	} else {
		r->column++;
	}
	
	r->at++;
	return c;
}

static void skip_whitespace(json_reader *r) {
	while(isspace(peekc(r))) popc(r);
}

static int appendc(esh_state *esh, json_reader *r, char c) {
	if(r->str_buff_len == r->str_buff_cap) {
		size_t new_cap = r->str_buff_cap * 3 / 2 + 1;
		char *new_buff = esh_realloc(esh, r->str_buff, sizeof(char) * new_cap);
		if(!new_buff) {
			esh_err_printf(esh, "JSON Parser: Unable to grow string buffer (out of memory?)");
			return 1;
		}
		
		r->str_buff = new_buff;
		r->str_buff_cap = new_cap;
	}
	
	r->str_buff[r->str_buff_len++] = c;
	
	return 0;
}

static int appends(esh_state *esh, json_reader *r, const char *str, size_t len) {
	while(len--) {
		if(appendc(esh, r, *str)) return 1;
		str++;
	}
	return 0;
}

int parse_string(esh_state *esh, json_reader *r) {
	char c = popc(r);
	if(c != '"') {
		esh_err_printf(esh, "JSON Parser: Expected string at line %zu, char %zu", r->line, r->column);
		return 1;
	}
	
	r->str_buff_len = 0;
	bool prev_was_escape = false;
	while(true) {
		char c = popc(r);
		if(c == '\0' || c == '\n') {
			esh_err_printf(esh, "JSON Parser: Unterminated string at line %zu, char %zu", r->line, r->column);
			return 1;
		}
		
		if(prev_was_escape) {
			prev_was_escape = false;
			switch(c) {
				case '"':
				case '\\':
					if(appendc(esh, r, c)) return 1;
					break;
				
				case 'n':
					if(appendc(esh, r, '\n')) return 1;
					break;
				case 't':
					if(appendc(esh, r, '\t')) return 1;
					break;
				
				default:
					esh_err_printf(esh, "JSON Parser: Unrecognized escape character '\\%c at line %zu, char %zu", c, r->line, r->column);
					return 1;
			}
		} else if(c == '\\') {
			prev_was_escape = true;
		} else if(c == '"') {
			break;
		} else {
			if(appendc(esh, r, c)) return 1;
		}
	}
	
	return esh_new_string(esh, r->str_buff, r->str_buff_len);
}

static int parse_val(esh_state *esh, json_reader *r);

static int parse_object(esh_state *esh, json_reader *r) {
	char c = popc(r);
	if(c != '{') {
		esh_err_printf(esh, "JSON Parser: Expected object at line %zu, char %zu", r->line, r->column);
		return 1;
	}
	
	skip_whitespace(r);
	
	if(esh_object_of(esh, 0)) return 1;
	
	if(peekc(r) == '}') {
		popc(r);
		return 0;
	}
	
	while(true) {
		if(parse_string(esh, r)) return 1;
		skip_whitespace(r);
		if(popc(r) != ':') {
			esh_err_printf(esh, "JSON Parser: Expected ':' at line %zu, char %zu", r->line, r->column);
			return 1;
		}
		if(parse_val(esh, r)) return 1;
		
		if(esh_set(esh, -3, -2, -1)) return 1;
		esh_pop(esh, 2);
		
		if(peekc(r) == '}') {
			popc(r);
			break;
		}
		
		if(popc(r) != ',') {
			esh_err_printf(esh, "JSON Parser: Expected ',' at line %zu, char %zu", r->line, r->column);
			return 1;
		}
		skip_whitespace(r);
	}
	
	return 0;
}

static int expect_digits(esh_state *esh, json_reader *r) {
	size_t n = 0;
	while(true) {
		char c = peekc(r);
		if(!(c >= '0' && c <= '9')) {
			if(n == 0) {
				esh_err_printf(esh, "JSON Parser: Expected digit at line %zu, char %zu", r->line, r->column);
				return 1;
			}
			break;
		}
		n++;
		
		popc(r);
		if(appendc(esh, r, c)) return 1;
	}
	
	return 0;
}

static int parse_num(esh_state *esh, json_reader *r) {
	r->str_buff_len = 0;
	
	char c = popc(r);
	if(c == '-') {
		if(appendc(esh, r, c)) return 1;
		c = popc(r);
	}
	
	if(c == '0') {
		if(appendc(esh, r, c)) return 1;
	} else if(c >= '1' && c <= '9') {
		if(appendc(esh, r, c)) return 1;
		
		while(true) {
			c = peekc(r);
			if(!(c >= '0' && c <= '9')) break;
			popc(r);
			if(appendc(esh, r, c)) return 1;
		}
	} else {
		esh_err_printf(esh, "JSON Parser: Expected number at line %zu, char %zu", r->line, r->column);
		return 1;
	}
	
	if(peekc(r) == '.') {
		c = popc(r);
		if(appendc(esh, r, c)) return 1;
		if(expect_digits(esh, r)) return 1;
	}
	
	c = peekc(r);
	if(c == 'e' || c == 'E') {
		popc(r);
		if(appendc(esh, r, c)) return 1;
		
		c = peekc(r);
		if(c == '+' || c == '-') {
			popc(r);
			if(appendc(esh, r, c)) return 1;
		}
		
		if(expect_digits(esh, r)) return 1;
	}
	
	return esh_new_string(esh, r->str_buff, r->str_buff_len);
}

static int parse_array(esh_state *esh, json_reader *r) {
	char c = popc(r);
	if(c != '[') {
		esh_err_printf(esh, "JSON Parser: Expected array at line %zu, char %zu", r->line, r->column);
		return 1;
	}
	
	skip_whitespace(r);
	
	if(esh_object_of(esh, 0)) return 1;
	
	size_t n = 0;
	if(peekc(r) == ']') {
		popc(r);
		return 0;
	}
	
	while(true) {
		if(parse_val(esh, r)) return 1;
		if(esh_set_i(esh, -2, n, -1)) return 1;
		esh_pop(esh, 1);
		n++;
		
		char c = popc(r);
		if(c == ']') break;
		else if(c != ',') {
			esh_err_printf(esh, "JSON Parser: Expected ',' at line %zu, char %zu", r->line, r->column);
			return 1;
		}
	}
	
	return 0;
}

static int expect_word(esh_state *esh, json_reader *r, const char *s, size_t l, bool push) {
	r->str_buff_len = 0;
	while(l) {
		char c = popc(r);
		if(c != *s) {
			esh_err_printf(esh, "JSON Parser: Unexpected character '%c' at line %zu, char %zu", c, r->line, r->column);
			return 1;
		}
		if(appendc(esh, r, c)) return 1;
		l--;
		s++;
	}
	
	if(!push) return 0;
	
	return esh_new_string(esh, r->str_buff, r->str_buff_len);
}

static int parse_bool(esh_state *esh, json_reader *r) {
	char c = peekc(r);
	if(c == 'f') {
		return expect_word(esh, r, "false", 5, true); 
	} else if(c == 't') {
		return expect_word(esh, r, "true", 4, true);
	} else {
		esh_err_printf(esh, "JSON Parser: Expected boolean at line %zu, char %zu", r->line, r->column);
		return 1;
	}
}

static int parse_val(esh_state *esh, json_reader *r) {
	skip_whitespace(r);
	char c = peekc(r);
	
	int err = 0;
	if(c == '"') {
		err = parse_string(esh, r);
	} else if(c == '{') {
		err = parse_object(esh, r);
	} else if(c == '[') {
		err = parse_array(esh, r);
	} else if((c >= '0' && c <= '9') || c == '-') {
		err = parse_num(esh, r);
	} else if(c == 'f' || c == 't') {
		err = parse_bool(esh, r);
	} else if(c == 'n') {
		err = expect_word(esh, r, "null", 4, false);
		if(err) return 1;
		err = esh_push_null(esh);
	} else {
		esh_err_printf(esh, "JSON Parser: Unexpected character '%c' at line %zu, char %zu", c, r->line, r->column);
		err = 1;
	}
	
	if(!err) skip_whitespace(r);
	return err;
}

int parse_json(esh_state *esh, const char *json, size_t strlen) {
	esh_save_stack(esh);
	
	json_reader reader = { json, json + strlen, 1, 1, NULL, 0, 0 };
	int err = parse_val(esh, &reader);
	esh_free(esh, reader.str_buff);
	
	if(err) esh_restore_stack(esh);
	return err;
}

static int val_to_json(esh_state *esh, json_reader *r) {
	if(esh_is_null(esh, -1)) return appends(esh, r, "null", 4);
	
	size_t len;
	const char *str = esh_as_string(esh, -1, &len);
	if(str) {
		if(appendc(esh, r, '"')) return 1;
		while(len--) {
			switch(*str) {
				case '\n':
					if(appends(esh, r, "\\n", 2)) return 1;
					break;
				case '\t':
					if(appends(esh, r, "\\t", 2)) return 1;
					break;
				case '"':
					if(appends(esh, r, "\\\"", 2)) return 1;
					break;
				case '\\':
					if(appends(esh, r, "\\\\", 2)) return 1;
					break;
				
				default:
					if(appendc(esh, r, *str)) return 1;
			}
			str++;
		}
		if(appendc(esh, r, '"')) return 1;
		return 0;
	}
	
	if(esh_is_array(esh, -1)) {
		if(appendc(esh, r, '[')) return 1;
		size_t size = esh_object_len(esh, -1);
		for(size_t i = 0; i < size; i++) {
			if(i != 0) if(appends(esh, r, ", ", 2)) return 1;
			
			if(esh_index_i(esh, -1, i)) return 1;
			if(val_to_json(esh, r)) return 1;
			esh_pop(esh, 1);
		}
		if(appendc(esh, r, ']')) return 1;
		return 0;
	}
	
	if(appendc(esh, r, '{')) return 1;
	esh_iterator iter = esh_iter_begin(esh);
	while(true) {
		if(esh_iter_next(esh, -1, &iter)) return 1;
		if(iter.done) break;
		
		if(iter.step != 0) if(appends(esh, r, ", ", 2)) return 1;
		
		esh_swap(esh, -1, -2);
		if(val_to_json(esh, r)) return 1;
		esh_pop(esh, 1);
		if(appends(esh, r, ": ", 2)) return 1;
		if(val_to_json(esh, r)) return 1;
		esh_pop(esh, 1);
	}
	if(appendc(esh, r, '}')) return 1;
	
	return 0;
}

int to_json(esh_state *esh) {
	esh_save_stack(esh);
	
	json_reader reader = { NULL, 0, 1, 1, NULL, 0, 0 };
	
	int err = val_to_json(esh, &reader);
	if(err) { 
		esh_restore_stack(esh);
	} else {
		err = esh_new_string(esh, reader.str_buff, reader.str_buff_len);
	}
	
	
	esh_free(esh, reader.str_buff);
	return err;
}
