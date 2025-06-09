#include "graphics.h"
#include "../../esh.h"

#include <assert.h>

#include "glad_gl330/include/glad/glad.h"
#include <GLFW/glfw3.h>

typedef struct esh_graphics_window {
	esh_object object;
	GLFWwindow *window;
	
	const esh_graphics_renderer *renderer;
	void *userdata;
} esh_graphics_window;

static esh_type esh_graphics_window_type = {
	.name = "Window"
};

int esh_graphics_bind_window(esh_state *esh, long long offset) {
	esh_graphics_window *window = esh_as_type(esh, offset, &esh_graphics_window_type);
	if(!window) return 1;
	
	glfwMakeContextCurrent(window->window);
	return 0;
}

void esh_graphics_unbind_window(esh_state *esh) {
	(void) esh;
	glfwMakeContextCurrent(NULL);
}

void *esh_graphics_window_use_renderer(esh_state *esh, long long offset, const esh_graphics_renderer *renderer, size_t userdata_size) {
	esh_graphics_window *window = esh_as_type(esh, offset, &esh_graphics_window_type);
	if(!window) return NULL;
	
	if(userdata_size == 0) {
		esh_err_printf(esh, "Window renderer userdata size cannot be zero");
		return NULL;
	}
	
	void *userdata = esh_alloc(esh, userdata_size);
	if(!userdata) {
		esh_err_printf(esh, "Unable to allocate window renderer data (out of memory?)");
		return NULL;
	}
	if(renderer->init) {
		if(renderer->init(esh, userdata)) return NULL;
	}
	
	if(window->renderer && window->renderer->free) window->renderer->free(esh, window->userdata);
	
	window->renderer = renderer;
	window->userdata = userdata;
	return userdata;
}

esh_fn_result open_window(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 3);
	assert(i == 0);
	
	const char *name = esh_as_string(esh, 0, NULL);
	if(!name) return ESH_FN_ERR;
	
	long long x, y;
	if(esh_as_int(esh, 1, &x)) return ESH_FN_ERR;
	if(esh_as_int(esh, 2, &y)) return ESH_FN_ERR;
	
	GLFWwindow *gw = glfwCreateWindow(x, y, name, NULL, NULL);
	if(!gw) {
		const char *msg;
		glfwGetError(&msg);
		esh_err_printf(esh, "Failed to open window: %s", msg);
		return ESH_FN_ERR;
	}
	
	glfwMakeContextCurrent(gw);
	
	if(!gladLoadGLLoader( (GLADloadproc) glfwGetProcAddress )) {
		glfwDestroyWindow(gw);
		esh_err_printf(esh, "Failed to load glad");
		return ESH_FN_ERR;
	}
	
	glfwMakeContextCurrent(NULL);
	
	esh_graphics_window *window = esh_new_object(esh, sizeof(esh_graphics_window), &esh_graphics_window_type);
	if(!window) {
		glfwDestroyWindow(gw);
		return ESH_FN_ERR;
	}
	
	window->renderer = NULL;
	window->userdata = NULL;
	
	window->window = gw;
	return ESH_FN_RETURN(1);
}

esh_fn_result await_window(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1);
	assert(i == 0);
	
	esh_graphics_window *window = esh_as_type(esh, 0, &esh_graphics_window_type);
	if(!window) return ESH_FN_ERR;
	
	glfwMakeContextCurrent(window->window);
	glfwWaitEvents();
	glfwMakeContextCurrent(NULL);
	
	if(esh_push_null(esh)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

esh_fn_result close_window(esh_state *esh, size_t n_args, size_t i) {
	assert(n_args == 1);
	assert(i == 0);
	
	esh_graphics_window *window = esh_as_type(esh, 0, &esh_graphics_window_type);
	if(!window) return ESH_FN_ERR;
	
	if(window->renderer && window->renderer->free) window->renderer->free(esh, window->userdata);
	glfwDestroyWindow(window->window);
	
	if(esh_push_null(esh)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

GLuint esh_graphics_compile_shader(esh_state *esh, const char *vert, const char *frag) {
	GLuint vert_shader = glCreateShader(GL_VERTEX_SHADER);
	GLuint frag_shader = glCreateShader(GL_FRAGMENT_SHADER);
	
	glShaderSource(vert_shader, 1, (const char *[]) { vert }, NULL);
	glShaderSource(frag_shader, 1, (const char *[]) { frag }, NULL);
	
	glCompileShader(vert_shader);
	GLint ok;
	glGetShaderiv(vert_shader, GL_COMPILE_STATUS, &ok);
	if(ok == GL_FALSE) {
		char err_msg[256];
		glGetShaderInfoLog(vert_shader, sizeof(err_msg), NULL, err_msg);
		esh_err_printf(esh, "Vertex shader compilation failed:\n%s", err_msg);
		return 0;
	}
	
	glCompileShader(frag_shader);
	glGetShaderiv(frag_shader, GL_COMPILE_STATUS, &ok);
	if(ok == GL_FALSE) {
		glDeleteShader(vert_shader);
		char err_msg[256];
		glGetShaderInfoLog(frag_shader, sizeof(err_msg), NULL, err_msg);
		esh_err_printf(esh, "Fragment shader compilation failed:\n%s", err_msg);
		return 0;
	}
	
	GLuint program = glCreateProgram();
	glAttachShader(program, vert_shader);
	glAttachShader(program, frag_shader);
	
	glLinkProgram(program);
	glDetachShader(program, vert_shader);
	glDeleteShader(vert_shader);
	glDetachShader(program, frag_shader);
	glDeleteShader(frag_shader);
	
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if(ok == GL_FALSE) {
		glDeleteProgram(program);
		char err_msg[256];
		glGetProgramInfoLog(program, sizeof(err_msg), NULL, err_msg);
		esh_err_printf(esh, "Shader linking failed:\n%s", err_msg);
		return 0;
	}
	
	return program;
}

// Primitives renderer
typedef struct primitive_vertex {
	GLfloat pos[2];
	GLubyte col[3];
} primitive_vertex;

typedef struct primitives_renderer {
	GLuint shader, vao, vbo, ebo;
	size_t vbo_len, vbo_cap;
	size_t ebo_len, ebo_cap;
} primitives_renderer;

static int primitives_renderer_init(esh_state *esh, void *p) {
	primitives_renderer *renderer = p;
	
	*renderer = (primitives_renderer) {
		.shader = 0, .vao = 0, .vbo = 0, .ebo = 0,
	
		.vbo_len = 0,
		.vbo_cap = 0,
		
		.ebo_len = 0,
		.ebo_cap = 0
	};
	
	glGenVertexArrays(1, &renderer->vao);
	glGenBuffers(1, &renderer->vbo);
	glGenBuffers(1, &renderer->ebo);
	
	renderer->shader = esh_graphics_compile_shader(
		esh,
		"#version 330 core\n"
		"layout(location = 0) in vec2 pos;\n"
		"layout(location = 1) in vec3 col;\n"
		"out vec3 vert_col;\n"
		"void main() {\n"
		"	vert_col = in_col;\n"
		"	gl_Position = pos;\n"
		"}"
		,
		"#version 330 core\n"
		"in vec3 vert_col;\n"
		"out vec4 col;\n"
		"void main() {\n"
		"	col = vec4(vert_col, 1);\n"
		"}"
	);
	
	if(!renderer->shader) {
		glDeleteBuffers(1, &renderer->vbo);
		glDeleteBuffers(1, &renderer->ebo);
		glDeleteVertexArrays(1, &renderer->vao);
		return 1;
	}
	
	return 0;
}

static void primitives_renderer_free(esh_state *esh, void *p) {
	(void) esh;
	primitives_renderer *renderer = p;
	
	glDeleteProgram(renderer->shader);
	glDeleteVertexArrays(1, &renderer->vao);
	glDeleteBuffers(1, &renderer->vbo);
	glDeleteBuffers(1, &renderer->ebo);
}

static esh_graphics_renderer primitives_renderer_type = {
	.free = primitives_renderer_free,
	.init = primitives_renderer_init
};

static size_t add_primitive_verts(primitives_renderer *renderer, const primitive_vertex *verts, size_t n_verts) {
	if(renderer->vbo_len + n_verts > renderer->vbo_cap) {
		renderer->vbo_cap = renderer->vbo_cap * 3 / 2 + n_verts;
		GLuint new_buffer;
		glGenBuffers(1, &new_buffer);
		
		glBindBuffer(GL_COPY_READ_BUFFER, renderer->vbo);
		glBindBuffer(GL_ARRAY_BUFFER, new_buffer);
		glBufferData(GL_ARRAY_BUFFER, sizeof(primitive_vertex) * renderer->vbo_cap, NULL, GL_DYNAMIC_DRAW);
		glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_ARRAY_BUFFER, 0, 0, sizeof(primitive_vertex) * renderer->vbo_len);
		glBindBuffer(GL_COPY_READ_BUFFER, 0);
		glDeleteBuffers(1, &renderer->vbo);
		renderer->vbo = new_buffer;
		
		glBindVertexArray(renderer->vao);
		glVertexAttribPointer(0, 2, GL_FLOAT, false, sizeof(primitive_vertex), (void *) offsetof(primitive_vertex, pos));
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(1, 3, GL_UNSIGNED_BYTE, true, sizeof(primitive_vertex), (void *) offsetof(primitive_vertex, col));
		glEnableVertexAttribArray(1);
		glBindVertexArray(0);
	} else {
		glBindBuffer(GL_ARRAY_BUFFER, renderer->vbo);
	}
	
	size_t vertex_id = renderer->vbo_len;
	renderer->vbo_len += n_verts;
	glBufferSubData(GL_ARRAY_BUFFER, sizeof(primitive_vertex) * vertex_id, sizeof(primitive_vertex) * n_verts, verts);
	
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	return vertex_id;
}

esh_fn_result draw_square(esh_state *esh, size_t n_args, size_t i) {
	assert(i == 0);
	assert(n_args == 5 || n_args == 6);
	
	primitives_renderer *renderer = esh_graphics_window_use_renderer(esh, 0, &primitives_renderer_type, sizeof(primitives_renderer));
	if(!renderer) return ESH_FN_ERR;
	
	float x1 = 0, y1 = 0, x2 = 0.5, y2 = 0.5;
	
	primitive_vertex verts[] = {
		{ .pos = { x1, y1 } },
		{ .pos = { x1, y2 } },
		{ .pos = { x2, y2 } },
		{ .pos = { x2, y2 } },
		{ .pos = { x2, y1 } },
		{ .pos = { x1, y1 } }
	};
	
	add_primitive_verts(renderer, verts, 6);
	
	if(esh_push_null(esh)) return ESH_FN_ERR;
	return ESH_FN_RETURN(1);
}

int esh_graphics_init(esh_state *esh) {
	(void) esh;
	static int has_init = 0;
	if(!has_init) {		
		if(!glfwInit()) return 1;
		
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		
		has_init = 1;
	}
	
	esh_save_stack(esh);
	if(esh_object_of(esh, 0)) goto ERR;
	
	if(esh_new_c_fn(esh, "open-window", open_window, 3, 0, false)) goto ERR;
	if(esh_set_cs(esh, -2, "open-window", -1)) goto ERR;
	esh_pop(esh, 1);
	
	if(esh_new_c_fn(esh, "await-window", await_window, 1, 0, false)) goto ERR;
	if(esh_set_cs(esh, -2, "await-window", -1)) goto ERR;
	esh_pop(esh, 1);
	
	if(esh_new_c_fn(esh, "close-window", close_window, 1, 0, false)) goto ERR;
	if(esh_set_cs(esh, -2, "close-window", -1)) goto ERR;
	esh_pop(esh, 1);
	
	return 0;
	
	esh_restore_stack(esh);
	ERR:
	return 1;
}
