#include "esh_stdlib.h"

#include "esh.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "stdlib/utf8.h"

int esh_stdlib_print_val(esh_state *esh, long long i, FILE *f) {
	if(esh_is_null(esh, i)) {
		fputs("Null", f);
		return 0;
	}
	
	const char *str = esh_as_string(esh, i, NULL);
	if(str) {
		fputs(str, f);
	} else {
		putc('{', f);
		if(esh_is_array(esh, i)) {
			for(size_t n = 0; ; n++) {
				if(esh_index_i(esh, i, n)) return 1;
				if(esh_is_null(esh, -1)) {
					esh_pop(esh, 1);
					break;
				}
				if(n != 0) fputs(", ", f);
				if(esh_stdlib_print_val(esh, -1, f)) return 1;
				esh_pop(esh, 1);
			}
		} else {
			for(esh_iterator j = esh_iter_begin(esh);;) {
				if(esh_iter_next(esh, i, &j)) return 1;
				if(j.done) break;
				if(j.step != 0) fputs(", ", f);
				
				const char *key = esh_as_string(esh, -2, NULL);
				assert(key != NULL);
				fputs(key, f);
				fputs(" = ", f);
				
				if(esh_stdlib_print_val(esh, -1, f)) return 1;
				
				esh_pop(esh, 2);
			}
		}
		putc('}', f);
	}
	
	return 0;
}

/*@
	print ...
	...         any
	@returns    null
	
	Prints the given values to stdout, separated by spaces.
	Prints a newline at the end.
*/
static esh_fn_result print(esh_state *esh, size_t n_args, size_t i) {
	(void) i, (void) n_args;
	
	for(size_t i = 0; i < n_args; i++) {
		if(i != 0) putc(' ', stdout);
		if(esh_stdlib_print_val(esh, i, stdout)) return ESH_FN_ERR;
	}
	putc('\n', stdout);
	
	if(esh_push_null(esh)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

/*@
	forevery ... callback
	...          any
	callback     any -> any
	@returns     any
	
	Calls the given callback with each value in ..., one at a time.
	If the callback returns a non-null value, then the function returns early without iterating over the rest of the values.
	The return value is the non-null return value of the callback, or null if all values were iterated through.
	
	--- Examples
		# Prints 1 2 3
		forevery 1 2 3 with i do print $i end
		
		# x will be equal to 2
		x = forevery 1 2 3 with i do
			if $i == 2 then return $i end
		end
	---
*/
static esh_fn_result forevery(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args >= 1);
	if(i == n_args - 1) {
		return ESH_FN_RETURN(1);
	}
	
	if(i != 0) {
		if(!esh_is_null(esh, -1)) return ESH_FN_RETURN(1);
		esh_pop(esh, 1);
	}
	
	if(esh_dup(esh, -1)) return ESH_FN_ERR;
	if(esh_dup(esh, i)) return ESH_FN_ERR;
	return ESH_FN_CALL(1, 1);
}

/*@
	for from to by? callback
	from        int
	to          int
	by?         int
	callback    int -> any
	@returns    any
	
	Iterates from $from (inclusive) until $to (exclusive), incrementing with $by at each step; or by 1 in $by is not given. For each step, $callback is called with the given integer value.
	If $callback returns a non-null value, the loop is stopped early.
	The return value is the non-null value returned by $callback, or null if all values were iterated through.
	
	--- Examples
		# Prints 5, 6, 7
		for 5 8 $print
		
		# Prints 5 7 9
		for 5 11 2 $print
	---
*/
static esh_fn_result for_loop(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 3 || n_args == 4);
	
	struct { long long from, to, counter, by; } *locals = esh_locals(esh, sizeof(*locals), NULL);
	if(!locals) return ESH_FN_ERR;
	
	if(i == 0) {
		if(esh_as_int(esh, 0, &locals->from)) return ESH_FN_ERR;
		if(esh_as_int(esh, 1, &locals->to)) return ESH_FN_ERR;
		locals->counter = locals->from;
		if(n_args == 4) {
			if(esh_as_int(esh, 2, &locals->by)) return ESH_FN_ERR;
		} else {
			locals->by = 1;
		}
		
		if(locals->from > locals->to) {
			esh_err_printf(esh, "For loop: from value (%lli) is greater than to value (%lli)", locals->from, locals->to);
			return ESH_FN_ERR;
		}
	} else {
		locals->counter += locals->by;
		if(!esh_is_null(esh, -1)) return ESH_FN_RETURN(1);
		esh_pop(esh, 1);
	}
	
	if(locals->counter >= locals->to) {
		if(esh_push_null(esh)) return ESH_FN_ERR;
		return ESH_FN_RETURN(1);
	}
	
	if(esh_dup(esh, -1)) return ESH_FN_ERR;
	if(esh_push_int(esh, locals->counter)) return ESH_FN_ERR;
	return ESH_FN_CALL(1, 1);
}

static esh_fn_result gc(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 1);
	
	long long gc_sc;
	if(esh_as_int(esh, 0, &gc_sc)) return ESH_FN_ERR;
	if(gc_sc < 0) {
		esh_err_printf(esh, "GC step count must be positive");
		return ESH_FN_ERR;
	}
	
	esh_gc(esh, gc_sc);
	
	if(esh_push_null(esh)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

static esh_fn_result gc_conf(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 2);
	
	long long freq, step_size;
	if(esh_as_int(esh, 0, &freq)) return ESH_FN_ERR;
	if(esh_as_int(esh, 1, &step_size)) return ESH_FN_ERR;
	if(step_size < 0) {
		esh_err_printf(esh, "GC step count must be positive");
		return ESH_FN_ERR;
	}
	
	esh_gc_conf(esh, freq, step_size);
	
	if(esh_push_null(esh)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

/*@
	foreach-in obj callback
	obj         any
	callback    string any -> any
	@returns    any
	
	Calls $callback with every key-value entry in the object $obj.
	The order of iteration is unspecified.
	If $callback returns a non-null value the loop is stopped early.
	The return values is the non-null value returned by $callback, or null if all entries were iterated through.
	
	--- Examples
		foreach $obj with key value do print $key is $value end
	---
*/
static esh_fn_result foreach_in(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 2);
	
	struct {
		esh_iterator iter;
	} *locals = esh_locals(esh, sizeof(*locals), NULL);
	if(!locals) return ESH_FN_ERR;
	
	if(i == 0) locals->iter = esh_iter_begin(esh);
	else {
		if(!esh_is_null(esh, -1)) return ESH_FN_RETURN(1);
		esh_pop(esh, 1);
	}
	
	while(true) {
		if(esh_dup(esh, 1)) return ESH_FN_ERR;
		if(esh_iter_next(esh, 0, &locals->iter)) return ESH_FN_ERR;
		if(locals->iter.done) break;
		
		return ESH_FN_CALL(2, 1);
	}
	
	if(esh_push_null(esh)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

/*@
	sizeof x
	x           any
	@returns    int
	
	If $x is a string, the number of bytes in the string is returned.
	Otherwise the number of key-value pairs in $x is returned.
	
	--- Examples
		sizeof "foo" # Returns 3
		
		sizeof { a, b, c, d } # Returns 4
		
		sizeof { x = 1, y = 2 } # Returns 2
	---
*/
static esh_fn_result sizeof_fn(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1);
	assert(i == 0);
	
	size_t size;
	
	size_t len;
	if(esh_as_string(esh, 0, &len)) {
		size = len;
	} else {
		size = esh_object_len(esh, 0);
	}
	
	if(esh_push_int(esh, size)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

/*
	assert condition
	condition    any
	@returns     null
	
	Throws an error if $condition is falsy
*/
static esh_fn_result assert_fn(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1);
	assert(i == 0);
	
	if(!esh_as_bool(esh, 0)) {
		esh_err_printf(esh, "Assertion failed");
		return ESH_FN_ERR;
	}
	
	if(esh_push_null(esh)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

/*
	imap obj callback
	obj         object of T
	callback    T -> T
	@returns    object of T
	
	Applies the function $callback to every value in the given $obj and returns a new object as the result.
	
	--- Examples
		map { 1, 2, 3 } with x ($x * 2) # Gives { 2, 4, 6 }
		
		map { x = 1, y = 2, z = 3 } with x ($x * 2) # Gives { x = 2, y = 4, z = 6 }
	---
*/
static esh_fn_result imap(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 2);
	
	struct {
		esh_iterator iter;
	} *locals = esh_locals(esh, sizeof(*locals), NULL);
	if(!locals) return ESH_FN_ERR;
	
	if(i == 0) {
		locals->iter = esh_iter_begin(esh);
		if(esh_object_of(esh, 0)) return ESH_FN_ERR;
	} else {
		if(esh_set(esh, -3, -2, -1)) return ESH_FN_ERR;
		esh_pop(esh, 2);
	}
	
	if(esh_iter_next(esh, 0, &locals->iter)) return ESH_FN_ERR;
	
	if(!locals->iter.done) {
		if(esh_dup(esh, 1)) return ESH_FN_ERR;
		esh_swap(esh, -1, -2);
		
		return ESH_FN_CALL(1, 1);
	}
	
	return ESH_FN_RETURN(1);
}

/*
	kfilter obj callback
	obj         object of T
	callback    string, T -> bool
	@returns    object of T
	
	Returns a new object that contains only the key value pairs such that $callback returns true.
	Note that for arrays, this may produce a sparse array where some values will be missing.
	To filter arrays into arrays, see [ifilter].
	
	--- Examples
		kfilter { a = 5, b = 2, c = 9 } where x ($x > 4) # Gives { a = 5, c = 9 }
		
		kfilter { 5, 2, 9 } where x ($x > 4) # Gives { 0 = 5, 2 = 9 }
	---
*/
static esh_fn_result kfilter(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 2);
	
	struct {
		esh_iterator iter;
	} *locals = esh_locals(esh, sizeof(*locals), NULL);
	if(!locals) return ESH_FN_ERR;
	
	if(i == 0) {
		locals->iter = esh_iter_begin(esh);
		if(esh_object_of(esh, 0)) return ESH_FN_ERR;
	} else {
		if(esh_as_bool(esh, -1)) {
			if(esh_set(esh, 2, -3, -2)) return ESH_FN_ERR;
		}
		esh_pop(esh, 3);
	}
	
	if(esh_iter_next(esh, 0, &locals->iter)) return ESH_FN_ERR;
	
	if(!locals->iter.done) {
		if(esh_dup(esh, 1)) return ESH_FN_ERR; // Duplicate callback
		if(esh_dup(esh, -3)) return ESH_FN_ERR; // Duplicate key
		if(esh_dup(esh, -3)) return ESH_FN_ERR; // Duplicate value
		
		return ESH_FN_CALL(2, 1);
	}
	
	return ESH_FN_RETURN(1);
}

/*
	ifilter a callback
	a           array of T
	callback    T -> bool
	@returns    array of T
	
	Returns a new array that contains only the values such that $callback returns true.
	This function will iterate over $a using consecutive integer keys, starting at 0.
	It will stop at the first null value.
	
	--- Examples
		ifilter { 5, 2, 9 } where x ($x > 4) # Gives { 5, 9 }
	---
*/
static esh_fn_result ifilter(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 2);
	
	struct {
		size_t dst_counter;
	} *locals = esh_locals(esh, sizeof(*locals), NULL);
	if(!locals) return ESH_FN_ERR;
	
	if(i == 0) {
		locals->dst_counter = 0;
		if(esh_object_of(esh, 0)) return ESH_FN_ERR;
	} else {
		if(esh_as_bool(esh, -1)) {
			if(esh_set_i(esh, 2, locals->dst_counter++, -2)) return ESH_FN_ERR;
		}
		esh_pop(esh, 2); 
	}
	
	if(esh_index_i(esh, 0, i)) return ESH_FN_ERR;
	if(esh_is_null(esh, -1)) {
		esh_pop(esh, 1);
		return ESH_FN_RETURN(1);
	}
	
	if(esh_dup(esh, 1)) return ESH_FN_ERR; // Duplicate callback
	if(esh_dup(esh, -2)) return ESH_FN_ERR; // Duplicate value
	return ESH_FN_CALL(1, 1);
}

static esh_fn_result fndump(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1);
	assert(i == 0);
	
	if(esh_fndump(esh, stdout)) return ESH_FN_ERR;
	
	if(esh_push_null(esh)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

#include "stdlib/json.h"

/*@
	parse-json str
	str         string
	@returns    any
	
	Parses a string as JSON and returns the result.
*/
static esh_fn_result parse_json_fn(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1);
	assert(i == 0);
	
	size_t len;
	const char *json = esh_as_string(esh, 0, &len);
	if(!json) {
		esh_err_printf(esh, "Expected string as argument to parse-json");
		return ESH_FN_ERR;
	}
	
	int err = parse_json(esh, json, len);
	if(err) return ESH_FN_ERR;
	
	return ESH_FN_RETURN(1);
}

/*@
	to-json x
	x           any
	@returns    string
	
	Serializes a value into JSON.
	Objects are serialized into arrays if they contain only consecutive integer keys starting at 0, otherwise they are serialized into objects.
	Integers and booleans are serialized into strings.
*/
static esh_fn_result to_json_fn(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1);
	assert(i == 0);
	
	if(to_json(esh)) return ESH_FN_ERR;
	
	return ESH_FN_RETURN(1);
}

/*@
	fori a callback
	a           array of T
	callback    int, T -> V
	@returns    V | null
	
	Iterates over an array $a in order, calling $callback for each index-value pair.
	If $callback returns a non-null value, the loop is stopped early.
	The return value is the non-null value returned by $callback, or null if the all values where iterated over.
	The function stops iteration at the first null value.
	
	--- Examples
		fori { 1, 2, 3 } $print # Prints 1, 2, 3
		fori { 0 = x, 1 = y, 3 = z } $print # Prints x, y
	---
*/
static esh_fn_result fori(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 2);
	
	struct { size_t index; } *locals = esh_locals(esh, sizeof(*locals), NULL);
	if(!locals) return ESH_FN_ERR;
	
	if(i == 0) {
		locals->index = 0;
	} else {
		if(!esh_is_null(esh, -1)) return ESH_FN_RETURN(1);
		esh_pop(esh, 1);
	}
	
	if(esh_dup(esh, 1)) return ESH_FN_ERR;
	if(esh_push_int(esh, locals->index)) return ESH_FN_ERR;
	if(esh_index_i(esh, 0, locals->index++)) return ESH_FN_ERR;
	if(esh_is_null(esh, -1)) return ESH_FN_RETURN(1);
	return ESH_FN_CALL(2, 1);
}

static void write_fn_free(esh_state *esh, void *p) {
	(void) esh;
	fclose(*(FILE **) p);
}

/*@
	write str path
	str         string | coroutine of string | char-stream
	path        string
	@returns    null
	
	Writes the contents of the string $str into the file at $path.
*/
static esh_fn_result write_fn(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 2);
	
	if(i != 0) {
		FILE **f = esh_locals(esh, 0, NULL);
		if(esh_is_null(esh, -1)) return ESH_FN_RETURN(1);
		size_t len;
		const char *str = esh_as_string(esh, -1, &len);
		if(!str) return ESH_FN_ERR;
		if(fwrite(str, sizeof(char), len, *f) < len) {
			esh_err_printf(esh, "Unable to write to '%s': %s", esh_as_string(esh, 1, NULL), strerror(errno));
			return ESH_FN_ERR;
		}
		
		if(esh_dup(esh, 0)) return ESH_FN_ERR;
		return ESH_FN_NEXT(0, 1);
	}
	
	const char *path = esh_as_string(esh, 1, NULL);
	if(!path) {
		esh_err_printf(esh, "Write path must be string");
		return ESH_FN_ERR;
	}
	
	FILE *f = fopen(path, "w");
	if(!f) {
		esh_err_printf(esh, "Unable to open '%s'", path);
		return ESH_FN_ERR;
	}
	
	size_t len;
	const char *str = esh_as_string(esh, 0, &len);
	if(str) {
		size_t n = fwrite(str, sizeof(char), len, f);
		if(n < len) {
			esh_err_printf(esh, "Unable to write to '%s': %s", path, strerror(errno));
			return ESH_FN_ERR;
		}		
		fclose(f);
	
		if(esh_push_null(esh)) return ESH_FN_ERR;
		return ESH_FN_RETURN(1);
	} else {
		FILE **local_f = esh_locals(esh, sizeof(FILE *), write_fn_free);
		*local_f = f;
		
		if(esh_dup(esh, 0)) return ESH_FN_ERR;
		return ESH_FN_NEXT_S(0, 1);
	}
}

/*@
	isplit str delimiter?
	str          string
	delimiter?   string
	@returns     array of string
	
	Splits the given string $str at each occurence of $delimiter.
	If $delimiter is not given, the string is split by whitespace.
	The return value is an array of all resulting string segments.
	
	--- Examples
		isplit "a, b, c" "," # Returns { "a", " b", " c" }
		
		isplit "a    b c" # Returns { "a", "b", "c" }
	---
*/
static esh_fn_result isplit(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1 || n_args == 2);
	assert(i == 0);
	
	size_t str_len;
	const char *str = esh_as_string(esh, 0, &str_len);
	if(!str) {
		esh_err_printf(esh, "Attempting to split non-string value");
		return ESH_FN_ERR;
	}
	
	size_t pattern_len;
	const char *pattern = NULL;
	if(n_args == 2) {
		pattern = esh_as_string(esh, 1, &pattern_len);
		if(!pattern) {
			esh_err_printf(esh, "Attempting to use non-string value as split pattern");
			return ESH_FN_ERR;
		}
	}
	if(pattern && pattern_len == 0) {
		esh_err_printf(esh, "Pattern cannot be empty string");
		return ESH_FN_ERR;
	}
	size_t n_strs = 0;
	
	if(esh_object_of(esh, 0)) return ESH_FN_ERR;
	
	size_t begin = 0;
	for(size_t i = 0; i < str_len; i++) {
		size_t skip_chars = 0;
		if(!pattern) {
			for(size_t j = i; j < str_len; j++) {
				if(!isspace(str[j])) break;
				skip_chars++;
			}
		} else {
			skip_chars = pattern_len;
			for(size_t j = 0; j < pattern_len; j++) {
				if(i + j >= str_len) { skip_chars = 0; break; }
				if(str[i + j] != pattern[j]) { skip_chars = 0; break; }
			}
		}
		
		if(skip_chars) {
			if(esh_new_string(esh, str + begin, i - begin)) return ESH_FN_ERR;
			if(esh_set_i(esh, -2, n_strs, -1)) return ESH_FN_ERR;
			esh_pop(esh, 1);
			n_strs++;
			
			i += skip_chars - 1;
			begin = i + 1;
		}
	}
	
	if(esh_new_string(esh, str + begin, str_len - begin)) return ESH_FN_ERR;
	if(esh_set_i(esh, -2, n_strs, -1)) return ESH_FN_ERR;
	esh_pop(esh, 1);
	//n_strs++;
	
	//if(esh_new_array(esh, n_strs)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

/*@
	join a separator
	a            array of string
	separator    string
	@returns     string
	
	Joins an array of strings, $a, into a single string separated by $separator.
	Returns the resulting string.
	
	--- Examples
		join { a, b, c } "-" # Gives "a-b-c"
	---
*/
static esh_fn_result join(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 1 || n_args == 2);
	
	size_t join_with_len;
	const char *join_with = NULL;
	if(n_args == 2) {
		join_with = esh_as_string(esh, 1, &join_with_len);
		if(!join_with) {
			esh_err_printf(esh, "Attempting to join using non-string separator");
			return ESH_FN_ERR;
		}
	}
	
	esh_str_buff_begin(esh);
	
	for(size_t i = 0; ; i++) {		
		if(esh_index_i(esh, 0, i)) return ESH_FN_ERR;
		if(esh_is_null(esh, -1)) break;
		
		if(i != 0 && join_with) if(esh_str_buff_appends(esh, join_with, join_with_len)) return ESH_FN_ERR;
		
		size_t len;
		const char *str = esh_as_string(esh, -1, &len);
		if(!str) {
			esh_err_printf(esh, "Attempting to join non-string value");
			return ESH_FN_ERR;
		}
		if(esh_str_buff_appends(esh, str, len)) return ESH_FN_ERR;
		
		esh_pop(esh, 1);
	}
	
	size_t len;
	char *str = esh_str_buff(esh, &len);
	
	if(esh_new_string(esh, str, len)) return ESH_FN_ERR;
	
	return ESH_FN_RETURN(1);
}

/*@
	include path
	path        string
	@returns    null
	
	Executes the script at the given $path.
*/
static esh_fn_result include(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0 || i == 1);
	assert(n_args == 1);
	
	if(i == 0) {
		size_t path_len;
		const char *path = esh_as_string(esh, 0, &path_len);
		if(!path) {
			esh_err_printf(esh, "Attempting to pass non-string value as path");
			return ESH_FN_ERR;
		}
		
		if(esh_loadf(esh, path)) return ESH_FN_ERR;
		return ESH_FN_CALL(0, 1);
	} else {
		return ESH_FN_RETURN(1);
	}
}

/*@
	getenv name
	name         string
	@returns     string | null
	
	Returns the value of the environment variable with the given $name, or null if it does not exist.
*/
static esh_fn_result getenv_fn(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 1);
	
	const char *varname = esh_as_string(esh, 0, NULL);
	if(!varname) {
		esh_err_printf(esh, "Cannot use non-string value as environment variable name");
		return ESH_FN_ERR;
	}
	
	char *var = getenv(varname);
	if(var == NULL) {
		if(esh_push_null(esh)) return ESH_FN_ERR;
		return ESH_FN_RETURN(1);
	}
	
	if(esh_new_string(esh, var, strlen(var))) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

/*@
	beginswith str prefix
	str         string
	prefix      string
	@returns    bool
	
	Returns true if the given $str begins with the string $prefix, otherwise false.
	
	--- Examples
		beginswith "abc" "ab" # true
		
		beginswith "abc" bc" # false
	---
*/
static esh_fn_result beginswith(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 2);
	assert(i == 0);
	
	size_t str_len;
	const char *str = esh_as_string(esh, 0, &str_len);
	if(!str) {
		esh_err_printf(esh, "First argument must be string");
		return ESH_FN_ERR;
	}
	
	size_t prefix_len;
	const char *prefix = esh_as_string(esh, 1, &prefix_len);
	if(!prefix) {
		esh_err_printf(esh, "Second argument must be string");
		return ESH_FN_ERR;
	}
	
	bool result;
	if(prefix_len > str_len) {
		result = false;
		goto END;
	}
	
	result = true;
	while(prefix_len--) {
		if(*str != *prefix) {
			result = false;
			break;
		}
		str++;
		prefix++;
	}
	
	END:
	if(esh_push_bool(esh, result)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

#include "stdlib/pattern.h"

/*@
	match str pattern
	str         string
	pattern     string
	@returns    array of string | null
	
	Performs string pattern matching on the given string $str using $pattern, returning an array of captures, or null the string did not match.
	
	--- Examples
		match "Hello world!" '(*)%s(*)' # Gives { "Hello", "world!" }
		
		match "Helloworld!" '(*)%s(*)' # Gives null
	---
	
	The following special constructs are used in pattern matching:
	(...) - Captures
	%s    - One or more spaces
	%w    - One or more non-space characters
	%u    - One or more uppercase characters (A-Z)
	%l    - One or more lowercase characters (a-z)
	%a    - One or more alphabetic characters (A-Z, a-z)
	%h    - One or more hex digits (A-F, a-f, 0-9)
	%d    - One or more decimal digits (0-9)
	%%    - One or more '%' characters
	
	The special characters beginning with % can be suffixed with any of the following characters to modify how many must be matched
	+    - One or more (the default)
	*    - Zero or more
	!    - Exactly one
	?    - Zero or one
	--- Examples
		'%s*' # Match zero or more spaces
	---
	
	Futhermore, the modifiers can be used by themselves, in which case they will match that number of *any* characters (bytes).
	--- Examples
		'*.txt' # Match any string has zero or more characters followed by .txt
	---
*/
static esh_fn_result match(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 2);
	assert(i == 0);
	
	size_t str_len;
	const char *str = esh_as_string(esh, 0, &str_len);
	if(!str) {
		esh_err_printf(esh, "First argument must be string");
		return ESH_FN_ERR;
	}
	
	size_t pattern_len;
	const char *pattern = esh_as_string(esh, 1, &pattern_len);
	if(!pattern) {
		esh_err_printf(esh, "Second argument must be string");
		return ESH_FN_ERR;
	}
	
	int res = esh_pattern_match(esh, str, str_len, pattern, pattern_len, true);
	if(res == -1) return ESH_FN_ERR;
	
	if(res) {
		size_t n_captures;
		size_t *captures = esh_pattern_match_captures(&n_captures);
		if(esh_object_of(esh, 0)) return ESH_FN_ERR;
		
		for(size_t i = 0; i < n_captures / 2; i++) {
			size_t from = captures[i * 2], to = captures[i * 2 + 1];
			if(esh_new_string(esh, str + from, to - from)) return ESH_FN_ERR;
			if(esh_set_i(esh, -2, i, -1)) return ESH_FN_ERR;
			esh_pop(esh, 1);
		}
	} else {
		if(esh_push_null(esh)) return ESH_FN_ERR;
	}
	
	return ESH_FN_RETURN(1);
}

/*@
	slice a from to
	a           array of T
	from        int
	to          int
	@returns    array of T
	
	Creates a slice of the given array $a starting at index $from (inclusive) and ending at index $to (exclusive).
	Both $to and $from may be negative, in which case they specify offsets from the end of the array (which is calculated using [sizeof]).
	If either index is out of bounds, the resulting array will be assigned null at those indicies (e.g those indicies will be empty).
	If the array is sparse (e.g contains null values) then these will also be included in the result array.
	Returns the resulting array.
*/
static esh_fn_result slice(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 3);
	assert(i == 0);
	
	size_t len = esh_object_len(esh, 0);
	
	long long from;
	if(esh_as_int(esh, 1, &from)) return ESH_FN_ERR;
	while(from < 0) from += len;
	
	long long to;
	if(esh_as_int(esh, 2, &to)) return ESH_FN_ERR;
	while(to < 0) to += len;
	
	if(esh_object_of(esh, 0)) return ESH_FN_ERR;
	
	for(long long i = from; i < to; i++) {
		if(esh_index_i(esh, 0, i)) return ESH_FN_ERR;
		if(esh_set_i(esh, -2, i - from, -1)) return ESH_FN_ERR;
		esh_pop(esh, 1);
	}
	
	return ESH_FN_RETURN(1);
}

static esh_fn_result union_fn(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 2);
	assert(i == 0);
	
	if(esh_object_of(esh, 0)) return ESH_FN_ERR;
	
	for(esh_iterator i = esh_iter_begin(esh);;) {
		if(esh_iter_next(esh, 0, &i)) return ESH_FN_ERR;
		
		if(i.done) break;
		
		if(esh_set(esh, 2, -2, -1)) return ESH_FN_ERR;
		esh_pop(esh, 2);
	}
	
	for(esh_iterator i = esh_iter_begin(esh);;) {
		if(esh_iter_next(esh, 1, &i)) return ESH_FN_ERR;
		
		if(i.done) break;
		
		if(esh_set(esh, 2, -2, -1)) return ESH_FN_ERR;
		esh_pop(esh, 2);
	}
	
	return ESH_FN_RETURN(1);
}


static esh_fn_result intersection(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 2);
	assert(i == 0);
	
	if(esh_object_of(esh, 0)) return ESH_FN_ERR;
	
	for(esh_iterator i = esh_iter_begin(esh);;) {
		if(esh_iter_next(esh, 0, &i)) return ESH_FN_ERR;
		
		if(i.done) break;
		
		if(esh_index(esh, 1, -2)) return ESH_FN_ERR;
		if(!esh_is_null(esh, -1)) {
			if(esh_set(esh, 2, -3, -1)) return ESH_FN_ERR;
		}
		esh_pop(esh, 3);
	}
	
	return ESH_FN_RETURN(1);
}

#include <time.h>
#include "stdlib/libtime.h"

static esh_fn_result time_fn(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 0);
	assert(i == 0);
	time_t t = time(NULL);
	if(esh_push_int(esh, t)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

static esh_fn_result localtime_fn(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 0 || n_args == 1);
	assert(i == 0);
	
	time_t t;
	if(n_args == 1) {
		long long i;
		if(esh_as_int(esh, 0, &i)) return ESH_FN_ERR;
		t = i;
	} else {
		t = time(NULL);
	}
	
	struct tm *tm = localtime(&t);
	if(tm == NULL) {
		esh_err_printf(esh, "Unable to convert time: %s", strerror(errno));
		return ESH_FN_ERR;
	}
	
	iso_time itime;
	tm_to_iso_time(tm, &itime, true);
	
	if(iso_time_to_string(esh, &itime)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

static esh_fn_result gmtime_fn(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 0 || n_args == 1);
	assert(i == 0);
	
	time_t t;
	if(n_args == 1) {
		long long i;
		if(esh_as_int(esh, 0, &i)) return ESH_FN_ERR;
		t = i;
	} else {
		t = time(NULL);
	}
	
	struct tm *tm = gmtime(&t);
	if(!tm) {
		esh_err_printf(esh, "Unable to convert time: %s", strerror(errno));
		return ESH_FN_ERR;
	}
	
	iso_time itime;
	tm_to_iso_time(tm, &itime, false);
	
	if(iso_time_to_string(esh, &itime)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

typedef enum { DATEK_UNKNOWN = 0, DATEK_SECONDS, DATEK_MINUTES, DATEK_HOURS, DATEK_DAYS, DATEK_WEEKS, DATEK_MONTHS, DATEK_YEARS } date_keyword;

static bool match_date_keyword_rest(const char *str, const char *match) {
	while(true) {
		if(*str == '\0' && *match == '\0') return true;
		
		if(*str == *match) {
			str++;
			match++;
			continue;
		} else if(*match == '\0') {
			if(*str == 's' && *(str + 1) == '\0') return true;
		}
		return false;
	}
	return true;
}

static date_keyword match_date_keyword(const char *str) {
	switch(*(str++)) {
		case 's':
			if(match_date_keyword_rest(str, "econd")) return DATEK_SECONDS;
			return DATEK_UNKNOWN;
		
		case 'h':
			if(match_date_keyword_rest(str, "our")) return DATEK_HOURS;
			return DATEK_UNKNOWN;
		
		case 'd':
			if(match_date_keyword_rest(str, "ay")) return DATEK_DAYS;
			return DATEK_UNKNOWN;
		
		case 'w':
			if(match_date_keyword_rest(str, "eek")) return DATEK_WEEKS;
			return DATEK_UNKNOWN;
		
		case 'm':
			switch(*(str++)) {
				case 'i':
					if(match_date_keyword_rest(str, "nute")) return DATEK_MINUTES;
					return DATEK_UNKNOWN;
				case 'o':
					if(match_date_keyword_rest(str, "nth")) return DATEK_MONTHS;
					return DATEK_UNKNOWN;
				
				default:
					return DATEK_UNKNOWN;
			}
		
		case 'y':
			if(match_date_keyword_rest(str, "ear")) return DATEK_YEARS;
			return DATEK_UNKNOWN;
		
		default:
			return DATEK_UNKNOWN;
	}
	
	
}

static void gm_mktime(struct tm *tm) {
	mktime(tm);
	if(tm->tm_isdst) {
		tm->tm_isdst = 0;
		tm->tm_hour -= 1;
		mktime(tm);
		tm->tm_isdst = 0;
	}
}

static esh_fn_result time_add(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args >= 1);
	
	if((n_args - 1) % 2 != 0) {
		esh_err_printf(esh, "The number of variadic arguments passed to time-add must be evenly divisible by two");
		return ESH_FN_ERR;
	}
	
	size_t len;
	const char *str = esh_as_string(esh, 0, &len);
	if(!str) return ESH_FN_ERR;
	
	iso_time time;
	if(parse_iso_time(esh, str, len, &time)) return ESH_FN_ERR;
	
	struct tm tm;
	iso_time_to_tm(&time, &tm);
	
	for(size_t i = 1; i < n_args; i += 2) {
		long long n;
		if(esh_as_int(esh, i, &n)) return ESH_FN_ERR;
		
		const char *keyword_str = esh_as_string(esh, i + 1, NULL);
		if(!keyword_str) return ESH_FN_ERR;
		
		date_keyword keyword = match_date_keyword(keyword_str);		
		switch(keyword) {
			case DATEK_SECONDS:
				tm.tm_sec += n;
				break;
			case DATEK_MINUTES:
				tm.tm_min += n;
				break;
			case DATEK_HOURS:
				tm.tm_hour += n;
				break;
			case DATEK_DAYS:
				tm.tm_mday += n;
				break;
			case DATEK_WEEKS:
				tm.tm_mday += 7 * n;
				break;
			case DATEK_MONTHS: {
				tm.tm_mon += n;
				int mdays = days_in_month(tm.tm_year + 1900, tm.tm_mon + 1);
				if(tm.tm_mday > mdays) tm.tm_mday = mdays; 
			} break;
			case DATEK_YEARS:
				tm.tm_year += n;
				break;
				
			case DATEK_UNKNOWN:
				esh_err_printf(esh, "Unknown date keyword '%s'", keyword_str);
				return ESH_FN_ERR;
		}
		if(time.local_time) mktime(&tm);
		else gm_mktime(&tm);
	}
	
	tm_to_iso_time(&tm, &time, time.local_time);
	if(iso_time_to_string(esh, &time)) return ESH_FN_ERR;
	
	return ESH_FN_RETURN(1);
}

static esh_fn_result forchars(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 2);
	
	size_t *at = esh_locals(esh, sizeof(size_t), NULL);
	if(at == NULL) return ESH_FN_ERR;
	
	if(i == 0) *at = 0;
	else {
		if(!esh_is_null(esh, -1)) return ESH_FN_RETURN(1);
		esh_pop(esh, 1); // Pop the return value of the callback function off the stack
	}
	
	size_t len;
	const char *str = esh_as_string(esh, 0, &len);
	if(!str) {
		esh_err_printf(esh, "Can only iterate over strings");
		return ESH_FN_ERR;
	}
	
	if(*at >= len) {
		if(esh_push_null(esh)) return ESH_FN_ERR;
		return ESH_FN_RETURN(1);
	}
	
	unsigned char clen = utf8_next(str[*at]);
	if(*at + clen > len) clen = len - *at;
	
	*at += clen;
	
	if(esh_dup(esh, 1)) return ESH_FN_ERR;
	if(esh_push_int(esh, i)) return ESH_FN_ERR;
	if(esh_new_string(esh, &str[*at - clen], clen)) return ESH_FN_ERR;
	return ESH_FN_CALL(2, 1);
}

static esh_fn_result strlen_fn(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1);
	assert(i == 0);
	
	size_t len;
	const char *str = esh_as_string(esh, 0, &len);
	if(!str) return ESH_FN_ERR;
	
	long long res = 0;
	for(size_t i = 0; i < len; i += utf8_next(str[i])) {
		res++;
	}

	if(esh_push_int(esh, res)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

static esh_fn_result strip(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1);
	assert(i == 0);
	
	size_t len;
	const char *str = esh_as_string(esh, 0, &len);
	if(!str) return ESH_FN_ERR;
	
	size_t start, end;
	for(start = 0; start < len && isspace(str[start]); start++);
	for(end = len; end > start && isspace(str[end - 1]); end--);
	
	if(esh_new_string(esh, str + start, end - start)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

static esh_fn_result repeat(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 2);
	assert(i == 0);
	
	size_t len;
	const char *str = esh_as_string(esh, 0, &len);
	if(!str) return ESH_FN_ERR;
	
	long long count;
	if(esh_as_int(esh, 1, &count)) return ESH_FN_ERR;
	
	esh_str_buff_begin(esh);
	for(long long i = 0; i < count; i++) if(esh_str_buff_appends(esh, str, len)) return ESH_FN_ERR;
	
	str = esh_str_buff(esh, &len);
	
	if(esh_new_string(esh, str, len)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

struct readlines_locals {
	FILE *f;
	void *test;
};

static void readlines_free(esh_state *esh, void *p) {
	(void) esh;
	struct readlines_locals *locals = p;
	if(locals->f) fclose(locals->f);
	free(((struct readlines_locals *) p)->test);
}

static esh_fn_result readlines(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 2);
	
	struct readlines_locals *locals = esh_locals(esh, sizeof(struct readlines_locals), readlines_free);
	if(!locals) return ESH_FN_ERR;
	
	if(i == 0) {
		const char *path = esh_as_string(esh, 0, NULL);
		if(!path) return ESH_FN_ERR;
		locals->f = fopen(path, "r");
		if(!locals->f) {
			esh_err_printf(esh, "Unable to open file '%s': %s", path, strerror(errno));
			return ESH_FN_ERR;
		}
		locals->test = esh_alloc(esh, 12);
	} else {
		if(!esh_is_null(esh, -1)) return ESH_FN_RETURN(1);
		esh_pop(esh, 1);
	}
	
	esh_str_buff_begin(esh);
	
	bool eof = false;
	while(true) {
		int c = fgetc(locals->f);
		if(c == EOF) {
			eof = true;
			break;
		} else if(c == '\n') break;
		
		if(esh_str_buff_appendc(esh, c)) return ESH_FN_ERR;
	}
	
	size_t len;
	char *buff = esh_str_buff(esh, &len);
	if(eof && len == 0) {
		if(esh_push_null(esh)) return ESH_FN_ERR;
		return ESH_FN_RETURN(1);
	}
	
	if(esh_dup(esh, 1)) return ESH_FN_ERR;
	if(esh_new_string(esh, buff, len)) return ESH_FN_ERR;
	return ESH_FN_CALL(1, 1);
}

static esh_fn_result puts_fn(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1);
	assert(i == 0);
	
	size_t len;
	const char *str = esh_as_string(esh, 0, &len);
	if(!str) return ESH_FN_ERR;
	
	fwrite(str, sizeof(char), len, stdout);
	fflush(stdout);
	
	if(esh_push_null(esh)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

static esh_fn_result ascii(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	
	esh_str_buff_begin(esh);
	for(size_t arg = 0; arg < n_args; arg++) {
		long long i;
		if(esh_as_int(esh, arg, &i)) return ESH_FN_ERR;
		if(i < 0 || i > 127) {
			esh_err_printf(esh, "Invalid ascii character: %lli", i);
			return ESH_FN_ERR;
		}
		
		if(esh_str_buff_appendc(esh, (char) i)) return ESH_FN_ERR;
	}
	
	size_t len;
	char *str = esh_str_buff(esh, &len);
	if(esh_new_string(esh, str, len)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

static esh_fn_result charcode(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 1);
	
	size_t len;
	const char *str = esh_as_string(esh, 0, &len);
	if(!str) return ESH_FN_ERR;
	
	if(len == 0) {
		esh_err_printf(esh, "Attempting to take charcode of empty string");
		return ESH_FN_ERR;
	}
	
	if(esh_push_int(esh, str[0])) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

static esh_fn_result isprint_fn(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 1);
	
	size_t len;
	const char *str = esh_as_string(esh, 0, &len);
	if(!str) return ESH_FN_ERR;
	
	for(size_t i = 0; i < len;) {
		size_t cl = utf8_next(str[i]);
		if(cl == 1 && !isprint(str[i])) {
			if(esh_push_bool(esh, false)) return ESH_FN_ERR;
			return ESH_FN_RETURN(1);
		}
		i += cl;
	}
	
	if(esh_push_bool(esh, true)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

static esh_fn_result try_fn(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0 || i == 1);
	assert(n_args >= 1);
	
	if(i == 0) {
		return ESH_FN_TRY_CALL(n_args - 1, 1);
	}
	if(esh_panic_caught(esh)) {
		if(esh_push_null(esh)) return ESH_FN_ERR;
		const char *err = esh_get_err(esh);
		if(esh_new_string(esh, err, strlen(err))) return ESH_FN_ERR;
	} else {
		if(esh_push_null(esh)) return ESH_FN_ERR;
	}
	return ESH_FN_RETURN(2);
}

static esh_fn_result stackdump(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 0);
	
	esh_stackdump(esh, stdout);
	if(esh_push_null(esh)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

static esh_fn_result eval(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0 || i == 1);
	assert(n_args == 1);
	
	if(i == 1) return ESH_FN_RETURN(1);
	
	const char *src = esh_as_string(esh, 0, NULL);
	if(!src) return ESH_FN_ERR;
	
	if(esh_loads(esh, "eval", src, true)) return ESH_FN_ERR;
	return ESH_FN_CALL(0, 1);
}

static esh_fn_result load(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 1);
	const char *src = esh_as_string(esh, 0, NULL);
	if(!src) return ESH_FN_ERR;
	
	if(esh_loads(esh, "eval", src, true)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

static esh_fn_result is_space(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 1);
	
	size_t len;
	const char *str = esh_as_string(esh, 0, &len);
	if(!str) return ESH_FN_ERR;
	for(size_t i = 0; i < len; i++) if(!isspace(str[i])) {
		if(esh_push_bool(esh, false)) return ESH_FN_ERR;
		return ESH_FN_RETURN(1);
	}
	
	if(esh_push_bool(esh, true)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

static esh_fn_result escape_pattern(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 1);
	
	size_t len;
	const char *str = esh_as_string(esh, 0, &len);
	if(!str) return ESH_FN_ERR;
	
	if(esh_pattern_escape(esh, str, len)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

static esh_fn_result is_string(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 1);
	
	if(esh_push_bool(esh, esh_as_string(esh, 0, NULL) != NULL)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

#include "libd.h"

static esh_fn_result load_dl(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 1);
	
	const char *path = esh_as_string(esh, 0, NULL);
	if(!path) return ESH_FN_ERR;
	
	libd *lib = load_libd(esh, path);
	if(!lib) return ESH_FN_ERR;
	
	int (*init)(esh_state *esh) = (int (*)(esh_state *)) libd_getf(esh, lib, "esh_lib_init");
	if(!init) return ESH_FN_ERR;
	
	int err = init(esh);
	if(err) {
		close_libd(esh, lib);
		return ESH_FN_ERR;
	}
	
	return ESH_FN_RETURN(1);
}

static esh_fn_result endswith(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 2);
	
	size_t str_len;
	const char *str = esh_as_string(esh, 0, &str_len);
	if(!str) return ESH_FN_ERR;
	
	size_t suffix_len;
	const char *suffix = esh_as_string(esh, 1, &suffix_len);
	if(!suffix) return ESH_FN_ERR;
	
	if(suffix_len > str_len) {
		if(esh_push_bool(esh, false)) return ESH_FN_ERR;
		return ESH_FN_RETURN(1);
	}
	
	bool result = true;
	for(size_t i = 0; i < suffix_len; i++) {
		if(str[str_len - suffix_len + i] != suffix[i]) {
			result = false;
			break;
		}
	}
	
	if(esh_push_bool(esh, result)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

static esh_fn_result exists(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 1);
	
	const char *path = esh_as_string(esh, 0, NULL);
	if(!path) return ESH_FN_ERR;
	
	FILE *f = fopen(path, "r");
	bool result = f != NULL;
	if(f != NULL) fclose(f);
	
	if(esh_push_bool(esh, result)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

/*@
	is-defined ...
	...         string
	@returns    boolean
	
	Checks whether all the given global variables are defined.
*/
static esh_fn_result is_defined(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	
	bool result = true;
	for(size_t i = 0; i < n_args; i++) {
		const char *name = esh_as_string(esh, i, NULL);
		if(!name) return ESH_FN_ERR;
		if(esh_get_global(esh, name)) {
			result = false;
			break;
		}
		esh_pop(esh, 1);
	}
	
	if(esh_push_bool(esh, result)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

/*@
	max i ...
	i           int
	...         int
	@returns    int
	
	Returns the largest integer
*/
static esh_fn_result max_fn(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args >= 1);
	
	long long res;
	if(esh_as_int(esh, 0, &res)) return ESH_FN_ERR;
	
	for(size_t i = 1; i < n_args; i++) {
		long long x;
		if(esh_as_int(esh, i, &x)) return ESH_FN_ERR;
		if(x > res) res = x;
	}
	
	if(esh_push_int(esh, res)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

/*@
	min i ...
	i           int
	...         int
	@returns    int
	
	Returns the smallest integer
*/
static esh_fn_result min_fn(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args >= 1);
	
	long long res;
	if(esh_as_int(esh, 0, &res)) return ESH_FN_ERR;
	
	for(size_t i = 1; i < n_args; i++) {
		long long x;
		if(esh_as_int(esh, i, &x)) return ESH_FN_ERR;
		if(x < res) res = x;
	}
	
	if(esh_push_int(esh, res)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

#include "stdlib/sort.h"

static int alphsort_cmp(esh_state *esh) {
	size_t a_len;
	const char *a = esh_as_string(esh, -1, &a_len);
	if(!a) return -1;
	
	size_t b_len;
	const char *b = esh_as_string(esh, -2, &b_len);
	if(!b) return -1;
	
	size_t m_len = a_len < b_len? a_len : b_len;
	int res = memcmp(a, b, sizeof(char) * m_len);
	if(res == 0) return a_len > b_len;
	return res > 0;
}

/*@
	alphsort a
	a           array of string
	@returns    array of string
	
	Sorts the entries in the given array in place (that is to say, it mutates the given array) alphabetically.
	Throws an error if any of the values in the array are not strings.
	If an error is thrown in the middle of sorting, the state of the array afterwards is undefined.
	Returns the given array.
*/
static esh_fn_result alphsort(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 1);
	
	size_t len = esh_object_len(esh, 0);
	
	int err = esh_sort(esh, 0, len, alphsort_cmp, false);
	if(err) return ESH_FN_ERR;

	return ESH_FN_RETURN(1);
}

static int numsort_cmp(esh_state *esh) {
	long long a, b;
	if(esh_as_int(esh, -2, &b)) return -1;
	if(esh_as_int(esh, -1, &a)) return -1;
	
	return a > b;
}

/*@
	numsort a
	a           array of int
	@returns    array of int
	
	Sorts the entries in the given array in place (that is to say, it mutates the given array) numerically.
	Throws an error if any of the values in the array are not integers.
	If an error is thrown in the middle of sorting, the state of the array afterwards is undefined.
	Returns the given array.
*/
static esh_fn_result numsort(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 1);
	
	size_t len = esh_object_len(esh, 0);
	
	int err = esh_sort(esh, 0, len, numsort_cmp, false);
	if(err) return ESH_FN_ERR;

	return ESH_FN_RETURN(1);
}

static char itob64(unsigned i) {
	if(i < 26) return 'A' + i;
	if(i < 52) return 'a' + (i - 26);
	if(i < 62) return '0' + (i - 52);
	
	if(i == 62) return '+';
	if(i == 63) return '/';
	
	assert(false);
}

static esh_fn_result base64_encode(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 1);
	
	size_t len;
	const char *str = esh_as_string(esh, 0, &len);
	
	esh_str_buff_begin(esh);
	for(size_t i = 0; i + 3 <= len; i += 3) {
		char s[4] = {
			itob64(str[i] >> 2),
			itob64(((str[i] & 3) << 4) | (str[i + 1] >> 4)),
			itob64(((str[i + 1] & 15) << 2) | (str[i + 2] >> 6)),
			itob64(str[i + 2] & 63)
		};
		if(esh_str_buff_appends(esh, s, 4)) return ESH_FN_ERR;
	}
	
	size_t rem = len % 3;
	if(rem == 1) {
		char s[4] = {
			itob64(str[len - 1] >> 2),
			itob64((str[len - 1] & 3) << 4),
			'=',
			'='
		};
		if(esh_str_buff_appends(esh, s, 4)) return ESH_FN_ERR;
	} else if(rem == 2) {
		char s[4] = {
			itob64(str[len - 2] >> 2),
			itob64(((str[len - 2] & 3) << 4) | (str[len - 1] >> 4)),
			itob64((str[len - 1] & 15) << 2),
			'='
		};
		if(esh_str_buff_appends(esh, s, 4)) return ESH_FN_ERR;
	}
	
	size_t res_len;
	const char *res = esh_str_buff(esh, &res_len);
	if(esh_new_string(esh, res, res_len)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

static char itohex(unsigned char i) {
	assert(i < 16);
	if(i < 10) return '0' + i;
	return 'a' + (i - 10);
}

static esh_fn_result hex_encode(esh_state *esh, size_t n_args, size_t i) {
	(void) n_args, (void) i;
	
	size_t len;
	const char *str = esh_as_string(esh, 0, &len);
	if(!str) return ESH_FN_ERR;
	
	esh_str_buff_begin(esh);
	for(size_t i = 0; i < len; i++) {
		char s[2] = { itohex(str[i] >> 4), itohex(str[i] & 15) };
		
		if(esh_str_buff_appends(esh, s, 2)) return ESH_FN_ERR;
	}
	
	size_t res_len;
	const char *res = esh_str_buff(esh, &res_len);
	if(esh_new_string(esh, res, res_len)) return ESH_FN_ERR;
	
	return ESH_FN_RETURN(1);
}

static int hextoi(char c) {
	if(c >= '0' && c <= '9') return c - '0';
	if(c >= 'a' && c <= 'f') return c - 'a' + 10;
	if(c >= 'A' && c <= 'F') return c - 'A' + 10;
	
	return -1;
}

static esh_fn_result hex_decode(esh_state *esh, size_t n_args, size_t i) {
	(void) n_args, (void) i;
	
	size_t len;
	const char *str = esh_as_string(esh, 0, &len);
	if(!str) return ESH_FN_ERR;
	
	if(len % 2) goto ERR;
	
	if(len >= 2 && str[0] == '0' && str[1] == 'x') {
		len -= 2;
		str += 2;
	}
	
	esh_str_buff_begin(esh);
	for(size_t i = 0; i < len / 2; i++) {
		int a = hextoi(str[i * 2 + 1]);
		int b = hextoi(str[i * 2]);
		
		if(a == -1 || b == -1) goto ERR;
		
		if(esh_str_buff_appendc(esh, (b << 4) | a)) return ESH_FN_ERR;
	}
	
	size_t res_len;
	const char *res = esh_str_buff(esh, &res_len);
	if(esh_new_string(esh, res, res_len)) return ESH_FN_ERR;
	
	return ESH_FN_RETURN(1);
	
	ERR:
	if(esh_push_null(esh)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

/*@
	substr from to?
	from        int
	to?         int
	@returns    string
	
	Extracts a substring from the given string.
	Both $from and $to specify byte offsets into the string.
	Negative offsets are treated as relative to the end of the string.-1 refers to the last byte in the string.
	If $to is not given, then the entire string after $to will be extracted.
	$from is inclusive, whilst $to is exclusive.
	Returns the extracted substring.
*/
static esh_fn_result substr(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 2 || n_args == 3);
	size_t len;
	long long slen;
	const char *str = esh_as_string(esh, 0, &len);
	if(!str) return ESH_FN_ERR;
	slen = len;
	
	long long from, to = slen;
	if(esh_as_int(esh, 1, &from)) return ESH_FN_ERR;
	if(n_args == 3 && esh_as_int(esh, 2, &to)) return ESH_FN_ERR;
	
	if(from < 0) from += slen;
	if(to < 0) to += slen;
	
	if(from < 0) from = 0;
	if(to < 0) to = 0;
	if(from > slen) from = slen;
	if(to > slen) to = slen;
	
	if(from > to) from = to;
	
	if(esh_new_string(esh, str + from, to - from)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

#include "stdlib/utf16.h"
/*@
	utf16/encode s
	s           string
	@returns    string
*/
static esh_fn_result utf16_encode_fn(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 1);

	size_t src_len;
	const char *src = esh_as_string(esh, 0, &src_len);
	if(!src) return ESH_FN_ERR;
	
	esh_str_buff_begin(esh);
	
	for(size_t i = 0; i < src_len;) {
		uint32_t c;
		i += utf8_decode(src, i, src_len, &c);
		
		uint16_t utf16[2];
		unsigned len = utf16_encode(utf16, c);
		for(unsigned j = 0; j < len; j++) {
			unsigned char bytes[2] = { utf16[j] & 0xFF, utf16[j] >> 8 };
			if(esh_str_buff_appends(esh, (char *) bytes, 2)) return ESH_FN_ERR;
		}
	}
	size_t res_len;
	const char *res = esh_str_buff(esh, &res_len);
	if(esh_new_string(esh, res, res_len)) return ESH_FN_ERR;
	
	return ESH_FN_RETURN(1);
}

/*@
	co fn
	fn          function
	@returns    coroutine of typeof(fn)
	
	Turns the given function into a coroutine and returns the function
*/
static esh_fn_result coroutine_fn(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1);
	assert(i == 0);
	if(esh_make_coroutine(esh, 0)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

static esh_fn_result next_fn(esh_state *esh, size_t n_args, size_t i) {
	(void) esh;
	assert(n_args == 1);
	assert(i == 0 || i == 1);
	if(i == 0) {
		return ESH_FN_NEXT(0, 1);
	}
	return ESH_FN_RETURN(1);
}

static esh_fn_result yield_fn(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1);
	assert(i == 0 || i == 1);
	if(i == 0) {
		return ESH_FN_YIELD(1, 0);
	}
	
	if(esh_push_null(esh)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

static esh_fn_result loop_fn(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1);
	
	if(i != 0) {
		if(!esh_is_null(esh, 1)) return ESH_FN_RETURN(1);
		esh_pop(esh, 1);
	}
	
	if(esh_dup(esh, 0)) return ESH_FN_ERR;
	
	return ESH_FN_CALL(0, 1);
}

static esh_fn_result foreach(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 2);
	
	if(i % 2 == 0) {
		if(i != 0) {
			if(!esh_is_null(esh, -1)) return ESH_FN_RETURN(1); // If the function returned non-null
			esh_pop(esh, 1);
		}
		if(esh_dup(esh, 0)) return ESH_FN_ERR;
		return ESH_FN_NEXT(0, 1);
	} else {
		if(esh_is_null(esh, -1)) return ESH_FN_RETURN(1); // If the coroutine yielded null
		
		if(esh_dup(esh, 1)) return ESH_FN_ERR;
		esh_swap(esh, -2, -1);
		return ESH_FN_CALL(1, 1);
	}
}

static esh_fn_result map(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 2);
	
	if(i % 3 == 0) {
		if(esh_dup(esh, 0)) return ESH_FN_ERR;
		return ESH_FN_NEXT(0, 1);
	} else if(i % 3 == 1) {
		if(esh_is_null(esh, -1)) return ESH_FN_RETURN(1);
		if(esh_dup(esh, 1)) return ESH_FN_ERR;
		esh_swap(esh, -1, -2);
		return ESH_FN_CALL(1, 1);
	} else {
		return ESH_FN_YIELD(1, 0);
	}
}

static esh_fn_result filter(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 2);
	
	if(i % 3 == 0) {
		if(esh_dup(esh, 0)) return ESH_FN_ERR;
		return ESH_FN_NEXT(0, 1);
	} else if(i % 3 == 1) {
		if(esh_is_null(esh, -1)) return ESH_FN_RETURN(1);
		if(esh_dup(esh, 1)) return ESH_FN_ERR;
		if(esh_dup(esh, -2)) return ESH_FN_ERR;
		return ESH_FN_CALL(1, 1);
	} else {
		bool cond = esh_as_bool(esh, -1);
		esh_pop(esh, 1);
		if(cond) {
			return ESH_FN_YIELD(1, 0);
		}
		esh_pop(esh, 1);
		return ESH_FN_REPEAT;
	}
}

static esh_fn_result chars(esh_state *esh, size_t n_args, size_t i) {
	(void) i;
	assert(n_args == 1);
	// TODO: Should char stream buffering be handled by the caller, or the char_stream_read function?
	char buff[4];
	long long res = esh_char_stream_read(esh, 0, buff, 1);
	if(res == -1) return ESH_FN_ERR;
	if(res == 0) return ESH_FN_RETURN(1);
	
	size_t clen = utf8_next(buff[0]);
	assert(clen <= 4 && clen >= 1);
	if(clen != 1) {
		res = esh_char_stream_read(esh, 0, buff + 1, clen - 1);
		if(res == -1) return ESH_FN_ERR;
		if((size_t) res < clen - 1) return ESH_FN_RETURN(1);
	}
	
	if(esh_new_string(esh, buff, clen)) return ESH_FN_ERR;
	return ESH_FN_YIELD(1, 0);	
}

static esh_fn_result as_string(esh_state *esh, size_t n_args, size_t i) {
	esh_str_buff_begin(esh);
	while(true) {
		char buff[512];
		long long n = esh_char_stream_read(esh, 0, buff, sizeof(buff));
		if(n == -1) return ESH_FN_ERR;
		
		if(esh_str_buff_appends(esh, buff, n)) return ESH_FN_ERR;
		
		if((size_t) n < sizeof(buff)) break;
	}
	
	size_t len;
	const char *buff = esh_str_buff(esh, &len);
	if(esh_new_string(esh, buff, len)) return ESH_FN_ERR;
	
	return ESH_FN_RETURN(1);
}

struct split_locals {
	char *buff;
	size_t len, cap, at, match;
	bool reading, at_end, reading_str;
};

static void split_free_locals(esh_state *esh, void *p) {
	struct split_locals *locals = p;
	esh_free(esh, locals->buff);
}

static esh_fn_result split(esh_state *esh, size_t n_args, size_t i) {
	struct split_locals *locals = esh_locals(esh, sizeof(*locals), split_free_locals);
	if(!locals) return ESH_FN_ERR;
	
	if(i == 0) {
		*locals = (struct split_locals) {
			.buff = NULL,
			.len = 0,
			.cap = 0,
			.match = 0,
			.reading = true,
			.at_end = false,
			.reading_str = false
		};
		
		if(esh_as_string(esh, 0, NULL)) {
			if(esh_dup(esh, 0)) return ESH_FN_ERR;
			locals->reading_str = true;
			return ESH_FN_REPEAT;
		}
		
		if(esh_dup(esh, 0)) return ESH_FN_ERR;
		return ESH_FN_NEXT_S(0, 1);
	}
	
	if(locals->reading) {
		locals->at_end = esh_is_null(esh, -1);
		if(!locals->at_end) {
			size_t len;
			const char *str = esh_as_string(esh, -1, &len);
			if(!str) return ESH_FN_ERR;
			
			if(locals->len + len > locals->cap) {
				locals->cap = locals->cap * 3 / 2 + len;
				char *new_buff = esh_realloc(esh, locals->buff, sizeof(char) * locals->cap);
				if(!new_buff) return ESH_FN_ERR;
				locals->buff = new_buff;
			}
			
			memcpy(locals->buff + locals->len, str, len);
			locals->len += len;
		}
		esh_pop(esh, 1);
	}
	locals->reading = false;
	if(locals->reading_str) locals->at_end = true;
	
	size_t pattern_len = 0;
	const char *pattern = NULL;
	if(n_args == 2) if( !(pattern = esh_as_string(esh, 1, &pattern_len)) ) return ESH_FN_ERR;
	
	while(true) {
		if(locals->at == locals->len) {
			if(locals->at_end) break;
			if(esh_dup(esh, 0)) return ESH_FN_ERR;
			locals->reading = true;
			return ESH_FN_NEXT_S(0, 1);
		}
		
		char c = locals->buff[locals->at++];
		bool split = false;
		if(pattern != NULL) {
			if(locals->match < pattern_len && c == pattern[locals->match]) locals->match++;
			else locals->match = 0;
			
			if(locals->match == pattern_len) split = true;
		} else if(isspace(c)) {
			locals->match = 1;
			split = true;
		}
		
		if(split) {
			char *str = locals->buff;
			size_t len = locals->at - locals->match;
			if(pattern || len != 0) if(esh_new_string(esh, str, len)) return ESH_FN_ERR; // If matching whitespace, then empty strings should not be yielded (e.g "a<space><space>b" should give "a" "b"; not "a" "" "b"
			
			for(size_t i = 0, j = locals->at; j < locals->len; i++, j++) {
				locals->buff[i] = locals->buff[j];
			}
			locals->len = locals->len - locals->at;
			locals->match = 0;
			locals->at = 0;
			
			if(pattern || len != 0) return ESH_FN_YIELD(1, 0);
			else continue;
		}
	}
	
	if(pattern || locals->len != 0) {
		if(esh_new_string(esh, locals->buff, locals->len)) return ESH_FN_ERR;
		return ESH_FN_YIELD_LAST(1, 0);
	} else {
		if(esh_push_null(esh)) return ESH_FN_ERR;
		return ESH_FN_RETURN(1);
	}
}

static esh_fn_result includes(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 2);
	assert(i == 0);
	
	size_t strlen;
	const char *str = esh_as_string(esh, 0, &strlen);
	if(!str) return ESH_FN_ERR;
	
	size_t pattern_len;
	const char *pattern = esh_as_string(esh, 1, &pattern_len);
	if(!pattern) return ESH_FN_ERR;
	
	if(pattern_len > strlen) goto RES_FALSE;
	
	for(size_t i = 0; i < strlen - pattern_len + 1; i++) {
		for(size_t j = 0; j < pattern_len; j++) {
			if(str[i + j] != pattern[j]) goto NEXT;
		}
		goto RES_TRUE;
		NEXT:
		(void) 0;
	}
	
	RES_FALSE:
	if(esh_push_bool(esh, false)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);

	RES_TRUE:
	if(esh_push_bool(esh, true)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

/*@
	iter a
	a           array of T
	@returns    coroutine of T

	Returns a coroutine that yields all values in an array in order.
*/
static esh_fn_result iter(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1);
	
	if(esh_index_i(esh, 0, i)) return ESH_FN_ERR;
	if(esh_is_null(esh, -1)) return ESH_FN_RETURN(1);
	
	return ESH_FN_YIELD(1, 0);
}

/*@
	keys obj
	obj         object
	@returns    coroutine of string

	Returns a coroutine that yields all keys in an object.
*/
static esh_fn_result keys(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1);
	
	esh_iterator *iter = esh_locals(esh, sizeof(esh_iterator), NULL);
	if(i == 0) *iter = esh_iter_begin(esh);
	
	if(esh_iter_next(esh, 0, iter)) return ESH_FN_ERR;
	if(iter->done) {
		if(esh_push_null(esh)) return ESH_FN_ERR;
		return ESH_FN_RETURN(1);
	}
	esh_pop(esh, 1);
	
	return ESH_FN_YIELD(1, 0);
}

/*@
	values obj
	obj         object of T
	@returns    coroutine of T

	Returns a coroutine that yields all values in an object.
*/
static esh_fn_result values(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1);
	
	esh_iterator *iter = esh_locals(esh, sizeof(esh_iterator), NULL);
	if(i == 0) *iter = esh_iter_begin(esh);
	
	if(esh_iter_next(esh, 0, iter)) return ESH_FN_ERR;
	if(iter->done) {
		if(esh_push_null(esh)) return ESH_FN_ERR;
		return ESH_FN_RETURN(1);
	}
	esh_swap(esh, -1, -2);
	esh_pop(esh, 1);
	
	return ESH_FN_YIELD(1, 0);
}

/*@
	entries obj
	obj         object of T
	@returns    coroutine of { string, T }

	Returns a coroutine that yields all keys and values in an object. 
	Each yielded value is an array of two items, where the first entry is the key and the second the value.
*/
static esh_fn_result entries(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1);
	
	esh_iterator *iter = esh_locals(esh, sizeof(esh_iterator), NULL);
	if(i == 0) *iter = esh_iter_begin(esh);
	
	if(esh_iter_next(esh, 0, iter)) return ESH_FN_ERR;
	if(iter->done) {
		if(esh_push_null(esh)) return ESH_FN_ERR;
		return ESH_FN_RETURN(1);
	}
	if(esh_new_array(esh, 2)) return ESH_FN_ERR;
	
	return ESH_FN_YIELD(1, 0);
}

/*@
	collect in
	in          coroutine of T
	@returns    array of T

	Collects all values from a coroutine into an array.
*/
static esh_fn_result collect(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1);
	
	if(i == 0) {
		if(esh_object_of(esh, 0)) return ESH_FN_ERR;
		
		if(esh_dup(esh, 0)) return ESH_FN_ERR;
		return ESH_FN_NEXT(0, 1);
	}
	
	if(esh_is_null(esh, -1)) {
		esh_pop(esh, 1);
		return ESH_FN_RETURN(1);
	}
	
	if(esh_set_i(esh, 1, i - 1, -1)) return ESH_FN_ERR;
	esh_pop(esh, 1);
	
	if(esh_dup(esh, 0)) return ESH_FN_ERR;
	return ESH_FN_NEXT(0, 1);
}

/*@
	nth in i
	in          coroutine of T
	i           int
	@returns    T

	Returns the nth item from a coroutine. 1 represents the first item.
	Returns null if $i is less than 1
*/
static esh_fn_result nth(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 2);
	
	long long *counter = esh_locals(esh, sizeof(long long), NULL);
	if(!counter) return ESH_FN_ERR;
	
	if(i == 0) {
		if(esh_as_int(esh, 1, counter)) return ESH_FN_ERR;

		if(*counter < 1) {
			if(esh_push_null(esh)) return ESH_FN_ERR;
			return ESH_FN_RETURN(1);
		}
	} else {
		*counter -= 1;
		if(*counter == 0 || esh_is_null(esh, -1)) return ESH_FN_RETURN(1);
		esh_pop(esh, 1);
	}
	
	if(esh_dup(esh, 0)) return ESH_FN_ERR;
	return ESH_FN_NEXT(0, 1);
}

/*@
	replace s a b
	s           str
	a           str
	b           str
	@returns    str

	Replaces every occurence of $a in $s with $b, and returns the result.
	Returns $s unchanged if $a has a length of zero.
	If $b has a length of zero, each occurrence of $a is deleted.
*/
static esh_fn_result replace(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 3);
	assert(i == 0);
	
	size_t sl;
	const char *s = esh_as_string(esh, 0, &sl);
	if(!s) return ESH_FN_ERR;

	size_t al;
	const char *a = esh_as_string(esh, 1, &al);
	if(!s) return ESH_FN_ERR;

	size_t bl;
	const char *b = esh_as_string(esh, 2, &bl);
	if(!b) return ESH_FN_ERR;
	
	if(al > sl || al == 0) {
		if(esh_dup(esh, 0)) return ESH_FN_ERR;
		return ESH_FN_RETURN(1);
	}
	
	esh_str_buff_begin(esh);
	for(size_t i = 0; i < sl - al + 1; i++) {
		for(size_t j = 0; j < al; j++) {
			if(s[i + j] != a[j]) goto NO_MATCH;
		}
		
		if(esh_str_buff_appends(esh, b, bl)) return ESH_FN_ERR;
		i += al - 1;
		
		continue;
		NO_MATCH:
		if(esh_str_buff_appendc(esh, s[i])) return ESH_FN_ERR;
	}
	if(esh_str_buff_appends(esh, s + sl - al + 1, al - 1)) return ESH_FN_ERR;
	
	size_t res_len;
	const char *res = esh_str_buff(esh, &res_len);
	if(esh_new_string(esh, res, res_len)) return ESH_FN_ERR;
	
	return ESH_FN_RETURN(1);
}

#include "stdlib/unix.h"

int esh_load_stdlib(esh_state *esh) {
	#define REQ(x) if(x) return 1
	
	REQ(esh_new_c_fn(esh, "print", print, 0, 0, true));
	REQ(esh_set_global(esh, "print"));
	
	REQ(esh_new_c_fn(esh, "forevery", forevery, 1, 0, true));
	REQ(esh_set_global(esh, "forevery"));
	
	REQ(esh_new_c_fn(esh, "for", for_loop, 3, 1, false));
	REQ(esh_set_global(esh, "for"));

	REQ(esh_new_c_fn(esh, "gc", gc, 1, 0, false));
	REQ(esh_set_global(esh, "gc"));

	REQ(esh_new_c_fn(esh, "gc-conf", gc_conf, 1, 0, false));
	REQ(esh_set_global(esh, "gc-conf"));
	
	REQ(esh_new_c_fn(esh, "foreach-in", foreach_in, 2, 0, true));
	REQ(esh_set_global(esh, "foreach-in"));
	
	REQ(esh_new_c_fn(esh, "sizeof", sizeof_fn, 1, 0, false));
	REQ(esh_set_global(esh, "sizeof"));
	
	REQ(esh_new_c_fn(esh, "assert", assert_fn, 1, 0, false));
	REQ(esh_set_global(esh, "assert"));

	REQ(esh_new_c_fn(esh, "imap", imap, 2, 0, false));
	REQ(esh_set_global(esh, "imap"));

	REQ(esh_new_c_fn(esh, "kfilter", kfilter, 2, 0, false));
	REQ(esh_set_global(esh, "kfilter"));
	
	REQ(esh_new_c_fn(esh, "ifilter", ifilter, 2, 0, false));
	REQ(esh_set_global(esh, "ifilter"));
	
	REQ(esh_new_c_fn(esh, "fndump", fndump, 1, 0, false));
	REQ(esh_set_global(esh, "fndump"));

	REQ(esh_new_c_fn(esh, "parse-json", parse_json_fn, 1, 0, false));
	REQ(esh_set_global(esh, "parse-json"));
	
	REQ(esh_new_c_fn(esh, "to-json", to_json_fn, 1, 0, false));
	REQ(esh_set_global(esh, "to-json"));
	
	REQ(esh_new_c_fn(esh, "fori", fori, 2, 0, false));
	REQ(esh_set_global(esh, "fori"));
	
	REQ(esh_new_c_fn(esh, "write", write_fn, 2, 0, false));
	REQ(esh_set_global(esh, "write"));
	
	REQ(esh_new_c_fn(esh, "isplit", isplit, 1, 1, false));
	REQ(esh_set_global(esh, "isplit"));
	
	REQ(esh_new_c_fn(esh, "join", join, 1, 1, false));
	REQ(esh_set_global(esh, "join"));
	
	REQ(esh_new_c_fn(esh, "include", include, 1, 0, false));
	REQ(esh_set_global(esh, "include"));
	
	REQ(esh_new_c_fn(esh, "getenv", getenv_fn, 1, 0, false));
	REQ(esh_set_global(esh, "getenv"));
	
	REQ(esh_new_c_fn(esh, "beginswith", beginswith, 2, 0, false));
	REQ(esh_set_global(esh, "beginswith"));
	
	REQ(esh_new_c_fn(esh, "match", match, 2, 0, false));
	REQ(esh_set_global(esh, "match"));
	
	REQ(esh_new_c_fn(esh, "slice", slice, 3, 0, false));
	REQ(esh_set_global(esh, "slice"));
	
	REQ(esh_new_c_fn(esh, "union", union_fn, 2, 0, false));
	REQ(esh_set_global(esh, "union"));
	
	REQ(esh_new_c_fn(esh, "intersection", intersection, 2, 0, false));
	REQ(esh_set_global(esh, "intersection"));
	
	REQ(esh_new_c_fn(esh, "time", time_fn, 0, 0, false));
	REQ(esh_set_global(esh, "time"));
	
	REQ(esh_new_c_fn(esh, "localtime", localtime_fn, 0, 1, false));
	REQ(esh_set_global(esh, "localtime"));
	
	REQ(esh_new_c_fn(esh, "gmtime", gmtime_fn, 0, 1, false));
	REQ(esh_set_global(esh, "gmtime"));
	
	REQ(esh_new_c_fn(esh, "forchars", forchars, 2, 0, false));
	REQ(esh_set_global(esh, "forchars"));
	
	REQ(esh_new_c_fn(esh, "strlen", strlen_fn, 1, 0, false));
	REQ(esh_set_global(esh, "strlen"));

	REQ(esh_new_c_fn(esh, "strip", strip, 1, 0, false));
	REQ(esh_set_global(esh, "strip"));
	
	REQ(esh_new_c_fn(esh, "repeat", repeat, 2, 0, false));
	REQ(esh_set_global(esh, "repeat"));
	
	REQ(esh_new_c_fn(esh, "readlines", readlines, 2, 0, false));
	REQ(esh_set_global(esh, "readlines"));
	
	REQ(esh_new_c_fn(esh, "puts", puts_fn, 1, 0, false));
	REQ(esh_set_global(esh, "puts"));
	
	REQ(esh_new_c_fn(esh, "ascii", ascii, 0, 0, true));
	REQ(esh_set_global(esh, "ascii"));
	
	REQ(esh_new_c_fn(esh, "charcode", charcode, 1, 0, false));
	REQ(esh_set_global(esh, "charcode"));
	
	REQ(esh_new_c_fn(esh, "isprint", isprint_fn, 1, 0, false));
	REQ(esh_set_global(esh, "isprint"));
	
	REQ(esh_new_c_fn(esh, "try", try_fn, 1, 0, true));
	REQ(esh_set_global(esh, "try"));
	
	REQ(esh_new_c_fn(esh, "stackdump", stackdump, 0, 0, false));
	REQ(esh_set_global(esh, "stackdump"));
	
	REQ(esh_new_c_fn(esh, "time-add", time_add, 1, 0, true));
	REQ(esh_set_global(esh, "time-add"));
	
	REQ(esh_new_c_fn(esh, "eval", eval, 1, 0, false));
	REQ(esh_set_global(esh, "eval"));
	
	REQ(esh_new_c_fn(esh, "is-space", is_space, 1, 0, false));
	REQ(esh_set_global(esh, "is-space"));
	
	REQ(esh_new_c_fn(esh, "load", load, 1, 0, false));
	REQ(esh_set_global(esh, "load"));
	
	REQ(esh_new_c_fn(esh, "escape-pattern", escape_pattern, 1, 0, false));
	REQ(esh_set_global(esh, "escape-pattern"));
	
	REQ(esh_new_c_fn(esh, "is-string", is_string, 1, 0, false));
	REQ(esh_set_global(esh, "is-string"));
	
	REQ(esh_new_c_fn(esh, "load-dl", load_dl, 1, 0, false));
	REQ(esh_set_global(esh, "load-dl"));
	
	REQ(esh_new_c_fn(esh, "endswith", endswith, 2, 0, false));
	REQ(esh_set_global(esh, "endswith"));

	REQ(esh_new_c_fn(esh, "exists", exists, 1, 0, false));
	REQ(esh_set_global(esh, "exists"));
	
	REQ(esh_new_c_fn(esh, "is-defined", is_defined, 0, 0, true));
	REQ(esh_set_global(esh, "is-defined"));
	
	REQ(esh_new_c_fn(esh, "max", max_fn, 1, 0, true));
	REQ(esh_set_global(esh, "max"));
	
	REQ(esh_new_c_fn(esh, "min", min_fn, 1, 0, true));
	REQ(esh_set_global(esh, "min"));
	
	REQ(esh_new_c_fn(esh, "alphsort", alphsort, 1, 0, false));
	REQ(esh_set_global(esh, "alphsort"));

	REQ(esh_new_c_fn(esh, "numsort", numsort, 1, 0, false));
	REQ(esh_set_global(esh, "numsort"));
	
	REQ(esh_new_c_fn(esh, "base64/encode", base64_encode, 1, 0, false));
	REQ(esh_set_global(esh, "base64/encode"));
	
	REQ(esh_new_c_fn(esh, "hex/encode", hex_encode, 1, 0, false));
	REQ(esh_set_global(esh, "hex/encode"));
	
	REQ(esh_new_c_fn(esh, "hex/decode", hex_decode, 1, 0, false));
	REQ(esh_set_global(esh, "hex/decode"));
	
	REQ(esh_new_c_fn(esh, "substr", substr, 2, 1, false));
	REQ(esh_set_global(esh, "substr"));
	
	REQ(esh_new_c_fn(esh, "utf16/encode", utf16_encode_fn, 1, 0, false));
	REQ(esh_set_global(esh, "utf16/encode"));
	
	REQ(esh_new_c_fn(esh, "co", coroutine_fn, 1, 0, false));
	REQ(esh_set_global(esh, "co"));
	
	REQ(esh_new_c_fn(esh, "next", next_fn, 1, 0, false));
	REQ(esh_set_global(esh, "next"));
	
	REQ(esh_new_c_fn(esh, "yield", yield_fn, 1, 0, false));
	REQ(esh_set_global(esh, "yield"));
	
	REQ(esh_new_c_fn(esh, "loop", loop_fn, 1, 0, false));
	REQ(esh_set_global(esh, "loop"));
	
	REQ(esh_new_c_fn(esh, "foreach", foreach, 2, 0, false));
	REQ(esh_set_global(esh, "foreach"));

	REQ(esh_new_c_fn(esh, "map", map, 2, 0, false));
	REQ(esh_make_coroutine(esh, -1));
	REQ(esh_set_global(esh, "map"));
	
	REQ(esh_new_c_fn(esh, "filter", filter, 2, 0, false));
	REQ(esh_make_coroutine(esh, -1));
	REQ(esh_set_global(esh, "filter"));
	
	REQ(esh_new_c_fn(esh, "chars", chars, 1, 0, false));
	REQ(esh_make_coroutine(esh, -1));
	REQ(esh_set_global(esh, "chars"));
	
	REQ(esh_new_c_fn(esh, "as-string", as_string, 1, 0, false));
	REQ(esh_set_global(esh, "as-string"));

	REQ(esh_new_c_fn(esh, "split", split, 1, 1, false));
	REQ(esh_make_coroutine(esh, -1));
	REQ(esh_set_global(esh, "split"));
	
	REQ(esh_new_c_fn(esh, "includes", includes, 2, 0, false));
	REQ(esh_set_global(esh, "includes"));
	
	REQ(esh_new_c_fn(esh, "iter", iter, 1, 0, false));
	REQ(esh_make_coroutine(esh, -1));
	REQ(esh_set_global(esh, "iter"));

	REQ(esh_new_c_fn(esh, "keys", keys, 1, 0, false));
	REQ(esh_make_coroutine(esh, -1));
	REQ(esh_set_global(esh, "keys"));
	
	REQ(esh_new_c_fn(esh, "values", values, 1, 0, false));
	REQ(esh_make_coroutine(esh, -1));
	REQ(esh_set_global(esh, "values"));

	REQ(esh_new_c_fn(esh, "entries", entries, 1, 0, false));
	REQ(esh_make_coroutine(esh, -1));
	REQ(esh_set_global(esh, "entries"));
	
	REQ(esh_new_c_fn(esh, "collect", collect, 1, 0, false));
	REQ(esh_set_global(esh, "collect"));

	REQ(esh_new_c_fn(esh, "nth", nth, 2, 0, false));
	REQ(esh_set_global(esh, "nth"));

	REQ(esh_new_c_fn(esh, "replace", replace, 3, 0, false));
	REQ(esh_set_global(esh, "replace"));

	#ifdef __unix__
	REQ(esh_unix_stdlib_init(esh));
	#endif
	
	const char *platform;
	#if defined(__unix__)
	platform = "unix";
	#elif defined(_WIN32)
	platform = "windows";
	#else
	platform = "other";
	#endif
	
	REQ(esh_new_string(esh, platform, strlen(platform)));
	REQ(esh_set_global(esh, "platform"));
	
	return 0;
}
