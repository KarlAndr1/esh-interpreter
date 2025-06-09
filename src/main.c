#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "esh.h"
#include "esh_stdlib.h"

#include "colours.h"

#ifdef __unix__
#include <unistd.h>
#endif

static const char *is_prefixed(const char *str, const char *prefix) {
	while(true) {
		if(*prefix == '\0') return str;
		if(*str == '\0') return NULL;
		if(*str != *prefix) return NULL;
		
		str++;
		prefix++;
	}
}

static void print_current_dir() {
	#ifdef __unix__
	
	char buff[512];
	if(!getcwd(buff, sizeof(buff))) return;
	
	const char *home = getenv("HOME");
	if(!home) return;
	
	const char *from = is_prefixed(buff, home);
	if(!from) from = buff;
	else putchar('~');
	
	fputs(from, stdout);
	
	#else
	const char *pwd = getenv("PWD");
	if(!pwd) return;
	
	fputs(pwd, stdout);
	#endif
}

static void prompt(esh_state *esh) {
	printf("%s - %s\n", esh_get_project_name(), esh_get_version());
	
	char line[1024];
	while(1) {
		fputs(COL_A, stdout);
		print_current_dir();
		fputs(COL_B " $ " COL_RESET, stdout);
		if(!fgets(line, sizeof(line), stdin)) break;
		if(strcmp(line, "quit\n") == 0) break;
		
		int err;
		err = esh_loads(esh, "stdin", line, true);
		if(err) {
			fprintf(stderr, COL_ERR "Syntax Error: %s\n" COL_RESET, esh_get_err(esh));
			continue;
		}
		
		err = esh_exec_fn(esh);
		if(err) fprintf(stderr, COL_ERR "%s\nIn:\n%s\n" COL_RESET, esh_get_err(esh), esh_get_stack_trace(esh));
		else {
			if(!esh_is_null(esh, -1)) {
				fputs("> ", stdout);
				esh_save_stack(esh);
				esh_stdlib_print_val(esh, -1, stdout);
				esh_restore_stack(esh);
				putc('\n', stdout);
			}
			esh_pop(esh, 1);
		}
	}
}

static void run_rcfile(esh_state *esh, const char *file) {
	const char *home = getenv("HOME");
	if(!home) return;
	
	size_t home_len = strlen(home);
	size_t file_len = strlen(file);
	
	char *path_buff = esh_alloc(esh, sizeof(char) * (home_len + file_len + 1));
	if(!path_buff) {
		fputs("Unable to allocate buffer for rc file path (out of memory?)", stderr);
		exit(1);
	}
	memcpy(path_buff, home, home_len);
	memcpy(path_buff + home_len, file, file_len);
	path_buff[home_len + file_len] = '\0';
	
	int err = esh_loadf(esh, path_buff);
	if(err == 1) {
		fprintf(stderr, COL_ERR "Syntax Error: %s\n" COL_RESET, esh_get_err(esh));
		goto END;
	} else if(err == 2) { // Unable to find/open file
		goto END;
	}
	
	if(esh_exec_fn(esh)) fprintf(stderr, COL_ERR "%s\nIn:\n%s\n" COL_RESET, esh_get_err(esh), esh_get_stack_trace(esh));
	else esh_pop(esh, 1);
	
	END:
	esh_free(esh, path_buff);
}

typedef struct cmd_opts {
	const char *script;
	int args_from;
	
	int gc_freq;
} cmd_opts;

static bool parse_longopt(const char *opt, const char *next_arg, cmd_opts *opts) {
	(void) next_arg, (void) opts;
	
	if(strcmp(opt, "gc-freq") == 0) {
		if(!next_arg) {
			fprintf(stderr, "'--%s' requires an argument\n", opt);
			exit(-1);
		}
		errno = 0;
		opts->gc_freq = strtol(next_arg, NULL, 10);
		if(errno) {
			fprintf(stderr, "Invalid option '%s' for '--%s' (must be a valid integer)\n", next_arg, opt);
			exit(-1);
		}
		return true;
	}
	
	fprintf(stderr, "Unkown option '--%s'\n", opt);
	exit(-1);
}

static bool parse_shortopt(char opt, const char *next_arg, cmd_opts *opts) {
	(void) next_arg, (void) opts;
	switch(opt) {
		default:
			fprintf(stderr, "Unkown flag '-%c'\n", opt);
			exit(-1);
	}
}

static void parse_cmd_opts(int argc, const char **argv, cmd_opts *opts) {
	opts->script = NULL;
	opts->gc_freq = -1;
	
	for(int i = 1; i < argc; i++) {
		const char *arg = argv[i];
		const char *next = i == argc - 1? NULL : argv[i + 1];
		if(arg[0] == '-') {
			bool used_arg;
			if(arg[1] == '-') used_arg = parse_longopt(arg + 2, next, opts);
			else for(const char *c = arg + 1; *c != '\0'; c++) {
				bool res = parse_shortopt(*c, next, opts);
				if(res && used_arg) {
					fprintf(stderr, "Multiple flags expecting arguments in argument '%s'\n", arg);
					exit(-1);
				}
				used_arg = res;
			}
			if(used_arg) i++;
		} else {
			opts->script = arg;
			opts->args_from = i + 1;
			return;
		}
	}
}

int main(int argc, const char **argv) {
	esh_state *esh = esh_open(NULL);
	
	if(!esh) {
		fprintf(stderr, "Unable to open interpreter\n");
		return 1;
	}
	
	if(esh_load_stdlib(esh)) {
		fprintf(stderr, "Unable to load stdlib: %s", esh_get_err(esh));
		esh_close(esh);
		return 1;
	}
	
	run_rcfile(esh, "/.eshrc");
	
	cmd_opts opts;
	parse_cmd_opts(argc, argv, &opts);
	esh_gc_conf(esh, opts.gc_freq, -1);
	
	if(opts.script) {
		if(esh_object_of(esh, 0)) goto ESH_ERR;
		for(int i = 0; i < argc - opts.args_from; i++) {
			const char *arg = argv[opts.args_from + i];
			if(esh_new_string(esh, arg, strlen(arg))) goto ESH_ERR;
			if(esh_set_i(esh, -2, i, -1)) goto ESH_ERR;
			esh_pop(esh, 1);
		}
		if(esh_set_global(esh, "argv")) goto ESH_ERR;
		esh_pop(esh, 1);
		
		if(esh_loadf(esh, opts.script)) goto ESH_ERR;
		
		if(esh_exec_fn(esh)) {
			fprintf(stderr, COL_ERR "%s\nIn:\n%s\n" COL_RESET, esh_get_err(esh), esh_get_stack_trace(esh));
			esh_close(esh);
			return 1;
		}
	} else {
		if(!esh_get_global(esh, "esh-prompt")) {
			if(esh_exec_fn(esh)) {
				fprintf(stderr, COL_ERR "%s\nIn:\n%s\n" COL_RESET, esh_get_err(esh), esh_get_stack_trace(esh));
				esh_close(esh);
				return 1;
			}
		} else {
			prompt(esh);
		}
	}
	
	esh_close(esh);
	return 0;
	
	ESH_ERR:
	fprintf(stderr, COL_ERR "%s\n" COL_RESET, esh_get_err(esh));
	esh_close(esh);
	return 1;
}
