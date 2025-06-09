#ifndef ESH_GRAPHICS_H_INCLUDED
#define ESH_GRAPHICS_H_INCLUDED

#include "../../esh.h"

void esh_graphics_unbind_window(esh_state *esh);
int esh_graphics_bind_window(esh_state *esh, long long offset);

typedef struct esh_graphics_renderer {
	void (*render)(esh_state *esh, void *userdata);
	void (*free)(esh_state *esh, void *userdata);
	int (*init)(esh_state *esh, void *userdata);
} esh_graphics_renderer;

void *esh_graphics_window_use_renderer(esh_state *esh, long long offset, const esh_graphics_renderer *renderer, size_t userdata_size);

int esh_graphics_init(esh_state *esh);

#endif
