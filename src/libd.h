#ifndef LIBD_H_INCLUDED
#define LIBD_H_INCLUDED

#include "esh.h"

typedef struct libd libd;

void *load_libd(esh_state *esh, const char *path);
int close_libd(esh_state *esh, libd *lib);
void (*libd_getf(esh_state *esh, libd *lib, const char *name))();


#endif
