#include "esh.h"
extern int esh_load_stdlib(esh_state *esh);

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void eassert_impl(esh_state *esh, int cond, const char *expr, const char *file, int line, const char *msg) {
	if(!cond) {
		fprintf(stderr, "Assertion '%s' (%s:%i) failed\n", expr, file, line);
		if(msg) fprintf(stderr, "%s\n", msg);
		fprintf(stderr, "ESH error: %s\n", esh_get_err(esh));
		fputs("ESH Stack\n", stderr);
		esh_stackdump(esh, stderr);
		exit(1);
	}
}

#define ASSERT(cond, msg) eassert_impl(esh, cond, #cond, __FILE__, __LINE__, msg)

#define ASSERT_GLOBAL_STR(var, expect_value) eassert_impl(esh, !esh_get_global(esh, var) && strcmp(esh_as_string(esh, -1, NULL), expect_value) == 0, "Global $" var " == " expect_value, __FILE__, __LINE__, NULL)

static esh_state *t_env(const char *src) {
	esh_state *esh = esh_open(NULL);
	ASSERT(esh != NULL, NULL);
	
	if(src) {
		int err = esh_loads(esh, "test", src, false);
		ASSERT(!err, NULL);
	}
	
	return esh;
}

void test_open_and_close_esh() { // Tests that there are no memory leaks or out of bounds accesses
	esh_state *esh = esh_open(NULL);
	esh_close(esh);
}

void test_esh_pop_empty_stack() {
	esh_state *esh = esh_open(NULL);
	
	esh_pop(esh, 1); // A pop on an empty stack should do nothing
	
	esh_close(esh);
}

#include "esh_c.h"
static int cmd_calls = 0;
static esh_fn_result cmd_handler(esh_state *esh, size_t n_args, size_t i) {
	cmd_calls++;
	
	ASSERT(i == 0, NULL);
	
	size_t cmdlen;
	const char *cmd = esh_as_string(esh, -3, &cmdlen);
	bool pipe_in = esh_as_bool(esh, -2);
	bool capture = esh_as_bool(esh, -1);
	
	ASSERT(!capture, NULL);
	ASSERT(strcmp(cmd, "testcmd") == 0, NULL);
	ASSERT(cmdlen == strlen("testcmd"), NULL);
	ASSERT(n_args == 2 + 3, NULL);
	ASSERT(!pipe_in, NULL);
	
	ASSERT(strcmp(esh_as_string(esh, 0, NULL), "foo") == 0, NULL);
	ASSERT(strcmp(esh_as_string(esh, 1, NULL), "bar") == 0, NULL);
	
	return ESH_FN_RETURN(1);
}

void test_cmd_exec() {
	esh_state *esh = esh_open(NULL);
	
	ASSERT(!esh_new_c_fn(esh, "cmd", cmd_handler, 0, 0, true), NULL);
	esh_set_cmd(esh);
	
	const char *src =
		"testcmd foo bar"
	;
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT(cmd_calls == 1, NULL);
	
	esh_close(esh);
}

void test_global_vars() {
	esh_state *esh = esh_open(NULL);
	
	ASSERT(!esh_new_c_fn(esh, "cmd", cmd_handler, 0, 0, true), NULL);
	esh_set_cmd(esh);
	
	const char *src =
		"x = foo\n"
		"y = bar\n"
		"testcmd $x $y"
	;
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT(cmd_calls == 1, NULL);
	
	esh_close(esh);
}

void test_function() {
	esh_state *esh = esh_open(NULL);
	
	ASSERT(!esh_new_c_fn(esh, "cmd", cmd_handler, 0, 0, true), NULL);
	esh_set_cmd(esh);
	
	const char *src =
		"x = with x y do testcmd $x $y end\n"
		"y = somevalue\n"
		"$x foo bar"
	;
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT(cmd_calls == 1, NULL);
	
	esh_close(esh);
}

void test_function_as_cmd() {
	esh_state *esh = esh_open(NULL);
	
	ASSERT(!esh_new_c_fn(esh, "cmd", cmd_handler, 0, 0, true), NULL);
	esh_set_cmd(esh);
	
	const char *src =
		"x = with x y do testcmd $x $y end\n"
		"y = somevalue\n"
		"x foo bar"
	;
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT(cmd_calls == 1, NULL);
	
	esh_close(esh);
}


size_t c_fn1_calls = 0;
esh_fn_result c_fn1(esh_state *esh, size_t n_args, size_t i) {
	c_fn1_calls++;
	
	ASSERT(i == 0, NULL);
	ASSERT(n_args == 2, NULL);
	
	ASSERT(strcmp(esh_as_string(esh, 0, NULL), "foo") == 0, NULL);
	ASSERT(strcmp(esh_as_string(esh, 1, NULL), "bar") == 0, NULL);
	
	return ESH_FN_RETURN(1);
}

void test_c_fn() {
	esh_state *esh = esh_open(NULL);
	
	int err = esh_new_c_fn(esh, "cfn", c_fn1, 2, 0, false);
	ASSERT(!err, NULL);
	err = esh_set_global(esh, "cfn");
	ASSERT(!err, NULL);
	
	const char *src =
		"$cfn foo bar"
	;
	err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT(c_fn1_calls == 1, NULL);
	
	esh_close(esh);
}

void test_c_fn_as_cmd() {
	esh_state *esh = esh_open(NULL);
	
	int err = esh_new_c_fn(esh, "cfn", c_fn1, 2, 0, false);
	ASSERT(!err, NULL);
	err = esh_set_global(esh, "cfn");
	ASSERT(!err, NULL);
	
	const char *src =
		"cfn foo bar"
	;
	err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT(c_fn1_calls == 1, NULL);
	
	esh_close(esh);
}

void test_local_vars() {
	esh_state *esh = esh_open(NULL);
	
	ASSERT(!esh_new_c_fn(esh, "cmd", cmd_handler, 0, 0, true), NULL);
	esh_set_cmd(esh);
	
	const char *src =
		"x = with x do\n"
		"	local y = somevalue\n"
		"	y = bar\n"
		"	testcmd $x $y\n" 
		"end\n"
		"x foo"
	;
	
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT(cmd_calls == 1, NULL);
	
	esh_close(esh);
}

void test_if_true() {
	esh_state *esh = esh_open(NULL);
	
	ASSERT(!esh_new_c_fn(esh, "cmd", cmd_handler, 0, 0, true), NULL);
	esh_set_cmd(esh);
	
	const char *src = "if true then testcmd foo bar end";
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT(cmd_calls == 1, NULL);
	
	esh_close(esh);
}

void test_if_false() {
	esh_state *esh = esh_open(NULL);
	
	ASSERT(!esh_new_c_fn(esh, "cmd", cmd_handler, 0, 0, true), NULL);
	esh_set_cmd(esh);
	
	const char *src = "if null then testcmd foo bar end";
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT(cmd_calls == 0, NULL);
	
	esh_close(esh);
}

void test_if_else() {
	esh_state *esh = esh_open(NULL);
	
	ASSERT(!esh_new_c_fn(esh, "cmd", cmd_handler, 0, 0, true), NULL);
	esh_set_cmd(esh);
	
	const char *src = "if null then f x y else testcmd foo bar end";
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT(cmd_calls == 1, NULL);
	
	esh_close(esh);
}

void test_if_else_if_else() {
	esh_state *esh = esh_open(NULL);
	
	ASSERT(!esh_new_c_fn(esh, "cmd", cmd_handler, 0, 0, true), NULL);
	esh_set_cmd(esh);
	
	const char *src = "if null then f x y else if true then testcmd foo bar else testcmd 1 2 end";
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT(cmd_calls == 1, NULL);
	
	esh_close(esh);
}

void test_return() {
	esh_state *esh = esh_open(NULL);
	
	ASSERT(!esh_new_c_fn(esh, "cmd", cmd_handler, 0, 0, true), NULL);
	esh_set_cmd(esh);
	
	const char *src = 
		"f = with x y do\n"
		"	return $y\n"
		"	othercmd foo\n"
		"end\n"
		"x = f test bar\n"
		"testcmd foo $x"
	;
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT(cmd_calls == 1, NULL);
	
	esh_close(esh);
}

static int cmd2_calls = 0;
static esh_fn_result cmd_handler2(esh_state *esh, size_t n_args, size_t i) {
	cmd2_calls++;
	
	ASSERT(i == 0, NULL);
	
	size_t cmdlen;
	const char *cmd = esh_as_string(esh, -3, &cmdlen);
	bool pipe_in = esh_as_bool(esh, -2);
	bool capture = esh_as_bool(esh, -1);
	
	ASSERT(!capture, NULL);
	ASSERT(strcmp(cmd, "testcmd") == 0, NULL);
	ASSERT(cmdlen == strlen("testcmd"), NULL);
	ASSERT(n_args == 1 + 3, NULL);
	ASSERT(!pipe_in, NULL);
	
	ASSERT(strcmp(esh_as_string(esh, 0, NULL), "hello world") == 0, NULL);
	
	return ESH_FN_RETURN(1);
}

void test_quoted_str() {
	esh_state *esh = esh_open(NULL);
	
	ASSERT(!esh_new_c_fn(esh, "cmd", cmd_handler2, 0, 0, true), NULL);
	esh_set_cmd(esh);
	
	const char *src =
		"testcmd \"hello world\""
	;
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT(cmd2_calls == 1, NULL);
	
	esh_close(esh);
}


size_t c_varfn_calls = 0;
esh_fn_result c_varfn1(esh_state *esh, size_t n_args, size_t i) {
	c_varfn_calls++;
	
	ASSERT(i == 0, NULL);
	
	if(c_varfn_calls == 1) {
		ASSERT(n_args == 2, NULL);
		ASSERT(strcmp(esh_as_string(esh, 0, NULL), "foo") == 0, NULL);
		ASSERT(strcmp(esh_as_string(esh, 1, NULL), "bar") == 0, NULL);
	} else {
		ASSERT(n_args == 3, NULL);
		ASSERT(strcmp(esh_as_string(esh, 0, NULL), "x") == 0, NULL);
		ASSERT(strcmp(esh_as_string(esh, 1, NULL), "y") == 0, NULL);
		ASSERT(strcmp(esh_as_string(esh, 2, NULL), "z") == 0, NULL);
	}
	
	return ESH_FN_RETURN(1);
}

void test_c_variadic() {
	esh_state *esh = esh_open(NULL);
	
	int err = esh_new_c_fn(esh, "cfn", c_varfn1, 2, 0, true);
	ASSERT(!err, NULL);
	err = esh_set_global(esh, "cfn");
	ASSERT(!err, NULL);
	
	const char *src =
		"cfn foo bar\n"
		"cfn x y z"
	;
	err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT(c_varfn_calls == 2, NULL);
	
	esh_close(esh);
}

size_t ta_assertv_calls = 0;
long long ta_assertv_val;
esh_fn_result ta_assertv(esh_state *esh, size_t n_args, size_t i) {
	ta_assertv_calls++;
	ASSERT(i == 0, NULL);
	ASSERT(n_args == 1, NULL);
	
	long long val;
	int err = esh_as_int(esh, 0, &val);
	ASSERT(!err, NULL);
	ASSERT(val == ta_assertv_val, NULL);
	
	return ESH_FN_RETURN(1);
}

void test_add() {
	esh_state *esh = esh_open(NULL);
	
	int err = esh_new_c_fn(esh, "assertv", ta_assertv, 1, 0, true);
	ASSERT(!err, NULL);
	err = esh_set_global(esh, "assertv");
	
	const char *src =
		"assertv (25 + 202 + 2)"
	;
	ta_assertv_val = 25 + 202 + 2;
	err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT(ta_assertv_calls == 1, NULL);
	
	esh_close(esh);
}

void test_sub() {
	esh_state *esh = esh_open(NULL);
	
	int err = esh_new_c_fn(esh, "assertv", ta_assertv, 1, 0, true);
	ASSERT(!err, NULL);
	err = esh_set_global(esh, "assertv");
	
	const char *src =
		"assertv (25 - 202 + 2)"
	;
	ta_assertv_val = 25 - 202 + 2;
	err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT(ta_assertv_calls == 1, NULL);
	
	esh_close(esh);
}

void test_mul() {
	esh_state *esh = esh_open(NULL);
	
	int err = esh_new_c_fn(esh, "assertv", ta_assertv, 1, 0, true);
	ASSERT(!err, NULL);
	err = esh_set_global(esh, "assertv");
	
	const char *src =
		"assertv (22 + 41 * 2)"
	;
	ta_assertv_val = 22 + 41 * 2;
	err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT(ta_assertv_calls == 1, NULL);
	
	esh_close(esh);
}

void test_div() {
	esh_state *esh = esh_open(NULL);
	
	int err = esh_new_c_fn(esh, "assertv", ta_assertv, 1, 0, true);
	ASSERT(!err, NULL);
	err = esh_set_global(esh, "assertv");
	
	const char *src =
		"assertv (22 + 41 / 2)"
	;
	ta_assertv_val = 22 + 41 / 2;
	err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT(ta_assertv_calls == 1, NULL);
	
	esh_close(esh);
}

void test_bool_ops() {
	esh_state *esh = esh_open(NULL);
	
	const char *src =
		"x = true and (null or true)\n"
		"y = (null or (true and null)) or test"
	;
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("x", "true");

	ASSERT_GLOBAL_STR("y", "test");
	
	esh_close(esh);
}

void test_short_circuit1() {
	esh_state *esh = esh_open(NULL);
	
	const char *src = // If the command section is invoked, it should produce an errors since no command handler or any global variables have been set
		"x = true or command x\n"
		"y = null and command y"
	;
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	esh_close(esh);
}

void test_short_circuit2() {
	esh_state *esh = esh_open(NULL);
	
	const char *src =
		"x = null or command x\n"
	;
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(err, NULL);
	
	esh_close(esh);
}

void test_short_circuit3() {
	esh_state *esh = esh_open(NULL);
	
	const char *src =
		"x = true and command x\n"
	;
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(err, NULL);
	
	esh_close(esh);
}

void test_eq() {
	esh_state *esh = esh_open(NULL);
	
	const char *src =
		"x = foo == bar or test\n"
		"y = with x do end == bar or test\n"
		"z = foo == foo"
	;
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("x", "test");
	ASSERT_GLOBAL_STR("y", "test");
	ASSERT_GLOBAL_STR("z", "true");
	
	esh_close(esh);
}

void test_neq() {
	esh_state *esh = esh_open(NULL);
	
	const char *src =
		"x = foo != bar\n"
		"y = with x do end != bar\n"
		"z = foo != foo or test"
	;
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("x", "true");
	ASSERT_GLOBAL_STR("y", "true");
	ASSERT_GLOBAL_STR("z", "test");
	
	esh_close(esh);
}

void test_less() {
	esh_state *esh = esh_open(NULL);
	
	const char *src =
		"x = 53 < 20 or test\n"
		"y = 5 < 5 or test\n"
		"z = 2 < 5"
	;
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("x", "test");
	ASSERT_GLOBAL_STR("y", "test");
	ASSERT_GLOBAL_STR("z", "true");
	
	esh_close(esh);
}

void test_greater() {
	esh_state *esh = esh_open(NULL);
	
	const char *src =
		"x = 20 > 53 or test\n"
		"y = 5 > 5 or test\n"
		"z = 5 > 2"
	;
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("x", "test");
	ASSERT_GLOBAL_STR("y", "test");
	ASSERT_GLOBAL_STR("z", "true");
	
	esh_close(esh);
}

void test_fn_decl() {
	esh_state *esh = esh_open(NULL);
	
	const char *src =
		"function foo with x y do\n"
		"	local z = $x + $y\n"
		"	return $z * 2\n"
		"end\n"
		"x = foo 5 3"
	;
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("x", "16");
	
	esh_close(esh);
}

void test_expr_fn() {
	esh_state *esh = esh_open(NULL);
	
	const char *src =
		"foo = with x y (($x + $y) * 2)\n"
		"x = foo 5 3"
	;
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("x", "16");
	
	esh_close(esh);
}

void test_object_literal() {
	esh_state *esh = esh_open(NULL);
	
	const char *src = 
		"foo = { foo = bar, x = y }\n"
		"x = $foo:foo\n"
		"y = $foo:x"
	;
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("x", "bar");
	ASSERT_GLOBAL_STR("y", "y");
	
	esh_close(esh);
}

void test_index_placeholder() {
	esh_state *esh = esh_open(NULL);
	
	const char *src = 
		"x = {}\n"
		"assertv ($x:foo or 10)"
	;
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_new_c_fn(esh, "assertv", ta_assertv, 1, 0, true);
	ASSERT(!err, NULL);
	err = esh_set_global(esh, "assertv");
	ta_assertv_val = 10;
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	esh_close(esh);
}

void test_index_placeholder2() {
	esh_state *esh = esh_open(NULL);
	
	const char *src = 
		"x = { foo = bar }\n"
		"assertv ($x:foo and 10)"
	;
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_new_c_fn(esh, "assertv", ta_assertv, 1, 0, true);
	ASSERT(!err, NULL);
	err = esh_set_global(esh, "assertv");
	ta_assertv_val = 10;
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	esh_close(esh);
}

void test_index_assign() {
	esh_state *esh = esh_open(NULL);
	
	const char *src = 
		"obj = { foo = bar, etc = {} }\n"
		"obj:foo = bar2\n"
		"obj:etc:foobar = 42\n"
		"key = somekey\n"
		"obj:$key = somevalue\n"
		"\n"
		"x = $obj:foo\n"
		"y = $obj:etc:foobar\n"
		"z = $obj:somekey"
	;
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("x", "bar2");
	ASSERT_GLOBAL_STR("y", "42");
	ASSERT_GLOBAL_STR("z", "somevalue");
	
	esh_close(esh);
}

void test_index_assign_local() {
	esh_state *esh = esh_open(NULL);
	
	const char *src = 
		"f = with x do\n"
		" x:foo = bar\n"
		"end\n"
		"obj = {}\n"
		"f $obj\n"
		"x = $obj:foo"
	;
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("x", "bar");

	esh_close(esh);
}

void test_closures() {
	esh_state *esh = esh_open(NULL);
	
	const char *src = 
		"f = with x do return with y ($x + $y) end\n"
		"add1 = f 1\n"
		"add7 = f 7\n"
		"x = add1 5\n"
		"y = add7 3"
	;
	
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("x", "6");
	ASSERT_GLOBAL_STR("y", "10");

	esh_close(esh);
}

void test_pipe() {
	esh_state *esh = esh_open(NULL);
	
	const char *src = 
		"function sum with x y do return $x + $y end\n"
		"function prod with x y do return $x * $y end\n"
		"x = 10 | sum 5 | prod 2"
	;
	
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("x", "30");

	esh_close(esh);
}

void test_statement_pipe() {
	esh_state *esh = esh_open(NULL);
	
	const char *src =
		"function yield10 with do return 10 end\n"
		"function sum with x y do return $x + $y end\n"
		"function prod with x y do return $x * $y end\n"
		"yield10 | sum 5 | prod 2 | assertv"
	;
	
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	
	err = esh_new_c_fn(esh, "assertv", ta_assertv, 1, 0, true);
	ASSERT(!err, NULL);
	err = esh_set_global(esh, "assertv");
	ta_assertv_val = 30;
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT(ta_assertv_calls == 1, NULL);

	esh_close(esh);
}

static int pipe_in_cmd_calls = 0;
static esh_fn_result pipe_in_cmd(esh_state *esh, size_t n_args, size_t i) {
	pipe_in_cmd_calls++;
	
	ASSERT(i == 0, NULL);
	
	size_t cmdlen;
	const char *cmd = esh_as_string(esh, -3, &cmdlen);
	bool pipe_in = esh_as_bool(esh, -2);
	bool capture = esh_as_bool(esh, -1);
	
	ASSERT(!capture, NULL);
	ASSERT(strcmp(cmd, "testcmd") == 0, NULL);
	ASSERT(cmdlen == strlen("testcmd"), NULL);
	ASSERT(n_args == 1 + 3, NULL);
	ASSERT(pipe_in, NULL);
	
	ASSERT(strcmp(esh_as_string(esh, 0, NULL), "30") == 0, NULL);
	
	return ESH_FN_RETURN(1);
}

void test_command_pipe() {
	esh_state *esh = esh_open(NULL);
	
	const char *src = 
		"function yield10 with do return 10 end\n"
		"function sum with x y do return $x + $y end\n"
		"function prod with x y do return $x * $y end\n"
		"yield10 | sum 5 | prod 2 | testcmd"
	;
	
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	ASSERT(!err, NULL);
	
	ASSERT(!esh_new_c_fn(esh, "cmd", pipe_in_cmd, 0, 0, true), NULL);
	esh_set_cmd(esh);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT(pipe_in_cmd_calls == 1, NULL);

	esh_close(esh);
}

void test_array_literal() {
	esh_state *esh = t_env(
		"v = etc\n"
		"a = { foo, bar, $v }\n"
		"x = $a:0\n"
		"y = $a:1\n"
		"z = $a:2"
	);
	
	int err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("x", "foo");
	ASSERT_GLOBAL_STR("y", "bar");
	ASSERT_GLOBAL_STR("z", "etc");
	
	esh_close(esh);
}

void test_mixed_array_object_literal() {
	esh_state *esh = t_env(
		"v = etc\n"
		"a = { foo, foobar = $v, bar }\n"
		"x = $a:0\n"
		"y = $a:1\n"
		"z = $a:foobar"
	);
	
	int err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("x", "foo");
	ASSERT_GLOBAL_STR("y", "bar");
	ASSERT_GLOBAL_STR("z", "etc");
	
	esh_close(esh);
}

static esh_fn_result te_yield_value(esh_state *esh, size_t n_args, size_t i) {
	ASSERT(n_args == 0, NULL);
	ASSERT(i == 0, NULL);
	
	esh_new_string(esh, "yieldval", 8);
	return ESH_FN_RETURN(1);	
}

void test_excl_command() {
	esh_state *esh = t_env(
		"x = f\n"
		"y = f!"
	);
	
	int err = esh_new_c_fn(esh, "f", te_yield_value, 0, 0, false);
	ASSERT(!err, NULL);
	err = esh_set_global(esh, "f");
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("x", "f");
	ASSERT_GLOBAL_STR("y", "yieldval");
	
	esh_close(esh);
}

void test_escape_chars() {
	esh_state *esh = t_env(
		"x = 'foo\\nbar'\n"
		"y = 'foo\\\\bar'\n"
		"z = 'foo\\'bar'\n"
	);
	
	int err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("x", "foo\nbar");
	ASSERT_GLOBAL_STR("y", "foo\\bar");
	ASSERT_GLOBAL_STR("z", "foo'bar");
	esh_close(esh);
}

void test_string_interpolation() {
	esh_state *esh = t_env(
		"val = foobar\n"
		"x = \"foo $val bar\"\n"
		"y = \"result is $(cfn foo bar)\"\n"
		"z = \"nested $(\"foo $val\")\"\n"
	);
	
	int err = esh_new_c_fn(esh, "cfn", c_fn1, 2, 0, false);
	ASSERT(!err, NULL);
	err = esh_set_global(esh, "cfn");
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT(c_fn1_calls == 1, NULL);
	
	ASSERT_GLOBAL_STR("x", "foo foobar bar");
	ASSERT_GLOBAL_STR("y", "result is bar");
	ASSERT_GLOBAL_STR("z", "nested foo foobar");

	esh_close(esh);
}

void test_remove_obj_entry() {
	esh_state *esh = t_env(
		"obj = { foo = bar, x = y, etc = val}\n"
		"x = sizeof $obj\n"
		"obj:foo = null\n"
		"y = sizeof $obj\n"
		"obj:etc = null\n"
		"z = sizeof $obj\n"
		"obj:x = null\n"
		"w = sizeof $obj"
	);
	int err = esh_load_stdlib(esh);
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("x", "3");
	ASSERT_GLOBAL_STR("y", "2");
	ASSERT_GLOBAL_STR("z", "1");
	ASSERT_GLOBAL_STR("w", "0");
	
	esh_close(esh);
}

void test_local_function() {
	esh_state *esh = t_env(
		"function f1 with x y do return $x + $y end\n"
		"function f2 with x do\n"
			"function f1 with x y do\n"
				"return $x * $y\n"
			"end\n"
			"return f1 $x 5\n"
		"end\n"
		"x = f2 3"
	);
	
	int err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("x", "15");

	esh_close(esh);
}

void test_local_function_rec() {
	esh_state *esh = t_env(
		"function f1 with x do assert false end\n"
		"function f2 with x do\n"
			"function f1 with x do\n"
				"if $x == 0 then return 0 end\n"
				"return (f1 ($x - 1)) + $x\n"
			"end\n"
			"return f1 $x\n"
		"end\n"
		"x = f2 5"
	);
	
	int err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("x", "15");

	esh_close(esh);
}

void test_block_scopes() {
	esh_state *esh = t_env(
		"function f with do\n"
			"local x = 1\n"
			"if true then\n"
				"r1 = $x\n"
				"x = 2\n"
				"r2 = $x\n"
				"local x = 3\n"
				"r3 = $x\n"
				"x = 4\n"
				"r4 = $x\n"
			"else if false then\n"
			"	local x = foo\n"
			"else\n"
			"	local x = bar\n"
			"end\n"
			"r5 = $x\n"
			"x = 5\n"
			"r6 = $x\n"
		"end\n"
		"f"
	);
	
	int err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("r1", "1");
	ASSERT_GLOBAL_STR("r2", "2");
	ASSERT_GLOBAL_STR("r3", "3");
	ASSERT_GLOBAL_STR("r4", "4");
	ASSERT_GLOBAL_STR("r5", "2");
	ASSERT_GLOBAL_STR("r6", "5");

	esh_close(esh);
}

void test_multiple_return() {
	esh_state *esh = t_env(
		"function f1 with x do\n"
		"	return $x * 2, $x * $x\n"
		"end\n"
		"function f2 with do\n"
		"	local x = f1 5\n"
		"	local y, z = f1 10\n"
		"	r1 = $x:0\n"
		"	r2 = $x:1\n"
		"	r3 = $y\n"
		"	r4 = $z\n"
		"	r5, r6 = f1 20\n"
		"end\n"
		"f2"
	);
	
	int err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("r1", "10");
	ASSERT_GLOBAL_STR("r2", "25");
	ASSERT_GLOBAL_STR("r3", "20");
	ASSERT_GLOBAL_STR("r4", "100");
	ASSERT_GLOBAL_STR("r5", "40");
	ASSERT_GLOBAL_STR("r6", "400");

	esh_close(esh);
}

void test_unpack() {
	esh_state *esh = t_env(
		"function f with do\n"
		"	local x, y = { 10, 25, 35 }\n"
		"	r1 = $x\n"
		"	r2 = $y\n"
		" 	local z, w = { 40 }\n"
		"	r3 = $z\n"
		"	r4 = $w or foobar\n"
		"end\n"
		"f"
	);
	
	int err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("r1", "10");
	ASSERT_GLOBAL_STR("r2", "25");
	ASSERT_GLOBAL_STR("r3", "40");
	ASSERT_GLOBAL_STR("r4", "foobar");

	esh_close(esh);
}

void test_unpack_assign() {
	esh_state *esh = t_env(
		"function f with x do\n"
		"	x, r1, r2 = { foo, bar, etc }\n"
		"	r3 = $x\n"
		"end\n"
		"f 1"
	);
	
	int err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT_GLOBAL_STR("r1", "bar");
	ASSERT_GLOBAL_STR("r2", "etc");
	ASSERT_GLOBAL_STR("r3", "foo");

	esh_close(esh);
}

size_t multi_return_self_calls = 0;
esh_fn_result multi_return_self_call(esh_state *esh, size_t n_args, size_t i) {
	if(i == 0) multi_return_self_calls++;
	ASSERT(i == 0 || i == 1, NULL);
	ASSERT(n_args == 1 || n_args == 2, NULL);
	
	if(i == 1) ASSERT(n_args == 2, NULL);
	
	if(n_args == 1) {
		long long num;
		ASSERT(!esh_as_int(esh, 0, &num), NULL);
		for(long long i = 0; i < num; i++) ASSERT(!esh_push_int(esh, i), NULL);
		return ESH_FN_RETURN(num);
	} else {
		long long *n_returned = esh_locals(esh, sizeof(long long), NULL);
		if(i == 0) {
			ASSERT(!esh_as_int(esh, 1, n_returned), NULL);
			return ESH_FN_CALL(1, 3);
		}
		
		long long to = *n_returned;
		if(to > 3) to = 3;
		for(long long i = 0; i < to; i++) {
			long long x;
			ASSERT(!esh_as_int(esh, i, &x) && x == i, "The returned values should be integers in ascending order starting at 0");
		}
		
		for(long long i = *n_returned; i < 3; i++) ASSERT(esh_is_null(esh, i), "The rest of the return values; if the function returned less than three, should be null");
		
		if(*n_returned >= 3) {
			long long x;
			ASSERT(!esh_as_int(esh, -1, &x) && x == 2, "If 3 or more valeus were returned, the value at the top of the stack should be 2");
		}
		else ASSERT(esh_is_null(esh, -1), "If less than 3 values were returned, the value at the top of the stack should be null");
	}
	
	ASSERT(!esh_push_null(esh), NULL);
	return ESH_FN_RETURN(1);
}

void test_c_fn_multi_return() {
	esh_state *esh = t_env(
		"f $f 5\n"
		"f $f 2\n"
		"f $f 3\n"
	);
	
	int err = esh_new_c_fn(esh, "f", multi_return_self_call, 1, 1, false);
	ASSERT(!err, NULL);
	err = esh_set_global(esh, "f");
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	ASSERT(multi_return_self_calls == 6, NULL);
	
	esh_close(esh);
}

void test_unpack_null_return() {
	esh_state *esh = t_env(
		"function f with x do\n"
		"	local y = $x * 2\n"
		"end\n"
		"x, y = f 1"
	);
	
	int err = esh_exec_fn(esh);
	ASSERT(err, NULL);
	
	esh_close(esh);
}

void test_unpack_null() {
	esh_state *esh = t_env(
		"x, y = null"
	);
	
	int err = esh_exec_fn(esh);
	ASSERT(err, NULL);
	
	esh_close(esh);
}

void test_exec_non_upval_function() {
	esh_state *esh = t_env(
		"function f with do\n"
		"   local x = 10\n"
		"   local y = 20\n"
		"	local z = $x + $y\n"
		"	return $z\n"
		"end"
	);
	
	int err = esh_exec_fn(esh);
	ASSERT(!err, NULL);
	
	err = esh_get_global(esh, "f");
	ASSERT(!err, NULL);
	
	err = esh_exec_fn(esh); // Main test
	ASSERT(!err, NULL);
	
	esh_close(esh);
}
