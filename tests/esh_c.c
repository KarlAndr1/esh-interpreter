#include "esh.h"
#include "esh_c.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>

static void expect_valid(const char *src) {
	esh_state *esh = esh_open(NULL);
	
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	if(err != 0) fprintf(stderr, "%s\n", esh_get_err(esh));
	assert(err == 0);
	
	esh_close(esh);
}

static void expect_invalid(const char *src) {
	esh_state *esh = esh_open(NULL);
	
	int err = esh_compile_src(esh, "test", src, strlen(src), false);
	assert(err != 0);
	
	esh_close(esh);
}

void test_valid_syntax_1() {
	expect_valid("cmd arg1 $arg2");
}

void test_valid_syntax_2() {
	expect_valid("testword");
}

void test_valid_syntax_3() {
	expect_valid("x = cmd arg1 $arg2 arg3");
}

void test_valid_syntax_fn() {
	expect_valid("x = with x y z do echo $x $y $z end");
}

void test_valid_local_syntax() {
	expect_valid(
		"x = with x do\n"
		"	local y = bar\n"
		"	echo $x $y\n" 
		"end"
	);
}

void test_syntax_if() {
	expect_valid(
		"if foo then echo bar end"
	);
}

void test_syntax_if_else() {
	expect_valid(
		"if foo then echo bar else echo foo end"
	);
}

void test_syntax_if_else_if() {
	expect_valid(
		"if foo then echo bar else if bar then echo foo end"
	);
}

void test_syntax_return() {
	expect_valid(
		"with x y do return f $x $y end"
	);
}

void test_syntax_ops() {
	expect_valid(
		"print ($x + 5 - 2 * 3 / 10 and true or false > 10 < 20 == 2 != foo)"
	);
}

void test_syntax_fn_decl() {
	expect_valid(
		"function foo with x y do\n"
		"	local z = $x + $y\n"
		"	return $z * 2\n"
		"end"
	);
}

void test_syntax_expr_fn() {
	expect_valid(
		"with x y ($x + $y)"
	);
}

void test_syntax_assign_index() {
	expect_valid("x:foo = foobar");
	expect_valid("x:foo:bar = foobar");
	expect_valid("x:$foo:$bar = foobar");
}

void test_local_var_blocks() {
	expect_valid(
		"function f with x y do\n"
		"	local w = 0\n"
		"	if true then\n"
		"		local w = 0\n"
		"	else if false then\n"
		"		local w = 0\n"
		"	else\n"
		"		local w = 0\n"
		"	end\n"
		"end"
	);
}

void test_var_redeclaration() {
	expect_invalid(
		"function f with x y do\n"
		"	local w = 0\n"
		"	local w = 1\n"
		"end\n"
	);
}

void test_var_redeclaration_arg() {
	expect_invalid(
		"function f with x y do\n"
		"	local x = 0\n"
		"end\n"
	);
}

void test_arg_redeclaration() {
	expect_invalid(
		"function f with x y x do\n"
		"	return $x\n"
		"end\n"
	);
}

void test_const_decl() {
	expect_valid(
		"function f with a b do\n"
			"local const x = foo\n"
		"end"
	);
}

void test_const_reassign() {
	expect_invalid(
		"function f with a b do\n"
			"local const x = foo\n"
			"x = bar\n"
		"end"
	);
}
