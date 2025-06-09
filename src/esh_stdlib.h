#ifndef ESH_STDLIB_H_INCLUDED
#define ESH_STDLIB_H_INCLUDED

#include "esh.h"
#include <stdio.h>

int esh_stdlib_print_val(esh_state *esh, long long i, FILE *f);

int esh_load_stdlib(esh_state *esh);

typedef struct esh_char_stream esh_char_stream;

long long esh_char_stream_read(esh_state *esh, long long offset, char *buff, size_t n);
bool esh_is_char_stream(esh_state *esh, long long offset);

#endif
