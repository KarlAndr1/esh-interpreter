#ifndef JSON_H_INCLUDED
#define JSON_H_INCLUDED

#include "../esh.h"

int parse_json(esh_state *esh, const char *json, size_t strlen);

int to_json(esh_state *esh);

#endif
