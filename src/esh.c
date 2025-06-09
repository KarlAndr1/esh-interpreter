#define INCLUDE_PRIVATE_API
#include "esh.h"
#include "esh_object.h"

#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

const char *esh_get_project_name() {
	#ifdef DEBUG
	return PROJECT_NAME " (DEBUG BUILD)";
	#else
	return PROJECT_NAME;
	#endif
}

const char *esh_get_version() {
	return MAJOR_VERSION "." MINOR_VERSION;
}

char *int_to_str(long long i, size_t *out_len) {
	// ceil(log10(2^64 - 1)) == 20
	#define MAX_DIGITS 20
	static char str[1 + MAX_DIGITS];	
	
	if(i == 0) {
		str[0] = '0';
		str[1] = '\0';
		*out_len = 1;
		return str;
	}
	
	bool negative = false;
	if(i < 0) {
		negative = true;
		i = -i;
	}
	
	size_t l = 0;
	char *s = str + sizeof(str);
	for(; l < MAX_DIGITS; l++) {
		if(i == 0) break;
		
		*(--s) = '0' + (i % 10);
		i /= 10;
	}
	
	if(negative) {
		*(--s) = '-';
		l++;
	}
	
	#undef MAX_DIGITS
	
	*out_len = l;
	return s;
}

static void inc_gc(esh_state *esh) {
	if(esh->gc_freq > 0) {
		esh->alloc_step++;
		if(esh->alloc_step >= (unsigned) esh->gc_freq) {
			esh->alloc_step = 0;
			esh_gc(esh, esh->gc_step_size);
		}
	}
}

void *esh_alloc(esh_state *esh, size_t n) {
	inc_gc(esh);
	return esh->realloc(NULL, n);
}

void esh_free(esh_state *esh, void *p) {
	esh->realloc(p, 0);
}

void *esh_realloc(esh_state *esh, void *p, size_t n) {
	inc_gc(esh);
	return esh->realloc(p, n);
}

static int opt_req_stack(esh_state *esh, size_t n) {
	assert(esh->current_thread->stack_len >= esh->current_thread->current_frame.stack_base);
	size_t len = esh->current_thread->stack_len - esh->current_thread->current_frame.stack_base;
	if(len < n) return 1;
	return 0;
}

#ifdef NO_STACK_CHECK 
#define opt_req_stack(a, b) (0)
#endif

static size_t stack_size(esh_state *esh) {
	assert(esh->current_thread->stack_len >= esh->current_thread->current_frame.stack_base);
	return esh->current_thread->stack_len - esh->current_thread->current_frame.stack_base;
}

void esh_err_printf(esh_state *esh, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vsnprintf(esh->err_buff, esh->err_buff_cap, fmt, args);
	va_end(args);
}

typedef struct stack_trace_buff {
	char *buff;
	size_t len, cap;
	bool err;
} stack_trace_buff;

static void stack_trace_buff_add(esh_state *esh, stack_trace_buff *buff, const char *s, size_t len) {
	if(buff->err) return;
	
	if(buff->len + len > buff->cap) {
		size_t new_cap = buff->cap * 3 / 2 + len;
		char *new_buff = esh_realloc(esh, buff->buff, sizeof(char) * new_cap);
		if(!new_buff) {
			buff->err = true;
			esh_free(esh, buff->buff);
			buff->buff = NULL;
			return;
		}
		
		buff->cap = new_cap;
		buff->buff = new_buff;
	}
	
	memcpy(buff->buff + buff->len, s, sizeof(char) * len);
	buff->len += len;
}

static void add_stack_trace_entry(esh_state *esh, esh_function *fn, size_t instr, stack_trace_buff *buff) {
	assert(fn != NULL);
	const char *fn_name = fn->name;
	if(!fn_name) fn_name = "Anonymous";
	
	stack_trace_buff_add(esh, buff, fn_name, strlen(fn_name));
	
	if(!fn->c_fn) {
		size_t line = 1;
		for(size_t i = 0; i < fn->line_dirs_len; i++) {
			if(fn->line_dirs[i].instr_index > instr) break;
			line = fn->line_dirs[i].line;
		}
		
		size_t line_str_len;
		char *line_str = int_to_str(line, &line_str_len);
		stack_trace_buff_add(esh, buff, ":", 1);
		stack_trace_buff_add(esh, buff, line_str, line_str_len);
	}
}

static void generate_stack_trace(esh_state *esh) {
	assert(esh->stack_trace == NULL);
	
	stack_trace_buff buff = { NULL, 0, 0, false };
	
	add_stack_trace_entry(esh, esh->current_thread->current_frame.fn, esh->current_thread->current_frame.instr_index, &buff);
	
	for(size_t i = esh->current_thread->stack_frames_len; i > 0; i--) {
		esh_stack_frame *frame = &esh->current_thread->stack_frames[i - 1];
		stack_trace_buff_add(esh, &buff, "\n", 1);
		add_stack_trace_entry(esh, frame->fn, frame->instr_index, &buff);
	}
	
	stack_trace_buff_add(esh, &buff, "\0", 1);
	
	esh->stack_trace = buff.buff;
}

const char *esh_get_err(esh_state *esh) {
	return esh->err_buff;
}

const char *esh_get_stack_trace(esh_state *esh) {
	if(!esh->stack_trace) return "No stack trace available";
	return esh->stack_trace;
}

bool esh_panic_caught(esh_state *esh) {
	return esh->panic_caught;
}

#ifndef NO_MALLOC
#include <stdlib.h>
static void *default_realloc_callback(void *p, size_t n) {
	if(n == 0) {
		free(p);
		return NULL;
	} else {
		return realloc(p, n);
	}
}
#endif

static void free_stack_frame(esh_state *esh, esh_stack_frame *frame);

static void coroutine_free(esh_state *esh, void *p) {
	esh_co_thread *c = p;
	for(size_t i = 0; i < c->stack_frames_len; i++) {
		free_stack_frame(esh, &c->stack_frames[i]);
	}
	free_stack_frame(esh, &c->current_frame);
	
	esh_free(esh, c->stack);
	esh_free(esh, c->stack_frames);
}

static esh_type string_type = { .name = "string", .on_free = NULL };
static esh_type function_type = { .name = "function implementation", .on_free = NULL };
static esh_type closure_type = { .name = "function", .on_free = NULL };
static esh_type env_type = { .name = "function environment", .on_free = NULL };
static esh_type co_thread_type = { .name = "coroutine", .on_free = coroutine_free };

static void *alloc_object(esh_state *esh, size_t s, esh_type *type);
static void free_object(esh_state *, esh_object *);

#define DEFAULT_ERR_BUFF_CAP 512
esh_state *esh_open(void *(*realloc)(void *, size_t)) {
	#ifndef NO_MALLOC
	if(!realloc) realloc = default_realloc_callback;
	#endif
	
	esh_state *esh = realloc(NULL, sizeof(esh_state));
	if(!esh) goto ERR_ALLOC_STATE;
	esh->realloc = realloc;
	
	esh->err_buff_cap = DEFAULT_ERR_BUFF_CAP;
	esh->err_buff = esh_alloc(esh, sizeof(char) * esh->err_buff_cap);
	if(!esh->err_buff) goto ERR_ALLOC_ERR_MSG;
	esh->err_buff[0] = '\0';
	esh->stack_trace = NULL;
	esh->panic_caught = false;
	
	esh->objects = NULL;
	esh->visited = NULL;
	esh->to_visit = NULL;
	
	esh->threads = NULL;
	esh->threads_len = 0;
	esh->threads_cap = 0;
	
	esh->current_thread = alloc_object(esh, sizeof(esh_co_thread), &co_thread_type);
	if(!esh->current_thread) goto ERR_ALLOC_COROUTINE;
	esh->current_thread->stack = NULL;
	esh->current_thread->stack_len = 0;
	esh->current_thread->stack_cap = 0;
	
	esh->current_thread->stack_frames = NULL;
	esh->current_thread->stack_frames_len = 0;
	esh->current_thread->stack_frames_cap = 0;
	
	esh->current_thread->is_done = false;
	
	esh->current_thread->current_frame = (esh_stack_frame) {
		.stack_base = 0,
		.fn = NULL,
		.env = NULL,
		.instr_index = 0,
		.n_args = 0,
		.expected_returns = 0,
		.c_locals = NULL,
		.c_locals_free = NULL,
		.catch_panic = false
	};
	
	esh->current_thread->stack_len = 0;
	esh->current_thread->stack_cap = 64; // Default stack size
	esh->current_thread->stack = esh_alloc(esh, sizeof(esh_val) * esh->current_thread->stack_cap);
	if(!esh->current_thread->stack) goto ERR_ALLOC_STACK;
	
	esh->saved_stack_len = 0;
	
	esh->gc_freq = 0; // Initially, the GC should be disabled
	esh->gc_step_size = 0;
	esh->alloc_step = 0;
	
	esh->globals = alloc_object(esh, sizeof(esh_object), NULL);
	if(!esh->globals) goto ERR_ALLOC_GLOBALS;
	
	esh->cmd = ESH_NULL;
	
	esh->gc_freq = 256; // Run the GC every 256 allocations
	esh->gc_step_size = 64; // Run at most 64 steps at a time
	
	esh->str_buff = NULL;
	esh->str_buff_len = 0;
	esh->str_buff_cap = 0;
	
	return esh;
	
	ERR_ALLOC_GLOBALS:
	
	ERR_ALLOC_STACK:
	free_object(esh, &esh->current_thread->obj);
	
	ERR_ALLOC_COROUTINE:
	esh_free(esh, esh->err_buff);
	
	ERR_ALLOC_ERR_MSG:
	esh->realloc(esh, 0);
	
	ERR_ALLOC_STATE:
	return NULL;
}

void esh_set_cmd(esh_state *esh) {
	if(stack_size(esh) == 0) return;
	esh->cmd = esh->current_thread->stack[--esh->current_thread->stack_len];
}

void esh_close(esh_state *esh) {
	esh_free(esh, esh->err_buff);
	esh_free(esh, esh->stack_trace);
	
	esh_free(esh, esh->str_buff);
	
	esh_free(esh, esh->threads);
	
	// Free all objects
	esh_object *next = esh->objects;
	for(esh_object *obj = next; next != NULL; obj = next) {
		next = obj->next;
		free_object(esh, obj);
	}
	
	next = esh->visited;
	for(esh_object *obj = next; next != NULL; obj = next) {
		next = obj->next;
		free_object(esh, obj);
	}
	
	next = esh->to_visit;
	for(esh_object *obj = next; next != NULL; obj = next) {
		next = obj->next;
		free_object(esh, obj);
	}
	
	esh->realloc(esh, 0);
}

void esh_pop(esh_state *esh, size_t n) {
	if(esh->current_thread->stack_len >= esh->current_thread->current_frame.stack_base + n) esh->current_thread->stack_len -= n;
	else esh->current_thread->stack_len = esh->current_thread->current_frame.stack_base;
}

int esh_req_stack(esh_state *esh, size_t n) {
	if(esh->current_thread->stack_cap - esh->current_thread->stack_len < n) {
		size_t new_cap = esh->current_thread->stack_cap * 3 / 2 + n;
		esh_val *new_stack = esh_realloc(esh, esh->current_thread->stack, sizeof(esh_val) * new_cap);
		if(!new_stack) {
			esh_err_printf(esh, "Unable to grow stack (out of memory?)");
			return 1;
		}
		esh->current_thread->stack = new_stack;
		esh->current_thread->stack_cap = new_cap;
	}
	return 0;
}

// VM

static void *val_as_object(esh_val val, const esh_type *type) {
	if(val == NULL) return NULL;
	int bit_tag = ((uintptr_t) val) & 1;
	
	if(bit_tag == 1) return NULL;
	
	esh_object *obj = val;
	if(type != NULL && obj->type != type) return NULL;
	
	return obj;
}

static const char *val_as_string(esh_val *val, size_t *opt_out_len) {
	if(((uintptr_t) *val) & 1) {
		char *s = (char *) val;
		#ifndef BIG_ENDIAN
		s++;
		#endif
		if(opt_out_len) *opt_out_len = strlen(s);
		return s;
	}
	
	esh_string *str = val_as_object(*val, &string_type);
	if(!str) return NULL;
	
	if(opt_out_len) *opt_out_len = str->len;
	return str->str;
}

static int stack_offset(esh_state *esh, long long offset, size_t *out_index) {
	size_t items_in_frame = esh->current_thread->stack_len - esh->current_thread->current_frame.stack_base;
	if(offset < 0) {
		if((size_t) -offset > items_in_frame) goto ERR;
		*out_index = esh->current_thread->stack_len + offset;
	} else {
		if((size_t) offset >= items_in_frame) goto ERR;
		*out_index = esh->current_thread->current_frame.stack_base + offset;
	}
	
	return 0;
	
	ERR:
	esh_err_printf(esh, "Invalid stack offset '%lli'. Only %llu items in stack frame", offset, (long long) items_in_frame);
	return 1;
}

const char *esh_as_string(esh_state *esh, long long offset, size_t *opt_out_len) {
	size_t index;
	if(stack_offset(esh, offset, &index)) return NULL;
	
	esh_val *val = &esh->current_thread->stack[index];
	const char *str = val_as_string(val, opt_out_len);
	if(!str) esh_err_printf(esh, "Unable to implicitly convert object to string");
	
	return str;
}

int val_as_int(esh_val *val, long long *out_int) {
	const char *str = val_as_string(val, NULL);
	if(!str) return 1;
	
	bool negative = false;
	if(*str == '-') {
		str++;
		negative = true;
	}
	
	*out_int = 0;
	
	while(*str >= '0' && *str <= '9') {
		*out_int *= 10;
		*out_int += *str - '0';
		str++;
	}
	
	if(negative) *out_int *= -1;
	return 0;
}

int esh_as_int(esh_state *esh, long long offset, long long *out_int) {
	size_t index;
	if(stack_offset(esh, offset, &index)) return 1;
	
	esh_val *val = &esh->current_thread->stack[index];
	if(val_as_int(val, out_int)) {
		esh_err_printf(esh, "Unable to implicitly convert value to integer");
		return 1;
	}
	return 0;
}

bool esh_is_null(esh_state *esh, long long offset) {
	size_t index;
	if(stack_offset(esh, offset, &index)) return false;
	
	return esh->current_thread->stack[index] == NULL;
}

bool esh_is_array(esh_state *esh, long long offset) {
	size_t index;
	if(stack_offset(esh, offset, &index)) return false;
	
	esh_object *obj = val_as_object(esh->current_thread->stack[index], NULL);
	if(!obj) return false;
	
	if(obj->len == 0) return false;
	
	for(size_t i = 0; i < obj->len; i++) {
		size_t keylen;
		const char *key = int_to_str(i, &keylen);
		esh_val _;
		if(!esh_object_get(esh, obj, key, keylen, &_)) return false;
	}
	
	return true;
}

bool val_as_bool(esh_val *val) {
	return *val != ESH_NULL;
}

bool esh_as_bool(esh_state *esh, long long offset) {
	size_t index;
	if(stack_offset(esh, offset, &index)) return false;
	
	return val_as_bool(&esh->current_thread->stack[index]);
}

void *esh_as_type(esh_state *esh, long long offset, const esh_type *type) {
	size_t index;
	if(stack_offset(esh, offset, &index)) return NULL;
	
	void *p = val_as_object(esh->current_thread->stack[index], type);
	if(p == NULL) esh_err_printf(esh, "Unable to implicitly convert value to %s", type->name);
	return p;
}

static int stack_push(esh_state *esh, esh_val val) {
	if(esh->current_thread->stack_len == esh->current_thread->stack_cap) {
		esh_err_printf(esh, "Stack overflow");
		return 1;
	}
	
	esh->current_thread->stack[esh->current_thread->stack_len++] = val;
	
	return 0;
}

static esh_val stack_pop(esh_state *esh, size_t n) {
	if(n == 0) return NULL;
	assert(esh->current_thread->stack_len > esh->current_thread->current_frame.stack_base);
	esh->current_thread->stack_len -= n;
	return esh->current_thread->stack[esh->current_thread->stack_len];
}

static int stack_resv(esh_state *esh, size_t n) {
	if(esh_req_stack(esh, n)) return 1;
	
	for(size_t i = 0; i < n; i++) esh->current_thread->stack[esh->current_thread->stack_len + i] = ESH_NULL;
	esh->current_thread->stack_len += n;
	return 0;
}

/*
const char *esh_val_as_str(esh_val *val, size_t *out_len) {
	if(val == NULL) return NULL;
	
	uintptr_t i = (uintptr_t) val;
	
	char *str;
	size_t len;
	
	if(i & 1) { // Inline / short string
		len = (i >> 1) & 0xFF;
		str = ((char *) val) + 1;
	} else {
		esh_object *obj = *val;
		if(obj->tag != TAG_STRING) return NULL;
		
		esh_string *str_obj = (esh_string *) obj;
		
		str = str_obj->str;
		len = str_obj->len;
	}
	
	if(out_len) *out_len = len;
	return str;
}
*/

static void *alloc_object(esh_state *esh, size_t s, esh_type *type) {
	assert(s >= sizeof(esh_object));
	
	esh_object *obj = esh_alloc(esh, s);
	if(!obj) {
		esh_err_printf(esh, "Unable to create object (ouf of memory?)");
		esh->current_thread->stack_len--;
		return NULL;
	}
	
	obj->type = type;
	
	obj->next = esh->objects;
	if(obj->next) obj->next->prev = obj;
	obj->prev = NULL;
	esh->objects = obj;
	
	obj->gc_tag = 0;
	
	esh_object_init_entries(esh, obj);
	
	return obj;
}

void *esh_new_object(esh_state *esh, size_t s, esh_type *type) {
	if(stack_push(esh, NULL)) return NULL;
	
	esh_object *obj = alloc_object(esh, s, type);
	if(!obj) return NULL;

	esh->current_thread->stack[esh->current_thread->stack_len - 1] = obj;
	
	return obj;
}

static void gc_obj_write_barrier(esh_state *esh, esh_object *obj);

int esh_new_array(esh_state *esh, size_t n) {
	if(stack_size(esh) < n) {
		esh_err_printf(esh, "Not enough items on stack to create array (%zu/%zu)", stack_size(esh), n);
		return 1;
	}
	
	esh_object *obj = esh_new_object(esh, sizeof(esh_object), NULL);
	if(!obj) return 1;
	
	for(size_t i = 0; i < n; i++) {
		size_t keylen;
		char *key = int_to_str(i, &keylen);
		
		gc_obj_write_barrier(esh, esh->globals);
		if(esh_object_set(esh, obj, key, keylen, esh->current_thread->stack[esh->current_thread->stack_len - n - 1 + i])) {
			esh_err_printf(esh, "Unable to set array entry (out of memory?)");
			esh->current_thread->stack_len--;
			return 1;
		}
	}
	
	esh_val obj_val = esh->current_thread->stack[esh->current_thread->stack_len - 1];
	esh->current_thread->stack_len -= n;
	esh->current_thread->stack[esh->current_thread->stack_len - 1] = obj_val;
	return 0;
}

int esh_push_int(esh_state *esh, long long i) {
	size_t len;
	char *str = int_to_str(i, &len);
	
	return esh_new_string(esh, str, len);
}

int esh_push_bool(esh_state *esh, bool b) {
	if(b) return esh_new_string(esh, "true", 4);
	return esh_push_null(esh);
}

bool vals_equal(esh_val *a, esh_val *b) {
	if(*a == *b) return true;
	
	size_t alen, blen;
	const char *astr, *bstr;
	
	if(!(astr = val_as_string(a, &alen))) return false;
	if(!(bstr = val_as_string(b, &blen))) return false;
	
	if(alen != blen) return false;
	
	return memcmp(astr, bstr, sizeof(char) * alen) == 0;
}

/*
int esh_new_array(esh_state *esh, size_t n) {
	if(stack_size(esh) < n) {
		esh_err_printf(esh, "Not enough items on stack to create array (%zi/%zi)", stack_size(esh), n);
		return 1;
	}
	
	esh_object *obj = esh_new_object(esh, sizeof(esh_object), ESH_TAG_OBJECT);
	if(!obj) {
		esh_err_printf(esh, "Unable to create array object (out of memory?)");
		return 1;
	}
	
	for(size_t i = 0; i < n; i++) {
		size_t len;
		const char *key = int_to_str(i, &len);
		
		if(esh_object_set(esh, obj, key, len, esh->current_thread->stack[esh->current_thread->stack_len - n - 1 + i])) {
			esh_pop(esh);
			esh_err_printf(esh, "Unable to set array object members (out of memory?)");
			return 1;
		}
	}
	
	esh_val obj_val = esh->current_thread->stack[esh->current_thread->stack_len - 1];
	esh->current_thread->stack_len -= n + 1;
	esh->current_thread->stack[esh->current_thread->stack_len++] = obj_val;
	
	return 0;
}
*/

int esh_new_fn(esh_state *esh, const char *name, size_t name_len) {
	esh_function *fn = esh_new_object(esh, sizeof(esh_function), &function_type);
	if(!fn) return 1;
	
	fn->imms = NULL;
	fn->imms_len = 0;
	fn->imms_cap = 0;
	
	fn->jmps = 0;
	fn->jmps_len = 0;
	fn->jmps_cap = 0;
	
	fn->n_args = 0;
	fn->opt_args = 0;
	fn->n_locals = 0;
	fn->upval_locals = false;
	
	fn->instr = NULL;
	fn->instr_len = 0;
	fn->instr_cap = 0;
	
	fn->line_dirs = NULL;
	fn->line_dirs_len = 0;
	fn->line_dirs_cap = 0;
	
	fn->c_fn = NULL;
	
	fn->variadic = false;
	
	if(name != NULL) {
		fn->name = esh_alloc(esh, sizeof(char) * (name_len + 1));
		if(!fn->name) {
			esh_err_printf(esh, "Unable to allocate buffer for function name (out of memory?)");
			return 1;
		}
		memcpy(fn->name, name, sizeof(char) * name_len);
		fn->name[name_len] = '\0';
	} else {
		fn->name = NULL;
	}
	
	return 0;
}

int esh_new_c_fn(esh_state *esh, const char *name, esh_fn_result (*f)(esh_state *, size_t, size_t), size_t n_args, size_t opt_args, bool variadic) {
	esh_closure *cl = esh_new_object(esh, sizeof(esh_closure), &closure_type);
	if(!cl) return 1;
	cl->env = NULL;
	cl->fn = NULL;
	cl->obj.is_const = true;
	cl->is_coroutine = false;
	
	if(esh_new_fn(esh, name, strlen(name))) {
		esh_pop(esh, 1);
		return 1;
	}
	
	esh_function *fn = esh_as_type(esh, -1, &function_type);
	fn->n_args = n_args;
	fn->opt_args = opt_args;
	cl->fn = fn;
	cl->env = NULL;
	
	fn->c_fn = f;
	
	fn->variadic = variadic;
	
	esh_pop(esh, 1);
	
	return 0;
}

int esh_set_global(esh_state *esh, const char *name) {
	if(stack_size(esh) == 0) {
		esh_err_printf(esh, "Not enough items on stack to set global");
		return 1;
	}
	
	esh_val val = esh->current_thread->stack[esh->current_thread->stack_len - 1];
	gc_obj_write_barrier(esh, esh->globals);
	if(esh_object_set(esh, esh->globals, name, strlen(name), val)) {
		esh->current_thread->stack_len--;
		esh_err_printf(esh, "Unable to set global (out of memory?)");
		return 1;
	}
	
	esh->current_thread->stack_len--;
	return 0;
}

int esh_get_global(esh_state *esh, const char *name) {
	esh_val val;
	if(esh_object_get(esh, esh->globals, name, strlen(name), &val)) {
		if(stack_push(esh, val)) return 1;
		
		return 0;
	}
	
	esh_err_printf(esh, "Unkown global variable '%s'", name);
	return 1;
}

int esh_new_string(esh_state *esh, const char *str, size_t len) {
	if(len < sizeof(void *) - 2) {
		uintptr_t short_str = 1;
		char *s = (char *) &short_str;
		#ifndef BIG_ENDIAN
		s++; 
		#endif
		if(len != 0) memcpy(s, str, sizeof(char) * len);
		s[len] = '\0';
		if(stack_push(esh, (esh_val) short_str)) return 1;
		
		return 0;
	}
	
	esh_string *obj = esh_new_object(esh, sizeof(esh_string) + sizeof(char) * (len + 1), &string_type);
	if(!obj) return 1;
	
	obj->obj.is_const = true;
	
	obj->len = len;
	memcpy(obj->str, str, sizeof(char) * len);
	obj->str[len] = '\0';
	
	return 0;
}

int esh_push_null(esh_state *esh) {
	if(stack_push(esh, NULL)) return 1;
	return 0;
}

int esh_object_of(esh_state *esh, size_t n) {
	if(stack_size(esh) < n * 2) {
		esh_err_printf(esh, "Not enough items on stack for object creation (%zu/%zu)", (size_t) stack_size(esh), (size_t) n * 2);
		return 1;
	}
	
	esh_object *obj = esh_new_object(esh, sizeof(esh_object), NULL);
	if(!obj) return 1;
	
	for(size_t i = 0; i < n; i++) {
		size_t index = esh->current_thread->stack_len - (n - i) * 2 - 1;
		size_t keylen;
		const char *key = val_as_string(&esh->current_thread->stack[index], &keylen);
		if(!key) {
			esh_err_printf(esh, "Key value is not string");
			return 1;
		}
		
		esh_val val = esh->current_thread->stack[index + 1];
		
		gc_obj_write_barrier(esh, obj);
		if(esh_object_set(esh, obj, key, keylen, val)) {
			esh_err_printf(esh, "Unable to add entry to object literal (out of memory?)");
			return 1;
		}
	}
	
	esh_val obj_val = esh->current_thread->stack[esh->current_thread->stack_len - 1];
	esh->current_thread->stack_len -= n * 2;
	esh->current_thread->stack[esh->current_thread->stack_len - 1] = obj_val;
	
	return 0;
}

size_t esh_object_len(esh_state *esh, long long offset) {
	size_t index;
	if(stack_offset(esh, offset, &index)) return 0;
	
	esh_object *obj = val_as_object(esh->current_thread->stack[index], NULL);
	if(!obj) return 0;
	
	return obj->len;
}

int esh_dup(esh_state *esh, long long offset) {
	size_t index;
	if(stack_offset(esh, offset, &index)) return 1;
	
	if(stack_push(esh, esh->current_thread->stack[index])) return 1;
	
	return 0;
}

int esh_swap(esh_state *esh, long long a, long long b) {
	size_t index_a, index_b;
	if(stack_offset(esh, a, &index_a) || stack_offset(esh, b, &index_b)) return 1;
	
	esh_val tmp = esh->current_thread->stack[index_a];
	esh->current_thread->stack[index_a] = esh->current_thread->stack[index_b];
	esh->current_thread->stack[index_b] = tmp;
	
	return 0;
}

static void free_object(esh_state *esh, esh_object *obj) {
	if(obj->type && obj->type->on_free) obj->type->on_free(esh, obj);
	
	if(obj->type == &function_type) {
		esh_function *fn = (esh_function *) obj;
		esh_free(esh, fn->imms);
		esh_free(esh, fn->jmps);
		esh_free(esh, fn->instr);
		esh_free(esh, fn->line_dirs);
		esh_free(esh, fn->name);
	}
	
	esh_object_free_entries(esh, obj);
	esh_free(esh, obj);
}

#define INSTR_SIZE 4

static int encode_instr(esh_state *esh, uint8_t *instr, esh_opcode op, uint64_t arg, uint64_t l) {
	#define MAXBIT(x) ((1 << x) - 1)
	if(arg > MAXBIT(16)) {
		esh_err_printf(esh, "Instruction argument out of range (too many locals, globals, immediates, branches etc?)");
		return 1;
	}
	if(l > MAXBIT(8)) {
		esh_err_printf(esh, "Instruction l-argument out of range (attempting to access local variable from deeply nested scope or closure?)");
		return 1;
	}
	#undef MAXBIT
	
	instr[0] = op;
	instr[1] = arg & 0xFF;
	instr[2] = (arg >> 8) & 0xFF;
	instr[3] = l & 0xFF;
	
	return 0;
}

int esh_fn_append_instr(esh_state *esh, esh_opcode op, uint64_t arg, uint64_t l) {
	esh_function *fn = esh_as_type(esh, -1, &function_type);
	if(!fn) {
		esh_err_printf(esh, "Attempting to append instruction to non-function object");
		return 1;
	}
	
	if(fn->instr_len == fn->instr_cap) {
		size_t new_cap = fn->instr_cap * 3 / 2 + 1;
		uint8_t *new_buff = esh_realloc(esh, fn->instr, sizeof(uint8_t) * INSTR_SIZE * new_cap);
		if(!new_buff) {
			esh_err_printf(esh, "Unable to grow instruction buffer (out of memory?)");
			return 1;
		}
		
		fn->instr_cap = new_cap;
		fn->instr = new_buff;
	}
	
	if(encode_instr(esh, fn->instr + fn->instr_len * INSTR_SIZE, op, arg, l)) return 1;
	fn->instr_len++;
	
	return 0;
}

int esh_fn_finalize(esh_state *esh, size_t n_args, size_t opt_args, size_t n_locals, bool upval_locals, bool make_closure) {
	esh_function *fn = esh_as_type(esh, -1, &function_type);
	if(!fn) {
		esh_err_printf(esh, "Attempting to set locals count of non-function object");
		return 1;
	}
	
	if(make_closure) {
	esh_closure *cl = esh_new_object(esh, sizeof(esh_closure), &closure_type);
		if(!cl) return 1;
		
		cl->fn = fn;
		cl->env = NULL;
		cl->obj.is_const = true;
		cl->is_coroutine = false;
		esh->current_thread->stack[esh->current_thread->stack_len - 2] = esh->current_thread->stack[esh->current_thread->stack_len - 1];
		esh->current_thread->stack_len--;
	}
	
	fn->n_args = n_args;
	fn->opt_args = opt_args;
	fn->n_locals = n_locals;
	fn->upval_locals = upval_locals;
	
	return 0;
}

int esh_fn_add_imm(esh_state *esh, uint64_t *out_ref) {
	esh_function *fn = esh_as_type(esh, -2, &function_type);
	if(!fn) {
		esh_err_printf(esh, "Attempting to add immediate to non-function object");
		return 1;
	}
	
	gc_obj_write_barrier(esh, &fn->obj);
	
	assert(esh->current_thread->stack_len >= 2);
	esh_val val = esh->current_thread->stack[esh->current_thread->stack_len - 1];

	if(fn->imms_len == fn->imms_cap) {
		size_t new_cap = fn->imms_cap * 3 / 2 + 1;
		esh_val *new_buff = esh_realloc(esh, fn->imms, sizeof(esh_val) * new_cap);
		if(!new_buff) {
			esh_err_printf(esh, "Unable to grow immediate buffer (out of memory?)");
			return 1;
		}
		
		fn->imms_cap = new_cap;
		fn->imms = new_buff;
	}
	
	*out_ref = fn->imms_len;
	fn->imms[fn->imms_len++] = val;
	esh_pop(esh, 1);
	
	return 0;
}

int esh_fn_new_label(esh_state *esh, uint64_t *out_ref) {
	esh_function *fn = esh_as_type(esh, -1, &function_type);
	if(!fn) {
		esh_err_printf(esh, "Attempting to add label to non-function object");
		return 1;
	}
	
	if(fn->jmps_len == fn->jmps_cap) {
		size_t new_cap = fn->jmps_cap * 3 / 2 + 1;
		size_t *new_buff = esh_realloc(esh, fn->jmps, sizeof(size_t) * new_cap);
		if(!new_buff) {
			esh_err_printf(esh, "Unable to grow label buffer (out of memory?)");
			return 1;
		}
		
		fn->jmps_cap = new_cap;
		fn->jmps = new_buff;
	}
	
	*out_ref = fn->jmps_len;
	fn->jmps[fn->jmps_len++] = fn->instr_len;
	return 0;
}

int esh_fn_put_label(esh_state *esh, uint64_t label) {
	esh_function *fn = esh_as_type(esh, -1, &function_type);
	if(!fn) {
		esh_err_printf(esh, "Attempting to set label of non-function object");
		return 1;
	}
	
	if(label >= fn->jmps_len) {
		esh_err_printf(esh, "Label index out of bounds");
		return 1;
	}
	
	fn->jmps[label] = fn->instr_len;
	return 0;
}

int esh_fn_line_directive(esh_state *esh, size_t line) {
	esh_function *fn = esh_as_type(esh, -1, &function_type);
	if(!fn) {
		esh_err_printf(esh, "Attempting to set label of non-function object");
		return 1;
	}
	
	if(fn->line_dirs_len != 0 && fn->line_dirs[fn->line_dirs_len - 1].line == line) return 0;
	
	if(fn->line_dirs_len == fn->line_dirs_cap) {
		size_t new_cap = fn->line_dirs_cap * 3 / 2 + 1;
		esh_fn_line_dir *new_buff = esh_realloc(esh, fn->line_dirs, sizeof(esh_fn_line_dir) * new_cap);
		if(!new_buff) {
			esh_err_printf(esh, "Unable to grow function line directive buffer (out of memory?)");
			return 1;
		}
		
		fn->line_dirs_cap = new_cap;
		fn->line_dirs = new_buff;
	}
	
	fn->line_dirs[fn->line_dirs_len++] = (esh_fn_line_dir) { .instr_index = fn->instr_len, .line = line };
	return 0;
}

/*
int esh_fn_ref_instr(esh_state *esh, size_t *out_ref) {
	esh_function *fn = as_object(esh, ESH_TAG_FUNCTION, 1);
	if(!fn) {
		esh_err_printf(esh, "Attempting to reference instruction of non-function object");
		return 1;
	}
	
	if(fn->instr_len == 0) {
		esh_err_printf(esh, "Attempting to reference instruction in empty function");
		return 1;
	}
	*out_ref = fn->instr_len - 1;
	return 0;
}

int esh_fn_set_instr(esh_state *esh, size_t ref, esh_opcode op, uint64_t arg, uint64_t l) {
	esh_function *fn = as_object(esh, ESH_TAG_FUNCTION, 1);
	if(!fn) {
		esh_err_printf(esh, "Attempting to set instruction in non-function object");
		return 1;
	}
	
	if(ref >= fn->instr_len) {
		esh_err_printf(esh, "Instruction index out of bounds");
		return 1;
	}
	
	return encode_instr(esh, fn->instr + ref * INSTR_SIZE, op, arg, l);
}
*/

const char *instr_name(esh_opcode op) {
	switch(op) {
		#define X(op) case ESH_INSTR_##op: return #op;
		ESH_OPCODES
		#undef X
	}
	
	assert(false);
	return "";
}

typedef struct instr_regs {
	esh_opcode op;
	uint16_t arg;
	uint8_t l;
} instr_regs;

static void decode_instr(uint8_t *p, instr_regs *out) {
	out->op = p[0];
	out->arg = p[1] | (p[2] << 8);
	out->l = p[3];
}

static void print_val(esh_val val, FILE *f) {
	if(val == NULL) {
		fputs("Null", f);
		return;
	}
	
	const char *str = val_as_string(&val, NULL);
	if(str) {
		fprintf(f, "\"%s\"", str);
		return;
	}
	
	if(val_as_object(val, &function_type)) {
		fputs("Function Impl", f);
		return;
	}
	
	if(val_as_object(val, &closure_type)) {
		fputs("Function", f);
		return;
	}
	
	esh_object *obj = val_as_object(val, NULL);
	if(obj) {
		fprintf(f, "Object [%s] (%llu, %llu)", obj->type? obj->type->name : "object", (unsigned long long) obj->len, (unsigned long long) obj->cap);
		return;
	}
	
	fputs("Other", f);
	return;
}

int esh_fndump(esh_state *esh, FILE *f) {
	esh_closure *cl = esh_as_type(esh, -1, &closure_type);
	if(!cl) {
		esh_err_printf(esh, "Attempting to fndump non-function object");
		return 1;
	}
	esh_function *fn = cl->fn;
	
	fprintf(f, "Arguments: %zu\nLocals: %zu\nImms: %zu\n",  fn->n_args, fn->n_locals, fn->imms_len);
	
	for(size_t i = 0; i < fn->instr_len; i++) {
		bool label = false;
		for(size_t j = 0, k = 0; j < fn->jmps_len; j++) {
			if(fn->jmps[j] == i) {
				label = true;
				if(k != 0) fputs(", ", f);
				fprintf(f, "%zu", j);
				k++;
			}
		}
		if(label) fputs(":\n", f);
		
		size_t index = i * INSTR_SIZE;
		instr_regs instr;
		decode_instr(fn->instr + index, &instr);
		
		fprintf(f, "%s (%u:%u)", instr_name(instr.op), instr.arg, instr.l);
		if((instr.op == ESH_INSTR_IMM || instr.op == ESH_INSTR_LOAD_G || instr.op == ESH_INSTR_STORE_G) && instr.arg < fn->imms_len) {
			fputs(" # ", f);
			print_val(fn->imms[instr.arg], f);
		} else if(instr.op == ESH_INSTR_JMP || instr.op == ESH_INSTR_JMP_IFN || instr.op == ESH_INSTR_JMP_IF) {
			fprintf(f, " # %llu", (unsigned long long) instr.arg);
		}
		
		putc('\n', f);
	}
	
	return 0;
}

void esh_stackdump(esh_state *esh, FILE *f) {
	fprintf(f, "Length: %zu\nBase: %zu\n", esh->current_thread->stack_len, esh->current_thread->current_frame.stack_base);
	fputs("__STACK TOP__\n", f);
	for(size_t i = esh->current_thread->stack_len; i > 0; i--) {
		size_t index = i - 1;
		
		esh_val val = esh->current_thread->stack[index];
		print_val(val, f);
		putc('\n', f);
		
		if(index == esh->current_thread->current_frame.stack_base) fputs("__STACK BASE__\n", f);
	}
	fputs("__STACK END__\n", f);
}

void *esh_locals(esh_state *esh, size_t size, void (*free)(esh_state *, void *)) {
	if(esh->current_thread->current_frame.c_locals) return esh->current_thread->current_frame.c_locals;
	
	esh->current_thread->current_frame.c_locals = esh_alloc(esh, size);
	esh->current_thread->current_frame.c_locals_free = free;
	if(!esh->current_thread->current_frame.c_locals) {
		esh_err_printf(esh, "Unable to allocate C function locals (out of memory?)");
	}
	
	return esh->current_thread->current_frame.c_locals;
}

int esh_make_coroutine(esh_state *esh, long long fn) {
	size_t index;
	if(stack_offset(esh, fn, &index)) return 1;
	
	esh_closure *f = val_as_object(esh->current_thread->stack[index], &closure_type);
	if(!f) {
		esh_err_printf(esh, "Attempting to create coroutine from non-function object");
		return 1;
	}
	
	f->is_coroutine = true;
	return 0;
}

static void obj_list_pop(esh_object **root, esh_object *obj) {	
	if(obj->prev == NULL) {
		assert(*root == obj);
		*root = obj->next;
		if(obj->next) obj->next->prev = NULL;
	} else {
		obj->prev->next = obj->next;
		if(obj->next) obj->next->prev = obj->prev;
	}
	
	obj->next = NULL;
	obj->prev = NULL;
}

static void obj_list_add(esh_object **root, esh_object *obj) {
	assert(obj->next == NULL);
	assert(obj->prev == NULL);
	
	if(*root) {
		assert((*root)->prev == NULL);
		(*root)->prev = obj;
	}
	
	obj->next = *root;
	*root = obj;
}

static void gc_mark_to_visit(esh_state *esh, esh_val val) {
	esh_object *obj = val_as_object(val, NULL);
	if(!obj) return; // Non heap allocated objects/values are ignored
	
	if(obj->gc_tag == 2 || obj->gc_tag == 1) return; // Visited
	
	obj_list_pop(&esh->objects, obj);
	obj_list_add(&esh->to_visit, obj);
	
	obj->gc_tag = 1;
}

static void gc_mark_stack_frame(esh_state *esh, esh_stack_frame *frame) {
	gc_mark_to_visit(esh, frame->fn);
	gc_mark_to_visit(esh, frame->env);
}

static void gc_trace_obj(esh_state *esh, esh_object *obj) {
	assert(obj->gc_tag == 1); // Must be in the "to-visit" set

	for(size_t i = 0; i < obj->cap; i++) {
		esh_object_entry *entry = &obj->entries[i];
		if(entry->key == NULL || entry->deleted) continue;
		
		gc_mark_to_visit(esh, entry->val);
	}
	
	if(obj->type == &function_type) {
		esh_function *fn = (esh_function *) obj;
		for(size_t i = 0; i < fn->imms_len; i++) gc_mark_to_visit(esh, fn->imms[i]);
	} else if(obj->type == &closure_type) {
		esh_closure *cl = (esh_closure *) obj;
		gc_mark_to_visit(esh, cl->fn);
		gc_mark_to_visit(esh, cl->env);
	} else if(obj->type == &env_type) {
		esh_env *env = (esh_env *) obj;
		for(size_t i = 0; i < env->n_locals; i++) gc_mark_to_visit(esh, env->locals[i]);
		gc_mark_to_visit(esh, env->parent);
	} else if(obj->type == &co_thread_type) {
		esh_co_thread *co = (esh_env *) obj;
		for(size_t i = 0; i < co->stack_len; i++) gc_mark_to_visit(esh, co->stack[i]);
		for(size_t i = 0; i < co->stack_frames_len; i++)  gc_mark_stack_frame(esh, &co->stack_frames[i]);
		gc_mark_stack_frame(esh, &co->current_frame);
	}
}

void esh_gc(esh_state *esh, size_t n) {
	bool do_full_sweep = n == 0;
	
	// Scan roots
	for(size_t i = 0; i < esh->current_thread->stack_len; i++) {
		gc_mark_to_visit(esh, esh->current_thread->stack[i]);
	}
	for(size_t i = 0; i < esh->current_thread->stack_frames_len; i++) {
		gc_mark_stack_frame(esh, &esh->current_thread->stack_frames[i]);
	}
	gc_mark_stack_frame(esh, &esh->current_thread->current_frame);
	
	gc_mark_to_visit(esh, esh->globals);
	gc_mark_to_visit(esh, esh->cmd);
	gc_mark_to_visit(esh, esh->current_thread);
	
	for(size_t i = 0; i < esh->threads_len; i++) {
		gc_mark_to_visit(esh, esh->threads[i]);
	}
	
	// Iterate over the to_visit set
	while(esh->to_visit) {
		if(!do_full_sweep) {
			if(n == 0) return;
			n--;
		}
		
		esh_object *obj = esh->to_visit;
		obj_list_pop(&esh->to_visit, obj);
		
		gc_trace_obj(esh, obj);
		
		obj->gc_tag = 2; // Move to the "visited" set
		obj_list_add(&esh->visited, obj);
	}
	
	size_t alive = 0, freed = 0;
	esh_object *next;
	for(esh_object *i = esh->objects; i != NULL; i = next) {
		freed++;
		next = i->next;
		free_object(esh, i);
	}
	
	esh->objects = esh->visited;
	esh->visited = NULL;
	for(esh_object *i = esh->objects; i != NULL; i = i->next) {
		alive++;
		i->gc_tag = 0;
	}
	
	assert(esh->visited == NULL);
	assert(esh->to_visit == NULL);
	assert(esh->objects == NULL || esh->objects->prev == NULL);
	
	//printf("%zu objects left (%zu freed)\n", alive, freed);
}

void esh_gc_conf(esh_state *esh, int gc_freq, int gc_step_size) {
	if(gc_freq != -1) esh->gc_freq = gc_freq;
	if(gc_step_size != -1) esh->gc_step_size = gc_step_size;
}

static void gc_obj_write_barrier(esh_state *esh, esh_object *obj) {
	if(obj->gc_tag == 2) {
		obj->gc_tag = 1;
		obj_list_pop(&esh->visited, obj);
		obj_list_add(&esh->to_visit, obj);
	}
}

esh_iterator esh_iter_begin(esh_state *esh) {
	(void) esh;
	return (esh_iterator) { false, -1, 0 };
}
int esh_iter_next(esh_state *esh, long long offset, esh_iterator *iter) {
	if(iter->done) {
		esh_err_printf(esh, "Attempting to iterate past end of object");
		return 1;
	}
	
	iter->step++;
	
	size_t index;
	if(stack_offset(esh, offset, &index)) return 1;
	esh_object *obj = val_as_object(esh->current_thread->stack[index], NULL);
	if(!obj) { // Strings/inline values have no members
		iter->done = true;
		return 0;
	}
	
	while(iter->index < obj->cap) {
		esh_object_entry *entry = &obj->entries[iter->index];
		if(entry->key != NULL && !entry->deleted) {
			if(esh_new_string(esh, entry->key, entry->keylen)) return 1;
			if(stack_push(esh, entry->val)) return 1;
			
			iter->index++;
			return 0;
		}
		
		iter->index++;
	}
	
	iter->done = true;
	return 0;
}

int esh_index(esh_state *esh, long long obj, long long key) {
	size_t index;
	if(stack_offset(esh, key, &index)) return 1;
	
	size_t keylen;
	const char *keystr = val_as_string(&esh->current_thread->stack[index], &keylen);
	if(!keystr) {
		esh_err_printf(esh, "Attempting to index object with non-string key");
		return 1;
	}
	
	return esh_index_s(esh, obj, keystr, keylen);
}

int esh_index_s(esh_state *esh, long long object, const char *key, size_t keylen) {
	size_t index;
	if(stack_offset(esh, object, &index)) return 1;
	
	esh_val val = NULL;
	esh_object *obj = val_as_object(esh->current_thread->stack[index], NULL);
	if(!obj) goto END;
	
	esh_val res;
	if(esh_object_get(esh, obj, key, keylen, &res)) val = res;
	
	END:
	if(stack_push(esh, val)) return 1;
	return 0;
}

int esh_index_i(esh_state *esh, long long object, long long i) {
	size_t keylen;
	char *key = int_to_str(i, &keylen);
	return esh_index_s(esh, object, key, keylen);
}

int esh_set(esh_state *esh, long long obj, long long key, long long value) {
	size_t key_index;
	if(stack_offset(esh, key, &key_index)) return 1;
	
	size_t strlen;
	const char *str = val_as_string(&esh->current_thread->stack[key_index], &strlen);
	if(!str) {
		esh_err_printf(esh, "Attempting to use non-string value as key");
		return 1;
	}
	
	return esh_set_s(esh, obj, str, strlen, value);
}

int esh_set_s(esh_state *esh, long long obj, const char *key, size_t keylen, long long value) {
	size_t obj_index, value_index;
	if(stack_offset(esh, obj, &obj_index) || stack_offset(esh, value, &value_index)) return 1;
	
	esh_object *obj_p = val_as_object(esh->current_thread->stack[obj_index], NULL);
	if(!obj_p) {
		esh_err_printf(esh, "Attempting to set index of immutable object");
		return 1;
	}
	
	gc_obj_write_barrier(esh, obj_p);
	return esh_object_set(esh, obj_p, key, keylen, esh->current_thread->stack[value_index]);
}

int esh_set_cs(esh_state *esh, long long obj, const char *key, long long value) {
	return esh_set_s(esh, obj, key, strlen(key), value);
}

int esh_set_i(esh_state *esh, long long obj, long long i, long long value) { 
	size_t strlen;
	const char *str = int_to_str(i, &strlen);
	return esh_set_s(esh, obj, str, strlen, value);
}

void esh_save_stack(esh_state *esh) {
	esh->saved_stack_len = esh->current_thread->stack_len;
}

void esh_restore_stack(esh_state *esh) {
	if(esh->saved_stack_len >= esh->current_thread->current_frame.stack_base && esh->saved_stack_len <= esh->current_thread->stack_len) esh->current_thread->stack_len = esh->saved_stack_len;
}

void esh_str_buff_begin(esh_state *esh) {
	esh->str_buff_len = 0;
}

static int str_buff_resv(esh_state *esh, size_t n) {
	if(esh->str_buff_len + n > esh->str_buff_cap) {
		size_t new_cap = esh->str_buff_cap * 3 / 2 + n;
		char *new_buff = esh_realloc(esh, esh->str_buff, sizeof(char) * new_cap);
		if(!new_buff) {
			esh_err_printf(esh, "Unable to grow string buffer (out of memory?)");
			return 1;
		}
		
		esh->str_buff = new_buff;
		esh->str_buff_cap = new_cap;
	}
	
	return 0;
}
int esh_str_buff_appends(esh_state *esh, const char *str, size_t len) {
	if(len == 0) return 0;
	if(str_buff_resv(esh, len)) return 1;
	
	memcpy(esh->str_buff + esh->str_buff_len, str, sizeof(char) * len);
	esh->str_buff_len += len;
	
	return 0;
}
int esh_str_buff_appendc(esh_state *esh, char c) {
	if(str_buff_resv(esh, 1)) return 1;
	esh->str_buff[esh->str_buff_len++] = c;
	
	return 0;
}
char *esh_str_buff(esh_state *esh, size_t *opt_out_len) {
	if(opt_out_len) *opt_out_len = esh->str_buff_len;
	return esh->str_buff;
}

static esh_env *new_env_object(esh_state *esh, size_t n_locals) {
	esh_env *env = esh_new_object(esh, sizeof(esh_env) + sizeof(esh_val) * n_locals, &env_type);
	if(!env) return NULL;
	
	env->n_locals = n_locals;
	for(size_t i = 0; i < env->n_locals; i++) env->locals[i] = ESH_NULL;
	esh_pop(esh, 1);
	
	return env;
}

#define ESH_FN_DEFAULT_STACK_CAP 64 // Make sure that there's at least this much capacity for the stack for interpreted functions; even when the arguments are passed or locals are allocated on the stack (e.g the amount of allocated stack space should be n_locals + this)
#define C_FN_DEFAULT_STACK_CAP 16 // C functions require less stack capacity, as they can request more if needed

static int create_coroutine(esh_state *esh, size_t n_args, size_t expected_returns, esh_closure *fn) {
	assert(fn->is_coroutine);
	
	esh_val *args = &esh->current_thread->stack[esh->current_thread->stack_len - n_args];

	bool is_c_fn = fn->fn->c_fn != NULL;
	
	size_t required_stack_space;
	if(!is_c_fn) {
		if(fn->fn->upval_locals) {
			required_stack_space = ESH_FN_DEFAULT_STACK_CAP;
		} else {
			required_stack_space = ESH_FN_DEFAULT_STACK_CAP + fn->fn->n_locals;
		}
	} else {
		required_stack_space = C_FN_DEFAULT_STACK_CAP + n_args;
	}
	
	esh_co_thread *coroutine = esh_new_object(esh, sizeof(esh_co_thread), &co_thread_type);
	if(!coroutine) return 1;
	*coroutine = (esh_co_thread) {
		.obj = coroutine->obj,
		.stack = NULL,
		.stack_len = 0,
		.stack_cap = 0,
		
		.stack_frames = NULL,
		.stack_frames_len = 0,
		.stack_frames_cap = 0,
		
		.current_frame = {
			.stack_base = 0,
			.fn = fn->fn,
			.env = NULL,
			.instr_index = 0,
			.n_args = n_args,
			.expected_returns = 1,
			.c_locals = NULL,
			.c_locals_free = NULL,
			.catch_panic = false
		},
		
		.is_done = false
	};
	
	coroutine->stack = esh_alloc(esh, sizeof(esh_val *) * required_stack_space);
	if(!coroutine->stack) {
		esh_err_printf(esh, "Unable to allocate coroutine's stack, out of memory?");
		return 1;
	}
	coroutine->stack_cap = required_stack_space;
	
	if(!is_c_fn && fn->fn->upval_locals) { // The check for !is_c_fn might be redundant, as only interpreted functions should ever have upval_locals set
		coroutine->current_frame.env = new_env_object(esh, fn->fn->n_locals);
		gc_obj_write_barrier(esh, &coroutine->obj);
		if(!coroutine->current_frame.env) return 1;
		
		for(size_t i = 0; i < n_args; i++) {
			coroutine->current_frame.env->locals[i] = args[i];
		}
		coroutine->current_frame.env->parent = fn->env;
	} else {
		if(is_c_fn) coroutine->stack_len += n_args;
		else coroutine->stack_len += fn->fn->n_locals;
		for(size_t i = 0; i < n_args; i++) coroutine->stack[i] = args[i];
		for(size_t i = n_args; i < fn->fn->n_locals; i++) coroutine->stack[i] = ESH_NULL;
	}
	
	esh_val tmp = stack_pop(esh, 1);
	stack_pop(esh, n_args + 1); // Pop the args and the function
	stack_push(esh, tmp); // Leave the coroutine at the top of the stack
	
	if(expected_returns == 0) stack_pop(esh, 1);
	else if(expected_returns != 1) {
		if(stack_resv(esh, expected_returns - 1)) return 1;
	}
	
	return 0;
}

static int enter_fn(esh_state *esh, size_t n_args, size_t expected_returns, esh_val *opt_fn, bool catch_panic) {
	if(opt_req_stack(esh, n_args + 1u)) {
		esh_err_printf(esh, "Not enough items on stack for call (%zu, %zu)\n", stack_size(esh), n_args + 1u);
		return 1;
	}
	
	esh_closure *fn;
	if(opt_fn) fn = val_as_object(*opt_fn, &closure_type); 
	else fn = val_as_object(esh->current_thread->stack[esh->current_thread->stack_len - n_args - 1u], &closure_type);
	if(!fn) {
		esh_err_printf(esh, "Attempting to call non-function value");
		return 1;
	}
	
	if(n_args < fn->fn->n_args) {
		esh_err_printf(esh, "Not enough arguments provided to function (expected at least %zi, got %zi)", fn->fn->n_args, n_args);
		return 1;
	} else if(!fn->fn->variadic && n_args > fn->fn->n_args + fn->fn->opt_args) {
		esh_err_printf(esh, "Too many arguments provided to function (expected at most %zi, got %zi)", fn->fn->n_args, n_args);
		return 1;
	}
	
	if(fn->is_coroutine) return create_coroutine(esh, n_args, expected_returns, fn);
	
	if(esh->current_thread->stack_frames_len == esh->current_thread->stack_frames_cap) {
		size_t new_cap = esh->current_thread->stack_frames_cap * 3 / 2 + 1;
		esh_stack_frame *new_stack = esh_realloc(esh, esh->current_thread->stack_frames, sizeof(esh_stack_frame) * new_cap);
		if(!new_stack) {
			esh_err_printf(esh, "Unable to push stack frame (out of memory?)");
			return 1;
		}
		
		esh->current_thread->stack_frames = new_stack;
		esh->current_thread->stack_frames_cap = new_cap;
	}
	
	bool is_c_fn = fn->fn->c_fn != NULL;
	
	esh_env *new_env = NULL; 
	size_t new_stack_base = esh->current_thread->stack_len - n_args;
	if(!is_c_fn) {
		if(fn->fn->upval_locals) {
			if(esh_req_stack(esh, n_args + ESH_FN_DEFAULT_STACK_CAP)) return 1;
			
			new_env = new_env_object(esh, fn->fn->n_locals);
			if(!new_env) return 1;
		
			new_env->parent = fn->env;
			for(size_t i = 0; i < n_args; i++) {
				new_env->locals[i] = esh->current_thread->stack[esh->current_thread->stack_len - n_args + i];
			}
			esh->current_thread->stack_len -= n_args; // Remove the arguments but *not* the function
			new_stack_base = esh->current_thread->stack_len;
		} else {
			if(esh_req_stack(esh, fn->fn->n_locals + ESH_FN_DEFAULT_STACK_CAP)) return 1; // Locals take up some stack space, so make sure that there's n_locals + required space on the stack
			
			new_env = fn->env;
			assert(fn->fn->n_locals >= n_args);
			stack_resv(esh, fn->fn->n_locals - n_args);
		}
	} else {
		if(esh_req_stack(esh, n_args + C_FN_DEFAULT_STACK_CAP)) return 1;
	}
	
	esh->current_thread->stack_frames[esh->current_thread->stack_frames_len++] = esh->current_thread->current_frame;
	esh->current_thread->stack_frames[esh->current_thread->stack_frames_len - 1].expected_returns = expected_returns;
	esh->current_thread->stack_frames[esh->current_thread->stack_frames_len - 1].catch_panic = catch_panic;
	
	esh->current_thread->current_frame = (esh_stack_frame) {
		.stack_base = new_stack_base,
		.env = new_env,
		.fn = fn->fn,
		.n_args = n_args,
		.instr_index = 0,
		.c_locals = NULL,
		.c_locals_free = NULL,
		.catch_panic = false
	};
	
	return 0;
}

static int unpack_obj(esh_state *esh, size_t n) {
	if(opt_req_stack(esh, 1)) {
		esh_err_printf(esh, "Missing value on stack for unpack");
		return 1;
	}

	size_t obj_index = esh->current_thread->stack_len - 1;
	if(esh->current_thread->stack[obj_index] == ESH_NULL) {
		esh_err_printf(esh, "Cannot unpack null value");
		return 1;
	}
	
	if(stack_resv(esh, n)) return 1;
	
	esh_val obj_val = esh->current_thread->stack[obj_index];
	esh->current_thread->stack[obj_index] = esh->current_thread->stack[esh->current_thread->stack_len - 1];
	esh->current_thread->stack[esh->current_thread->stack_len - 1] = obj_val;
	
	esh_object *obj = val_as_object(obj_val, NULL);
	if(obj != NULL) {
		for(size_t i = 0; i < n; i++) {
			size_t keylen;
			const char *key = int_to_str(i, &keylen);
			esh_val val;
			if(esh_object_get(esh, obj, key, keylen, &val)) esh->current_thread->stack[esh->current_thread->stack_len - n - 1 + i] = val;
		}
	}
	
	esh->current_thread->stack_len--;
	return 0;
}

static void free_stack_frame(esh_state *esh, esh_stack_frame *frame) {
	if(frame->c_locals_free) frame->c_locals_free(esh, frame->c_locals);
	esh_free(esh, frame->c_locals);
	
	frame->c_locals = NULL;
	frame->c_locals_free = NULL;
}

static int leave_fn(esh_state *esh, size_t n) {
	if(esh->current_thread->stack_frames_len == 0) {
		assert(esh->threads_len != 0);
		
		esh->current_thread->is_done = true;
		
		esh->current_thread = esh->threads[--esh->threads_len];
		if(stack_resv(esh, esh->current_thread->current_frame.expected_returns)) return 1;
		return 0;
	}
	
	assert(esh->current_thread->stack_frames_len != 0);
	
	if(opt_req_stack(esh, n)) {
		esh_err_printf(esh, "Unable to execute return; not enough values on stack (%zu/%zu)", stack_size(esh), n);
		return 1;
	}
	
	esh_stack_frame prev_frame = esh->current_thread->stack_frames[esh->current_thread->stack_frames_len - 1];
	
	size_t ret_vals = n;
	if(prev_frame.expected_returns == 1 && n != 1) {
		ret_vals = 1;
		if(esh_new_array(esh, n)) return 1;
	} else if(n == 1 && prev_frame.expected_returns > 1) {
		ret_vals = prev_frame.expected_returns;
		if(unpack_obj(esh, prev_frame.expected_returns)) return 1;
	} else if(prev_frame.expected_returns > n) {
		ret_vals = prev_frame.expected_returns;
		if(stack_resv(esh, prev_frame.expected_returns - n)) return 1;
	}
	assert(ret_vals >= prev_frame.expected_returns);
	size_t ret_vals_begin = esh->current_thread->stack_len - ret_vals;
	
	esh->current_thread->stack_len = esh->current_thread->current_frame.stack_base;
	esh->current_thread->stack_len--; // Remove the function
	for(size_t i = 0; i < prev_frame.expected_returns; i++) {
		assert(esh->current_thread->stack_len < esh->current_thread->stack_cap);
		assert(ret_vals_begin + i < esh->current_thread->stack_cap);
		esh->current_thread->stack[esh->current_thread->stack_len++] = esh->current_thread->stack[ret_vals_begin + i];
	}
	
	free_stack_frame(esh, &esh->current_thread->current_frame);	
	esh->current_thread->current_frame = prev_frame;
	
	esh->current_thread->stack_frames_len--; // Wait until this point to actually remove the frame; due to garbage collection
	
	return 0;
}

static esh_val *index_local_var(esh_state *esh, size_t index, size_t uplevel, bool write_barrier) {
	if(!esh->current_thread->current_frame.fn->upval_locals) {
		if(uplevel == 0) {
			if(opt_req_stack(esh, index + 1)) {
				esh_err_printf(esh, "Local stack variable out of bounds (%llu/%llu)", (size_t) index, stack_size(esh));
				return NULL;
			}
			return &esh->current_thread->stack[esh->current_thread->current_frame.stack_base + index];
		}
		uplevel--;
	}
	
	esh_env *env = esh->current_thread->current_frame.env;
	for(size_t i = 0; i < uplevel; i++) {
		if(env == NULL || env->parent == NULL) {
			esh_err_printf(esh, "Variable uplevel out of bounds (%zu/%zu)", uplevel, i);
			return NULL;
		}
		env = env->parent;
	}
	
	assert(env != NULL);
	
	if(index >= env->n_locals) {
		esh_err_printf(esh, "Local variable out of bounds (%llu:%llu/%llu)", (size_t) index, uplevel, env->n_locals);
		return NULL;
	}
	
	if(write_barrier) gc_obj_write_barrier(esh, &env->obj);
	return &env->locals[index];
}

static int int_binop(esh_state *esh, long long *x, long long *y, const char *opname) {
	if(opt_req_stack(esh, 2)) {
		esh_err_printf(esh, "Not enough values on stack for %s operation", opname);
		return 1;
	}

	if(val_as_int(&esh->current_thread->stack[esh->current_thread->stack_len - 2], x)) {
		esh_err_printf(esh, "Unable to implicitly convert left value to integer for %s operation", opname);
		return 1;
	}
	if(val_as_int(&esh->current_thread->stack[esh->current_thread->stack_len - 1], y)) {
		esh_err_printf(esh, "Unable to implicitly convert right value to integer for %s operation", opname);
		return 1;
	}
	esh->current_thread->stack_len -= 2;
	return 0;
}

static int branch_instr(esh_state *esh, uint64_t jmp_index) {
	if(jmp_index >= esh->current_thread->current_frame.fn->jmps_len) {
		esh_err_printf(esh, "Jump label index out of range");
		return 1;
	}
	
	size_t dest = esh->current_thread->current_frame.fn->jmps[jmp_index];
	if(dest >= esh->current_thread->current_frame.fn->instr_len) {
		esh_err_printf(esh, "Jump label address out of range");
		return 1;
	}
	
	esh->current_thread->current_frame.instr_index = dest;
	return 0;
}

static bool fold_next_unpack_instr(esh_state *esh, uint64_t *out_n) {
	if(esh->current_thread->current_frame.instr_index < esh->current_thread->current_frame.fn->instr_len) {
		instr_regs instr;
		decode_instr(esh->current_thread->current_frame.fn->instr + esh->current_thread->current_frame.instr_index * INSTR_SIZE, &instr);
		if(instr.op == ESH_INSTR_UNPACK) {
			esh->current_thread->current_frame.instr_index++;
			*out_n = instr.arg;
			return true;
		}
	}
	*out_n = 1;
	return false;
}

static int run_vm(esh_state *esh, esh_closure *entrypoint) {
	assert(esh->current_thread->current_frame.env == NULL);
	
	#define ESH_DEFAULT_EXEC_STACK_SIZE 64
	if(esh_req_stack(esh, ESH_DEFAULT_EXEC_STACK_SIZE)) return 1;
	
	esh_env *new_env = NULL;
	if(entrypoint->fn->upval_locals && (entrypoint->fn->n_locals != 0 || entrypoint->env != NULL)) {
		new_env = new_env_object(esh, entrypoint->fn->n_locals);
		if(!new_env) return 1;
		new_env->parent = entrypoint->env;
	}
	
	esh->current_thread->current_frame = (esh_stack_frame) {
		.fn = entrypoint->fn,
		.env = new_env,
		.c_locals = NULL,
		.c_locals_free = NULL,
		.instr_index = 0,
		.n_args = 0,
		.expected_returns = 0,
		.stack_base = esh->current_thread->stack_len,
		.catch_panic = false
	};
	
	if(!entrypoint->fn->upval_locals) {
		if(stack_resv(esh, entrypoint->fn->n_locals)) return 1;
	}
	
	while(true) {
		esh_function *f = esh->current_thread->current_frame.fn;
		
		if(f->c_fn) {
			esh_fn_result res = f->c_fn(esh, esh->current_thread->current_frame.n_args, esh->current_thread->current_frame.instr_index);
			if(res.type == -1) goto PANIC; // Error
			if(res.type == 0) { // Return
				if(leave_fn(esh, res.n_args)) goto PANIC;
			} else if(res.type == 1 || res.type == 2) { // Call & try call
				esh->panic_caught = false; // Reset the flag, used in case of a "try call" to detect whether the function actually threw an error
				esh->current_thread->current_frame.instr_index++;
				if(enter_fn(esh, res.n_args, res.n_res, NULL, res.type == 2)) goto PANIC;
			} else if(res.type == 3 || res.type == 7) { // Yield or Yield_last
				esh->current_thread->current_frame.instr_index++;
				assert(res.n_res == 0);
				assert(res.n_args == 1);
				
				if(esh->threads_len == 0) {
					esh_err_printf(esh, "Attempting to yield from top function");
					goto PANIC;
				}
				if(stack_size(esh) < res.n_args) {
					esh_err_printf(esh, "Not enough items on stack for yield (%zu/%zu)", stack_size(esh), res.n_args);
					goto PANIC;
				}
				
				esh_val yield_val = stack_pop(esh, 1);
				
				if(res.type == 7) esh->current_thread->is_done = true; // If yield_last
				
				gc_obj_write_barrier(esh, &esh->current_thread->obj); // The object might've updated whilst executing; e.g the stack might've changed
				esh->current_thread = esh->threads[--esh->threads_len];
				
				if(stack_push(esh, yield_val)) goto PANIC;
			} else if(res.type == 4 || res.type == 6) { // Next or Next_S
				esh->current_thread->current_frame.instr_index++;
				assert(res.n_args == 0);
				assert(res.n_res == 1);
				
				if(stack_size(esh) < 1 + res.n_args) {
					esh_err_printf(esh, "Not enough items on stack for coroutine invocation (%zu/%zu)", stack_size(esh), res.n_args + 1);
					goto PANIC;
				}
				
				if(esh->threads_len == esh->threads_cap) {
					size_t new_cap = esh->threads_cap * 3 / 2 + 1;
					esh_co_thread **new_stack = esh_realloc(esh, esh->threads, sizeof(esh_co_thread *) * new_cap);
					if(!new_stack) {
						esh_err_printf(esh, "Unable to grow coroutine stack (out of memory?)");
						goto PANIC;
					}
					
					esh->threads = new_stack;
					esh->threads_cap = new_cap;
				}
				
				esh->current_thread->current_frame.expected_returns = res.n_res;
				
				stack_pop(esh, res.n_args);
				esh_object *obj = val_as_object(stack_pop(esh, 1), NULL);
				if(!obj || !obj->type || (!obj->type->next && obj->type != &co_thread_type)) {
					esh_err_printf(esh, "Attempting to invoke non-coroutine object as coroutine");
					goto PANIC;
				}
				if(obj->type->next) {
					const size_t read_buff_size = 512;
					if(obj->type->next(esh, obj, res.type == 4? 1 : read_buff_size)) goto PANIC;
					continue;
				}
				esh_co_thread *co = (esh_co_thread *) obj;
				
				if(co->is_done) {
					if(stack_push(esh, ESH_NULL)) goto PANIC;
					continue;
				}
				
				gc_obj_write_barrier(esh, &esh->current_thread->obj); // The object might've updated whilst executing; e.g the stack might've changed
				esh->threads[esh->threads_len++] = esh->current_thread;
				esh->current_thread = co;
			} else if(res.type == 5) { // Repeat; e.g just increment the instr_index and invoke the function again
				esh->current_thread->current_frame.instr_index++;
			}
			continue;
		}
		
		#ifndef NO_STACK_CHECK
		if(esh->current_thread->current_frame.instr_index >= f->instr_len) {
			esh_err_printf(esh, "Instruction index out of bounds");
			goto PANIC;
		}
		#endif
		
		uint8_t *instr_p = f->instr + esh->current_thread->current_frame.instr_index * INSTR_SIZE;
		instr_regs instr;
		decode_instr(instr_p, &instr);
		
		switch(instr.op) {
			case ESH_INSTR_POP: {
				if(opt_req_stack(esh, 1)) {
					esh_err_printf(esh, "Missing value on stack for pop operation");
					goto PANIC;
				}
				
				esh->current_thread->stack_len--;
			} break;
			
			case ESH_INSTR_DUP: {
				if(stack_size(esh) == 0) {
					esh_err_printf(esh, "Missing value on stack for dup operation");
					goto PANIC;
				}
				
				if(stack_push(esh, esh->current_thread->stack[esh->current_thread->stack_len - 1])) goto PANIC;
			} break;
			
			case ESH_INSTR_SWAP: {
				if(opt_req_stack(esh, 2)) {
					esh_err_printf(esh, "Missing values on stack for swap operation");
					goto PANIC;
				}
				
				esh_val tmp = esh->current_thread->stack[esh->current_thread->stack_len - 1];
				esh->current_thread->stack[esh->current_thread->stack_len - 1] = esh->current_thread->stack[esh->current_thread->stack_len - 2];
				esh->current_thread->stack[esh->current_thread->stack_len - 2] = tmp;
			} break;
			
			case ESH_INSTR_IMM:
				if(instr.arg >= f->imms_len) {
					esh_err_printf(esh, "Immediate index out of bounds (%llu/%llu)", instr.arg, f->imms_len);
					goto PANIC;
				}
				
				if(stack_push(esh, f->imms[instr.arg])) goto PANIC;
				
				break;
			
			case ESH_INSTR_PUSH_NULL:
				if(stack_push(esh, NULL)) goto PANIC;
				break;
			
			case ESH_INSTR_STORE_G: {
				if(instr.arg >= f->imms_len) {
					esh_err_printf(esh, "Immediate index for global store out of bounds (%llu/%llu)", instr.arg, f->imms_len);
					goto PANIC;
				}
				
				size_t len;
				const char *name = val_as_string(&f->imms[instr.arg], &len);
				if(!name) {
					esh_err_printf(esh, "Global variable name not a string");
					goto PANIC;
				}
				
				if(esh->current_thread->stack_len - esh->current_thread->current_frame.stack_base == 0) {
					esh_err_printf(esh, "Missing value on stack for global store");
					goto PANIC;
				}
				
				esh_val val = esh->current_thread->stack[esh->current_thread->stack_len - 1];
				//esh_val val = esh->current_thread->stack[--esh->current_thread->stack_len];
				gc_obj_write_barrier(esh, esh->globals);
				if(esh_object_set(esh, esh->globals, name, len, val)) {
					esh_err_printf(esh, "Unable to set global (out of memory?)");
					goto PANIC;
				}
				esh->current_thread->stack_len--;
			} break;
			
			case ESH_INSTR_LOAD_G: {
				if(instr.arg >= f->imms_len) {
					esh_err_printf(esh, "Immediate index for global load out of bounds (%llu/%llu)", instr.arg, f->imms_len);
					goto PANIC;
				}
				
				size_t len;
				const char *name = val_as_string(&f->imms[instr.arg], &len);
				if(!name) {
					esh_err_printf(esh, "Global variable name not a string");
					goto PANIC;
				}
				
				esh_val val;
				if(!esh_object_get(esh, esh->globals, name, len, &val)) {
					esh_err_printf(esh, "Unknown global variable '%.*s'", (int) len, name);
					goto PANIC;
				}
				
				if(stack_push(esh, val)) goto PANIC;
			} break;
			
			case ESH_INSTR_LOAD: {
				esh_val *local = index_local_var(esh, instr.arg, instr.l, false);
				if(!local) goto PANIC;

				if(stack_push(esh, *local)) goto PANIC;
			} break;
			
			case ESH_INSTR_STORE: {
				if(opt_req_stack(esh, 1)) {
					esh_err_printf(esh, "Missing value on stack for local store");
					goto PANIC;
				}
				
				esh_val *local = index_local_var(esh, instr.arg, instr.l, true);
				if(!local) goto PANIC;
				
				*local = esh->current_thread->stack[--(esh->current_thread->stack_len)];
			} break;
			
			case ESH_INSTR_CMD: {
				esh->current_thread->current_frame.instr_index++;
				
				size_t expected_returns;
				fold_next_unpack_instr(esh, &expected_returns);
				
				size_t stack_items = esh->current_thread->stack_len - esh->current_thread->current_frame.stack_base;
				if(stack_items < instr.arg + 1u) {
					esh_err_printf(esh, "Not enough arguments on stack to invoke command (%llu/%llu)", stack_items, instr.arg + 1u);
					goto PANIC;
				}
				
				size_t cmdlen;
				const char *cmd = val_as_string(&esh->current_thread->stack[esh->current_thread->stack_len - instr.arg - 1u], &cmdlen);
				if(!cmd) {
					esh_err_printf(esh, "Expected string as command");
					goto PANIC;
				}
				
				esh_val global;
				if(esh_object_get(esh, esh->globals, cmd, cmdlen, &global)) {
					if(enter_fn(esh, instr.arg, expected_returns, &global, false)) goto PANIC;
					continue; // Don't increment instr index
				}
				
				if(esh->cmd == ESH_NULL) {
					esh_err_printf(esh, "Unknown command '%s' (no command handler set)", cmd);
					goto PANIC;
				}
				
				bool capture_output = (instr.l & 1) != 0;
				bool pipe_in = (instr.l & 2) != 0;
				if(stack_push(esh, esh->current_thread->stack[esh->current_thread->stack_len - instr.arg - 1u])) goto PANIC;
				if(esh_push_bool(esh, pipe_in)) goto PANIC;
				if(esh_push_bool(esh, capture_output)) goto PANIC;
				
				if(enter_fn(esh, instr.arg + 3, expected_returns, &esh->cmd, false)) goto PANIC;
				continue; // Don't increment instr index
			} break;
			
			case ESH_INSTR_CALL: {
				esh->current_thread->current_frame.instr_index++;
				size_t expected_returns;
				fold_next_unpack_instr(esh, &expected_returns);
				if(enter_fn(esh, instr.arg, expected_returns, NULL, false)) goto PANIC;
				continue; // Don't increment the instr_index
			} break;
			
			case ESH_INSTR_PROP: {
				if(opt_req_stack(esh, 1)) {
					esh_err_printf(esh, "Missing value on stack for prop operation");
					goto PANIC;
				}
				
				if(esh->current_thread->stack[esh->current_thread->stack_len - 1] == ESH_NULL) {
					if(esh->current_thread->stack_frames_len == 0) {
						return 0;
					}
					if(leave_fn(esh, 1)) goto PANIC;
					continue;
				}
			} break;
			
			case ESH_INSTR_RET: {
				size_t n = instr.arg;
				if(esh->current_thread->stack_frames_len == 0 && esh->threads_len == 0) {
					if(opt_req_stack(esh, n)) {
						esh_err_printf(esh, "Unable to execute return; not enough values on stack (%zu/%zu)", stack_size(esh), n);
						goto PANIC;
					}
					free_stack_frame(esh, &esh->current_thread->current_frame);
					if(n != 1) if(esh_new_array(esh, n)) goto PANIC;
					return 0;
				}
				if(leave_fn(esh, instr.arg)) goto PANIC;
				continue; // Don't increment the instr_index
			} break;
			
			case ESH_INSTR_CLOSURE: {
				if(instr.arg >= f->imms_len) {
					esh_err_printf(esh, "Immediate index for closure function out of bounds (%llu/%llu)", instr.arg, f->imms_len);
					goto PANIC;
				}
				
				esh_function *fn = val_as_object(f->imms[instr.arg], &function_type);
				if(!fn) {
					esh_err_printf(esh, "Attempting to create closure from non-function object");
					goto PANIC;
				}
				
				esh_closure *closure = esh_new_object(esh, sizeof(esh_closure), &closure_type);
				if(!closure) goto PANIC;
				
				closure->env = esh->current_thread->current_frame.env;
				closure->fn = fn;
				closure->obj.is_const = true;
				closure->is_coroutine = false;
			} break;
			
			case ESH_INSTR_JMP_IF: {
				if(opt_req_stack(esh, 1)) {
					esh_err_printf(esh, "Missing value on stack for conditional jump");
					goto PANIC;
				}
				
				esh->current_thread->stack_len--;
				
				if(val_as_bool(&esh->current_thread->stack[esh->current_thread->stack_len - 1])) {
					if(branch_instr(esh, instr.arg)) goto PANIC;
					continue; // Don't increment the instr_index
				}
			} break;
			
			case ESH_INSTR_JMP_IFN: {
				if(opt_req_stack(esh, 1)) {
					esh_err_printf(esh, "Missing value on stack for conditional jump");
					goto PANIC;
				}
				
				esh->current_thread->stack_len--;
				
				if(!val_as_bool(&esh->current_thread->stack[esh->current_thread->stack_len])) {
					if(branch_instr(esh, instr.arg)) goto PANIC;
					continue; // Don't increment the instr_index
				}
			} break;
			
			case ESH_INSTR_JMP: {
				if(branch_instr(esh, instr.arg)) goto PANIC;
				continue; // Don't increment the instr_index
			} break;
			
			case ESH_INSTR_ADD: {
				long long x, y;
				if(int_binop(esh, &x, &y, "add")) goto PANIC;
				if(esh_push_int(esh, x + y)) goto PANIC;
			} break;
			
			case ESH_INSTR_SUB: {
				long long x, y;
				if(int_binop(esh, &x, &y, "sub")) goto PANIC;
				if(esh_push_int(esh, x - y)) goto PANIC;
			} break;
			
			case ESH_INSTR_MUL: {
				long long x, y;
				if(int_binop(esh, &x, &y, "mul")) goto PANIC;
				if(esh_push_int(esh, x * y)) goto PANIC;
			} break;
			
			case ESH_INSTR_DIV: {
				long long x, y;
				if(int_binop(esh, &x, &y, "div")) goto PANIC;
				
				long long res;
				if(y == 0) res = 0;
				else res = x / y;
				
				if(esh_push_int(esh, res)) goto PANIC;
			} break;
			
			case ESH_INSTR_EQ: {
				if(opt_req_stack(esh, 2)) {
					esh_err_printf(esh, "Missing values on stack for equality comparison");
					goto PANIC;
				}
				
				bool equal = vals_equal(&esh->current_thread->stack[esh->current_thread->stack_len - 1], &esh->current_thread->stack[esh->current_thread->stack_len - 2]);
				
				esh->current_thread->stack_len -= 2;
				if(esh_push_bool(esh, equal)) goto PANIC;
			} break;
			
			case ESH_INSTR_NEQ: {
				if(opt_req_stack(esh, 2)) {
					esh_err_printf(esh, "Missing values on stack for equality comparison");
					goto PANIC;
				}
				
				bool equal = vals_equal(&esh->current_thread->stack[esh->current_thread->stack_len - 1], &esh->current_thread->stack[esh->current_thread->stack_len - 2]);
				
				esh->current_thread->stack_len -= 2;
				if(esh_push_bool(esh, !equal)) goto PANIC;
			} break;
			
			case ESH_INSTR_LESS: {
				long long x, y;
				if(int_binop(esh, &x, &y, "less")) goto PANIC;
				if(esh_push_bool(esh, x < y)) goto PANIC;
			} break;
			
			case ESH_INSTR_GREATER: {
				long long x, y;
				if(int_binop(esh, &x, &y, "greater")) goto PANIC;
				if(esh_push_bool(esh, x > y)) goto PANIC;
			} break;
			
			case ESH_INSTR_NOT: {
				if(opt_req_stack(esh, 1)) {
					esh_err_printf(esh, "Missing value on stack for not operation");
					goto PANIC;
				}
				bool b = val_as_bool(&esh->current_thread->stack[--esh->current_thread->stack_len]);
				if(esh_push_bool(esh, !b)) goto PANIC;
			} break;
			
			case ESH_INSTR_NEW_OBJ: {
				if(esh_object_of(esh, instr.arg)) goto PANIC;
			} break;
			
			case ESH_INSTR_MAKE_CONST: {
				if(opt_req_stack(esh, 1)) {
					esh_err_printf(esh, "Missing value on stack for 'make const' operation");
					goto PANIC;
				}
				esh_object *obj = val_as_object(esh->current_thread->stack[esh->current_thread->stack_len - 1], NULL);
				if(obj) obj->is_const = true;
			} break;
			
			case ESH_INSTR_INDEX: {
				if(opt_req_stack(esh, 2)) {
					esh_err_printf(esh, "Not enough items on stack for object index operation (%zu/2)", (size_t) stack_size(esh));
					goto PANIC;
				}
				
				size_t keylen;
				const char *key = val_as_string(&esh->current_thread->stack[esh->current_thread->stack_len - 1], &keylen);
				if(!key) {
					esh_err_printf(esh, "Attempting to index object using non-key value");
					goto PANIC;
				}
				esh_object *obj = val_as_object(esh->current_thread->stack[esh->current_thread->stack_len - 2], NULL);
				
				esh_val res = NULL;
				if(obj) {
					esh_val val;
					if(esh_object_get(esh, obj, key, keylen, &val)) res = val;
				}
				
				esh->current_thread->stack_len -= 1;
				esh->current_thread->stack[esh->current_thread->stack_len - 1] = res;
			} break;
			
			case ESH_INSTR_SET_INDEX: {
				if(opt_req_stack(esh, 3)) {
					esh_err_printf(esh, "Not enough items on stack for object index operation (%zu/3)", (size_t) stack_size(esh));
					goto PANIC;
				}
				
				size_t keylen;
				const char *key = val_as_string(&esh->current_thread->stack[esh->current_thread->stack_len - 2], &keylen);
				if(!key) {
					esh_err_printf(esh, "Attempting to index object using non-key value");
					goto PANIC;
				}
				esh_object *obj = val_as_object(esh->current_thread->stack[esh->current_thread->stack_len - 3], NULL);
				if(!obj) {
					esh_err_printf(esh, "Attempting to mutate immutable object");
					goto PANIC;
				}
				esh_val val = esh->current_thread->stack[esh->current_thread->stack_len - 1];
				
				gc_obj_write_barrier(esh, obj);
				if(esh_object_set(esh, obj, key, keylen, val)) goto PANIC;
				
				esh->current_thread->stack_len -= 3;
			} break;
			
			case ESH_INSTR_UNPACK: {
				if(unpack_obj(esh, instr.arg)) goto PANIC;
			} break;
			
			case ESH_INSTR_CONCAT: {
				if(opt_req_stack(esh, instr.arg)) {
					esh_err_printf(esh, "Not enough items on stack for concat operation (%zu/%zu)", (size_t) stack_size(esh), (size_t) instr.arg);
					goto PANIC;
				}
				
				esh_str_buff_begin(esh);
				for(size_t i = 0; i < instr.arg; i++) {
					size_t len;
					const char *str = val_as_string(&esh->current_thread->stack[esh->current_thread->stack_len - instr.arg + i], &len);
					if(!str) {
						esh_err_printf(esh, "Attempting to concatenate non-string value");
						goto PANIC;
					}
					if(esh_str_buff_appends(esh, str, len)) goto PANIC;
				}
				
				esh->current_thread->stack_len -= instr.arg;
				
				size_t len;
				char *str = esh_str_buff(esh, &len);
				if(esh_new_string(esh, str, len)) goto PANIC;
			} break;
			
			default:
				esh_err_printf(esh, "Unknown instruction (%u)", (unsigned) instr.op);
				goto PANIC;
		}
		
		esh->current_thread->current_frame.instr_index++;
		continue;
		
		PANIC: {
			free_stack_frame(esh, &esh->current_thread->current_frame);
		
			bool catch = false;
			size_t rewind_to = 0;
			for(size_t i = esh->current_thread->stack_frames_len; i > 0; i--) {
				if(esh->current_thread->stack_frames[i - 1].catch_panic) {
					catch = true;
					rewind_to = i;
					break;
				}
			}
			
			if(!catch) generate_stack_trace(esh);
			for(size_t i = rewind_to; i < esh->current_thread->stack_frames_len; i++) {
				free_stack_frame(esh, &esh->current_thread->stack_frames[i]);
			}
			if(rewind_to == esh->current_thread->stack_frames_len) esh->current_thread->stack_len = esh->current_thread->current_frame.stack_base;
			else esh->current_thread->stack_len = esh->current_thread->stack_frames[rewind_to].stack_base;
			esh->current_thread->stack_frames_len = rewind_to;
			
			if(catch) {
				assert(esh->current_thread->stack_frames_len > 0);
				esh->panic_caught = true;
				esh->current_thread->current_frame = esh->current_thread->stack_frames[--esh->current_thread->stack_frames_len];
			} else {
				return 1;
			}
		}
	}
	
	return 0;
}

int esh_exec_fn(esh_state *esh) {
	esh_free(esh, esh->stack_trace);
	esh->stack_trace = NULL;
	
	if(esh->current_thread->current_frame.fn != NULL) {
		esh_err_printf(esh, "Attempting to exec from inside interpreted code");
		if(esh->current_thread->stack_len > 0) esh->current_thread->stack_len--;
		return 1;
	}
	assert(esh->current_thread->current_frame.env == NULL);
	assert(esh->current_thread->current_frame.stack_base == 0);
	assert(esh->current_thread->current_frame.c_locals == NULL);
	
	esh_closure *fn = esh_as_type(esh, -1, &closure_type);
	if(!fn) {
		esh_err_printf(esh, "Attempting to execute non-function object");
		if(esh->current_thread->stack_len > 0) esh->current_thread->stack_len--;
		return 1;
	}
	
	esh_stack_frame prev_frame = esh->current_thread->current_frame;
	size_t saved_stack_len = esh->current_thread->stack_len;
	int rerr = run_vm(esh, fn);
	
	assert(esh->current_thread->stack_frames_len == 0);
	assert(esh->current_thread->current_frame.c_locals == NULL);
	assert(esh->current_thread->current_frame.c_locals_free == NULL);
	
	esh_val rval;
	if(!rerr) {
		assert(stack_size(esh) > 0);
		rval = esh->current_thread->stack[esh->current_thread->stack_len - 1];
	}
	esh->current_thread->current_frame = prev_frame;
	esh->current_thread->stack_len = saved_stack_len;
	
	assert(esh->current_thread->stack_len > 0);
	
	if(!rerr) { // If no error; replace the function on the stack with the return value; otherwise just remove it
		esh->current_thread->stack[esh->current_thread->stack_len - 1] = rval;
	} else {
		esh->current_thread->stack_len--;
	}
	
	if(rerr) return 2;
	return 0;
}

#include "esh_c.h"

int esh_loads(esh_state *esh, const char *name, const char *src, bool interactive) {
	return esh_compile_src(esh, name, src, strlen(src), interactive);
}

int esh_loadf(esh_state *esh, const char *path) {
	size_t src_len = 0, src_cap = 0;
	char *src = NULL;
	
	FILE *f = fopen(path, "r");
	if(!f) {
		esh_err_printf(esh, "Unable to open script at '%s'", path);
		return 2;
	}
	
	while(true) {
		char buff[512];
		size_t n = fread(buff, 1, sizeof(buff), f);
		if(src_len + n > src_cap) {
			size_t new_cap = src_cap * 3 / 2 + n;
			char *new_src = esh_realloc(esh, src, sizeof(char) * new_cap);
			if(!new_src) {
				esh_err_printf(esh, "Unable to allocate buffer when loading script (out of memory?)");
				fclose(f);
				esh_free(esh, src);
				return 1;
			}
			
			src_cap = new_cap;
			src = new_src;
		}
		
		memcpy(src + src_len, buff, sizeof(char) * n);
		src_len += n;
		
		if(n < sizeof(buff)) break;
	}
	
	fclose(f);
	f = NULL;
	
	int err = esh_compile_src(esh, path, src, src_len, false);
	esh_free(esh, src);
	
	return err;
}
