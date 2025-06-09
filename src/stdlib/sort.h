#ifndef SORT_H_INCLUDED
#define SORT_H_INCLUDED

#include <stdbool.h>
#include "../esh.h"

int esh_sort(esh_state *esh, long long array, size_t len, int (*cmp)(esh_state *esh), bool reverse);

#endif
