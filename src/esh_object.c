#include "esh_object.h"

#include <assert.h>
#include <string.h>

static size_t strhash(const char *str, size_t len) {
	size_t hash = 0;
	for(size_t i = 0; i < len; i++) {
		hash *= 76934959338;
		hash += str[i];
		hash = hash ^ str[i] * 2525329532587438u;
	}
	
	return hash;
}

static esh_object_entry *object_find(esh_object_entry *entries, size_t cap, const char *key, size_t keylen) {
	assert(cap != 0);
	
	size_t index = strhash(key, keylen) % cap;
	for(size_t i = index; i < cap; i++) {
		if(
			entries[i].key == NULL ||
			(entries[i].keylen == keylen && memcmp(entries[i].key, key, sizeof(char) * keylen) == 0)
		) {
			return &entries[i];
		}
	}
	for(size_t i = 0; i < index; i++) {
		if(
			entries[i].key == NULL ||
			(entries[i].keylen == keylen && memcmp(entries[i].key, key, sizeof(char) * keylen) == 0)
		) {
			return &entries[i];
		}
	}
	return NULL;
}

bool esh_object_get(esh_state *esh, esh_object *obj, const char *key, size_t keylen, esh_val *out_val) {
	(void) esh;
	
	if(obj->len == 0) return false;
	
	esh_object_entry *entry = object_find(obj->entries, obj->cap, key, keylen);
	if(!entry || entry->key == NULL || entry->deleted) return false;
	
	*out_val = entry->val;
	return true;
}

int esh_object_set(esh_state *esh, esh_object *obj, const char *key, size_t keylen, esh_val val) {
	if(obj->is_const) {
		esh_err_printf(esh, "Attempting to mutate constant object");
		return 1;
	}
	
	if(val == ESH_NULL) {
		esh_object_delete_entry(esh, obj, key, keylen);
		return 0;
	}
	
	#define GROW_FACTOR(x) (x) * 2 + 1
	#define GROW_THRESHOLD(x) (x) / 3 * 2
	
	if(obj->len >= GROW_THRESHOLD(obj->cap)) {
		size_t new_cap = GROW_FACTOR(obj->cap);
		esh_object_entry *new_entries = esh_alloc(esh, sizeof(esh_object_entry) * new_cap);
		if(!new_entries) return 1;
		for(size_t i = 0; i < new_cap; i++) {
			new_entries[i].key = NULL;
			new_entries[i].deleted = false;
		}
		
		for(size_t i = 0; i < obj->cap; i++) {
			esh_object_entry *entry = &obj->entries[i];
			if(entry->key == NULL) continue;
			if(entry->deleted) {
				esh_free(esh, entry->key);
				continue;
			}
			
			*(object_find(new_entries, new_cap, entry->key, entry->keylen)) = *entry;
		}
		
		esh_free(esh, obj->entries);
		obj->entries = new_entries;
		obj->cap = new_cap;
	}
	
	assert(obj->len < obj->cap);
	
	esh_object_entry *entry = object_find(obj->entries, obj->cap, key, keylen);
	assert(entry != NULL);
	
	if(entry->key != NULL) {
		entry->val = val;
		if(entry->deleted) {
			entry->deleted = false;
			obj->len++;
		}
		return 0;
	}
	
	char *key_cpy = esh_alloc(esh, sizeof(char) * keylen);
	if(!key_cpy) return 1;
	
	memcpy(key_cpy, key, sizeof(char) * keylen);
	entry->key = key_cpy;
	entry->keylen = keylen;
	entry->val = val;
	
	obj->len++;
	
	return 0;
}

bool esh_object_delete_entry(esh_state *esh, esh_object *obj, const char *key, size_t keylen) {
	(void) esh;
	if(obj->cap == 0) return false;
	
	esh_object_entry *entry = object_find(obj->entries, obj->cap, key, keylen);	
	if(entry != NULL && entry->key != NULL && !entry->deleted) {
		entry->deleted = true;
		obj->len--;
		return true;
	}
	return false;
}

void esh_object_init_entries(esh_state *esh, esh_object *obj) {
	(void) esh;
	obj->entries = NULL;
	obj->len = 0;
	obj->cap = 0;
	obj->is_const = false;
}

void esh_object_free_entries(esh_state *esh, esh_object *obj) {
	for(size_t i = 0; i < obj->cap; i++) {
		esh_free(esh, obj->entries[i].key);
	}
	
	esh_free(esh, obj->entries);
	obj->entries = NULL;
	obj->len = 0;
	obj->cap = 0;
}
