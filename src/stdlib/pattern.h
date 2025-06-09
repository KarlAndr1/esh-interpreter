#ifndef PATTERN_H_INCLUDED
#define PATTERN_H_INCLUDED

#include "../esh.h"

#include <stdlib.h>

int esh_pattern_match(esh_state *esh, const char *str, size_t strlen, const char *pattern, size_t pattern_len, bool match_entire);
size_t *esh_pattern_match_captures(size_t *n_captures);

int esh_pattern_escape(esh_state *esh, const char *str, size_t strlen);

#endif
