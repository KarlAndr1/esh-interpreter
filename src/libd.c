#include "libd.h"



#ifdef __unix__

#include <dlfcn.h>

void *load_libd(esh_state *esh, const char *path) {
	void *p = dlopen(path, RTLD_LAZY);
	if(!p) esh_err_printf(esh, "Unable to load dynamic library '%s': %s", path, dlerror());
	return p;
}

int close_libd(esh_state *esh, libd *lib) {
	int err = dlclose(lib);
	if(err) esh_err_printf(esh, "Unable to close dynamic library: %s", dlerror());
	return err;
}

void (*libd_getf(esh_state *esh, libd *lib, const char *name))() {
	dlerror();
	void (*res)() = (void (*)()) dlsym(lib, name);
	const char *err = dlerror();
	if(res == NULL) {
		esh_err_printf(esh, "Unable to load dynamic library function '%s': %s", name, err? err : "Handle is null");
	}
	return res;
}

#else

void *load_libd(esh_state *esh, const char *path) {
	esh_err_printf(esh, "Unable to load dynamic library '%s': Dynamic loading is not supported for this platform", path);
	return NULL;
}

int close_libd(esh_state *esh, libd *lib) {
	esh_err_printf(esh, "Unable to close dynamic library: Dynamic loading is not supported for this platform");
	return 1;
}

void (*libd_getf(esh_state *esh, libd *lib, const char *name))() {
	esh_err_printf(esh, "Unable to load dynamic library function '%s': Dynamic loading is not supported for this platform", name);
	return NULL;
}

#endif
