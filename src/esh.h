#ifndef ESH_H_INCLUDED
#define ESH_H_INCLUDED

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

const char *esh_get_project_name();
const char *esh_get_version();

typedef struct esh_state esh_state;

esh_state *esh_open(void *(*realloc)(void *, size_t));
void esh_set_cmd(esh_state *esh);
void esh_close(esh_state *esh);

void *esh_alloc(esh_state *esh, size_t n);
void esh_free(esh_state *esh, void *p);
void *esh_realloc(esh_state *esh, void *p, size_t n);

void esh_err_printf(esh_state *esh, const char *fmt, ...);
const char *esh_get_err(esh_state *esh);
const char *esh_get_stack_trace(esh_state *esh);

bool esh_panic_caught(esh_state *esh);

void esh_pop(esh_state *esh, size_t n);

int esh_req_stack(esh_state *esh, size_t n);

const char *esh_as_string(esh_state *esh, long long offset, size_t *opt_out_len);
int esh_as_int(esh_state *esh, long long offset, long long *out_int);
bool esh_is_null(esh_state *esh, long long offset);
bool esh_as_bool(esh_state *esh, long long offset);
bool esh_is_array(esh_state *esh, long long offset);

typedef struct esh_type {
	const char *name;
	void (*on_free)(esh_state *esh, void *obj);
	int (*next)(esh_state *esh, void *obj, size_t size_hint);
} esh_type;

void *esh_as_type(esh_state *esh, long long offset, const esh_type *type);

typedef struct esh_object_entry esh_object_entry;

typedef struct esh_object {
	struct esh_object *next, *prev;
	unsigned char gc_tag;
	
	esh_type *type;
	
	bool is_const;
	size_t len, cap;
	struct esh_object_entry *entries;
} esh_object;

// VM

#define ESH_OPCODES \
	X(NULL) \
	\
	X(POP) \
	X(LOAD) \
	X(STORE) \
	X(LOAD_G) \
	X(STORE_G) \
	\
	X(JMP) \
	X(JMP_IFN) \
	X(JMP_IF) \
	\
	X(IMM) \
	X(PUSH_NULL) \
	\
	X(CALL) \
	X(RET) \
	X(CLOSURE) \
	X(CMD) \
	\
	X(ADD) \
	X(SUB) \
	X(MUL) \
	X(DIV) \
	X(LESS) \
	X(GREATER) \
	X(LESS_EQ) \
	X(GREATER_EQ) \
	X(EQ) \
	X(NEQ) \
	X(NOT) \
	\
	X(DUP) \
	X(SWAP) \
	\
	X(NEW_OBJ) \
	X(MAKE_CONST) \
	X(INDEX) \
	X(SET_INDEX) \
	X(UNPACK) \
	X(PROP) \
	\
	X(CONCAT)

typedef enum esh_opcode {
	#define X(op) ESH_INSTR_##op,
	ESH_OPCODES
	#undef X	
} esh_opcode;

int esh_new_fn(esh_state *esh, const char *name, size_t name_len);
int esh_fn_append_instr(esh_state *esh, esh_opcode op, uint64_t arg, uint64_t l);
int esh_fn_finalize(esh_state *esh, size_t n_args, size_t opt_args, size_t n_locals, bool upval_locals, bool make_closure);
int esh_fn_add_imm(esh_state *esh, uint64_t *out_ref);
int esh_fn_new_label(esh_state *esh, uint64_t *out_ref);
int esh_fn_put_label(esh_state *esh, uint64_t label);
int esh_fn_line_directive(esh_state *esh, size_t line);
int esh_new_string(esh_state *esh, const char *str, size_t len);
void *esh_new_object(esh_state *esh, size_t s, esh_type *type);
int esh_object_of(esh_state *esh, size_t n);
int esh_new_array(esh_state *esh, size_t n);
int esh_push_int(esh_state *esh, long long i);
int esh_push_bool(esh_state *esh, bool v);
int esh_push_null(esh_state *esh);
size_t esh_object_len(esh_state *esh, long long offset);
int esh_dup(esh_state *esh, long long offset);
int esh_swap(esh_state *esh, long long a, long long b);
int esh_fndump(esh_state *esh, FILE *f);
void esh_stackdump(esh_state *esh, FILE *f);

int esh_make_coroutine(esh_state *esh, long long fn);

void *esh_locals(esh_state *esh, size_t size, void (*free)(esh_state *, void *));

int esh_exec_fn(esh_state *esh);

int esh_loads(esh_state *esh, const char *name, const char *src, bool interactive);
int esh_loadf(esh_state *esh, const char *path);

typedef struct esh_fn_result {
	int type;
	size_t n_args, n_res;
} esh_fn_result;

#define ESH_FN_RETURN(n_vals) (esh_fn_result) { 0, n_vals, 0 }
#define ESH_FN_CALL(n_args, n_res) (esh_fn_result) { 1, n_args, n_res }
#define ESH_FN_ERR (esh_fn_result) { -1, 0, 0 }
#define ESH_FN_TRY_CALL(n_args, n_res) (esh_fn_result) { 2, n_args, n_res }
#define ESH_FN_YIELD(n_vals, n_res) (esh_fn_result) { 3, n_vals, n_res }
#define ESH_FN_NEXT(n_args, n_res) (esh_fn_result) { 4, n_args, n_res }
#define ESH_FN_REPEAT (esh_fn_result) { 5, 0, 0 }
#define ESH_FN_NEXT_S(n_args, n_res) (esh_fn_result) { 6, n_args, n_res }
#define ESH_FN_YIELD_LAST(n_vals, n_res) (esh_fn_result) { 7, n_vals, n_res }

int esh_new_c_fn(esh_state *esh, const char *name, esh_fn_result (*f)(esh_state *, size_t, size_t), size_t n_args, size_t opt_args, bool variadic);

int esh_set_global(esh_state *esh, const char *name);
int esh_get_global(esh_state *esh, const char *name);

void esh_gc(esh_state *esh, size_t n);
void esh_gc_conf(esh_state *esh, int gc_freq, int gc_step_size);

typedef struct esh_iterator {
	bool done;
	long long step;
	
	size_t index;
} esh_iterator;

esh_iterator esh_iter_begin(esh_state *esh);
int esh_iter_next(esh_state *esh, long long offset, esh_iterator *iter);

int esh_index(esh_state *esh, long long obj, long long key);
int esh_index_s(esh_state *esh, long long object, const char *key, size_t keylen);
int esh_index_i(esh_state *esh, long long object, long long i);

int esh_set(esh_state *esh, long long obj, long long key, long long value);
int esh_set_s(esh_state *esh, long long obj, const char *key, size_t keylen, long long value);
int esh_set_cs(esh_state *esh, long long obj, const char *key, long long value);
int esh_set_i(esh_state *esh, long long obj, long long i, long long value);

void esh_save_stack(esh_state *esh);
void esh_restore_stack(esh_state *esh);

void esh_str_buff_begin(esh_state *esh);
int esh_str_buff_appends(esh_state *esh, const char *str, size_t len);
int esh_str_buff_appendc(esh_state *esh, char c);
char *esh_str_buff(esh_state *esh, size_t *opt_out_len);

// PRIVATE API
#ifdef INCLUDE_PRIVATE_API

#include <stdint.h>

typedef void *esh_val;

#define ESH_NULL (NULL)

struct esh_object_entry {
	char *key;
	size_t keylen;
	esh_val val;
	bool deleted;
};

enum {
	ESH_TAG_OBJECT = 0,
	ESH_TAG_STRING,
	ESH_TAG_FUNCTION,
	ESH_TAG_CLOSURE,
	ESH_TAG_ENV,
};

typedef struct esh_fn_line_dir {
	size_t instr_index;
	size_t line;
} esh_fn_line_dir;

typedef struct esh_function {
	esh_object obj;
	
	esh_val *imms;
	size_t imms_len, imms_cap;
	
	size_t *jmps;
	size_t jmps_len, jmps_cap;
	
	size_t n_args;
	size_t opt_args;
	size_t n_locals;
	bool variadic;
	
	uint8_t *instr;
	size_t instr_len, instr_cap;
	
	esh_fn_line_dir *line_dirs;
	size_t line_dirs_len, line_dirs_cap;
	
	char *name;
	
	bool upval_locals;
	
	esh_fn_result (*c_fn)(esh_state *, size_t, size_t);
} esh_function;

typedef struct esh_string {
	esh_object obj;
	
	size_t len;
	char str[];
} esh_string;

typedef struct esh_env {
	esh_object obj;
	
	struct esh_env *parent;
	
	size_t n_locals;
	esh_val locals[];
} esh_env;

typedef struct esh_closure {
	esh_object obj;
	
	bool is_coroutine;
	
	esh_function *fn;
	esh_env *env;
} esh_closure;

typedef struct esh_stack_frame {
	size_t stack_base;
	esh_function *fn;
	esh_env *env;
	size_t instr_index;
	size_t n_args, expected_returns;
	void *c_locals;
	void (*c_locals_free)(esh_state *, void *);
	bool catch_panic;
} esh_stack_frame;

typedef struct esh_co_thread {
	esh_object obj;
	
	esh_stack_frame current_frame;
	esh_stack_frame *stack_frames;
	size_t stack_frames_len, stack_frames_cap;
	
	esh_val *stack;
	size_t stack_len, stack_cap;
	
	bool is_done;
} esh_co_thread;

struct esh_state {
	void *(*realloc)(void *, size_t);
	
	size_t err_buff_cap;
	char *err_buff;
	char *stack_trace;
	bool panic_caught;
	
//	esh_val *stack;
//	size_t stack_len, stack_cap;
	size_t saved_stack_len;
	
//	struct esh_stack_frame *stack_frames;
//	size_t stack_frames_len, stack_frames_cap;

//	esh_stack_frame current_frame;
	esh_co_thread **threads;
	size_t threads_len, threads_cap;
	esh_co_thread *current_thread;
	
	esh_object *objects;
	esh_object *visited, *to_visit;
	
	esh_object *globals;
	
	esh_val cmd;
	
	size_t alloc_step;
	int gc_freq;
	unsigned gc_step_size;
	
	char *str_buff;
	size_t str_buff_len;
	size_t str_buff_cap;
};

#undef INCLUDE_PRIVATE_API

#endif

#endif
