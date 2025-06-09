#include "esh_object.h"

#include <assert.h>

#define DUMMY_VAL (esh_val) 1

void test_add_entry() {
	esh_state *esh = esh_open(NULL);
	
	esh_object obj;
	esh_object_init_entries(esh, &obj);
	
	esh_object_set(esh, &obj, "foo", 3, DUMMY_VAL);
	assert(obj.len == 1);
	
	esh_object_free_entries(esh, &obj);	
	esh_close(esh);
}

void test_add_entries() {
	esh_state *esh = esh_open(NULL);
	
	esh_object obj;
	esh_object_init_entries(esh, &obj);
	
	esh_object_set(esh, &obj, "foo", 3, DUMMY_VAL);
	esh_object_set(esh, &obj, "foobar", 6, DUMMY_VAL);
	esh_object_set(esh, &obj, "bar", 3, DUMMY_VAL);
	assert(obj.len == 3);
	
	esh_object_free_entries(esh, &obj);	
	esh_close(esh);
}

void test_get_entries() {
	esh_state *esh = esh_open(NULL);
	
	esh_object obj;
	esh_object_init_entries(esh, &obj);
	
	esh_object_set(esh, &obj, "foo", 3, DUMMY_VAL);
	esh_object_set(esh, &obj, "foobar", 6, DUMMY_VAL);
	esh_object_set(esh, &obj, "bar", 3, DUMMY_VAL);
	
	esh_val _;
	assert(esh_object_get(esh, &obj, "foo", 3, &_));
	assert(esh_object_get(esh, &obj, "foobar", 6, &_));
	assert(esh_object_get(esh, &obj, "bar", 3, &_));
	assert(!esh_object_get(esh, &obj, "foo1", 4, &_));
	
	esh_object_free_entries(esh, &obj);	
	esh_close(esh);
}

void test_duplicate_entries() {
	esh_state *esh = esh_open(NULL);
	
	esh_object obj;
	esh_object_init_entries(esh, &obj);
	esh_object_set(esh, &obj, "foo", 3, DUMMY_VAL);
	esh_object_set(esh, &obj, "foobar", 6, DUMMY_VAL);
	esh_object_set(esh, &obj, "foo", 3, DUMMY_VAL);
	assert(obj.len == 2);
	
	esh_object_free_entries(esh, &obj);	
	esh_close(esh);
}

void test_delete_entry() {
	esh_state *esh = esh_open(NULL);
	
	esh_object obj;
	esh_object_init_entries(esh, &obj);
	
	esh_object_set(esh, &obj, "foo", 3, DUMMY_VAL);
	esh_object_set(esh, &obj, "foobar", 6, DUMMY_VAL);
	esh_object_set(esh, &obj, "bar", 3, DUMMY_VAL);
	assert(obj.len == 3);
	
	esh_object_delete_entry(esh, &obj, "foo", 3);
	
	assert(obj.len == 2);
	esh_val _;
	assert(!esh_object_get(esh, &obj, "foo", 3, &_));
	assert(esh_object_get(esh, &obj, "foobar", 6, &_));
	assert(esh_object_get(esh, &obj, "bar", 3, &_));
	assert(!esh_object_get(esh, &obj, "foo1", 4, &_));
	
	
	esh_object_free_entries(esh, &obj);
	esh_close(esh);
}

void test_delete_grow() {
	esh_state *esh = esh_open(NULL);
	
	esh_object obj;
	esh_object_init_entries(esh, &obj);
	
	esh_object_set(esh, &obj, "foo", 3, DUMMY_VAL);
	esh_object_set(esh, &obj, "bar", 3, DUMMY_VAL);
	assert(obj.len == 2);
	
	esh_object_delete_entry(esh, &obj, "foo", 3);
	
	size_t c1 = obj.cap;
	assert(obj.len == 1);
	
	esh_object_set(esh, &obj, "foobar", 6, DUMMY_VAL);
	esh_object_set(esh, &obj, "foobar2", 7, DUMMY_VAL);
	assert(obj.len == 3);
	assert(obj.cap != c1);
	
	esh_val _;
	assert(!esh_object_get(esh, &obj, "foo", 3, &_));
	assert(esh_object_get(esh, &obj, "foobar", 6, &_));
	assert(esh_object_get(esh, &obj, "foobar2", 7, &_));
	assert(esh_object_get(esh, &obj, "bar", 3, &_));
	assert(!esh_object_get(esh, &obj, "foo1", 4, &_));
	
	
	esh_object_free_entries(esh, &obj);
	esh_close(esh);
}

void test_delete_readd_entry() {
	esh_state *esh = esh_open(NULL);
	
	esh_object obj;
	esh_object_init_entries(esh, &obj);
	
	esh_object_set(esh, &obj, "foo", 3, DUMMY_VAL);
	esh_object_set(esh, &obj, "foobar", 6, DUMMY_VAL);
	esh_object_set(esh, &obj, "bar", 3, DUMMY_VAL);
	assert(obj.len == 3);
	
	esh_object_delete_entry(esh, &obj, "foo", 3);
	
	assert(obj.len == 2);
	
	esh_object_set(esh, &obj, "foo", 3, DUMMY_VAL);
	assert(obj.len == 3);
	
	esh_val _;
	assert(esh_object_get(esh, &obj, "foo", 3, &_));
	assert(esh_object_get(esh, &obj, "foobar", 6, &_));
	assert(esh_object_get(esh, &obj, "bar", 3, &_));
	assert(!esh_object_get(esh, &obj, "foo1", 4, &_));
	
	
	esh_object_free_entries(esh, &obj);
	esh_close(esh);
}
