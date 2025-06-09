#include "unix.h"

#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "utf8.h"

#ifdef __unix__ 

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

struct esh_char_stream {
	esh_object obj;
	
	int fd;

	pid_t pid;
	int exit_status;
};

size_t n_char_streams = 0;
static size_t char_stream_limit = 0;

static void char_stream_close(esh_state *esh, esh_char_stream *cs) {
	(void) esh;
	if(cs->fd == -1) return;
	if(cs->fd != STDIN_FILENO) close(cs->fd);
	cs->fd = -1;
	n_char_streams--;
}

static void char_stream_free(esh_state *esh, void *p) {
	(void) esh;
	esh_char_stream *cs = p;
	char_stream_close(esh, cs);
}

static int char_stream_next(esh_state *esh, void *p, size_t size_hint) {
	esh_char_stream *cs = p;
	if(cs->fd == -1) goto END;
	
	char c;
	char *res = &c;
	char *buff = NULL;
	if(size_hint > 1) {
		buff = esh_alloc(esh, sizeof(char) * size_hint);
		if(!buff) {
			esh_err_printf(esh, "Unable to allocate stream read buffer. Out of memory?");
			return 1;
		}
		res = buff;
	} 
	
	ssize_t n_read = read(cs->fd, res, size_hint);
	if(n_read == -1) {
		free(buff);
		esh_err_printf(esh, "Unable to read from stream: %s", strerror(errno));
		return 1;
	}
	if(n_read == 0) {
		free(buff);
		char_stream_close(esh, cs);
		goto END;
	}
	
	if(esh_new_string(esh, res, n_read)) {
		free(buff);
		return 1;
	}
	free(buff);
	return 0;
	
	END:
	if(esh_push_null(esh)) return 1;
	return 0;
}

static esh_type char_stream_type = {
	.name = "char-stream",
	.on_free = char_stream_free,
	.next = char_stream_next
};

/*
	Creates a new char stream from a file descriptor. Note that the char stream
	"takes ownership" of the given file descriptor; e.g it may reconfigure, close or modify
	it in any way after this call. It will also automatically close it when garbage collected, so the file
	descriptor should not be closed manually; outside of using char_stream_close(), of course.
*/
static esh_char_stream *new_char_stream(esh_state *esh, int fd) {
	if(char_stream_limit && n_char_streams >= char_stream_limit) esh_gc(esh, 0);
	
	esh_char_stream *cs = esh_new_object(esh, sizeof(esh_char_stream), &char_stream_type);
	if(!cs) return NULL;
	
	cs->fd = -1;
	cs->pid = -1;
	cs->exit_status = -1;
	
	if(fd != STDIN_FILENO) {
		int current_flags = fcntl(fd, F_GETFD);
		if(current_flags == -1) goto F_ERR;
		if(fcntl(fd, F_SETFD, current_flags | FD_CLOEXEC) == -1) goto F_ERR;
	}
	
	cs->fd = fd;
	n_char_streams++;
	
	return cs;
	
	F_ERR:
	esh_err_printf(esh, "Unable to configure char stream: %s", strerror(errno));
	return NULL;
}

int write_all(esh_state *esh, int fd, const char *str, size_t len) {
	int tries = 16;
	while(tries != 0) {
		ssize_t n = write(fd, str, len);
		if(n == -1) {
			esh_err_printf(esh, "Unable to write to file: %s", strerror(errno));
			return 1;
		}
		
		if((size_t) n == len) return 0;
		
		len -= n;
		str += n;
		if(n == 0) tries--;
	}
	
	return 0;
}

long long esh_char_stream_read(esh_state *esh, long long offset, char *buff, size_t n) {
	esh_char_stream *cs = esh_as_type(esh, offset, &char_stream_type);
	if(!cs) return -1;
	
	if(cs->fd == -1) return 0; // If it's been closed
	
	ssize_t n_read = read(cs->fd, buff, n);
	if(n_read == -1) {
		esh_err_printf(esh, "Unable to read from stream: %s", strerror(errno));
		return -1;
	}
	if(n_read == 0) {
		char_stream_close(esh, cs);
	}
	
	return n_read;
}

bool esh_is_char_stream(esh_state *esh, long long offset) {
	return esh_as_type(esh, offset, &char_stream_type) != NULL;
}

static pid_t fork_with(esh_state *esh, int *pipe_in, int *capture_stdout, int (*then)(esh_state *esh, void *), void *then_env) {
	int err_pipe[2] = { -1, -1 };
	int capture_pipe[2] = { -1, -1 };
	
	if(pipe(err_pipe)) {
		esh_err_printf(esh, "Unable to open pipe required for fork");
		goto ERR;
	}
	if(fcntl(err_pipe[1], F_SETFD, FD_CLOEXEC)) {
		esh_err_printf(esh, "Unable to configure pipe required for fork");
		goto ERR;
	}
	
	if(capture_stdout) {
		if(pipe(capture_pipe)) {
			esh_err_printf(esh, "Unable to open pipe required for fork capture");
			goto ERR;
		}
	}
	
	pid_t pid = fork();
	if(pid == 0) {
		// In child
		close(err_pipe[0]);
		
		if(capture_stdout) {
			close(capture_pipe[0]);
			capture_pipe[0] = -1;
			if(dup2(capture_pipe[1], STDOUT_FILENO) == -1) goto CHILD_ERR;
			close(capture_pipe[1]);
			capture_pipe[1] = -1;
		}
		
		if(pipe_in) if(dup2(*pipe_in, STDIN_FILENO) == -1) goto CHILD_ERR;
		
		if(then && then(esh, then_env)) {
			const char *err = esh_get_err(esh);
			write(err_pipe[1], err, strlen(err));
			exit(-1);
		}
		close(err_pipe[1]);
		
		return 0;
		
		CHILD_ERR:
		(void) 0;
		const char *err = strerror(errno);
		write(err_pipe[1], err, strlen(err));
		exit(-1);
	} else if(pid == -1) {
		esh_err_printf(esh, "Unable perform required fork");
		goto ERR;
	} else {
		close(err_pipe[1]);
		err_pipe[1] = -1;
		
		char err_buff[512];
		ssize_t n = read(err_pipe[0], err_buff, sizeof(err_buff));
		close(err_pipe[0]);
		err_pipe[0] = -1;
		
		if(n == -1) {
			esh_err_printf(esh, "Unable to read from command error pipe");
			goto ERR;
		} else if(n != 0) {
			esh_err_printf(esh, "%.*s", (int) n, err_buff);
			goto ERR;
		}
		
		if(capture_stdout) {
			close(capture_pipe[1]);
			capture_pipe[1] = -1;
			
			*capture_stdout = capture_pipe[0];
		}
		
		return pid;
	}
	
	ERR:
	if(err_pipe[0] != -1) close(err_pipe[0]);
	if(err_pipe[1] != -1) close(err_pipe[1]);
	if(capture_pipe[0] != -1) close(capture_pipe[0]);
	if(capture_pipe[1] != -1) close(capture_pipe[1]);
	return -1;
}

static pid_t fork_and_pipe_val(esh_state *esh, long long *pipe_in_val, int *coroutine_pipe, int *capture_stdout, int (*then)(esh_state *esh, void *), void *then_env) {
	*coroutine_pipe = -1;
	
	if(!pipe_in_val || esh_is_null(esh, *pipe_in_val)) return fork_with(esh, NULL, capture_stdout, then, then_env);
	

	esh_char_stream *cs = esh_as_type(esh, *pipe_in_val, &char_stream_type);
	const char *str; size_t len;
	if(cs) {
		pid_t pid = fork_with(esh, &cs->fd, capture_stdout, then, then_env); 
		if(pid != -1) char_stream_close(esh, cs);
		return pid;
	} else if( (str = esh_as_string(esh, *pipe_in_val, &len)) ) {
		int pipes[2];
		if(pipe(pipes)) {
			esh_err_printf(esh, "Unable to open pipe required for fork: %s", strerror(errno));
			return -1;
		}
		if(write(pipes[1], str, len) == -1 || fcntl(pipes[0], F_SETFD, FD_CLOEXEC) == -1) {
			close(pipes[0]);
			close(pipes[1]);
			esh_err_printf(esh, "Unable to configure pipe for process input: %s", strerror(errno));
			return -1;
		}
		close(pipes[1]);
		pid_t pid = fork_with(esh, &pipes[0], capture_stdout, then, then_env); 
		close(pipes[0]);
		return pid;
	} else { // Otherwise, assume that the value is a coroutine
		int pipes[2];
		if(pipe(pipes)) {
			esh_err_printf(esh, "Unable to open pipe required for fork: %s", strerror(errno));
			return -1;
		}
		if(fcntl(pipes[0], F_SETFD, FD_CLOEXEC) == -1 || fcntl(pipes[1], F_SETFD, FD_CLOEXEC) == -1) {
			esh_err_printf(esh, "Unable to configure pipe for prcess input: %s", strerror(errno));
			return -1;
		}
		pid_t pid = fork_with(esh, &pipes[0], capture_stdout, then, then_env);
		close(pipes[0]);
		if(pid != -1 || pid != 0) *coroutine_pipe = pipes[1];
		else close(pipes[0]);
		
		return pid;
	}
}

struct cmd_struct {
	const char *cmd;
	const char **args;
};

static int exec_cmd(esh_state *esh, void *p) {
	struct cmd_struct *cmd = p;
	execvp(cmd->cmd, (char * const*) cmd->args);
	
	esh_err_printf(esh, "Unable to exec command '%s': %s", cmd->cmd, strerror(errno));
	return 1;
}

static int get_cmd_args(esh_state *esh, size_t n_args, const char **cmd, const char ***args, bool *pipe_in, bool *capture) {
	assert(n_args >= 3);
	
	*cmd = esh_as_string(esh, -3, NULL);
	assert(cmd);
	*pipe_in = esh_as_bool(esh, -2);
	*capture = esh_as_bool(esh, -1);
	n_args -= 3;
	
	if(*pipe_in) n_args--; // If a value is being piped in, we don't want to treat it as a command argument, but instead write it to the process stdin
	
	*args = esh_alloc(esh, sizeof(char *) * (n_args + 2));
	if(!*args) {
		esh_err_printf(esh, "Unable to allocate command argument buffer for command invocation (out of memory?)");
		return 1;
	}
	
	(*args)[0] = *cmd;
	(*args)[n_args + 1] = NULL;
	
	for(size_t i = 0; i < n_args; i++) {
		(*args)[i + 1] = esh_as_string(esh, i + (*pipe_in? 1 : 0), NULL);
		if((*args)[i + 1] == NULL) {
			esh_err_printf(esh, "Can only pass string arguments to commands");
			esh_free(esh, *args);
			return 1;
		}
	}
	
	return 0;
}

static void unix_command_handler2_free(esh_state *esh, void *p) {
	(void) esh;
	int *coroutine_pipe = p;
	if(*coroutine_pipe != -1) close(*coroutine_pipe);
}

static esh_fn_result unix_command_handler2(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args >= 3);
	
	int *coroutine_pipe = esh_locals(esh, sizeof(int), unix_command_handler2_free);
	if(!coroutine_pipe) return ESH_FN_ERR;
	
	if(i == 0) *coroutine_pipe = -1;
	else {
		if(esh_is_null(esh, -1)) { 
			esh_pop(esh, 1);
			return ESH_FN_RETURN(1);
		}
		
		size_t len;
		const char *str = esh_as_string(esh, -1, &len);
		if(!str) return ESH_FN_ERR;
		
		if(write_all(esh, *coroutine_pipe, str, len)) return ESH_FN_ERR;
		esh_pop(esh, 1);
		
		if(esh_dup(esh, 0)) return ESH_FN_ERR;
		return ESH_FN_NEXT(0, 1);
	}
	
	const char *cmd, **args;
	bool pipe_in, capture;
	if(get_cmd_args(esh, n_args, &cmd, &args, &pipe_in, &capture)) return ESH_FN_ERR;
	
	long long pipe_val = 0;
	int process_out;
	struct cmd_struct cmd_args = { cmd, args };
	pid_t pid = fork_and_pipe_val(esh, pipe_in? &pipe_val : NULL, coroutine_pipe, capture? &process_out : NULL, exec_cmd, &cmd_args);
	
	if(pid == -1) {
		goto ERR;
	}
	
	if(!capture) {
		if(esh_push_null(esh)) goto ERR;
		waitpid(pid, NULL, 0);
	} else {
		esh_char_stream *res = new_char_stream(esh, process_out);
		if(!res) {
			close(process_out);
			goto ERR;
		}
		
		res->pid = pid;
	}
	
	esh_free(esh, args);
	
	if(*coroutine_pipe != -1) {
		if(esh_dup(esh, 0)) return ESH_FN_ERR;
		return ESH_FN_NEXT(0, 1);
	}
	
	return ESH_FN_RETURN(1);
	
	ERR:
	esh_free(esh, args);
	return ESH_FN_ERR;
}

static esh_fn_result open_files(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 0);
	assert(i == 0);
	
	if(esh_push_int(esh, n_char_streams)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

static esh_fn_result unix_cd(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 1);
	
	const char *path = esh_as_string(esh, 0, NULL);
	if(!path) return ESH_FN_ERR;
	
	chdir(path);
	
	if(esh_push_null(esh)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

#include <sys/types.h>
#include <dirent.h>

static esh_fn_result unix_dirlist(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 0 || n_args == 1 || n_args == 2);
	
	const char *path = ".";
	if(n_args >= 1) {
		path = esh_as_string(esh, 0, NULL);
		if(!path) {
			esh_err_printf(esh, "Dirlist path must be string");
			return ESH_FN_ERR;
		}
	}
	bool show_all = false;
	if(n_args == 2) {
		show_all = esh_as_bool(esh, 1);
	}
	
	if(esh_object_of(esh, 0)) return ESH_FN_ERR;
	
	DIR *dir = opendir(path);
	if(!dir) {
		esh_err_printf(esh, "Unable to open directory at '%s': %s", path, strerror(errno));
		return ESH_FN_ERR;
	}
	
	for(size_t i = 0;;) {
		errno = 0;
		struct dirent *entry = readdir(dir);
		if(entry == NULL && errno) {
			esh_err_printf(esh, "Unable to read directory at '%s': %s", path, strerror(errno));
			goto ERR;
		}
		
		if(entry == NULL) break;
		
		if(!show_all) {
			if(
				(entry->d_name[0] == '.' && entry->d_name[1] == '\0') ||
				(entry->d_name[0] == '.' && entry->d_name[1] == '.' && entry->d_name[2] == '\0')
			) continue;
		}
		
		if(esh_new_string(esh, entry->d_name, strlen(entry->d_name))) goto ERR;
		if(esh_set_i(esh, -2, i++, -1)) goto ERR;
		esh_pop(esh, 1);
	}
	
	closedir(dir);
	
	return ESH_FN_RETURN(1);
	
	ERR:
	closedir(dir);
	return ESH_FN_ERR;
}

#include <sys/stat.h>

static esh_fn_result unix_isdir(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 1);
	
	const char *path = esh_as_string(esh, -1, NULL);
	if(!path) {
		esh_err_printf(esh, "Expected string as argument to isdir");
		return ESH_FN_ERR;
	}
	
	struct stat res;
	if(stat(path, &res)) {
		esh_err_printf(esh, "Unable to stat file '%s': %s", path, strerror(errno));
		return ESH_FN_ERR;
	}
	
	if(esh_push_bool(esh, S_ISDIR(res.st_mode))) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

static esh_fn_result unix_isfile(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 1);
	
	const char *path = esh_as_string(esh, -1, NULL);
	if(!path) {
		esh_err_printf(esh, "Expected string as argument to isfile");
		return ESH_FN_ERR;
	}
	
	struct stat res;
	if(stat(path, &res)) {
		esh_err_printf(esh, "Unable to stat file '%s': %s", path, strerror(errno));
		return ESH_FN_ERR;
	}
	
	if(esh_push_bool(esh, S_ISREG(res.st_mode))) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

#include <termios.h>

struct rawprompt_locals {
	struct termios prev_termios;
	bool has_init;
};

static void rawprompt_free(esh_state *esh, void *p) {
	(void) esh;
	struct rawprompt_locals *locals = p;
	if(!locals->has_init) return;
	
	tcsetattr(STDIN_FILENO, TCSANOW, &locals->prev_termios);
}

static esh_fn_result unix_rawprompt(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1);
	
	struct rawprompt_locals *locals = esh_locals(esh, sizeof(struct rawprompt_locals), rawprompt_free);
	if(!locals) return ESH_FN_ERR;
	
	if(i == 0) {
		if(tcgetattr(STDIN_FILENO, &locals->prev_termios)) {
			esh_err_printf(esh, "Unable to enter raw terminal mode; cannot read current mode: %s", strerror(errno));
			return ESH_FN_ERR;
		}
		
		struct termios new_termios = locals->prev_termios;
		new_termios.c_lflag &= ~(ICANON | ECHO | ECHONL | ISIG);
		//cfmakeraw(&new_termios);
		if(tcsetattr(STDIN_FILENO, TCSANOW, &new_termios)) {
			esh_err_printf(esh, "Unable to enter raw terminal mode; cannot set mode: %s", strerror(errno));
			return ESH_FN_ERR;
		}
		locals->has_init = true;
	} else {
		if(!esh_is_null(esh, -1)) return ESH_FN_RETURN(1);
		esh_pop(esh, 1);
	}
	
	char initial_char;
	ssize_t n_read = read(STDIN_FILENO, &initial_char, 1);
	if(n_read < 0) goto FILE_ERR;
	
	if(n_read == 0) {
		if(esh_dup(esh, 0)) return ESH_FN_ERR;
		if(esh_new_string(esh, "", 0)) return ESH_FN_ERR;
		return ESH_FN_CALL(1, 1);
	}
	
	esh_str_buff_begin(esh);
	if(esh_str_buff_appendc(esh, initial_char)) return ESH_FN_ERR;
	
	unsigned char_len = utf8_next(initial_char);
	
	if(char_len != 1) {
		char rest[3];
		assert(char_len <= 4);
		ssize_t n_read = read(STDIN_FILENO, rest, char_len - 1);
		if(n_read < 0) goto FILE_ERR;
		if(esh_str_buff_appends(esh, rest, n_read)) return ESH_FN_ERR;
	} else if(initial_char == 27) { // Escape
		bool first = true;
		while(true) {
			char c;
			ssize_t n_read = read(STDIN_FILENO, &c, 1);
			if(n_read < 0) goto FILE_ERR;
			if(n_read == 0) break;
			
			if(esh_str_buff_appendc(esh, c)) return ESH_FN_ERR;
			
			if(first && c != '[') break;
			else if(isalpha(c)) break;
			
			first = false;
		}
	}
	
	if(esh_dup(esh, 0)) return ESH_FN_ERR;
	size_t len;
	char *str_buff = esh_str_buff(esh, &len);
	if(esh_new_string(esh, str_buff, len)) return ESH_FN_ERR;
	
	return ESH_FN_CALL(1, 1);

	FILE_ERR:
	esh_err_printf(esh, "Unable to read from terminal: %s", strerror(errno));
	return ESH_FN_ERR;
}

static esh_fn_result fork_fn(esh_state *esh, size_t n_args, size_t i) {
	if(i != 0) {
		if(esh_panic_caught(esh)) {
			fprintf(stderr, "Error in forked child: %s\n", esh_get_err(esh));
			exit(1);
		}
		exit(0);
	}
	
	int p_out;
	int p_in; // For coroutines this will need to be saved
	long long pipe_in = 0;
	pid_t pid = fork_and_pipe_val(esh, &pipe_in, &p_in, &p_out, NULL, NULL);
	if(pid == -1) return ESH_FN_ERR;
	
	if(pid == 0) { // In child
		return ESH_FN_TRY_CALL(n_args - 2, 1);
	} else { // In parent
		esh_char_stream *cs = new_char_stream(esh, p_out);
		if(!cs) { close(p_out); return ESH_FN_ERR; }
		cs->pid = pid;
		
		return ESH_FN_RETURN(1);
	}
}

static esh_fn_result read_fn(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1);
	assert(i == 0);
	
	const char *path = esh_as_string(esh, 0, NULL);
	
	int fd = open(path, O_RDONLY);
	if(fd == -1) {
		esh_err_printf(esh, "Unable to open file '%s': %s", path, strerror(errno));
		return ESH_FN_ERR;
	}
	
	if(!new_char_stream(esh, fd)) {
		close(fd);
		return ESH_FN_ERR;
	}
	
	return ESH_FN_RETURN(1);
}

static esh_fn_result limit_char_streams(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1);
	assert(i == 0);
	
	long long limit;
	if(esh_as_int(esh, 0, &limit)) return ESH_FN_ERR;
	
	if(limit < 0) {
		esh_err_printf(esh, "Char stream limit cannot be negative");
		return ESH_FN_ERR;
	}
	
	char_stream_limit = limit;
	
	if(esh_push_null(esh)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

static esh_fn_result close_fn(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1);
	assert(i == 0);
	
	esh_char_stream *cs = esh_as_type(esh, 0, &char_stream_type);
	if(!cs) return ESH_FN_ERR;
	
	char_stream_close(esh, cs);
	
	if(esh_push_null(esh)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

#include <signal.h>

static int sigchld_pipe[2];

void sigchld_handler(int sig, siginfo_t *info, void *uctx) {
	(void) uctx;
	if(sig != SIGCHLD) return;
	if(info->si_code != CLD_EXITED) return;
	
	//printf("Exited: %i\n", (int) info->si_pid);
	// The child exited: handle that here...
}

int esh_unix_stdlib_init(esh_state *esh) {
	#define REQ(x) if(x) return 1
	
	REQ(esh_new_c_fn(esh, "cmd", unix_command_handler2, 0, 0, true));
	esh_set_cmd(esh);

	REQ(esh_new_c_fn(esh, "cd", unix_cd, 1, 0, false));
	REQ(esh_set_global(esh, "cd"));

	REQ(esh_new_c_fn(esh, "dirlist", unix_dirlist, 0, 2, false));
	REQ(esh_set_global(esh, "dirlist"));

	REQ(esh_new_c_fn(esh, "isdir", unix_isdir, 1, 0, false));
	REQ(esh_set_global(esh, "isdir"));

	REQ(esh_new_c_fn(esh, "isfile", unix_isfile, 1, 0, false));
	REQ(esh_set_global(esh, "isfile"));

	REQ(esh_new_c_fn(esh, "rawprompt", unix_rawprompt, 1, 0, false));
	REQ(esh_set_global(esh, "rawprompt"));
	
	REQ(esh_new_c_fn(esh, "open-files", open_files, 0, 0, false));
	REQ(esh_set_global(esh, "open-files"));

	REQ(esh_new_c_fn(esh, "fork", fork_fn, 2, 0, true));
	REQ(esh_set_global(esh, "fork"));
	
	REQ(esh_new_c_fn(esh, "read", read_fn, 1, 0, false));
	REQ(esh_set_global(esh, "read"));
	
	REQ(esh_new_c_fn(esh, "limit-char-streams", limit_char_streams, 1, 0, false));
	REQ(esh_set_global(esh, "limit-char-streams"));
	
	REQ(esh_new_c_fn(esh, "close", close_fn, 1, 0, false));
	REQ(esh_set_global(esh, "close"));
	
	//int stdin_fd = dup(STDIN_FILENO);
	//if(stdin_fd == -1) {
	//	esh_err_printf(esh, "Unable to duplicate stdin file descriptor: %s", strerror(errno));
	//	return 1;
	//}
	if(!new_char_stream(esh, STDIN_FILENO)) {
		//close(stdin_fd);
		return 1;
	}
	REQ(esh_set_global(esh, "stdin"));
	
	struct sigaction act;
	memset(&act, 0, sizeof(act));
	sigemptyset(&act.sa_mask);
	act.sa_sigaction = sigchld_handler;
	act.sa_flags = SA_RESTART | SA_NOCLDWAIT | SA_SIGINFO;//SA_SIGINFO;
	
	if(sigaction(SIGCHLD, &act, NULL)) {
		esh_err_printf(esh, "Unable to set required signal handler: %s", strerror(errno));
		return 1;
	}
	
	return 0;
}

#else

int esh_unix_stdlib_init(esh_state *esh) {
	esh_err_printf(esh, "Unable to load UNIX stdlib: Unsupported platform");
	return 1;
}

#endif
