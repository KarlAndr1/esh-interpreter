#include "stdlib/pattern.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>

static int tmatch(const char *str, const char *pattern, bool entire) {
	esh_state *esh = esh_open(NULL);
	
	int res = esh_pattern_match(esh, str, strlen(str), pattern, strlen(pattern), entire);
	if(res == -1) {
		fprintf(stderr, "%s\n", esh_get_err(esh));
		exit(-1);
	}
	
	esh_close(esh);
	return res;
}

void test_match1() {
	assert(tmatch(
		"foobar.c",
		"*.c",
		true
	));
}

void test_capture1() {
	tmatch(
		"foobar.c",
		"(*).c",
		true
	);
	
	size_t n_captures;
	size_t *captures = esh_pattern_match_captures(&n_captures);
	assert(n_captures == 2);
	assert(captures[0] == 0);
	assert(captures[1] == 6);
}

void test_no_match1() {
	assert(!tmatch(
		"foobar.c",
		"*.h",
		true
	));
}

void test_match_columns() {
	assert(tmatch(
		"hello world foobar",
		"%s*(+)%s(+)%s(+)%s*",
		true
	));
	
	size_t n_captures;
	size_t *captures = esh_pattern_match_captures(&n_captures);
	assert(n_captures == 6);
	
	assert(captures[0] == 0);
	assert(captures[1] == 5);
	
	assert(captures[2] == 6);
	assert(captures[3] == 11);
	
	assert(captures[4] == 12);
	assert(captures[5] == 18);
}

void test_match_columns2() {
	assert(tmatch(
		"hello world foobar",
		"%s*(%w)%s(%w)%s(%w)%s*",
		true
	));
	
	size_t n_captures;
	size_t *captures = esh_pattern_match_captures(&n_captures);
	assert(n_captures == 6);
	
	assert(captures[0] == 0);
	assert(captures[1] == 5);
	
	assert(captures[2] == 6);
	assert(captures[3] == 11);
	
	assert(captures[4] == 12);
	assert(captures[5] == 18);
}
