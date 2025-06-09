#ifndef ESHC_H_INCLUDED
#define ESHC_H_INCLUDED

#include "esh.h"

int esh_compile_src(esh_state *esh, const char *name, const char *src, size_t len, bool interactive_mode);

#endif
