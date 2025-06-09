#ifndef ESH_HASHTABLE_INCLUDED
#define ESH_HASHTABLE_INCLUDED

#define INCLUDE_PRIVATE_API
#include "esh.h"

#include <stdbool.h>

bool esh_object_get(esh_state *esh, esh_object *obj, const char *key, size_t keylen, esh_val *out_val);
int esh_object_set(esh_state *esh, esh_object *obj, const char *key, size_t keylen, esh_val val);
bool esh_object_delete_entry(esh_state *esh, esh_object *obj, const char *key, size_t keylen);

void esh_object_init_entries(esh_state *esh, esh_object *obj);
void esh_object_free_entries(esh_state *esh, esh_object *obj);

#endif
