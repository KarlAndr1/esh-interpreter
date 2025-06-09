#include "esh_c.h"

#include <stdbool.h>
#include <ctype.h>
#include <assert.h>
#include <setjmp.h>
#include <string.h>

typedef enum token_type {
	TOK_NULL = 0,
	TOK_EOF,
	
	TOK_WORD,
	
	TOK_SIGIL,
	
	TOK_ASSIGN,
	
	TOK_WITH,
	TOK_DO,
	TOK_END,
	TOK_LOCAL,
	TOK_CONST,
	TOK_IF,
	TOK_ELSE,
	TOK_THEN,
	TOK_RETURN,
	TOK_FUNCTION,
	
	TOK_ADD,
	TOK_SUB,
	TOK_MUL,
	TOK_DIV,
	
	TOK_LESS,
	TOK_GREATER,
	TOK_EQUALS,
	TOK_NEQUALS,
	
	TOK_OPEN_BRACKET,
	TOK_CLOSE_BRACKET,
	
	TOK_OPEN_CURL,
	TOK_CLOSE_CURL,
	TOK_COMMA,
	
	TOK_COLON,
	
	TOK_PIPE,
	
	TOK_EXCL,
	TOK_OPT,
	
	TOK_AND,
	TOK_OR,
	TOK_NOT,
	
	TOK_STR_INTERP,
	
	TOK_NULL_LITERAL,
} token_type;

static const char *tok_name(token_type type) {
	switch(type) {
		case TOK_NULL:
			return "no token";
		case TOK_EOF:
			return "EOF";
		
		case TOK_WORD:
			return "word";
		
		case TOK_SIGIL:
			return "'$'";
		
		case TOK_ASSIGN:
			return "'='";
		
		case TOK_WITH:
			return "'with'";
		case TOK_DO:
			return "'do'";
		case TOK_END:
			return "'end'";
		case TOK_LOCAL:
			return "'local'";
		case TOK_CONST:
			return "'const'";
		case TOK_IF:
			return "'if'";
		case TOK_ELSE:
			return "'else'";
		case TOK_THEN:
			return "'then'";
		case TOK_RETURN:
			return "'return'";
		case TOK_FUNCTION:
			return "'function'";
		
		case TOK_ADD:
			return "'+'";
		case TOK_SUB:
			return "'-'";
		case TOK_MUL: 
			return "'*'";
		case TOK_DIV:
			return "'/'";
		
		case TOK_LESS:
			return "'<'";
		case TOK_GREATER:
			return "'>'";
		case TOK_EQUALS:
			return "'=='";
		case TOK_NEQUALS:
			return "'!='";
		
		case TOK_OPEN_BRACKET:
			return "'('";
		case TOK_CLOSE_BRACKET:
			return "')'";
		
		case TOK_OPEN_CURL:
			return "'{'";
		case TOK_CLOSE_CURL:
			return "'}'";
		case TOK_COMMA:
			return "','";
		
		case TOK_COLON:
			return "':'";
		
		case TOK_PIPE:
			return "'|'";
		
		case TOK_EXCL:
			return "'!'";
		case TOK_OPT:
			return "'?'";
		
		case TOK_AND:
			return "'and'";
		case TOK_OR:
			return "'or'";
		case TOK_NOT:
			return "'not'";
			
		case TOK_STR_INTERP:
			return "string";
		
		case TOK_NULL_LITERAL:
			return "'null'";
	}
	
	assert(false);
	return "";
}

typedef struct lex_token {
	token_type type;
	const char *start, *end;
	
	const char *str;
	size_t strlen;
	
	bool newline;
	size_t line;
} lex_token;

typedef struct fn_scope {
	size_t block_scopes_base;
	size_t n_locals;
	bool upval_locals;
} fn_scope;

typedef struct block_scope {
	size_t locals_base;
} block_scope;

typedef struct local_var {
	const char *name;
	size_t name_len;
	
	uint64_t index;
	bool is_const;
} local_var;

typedef struct compile_ctx {
	const char *src, *src_name;
	const char *at, *end;
	size_t line_counter;
	
	lex_token next_token;
	lex_token pushed_token;
	
	fn_scope *fn_scopes;
	size_t fn_scopes_len, fn_scopes_cap;
	
	block_scope *block_scopes;
	size_t block_scopes_len, block_scopes_cap;
	
	local_var *locals;
	size_t locals_len, locals_cap;
	
	lex_token *token_stack;
	size_t token_stack_len, token_stack_cap;
	
	jmp_buf err_jmp;
	
	char *str_buff;
	size_t str_buff_len, str_buff_cap;
	
	bool lex_next_as_string;
} compile_ctx;

static char err_buff[512];
static size_t err_buff_len = 0;

static void err_buff_write(char c) {
	if(err_buff_len == sizeof(err_buff)) err_buff[sizeof(err_buff) - 1] = c;
	else err_buff[err_buff_len++] = c;
}

static void err_buff_write_s(const char *s) {
	while(*s != '\0') err_buff_write(*(s++));
}

static char *error_ctx_str(compile_ctx *ctx, const char *from, const char *to) {
	if(to == NULL) to = from;
	
	err_buff_len = 0;
	
	const char *at = ctx->src;
	size_t line = 1;
	
	const char *line_start = at;
	bool prev_was_newline = false;
	while(at < from && at < ctx->end) {
		if(prev_was_newline) {
			line_start = at;
			prev_was_newline = false;
			line++;
		}
		if(*at == '\n') {
			prev_was_newline = true;
		}
		at++;
	}
	
	err_buff_write_s(ctx->src_name);
	err_buff_write_s(", on line ");
	char line_str[16];
	snprintf(line_str, sizeof(line_str), "%zu\n", line);
	err_buff_write_s(line_str);
	
	size_t spaces = 0;
	size_t tabs = 0;
	
	for(at = line_start; at < ctx->end; at++) {
		if(*at == '\n') break;
		
		if(at < from) {
			if(*at == '\t') tabs++;
			else spaces++;
		}
		
		err_buff_write(*at);
	}
	err_buff_write('\n');
	while(tabs--) err_buff_write('\t');
	while(spaces--) err_buff_write(' ');
	
	size_t range_len = to - from;
	if(range_len == 0) range_len = 1;
	while(range_len--) err_buff_write('^');
	
	err_buff_write('\0');
	
	return err_buff;
}

static void throw_err(compile_ctx *ctx) {
	longjmp(ctx->err_jmp, 1);
}

static void compile_err(esh_state *esh, compile_ctx *ctx, const char *msg, const char *from, const char *to) {
	esh_err_printf(esh, "%s\n%s", msg, error_ctx_str(ctx, from, to));
	throw_err(ctx);
}

static void str_buff_append(esh_state *esh, compile_ctx *ctx, char c) {
	if(ctx->str_buff_len == ctx->str_buff_cap) {
		size_t new_cap = ctx->str_buff_cap * 3 / 2 + 1;
		char *new_buff = esh_realloc(esh, ctx->str_buff, sizeof(char) * new_cap);
		if(!new_buff) {
			esh_err_printf(esh, "Unable to grow compiler string buffer (out of memory?)");
			throw_err(ctx);
		}
		
		ctx->str_buff = new_buff;
		ctx->str_buff_cap = new_cap;
	}
	
	ctx->str_buff[ctx->str_buff_len++] = c;
}

// Lexer

static char popc(compile_ctx *ctx) {
	if(ctx->at == ctx->end) return '\0';
	
	char c = *ctx->at;
	ctx->at++;
	
	if(c == '\n') ctx->line_counter++;
	
	return c;
}

static char peekc(compile_ctx *ctx) {
	if(ctx->at == ctx->end) return '\0';
	
	return *ctx->at;
}

static void skip_whitespace(compile_ctx *ctx, bool *newline) {
	*newline = false;
	bool comment = false;
	while(true) {
		char c = peekc(ctx);
		if(c == ' ' || c == '\t' || c == '\r' || c == '\n') { /* NOTHING */ }
		else if(c == '#') comment = !comment;
		else if(!comment) break; 
		
		popc(ctx);
		
		if(c == '\n') {
			comment = false;
			*newline = true;
		}
	}
}

static bool is_word_char(char c) {
	switch(c) {
		case '/':
		case '.':
		case '_':
		case '+':
		case '-':
		case '*':
		case '=':
			return true;
		
		default:
			return isalnum(c);
	}
}

static bool kmatch(const char *a, const char *b, size_t len) {
	while(len > 0) {
		if(*a != *b) return false;
		a++;
		b++;
		len--;
	}
	
	return true;
}

static token_type match_keyword(const char *s, size_t len) {
	switch(len) {
		case 1:
			if(s[0] == '=') return TOK_ASSIGN;
			if(s[0] == '+') return TOK_ADD;
			if(s[0] == '-') return TOK_SUB;
			if(s[0] == '*') return TOK_MUL;
			if(s[0] == '/') return TOK_DIV;
			break;
		
		case 2:
			if(kmatch(s, "do", 2)) return TOK_DO;
			if(kmatch(s, "if", 2)) return TOK_IF;
			if(kmatch(s, "or", 2)) return TOK_OR;
			if(kmatch(s, "==", 2)) return TOK_EQUALS;
			break;
			
		case 3:
			if(kmatch(s, "end", 3)) return TOK_END;
			if(kmatch(s, "and", 3)) return TOK_AND;
			if(kmatch(s, "not", 3)) return TOK_NOT;
			break;
		
		case 4:
			if(kmatch(s, "with", 4)) return TOK_WITH;
			if(kmatch(s, "else", 4)) return TOK_ELSE;
			if(kmatch(s, "then", 4)) return TOK_THEN;
			if(kmatch(s, "null", 4)) return TOK_NULL_LITERAL;
			break;
		
		case 5:
			if(kmatch(s, "local", 5)) return TOK_LOCAL;
			if(kmatch(s, "const", 5)) return TOK_CONST;
			break;
		
		case 6:
			if(kmatch(s, "return", 6)) return TOK_RETURN;
			break;
		
		case 8:
			if(kmatch(s, "function", 8)) return TOK_FUNCTION;
			break;
	}
	
	return TOK_NULL;
}

static token_type match_char_tok(char c) {
	switch(c) {
		case '$':
			return TOK_SIGIL;
		
		case '(':
			return TOK_OPEN_BRACKET;
		case ')':
			return TOK_CLOSE_BRACKET;
		
		case '{':
			return TOK_OPEN_CURL;
		case '}':
			return TOK_CLOSE_CURL;
		case ',':
			return TOK_COMMA;
		
		case ':':
			return TOK_COLON;
		
		case '?':
			return TOK_OPT;
		
		case '|':
			return TOK_PIPE;
		
		default:
			return TOK_NULL;
	}
}

static void lex_str(esh_state *esh, compile_ctx *ctx, const char *start, char terminator, bool newline, bool allow_interp) {
	const char *str_start = ctx->at;

	bool prev_was_escape = false;
	
	bool interp = false;
	while(true) {
		char c = popc(ctx);
		if(c == '\0') {
			compile_err(esh, ctx, "Unterminated string constant; reached EOF before closing quote", ctx->at, NULL);
		}
		if(prev_was_escape) {
			prev_was_escape = false;
			continue;
		}
		
		if(c == '\\') prev_was_escape = true;
		else if(c == terminator) break;
		else if(allow_interp && c == '$') {
			interp = true;
			break;
		}
	}
	const char *str_end = ctx->at - 1;
	
	ctx->next_token = (lex_token) {
		.type = interp? TOK_STR_INTERP : TOK_WORD,
		.start = start,
		.end = ctx->at,
		.str = str_start,
		.strlen = str_end - str_start,
		.newline = newline,
		.line = ctx->line_counter
	};
}

static void lex_next_as_string(compile_ctx *ctx) {
	assert(!ctx->lex_next_as_string);
	assert(ctx->pushed_token.type == TOK_NULL);
	ctx->lex_next_as_string = true;
}

static void next_token(esh_state *esh, compile_ctx *ctx) {
	(void) esh;
	
	if(ctx->lex_next_as_string) {
		ctx->lex_next_as_string = false;
		lex_str(esh, ctx, ctx->at, '"', false, true);
		return;
	}
	
	bool newline;
	skip_whitespace(ctx, &newline);
	
	const char *at = ctx->at;	
	char c = popc(ctx);
	
	if(c == '\0') {
		ctx->next_token = (lex_token) {
			.type = TOK_EOF,
			.start = at, 
			.end = ctx->at,
			.newline = newline,
			.line = ctx->line_counter
		};
		return;
	}
	

	token_type char_tok = TOK_NULL;

	if(c == '!') {
		if(peekc(ctx) == '=') {
			popc(ctx);
			char_tok = TOK_NEQUALS;
		} else {
			char_tok = TOK_EXCL;
		}
	} else if(c == '<') {
		char_tok = TOK_LESS;
	} else if(c == '>') {
		char_tok = TOK_GREATER;
	} else char_tok = match_char_tok(c);
	
	if(char_tok) {
		const char *end = ctx->at;
		ctx->next_token = (lex_token) {
			.type = char_tok,
			.start = at, 
			.end = end,
			.newline = newline,
			.line = ctx->line_counter
		};
		return;
	}
	
	if(c == '"' || c == '\'') {
		lex_str(esh, ctx, at, c, newline, c == '"');
		return;
	}
	
	if(!is_word_char(c)) {
		compile_err(esh, ctx, "Unexepcted character", at, NULL);
	}
	
	while(is_word_char(peekc(ctx))) {
		popc(ctx);
	}
	
	size_t word_len = ctx->at - at;
	
	token_type type = match_keyword(at, word_len);
	if(type == TOK_NULL) type = TOK_WORD;
	
	ctx->next_token = (lex_token) {
		.type = type,
		.start = at,
		.end = ctx->at,
		.str = at,
		.strlen = word_len,
		.newline = newline,
		.line = ctx->line_counter
	};
}

static lex_token peek_token(esh_state *esh, compile_ctx *ctx) {
	if(ctx->pushed_token.type != TOK_NULL) {
		return ctx->pushed_token;
	}
	(void) esh;
	return ctx->next_token;
}

static lex_token pop_token(esh_state *esh, compile_ctx *ctx) {
	if(ctx->pushed_token.type != TOK_NULL) {
		lex_token tok = ctx->pushed_token;
		ctx->pushed_token.type = TOK_NULL;
		return tok;
	}
	
	lex_token tok = ctx->next_token;
	next_token(esh, ctx);
	
	return tok;
}

static void push_token(esh_state *esh, compile_ctx *ctx, lex_token tok) {
	(void) esh;
	assert(ctx->pushed_token.type == TOK_NULL);
	
	ctx->pushed_token = tok;
}

static lex_token expect_token(esh_state *esh, compile_ctx *ctx, token_type type, const char *msg) {
	lex_token tok = pop_token(esh, ctx);
	if(tok.type != type) {
		esh_err_printf(
			esh, 
			"Expected %s, found %s %s\n%s", 
			tok_name(type), 
			tok_name(tok.type), 
			msg, 
			error_ctx_str(ctx, tok.start, tok.end)
		);
		throw_err(ctx);
	}
	return tok;
}

static bool accept_token(esh_state *esh, compile_ctx *ctx, token_type type, lex_token *opt_tok) {
	if(peek_token(esh, ctx).type == type) {
		lex_token tok = pop_token(esh, ctx);
		if(opt_tok) *opt_tok = tok;
		return true;
	}
	
	return false;
}

static bool next_is_newline(esh_state *esh, compile_ctx *ctx) {
	return peek_token(esh, ctx).newline;
}

// Compiler/parser

/*
	PROGRAM = STATEMENT* EOF
	
	STATEMENT =
		'local' WORD (',' WORD)* '=' EXPR
		WORD MEMBERS_LIST? '=' EXPR
		WORD (',' WORD)* '=' EXPR
		(WORD | CALL)
		'return' EXPR
		'if' EXPR 'then' STATEMENT* ('else' 'if' EXPR 'then' STATEMENT*) ('else' STATEMENT*)? 'end'
		'function' WORD with word* ('do' STATEMENT* 'end') | ('(' EXPR ')')
	
	DECLARATION =
		'local' WORD '=' EXPR
	
	ASSIGNMENT =
		WORD MEMBERS_LIST? = EXPR
	
	EXPR =
		UNARY_EXPR (BINOP UNARY_EXPR)*
	
	UNARY_EXPR =
		'not'? (TERM | CALL)
	
	CALL =
		TERM ('!' | (ARG_LIST '!'?)) ('|' TERM ARG_LIST? '!'?)* // Command invocation or function call
	
	TERM =
		S_TERM MEMBERS_LIST?
		WORD
	
	ARG_LIST =
		TERM+
	
	MEMBERS_LIST =
		(':' (S_TERM | WORD))+
	
	S_TERM = # A term without an index that is not a word
		'$' WORD
		'(' EXPR ')'
		'with' WORD* ('do' STATEMENT* 'end') | ('(' EXPR ')')
		'{' (OBJ_ENTRY (',' OBJ_ENTRY)*)? '}'
		'null'
	
	OBJ_ENTRY =
		EXPR '=' EXPR
		EXPR
*/
// IMPL FOR METHODS & INDEXING
/*
	$x:$y:$z ->
	push $x
	push $y
	push $z
	index 2
	
	$x:$y:$z $w ->
	push $x
	push $y
	push $z
	index_and_dup_obj 2
	push $w
	call 2
	
	Both can be compiled the same at the start; deciding between doing an indexing vs a method call can be delayed until
	all index terms have been parsed
*/

static void token_stack_push(esh_state *esh, compile_ctx *ctx, lex_token token) {
	if(ctx->token_stack_len == ctx->token_stack_cap) {
		size_t new_cap = ctx->token_stack_cap * 3 / 2 + 1;
		lex_token *new_buff = esh_realloc(esh, ctx->token_stack, sizeof(lex_token) * new_cap);
		if(new_buff == NULL) {
			esh_err_printf(esh, "Unable to reserve space on block scope stack (out of memory?)");
			throw_err(ctx);
		}
		
		ctx->token_stack_cap = new_cap;
		ctx->token_stack = new_buff;
	}
	
	ctx->token_stack[ctx->token_stack_len++] = token;
}

static lex_token *token_stack_pop(esh_state *esh, compile_ctx *ctx, size_t n) {
	(void) esh;
	assert(ctx->token_stack_len >= n);
	ctx->token_stack_len -= n;
	return &ctx->token_stack[ctx->token_stack_len];
}

static void new_block_scope(esh_state *esh, compile_ctx *ctx) {
	if(ctx->block_scopes_len == ctx->block_scopes_cap) {
		size_t new_cap = ctx->block_scopes_cap * 3 / 2 + 1;
		block_scope *new_buff = esh_realloc(esh, ctx->block_scopes, sizeof(block_scope) * new_cap);
		if(new_buff == NULL) {
			esh_err_printf(esh, "Unable to reserve space on block scope stack (out of memory?)");
			throw_err(ctx);
		}
		
		ctx->block_scopes_cap = new_cap;
		ctx->block_scopes = new_buff;
	}
	
	ctx->block_scopes[ctx->block_scopes_len++] = (block_scope) { .locals_base = ctx->locals_len };
}

static void new_fn_scope(esh_state *esh, compile_ctx *ctx) {
	if(ctx->fn_scopes_len == ctx->fn_scopes_cap) {
		size_t new_cap = ctx->fn_scopes_cap * 3 / 2 + 1;
		fn_scope *new_buff = esh_realloc(esh, ctx->fn_scopes, sizeof(fn_scope) * new_cap);
		if(new_buff == NULL) {
			esh_err_printf(esh, "Unable to reserve space on function scope stack (out of memory?)");
			throw_err(ctx);
		}
		
		ctx->fn_scopes_cap = new_cap;
		ctx->fn_scopes = new_buff;
	}
	
	ctx->fn_scopes[ctx->fn_scopes_len++] = (fn_scope) { .n_locals = 0, .upval_locals = false, .block_scopes_base = ctx->block_scopes_len };
	new_block_scope(esh, ctx);
}

static bool index_local(compile_ctx *ctx, const char *name, size_t name_len, bool current_block_only, size_t *out_index, size_t *out_uplevel, bool *is_const) {
	*out_uplevel = 0;
	size_t locals_top = ctx->locals_len;
	size_t blocks_top = ctx->block_scopes_len;
	for(size_t i = ctx->fn_scopes_len; i > 0; i--) {
		fn_scope *fn = &ctx->fn_scopes[i - 1];
		for(size_t j = blocks_top; j > fn->block_scopes_base; j--) {
			block_scope *block = &ctx->block_scopes[j - 1];
			for(size_t k = block->locals_base; k < locals_top; k++) {
				local_var *local = &ctx->locals[k];
				if(local->name_len == name_len && memcmp(local->name, name, sizeof(char) * name_len) == 0) {
					*out_index = local->index;
					*is_const = local->is_const;
					return true;
				}
			}
			locals_top = block->locals_base;
			
			if(current_block_only) return false;
		}
		blocks_top = fn->block_scopes_base;
		(*out_uplevel)++;
	}
	
	return false;
}

static bool find_local(compile_ctx *ctx, lex_token word, size_t *out_index, size_t *out_uplevel, bool *is_const) {
	assert(word.type == TOK_WORD);
	return index_local(ctx, word.str, word.strlen, false, out_index, out_uplevel, is_const);
}

static size_t new_local(esh_state *esh, compile_ctx *ctx, lex_token word, bool is_const) {
	assert(word.type == TOK_WORD);
	
	size_t a, b;
	bool c;
	if(index_local(ctx, word.str, word.strlen, true, &a, &b, &c)) compile_err(esh, ctx, "Redeclaration of local variable", word.start, word.end);
	
	assert(ctx->fn_scopes_len != 0);
	if(ctx->locals_len == ctx->locals_cap) {
		size_t new_cap = ctx->locals_cap * 3 / 2 + 1;
		local_var *new_buff = esh_realloc(esh, ctx->locals, sizeof(local_var) * new_cap);
		if(new_buff == NULL) {
			esh_err_printf(esh, "Unable to reserve space on locals stack (out of memory?)");
			throw_err(ctx);
		}
		
		ctx->locals_cap = new_cap;
		ctx->locals = new_buff;
	}
	
	size_t index = ctx->fn_scopes[ctx->fn_scopes_len - 1].n_locals++;
	ctx->locals[ctx->locals_len++] = (local_var) { .name = word.str, .name_len = word.strlen, .index = index, .is_const = is_const };
	return index;
}

static void upval_locals(compile_ctx *ctx, size_t n) {
	assert(n + 1 <= ctx->fn_scopes_len);
	for(size_t i = 0; i < n; i++) {
		ctx->fn_scopes[ctx->fn_scopes_len - 2 - i].upval_locals = true;
	}
}

static void leave_fn_scope(compile_ctx *ctx, size_t *n_locals, bool *upval_locals) {
	assert(ctx->fn_scopes_len != 0);
	assert(ctx->block_scopes_len != 0);
	
	fn_scope scope = ctx->fn_scopes[--ctx->fn_scopes_len];
	
	assert(scope.block_scopes_base < ctx->block_scopes_len); // A block scope is always pushed after a new fn scope is created
	block_scope base_block = ctx->block_scopes[scope.block_scopes_base];
	
	ctx->block_scopes_len = scope.block_scopes_base;
	ctx->locals_len = base_block.locals_base;
	
	*n_locals = scope.n_locals;
	*upval_locals = scope.upval_locals;
}

static void leave_block_scope(compile_ctx *ctx) {
	assert(ctx->fn_scopes_len != 0);
	assert(ctx->block_scopes_len != 0);
	
	fn_scope *scope = &ctx->fn_scopes[ctx->fn_scopes_len - 1];
	assert(ctx->block_scopes_len > scope->block_scopes_base + 1);
	
	ctx->block_scopes_len--;
}

static bool is_start_of_term(token_type tok) {
	switch(tok) {
		case TOK_WORD:
		case TOK_SIGIL:
		case TOK_WITH:
		case TOK_OPEN_BRACKET:
		case TOK_CONST:
		case TOK_OPEN_CURL:
		case TOK_STR_INTERP:
		case TOK_NULL_LITERAL:
			return true;
		
		default:
			return false;
	}
}

static uint64_t add_str_imm(esh_state *esh, compile_ctx *ctx, lex_token word) {
	assert(word.type == TOK_WORD || word.type == TOK_STR_INTERP);
	
	ctx->str_buff_len = 0;
	
	bool prev_was_escape = false;
	for(size_t i = 0; i < word.strlen; i++) {
		char c = word.str[i];
		if(prev_was_escape) {
			switch(c) {
				case 'n':
					str_buff_append(esh, ctx, '\n');
					break;
				case 'r':
					str_buff_append(esh, ctx, '\r');
					break;
				case 't':
					str_buff_append(esh, ctx, '\t');
					break;
				case '0':
					str_buff_append(esh, ctx, '\0');
					break;
					
				case '\'':
				case '"':
				case '\\':
				case '$':
					str_buff_append(esh, ctx, c);
					break;
				
				default:
					compile_err(esh, ctx, "Unknown escape character in string", word.start, word.end);
					break;
			}
			prev_was_escape = false;
		} else {
			if(c == '\\') prev_was_escape = true;
			else str_buff_append(esh, ctx, c);
		}
	}
	
	if(esh_new_string(esh, ctx->str_buff, ctx->str_buff_len)) throw_err(ctx);
	uint64_t ref;
	if(esh_fn_add_imm(esh, &ref)) throw_err(ctx);
	
	return ref;
}

static void compile_word(esh_state *esh, compile_ctx *ctx, lex_token word) {
	uint64_t ref = add_str_imm(esh, ctx, word);
	if(esh_fn_append_instr(esh, ESH_INSTR_IMM, ref, 0)) throw_err(ctx);
}

static bool compile_statement(esh_state *esh, compile_ctx *ctx, bool top_level);
static void compile_expression(esh_state *esh, compile_ctx *ctx);

// Returns true if the variable is local
static bool compile_local_var_load(esh_state *esh, compile_ctx *ctx, lex_token word) {
	size_t local_index, local_uplevel;
	bool ignore;
	if(find_local(ctx, word, &local_index, &local_uplevel, &ignore)) {
		upval_locals(ctx, local_uplevel);
		if(esh_fn_append_instr(esh, ESH_INSTR_LOAD, local_index, local_uplevel)) throw_err(ctx);
		return true;
	}
	return false;
}

static void compile_var_load(esh_state *esh, compile_ctx *ctx, lex_token word) {
	if(!compile_local_var_load(esh, ctx, word)) {
		uint64_t imm = add_str_imm(esh, ctx, word);
		if(esh_fn_append_instr(esh, ESH_INSTR_LOAD_G, imm, 0)) throw_err(ctx);
	}
}

static void compile_function(esh_state *esh, compile_ctx *ctx, lex_token *opt_name) {
	const char *name = NULL;
	size_t name_len = 0;
	if(opt_name) {
		assert(opt_name->type == TOK_WORD);
		name = opt_name->str;
		name_len = opt_name->strlen;
	}
	
	if(esh_new_fn(esh, name, name_len)) throw_err(ctx);
	new_fn_scope(esh, ctx);
	lex_token arg;
	size_t n_args = 0, opt_args = 0;
	while(accept_token(esh, ctx, TOK_WORD, &arg)) {
		if(accept_token(esh, ctx, TOK_OPT, NULL)) opt_args++;
		else {
			if(opt_args != 0) compile_err(esh, ctx, "Cannot have a non-optional argument following optional arguments", arg.start, arg.end);
			n_args++;
		}
		new_local(esh, ctx, arg, false);
	}
	
	if(accept_token(esh, ctx, TOK_OPEN_BRACKET, NULL)) {
		compile_expression(esh, ctx);
		expect_token(esh, ctx, TOK_CLOSE_BRACKET, "following function expression");
		if(esh_fn_append_instr(esh, ESH_INSTR_RET, 1, 0)) throw_err(ctx);
	} else {
		expect_token(esh, ctx, TOK_DO, "following arguments");
		while(!accept_token(esh, ctx, TOK_END, NULL)) {
			bool value = compile_statement(esh, ctx, false);
			if(value) if(esh_fn_append_instr(esh, ESH_INSTR_POP, 0, 0)) throw_err(ctx);
		}
		
		if(esh_fn_append_instr(esh, ESH_INSTR_PUSH_NULL, 0, 0)) throw_err(ctx); // Add implict "return NULL" at the end
		if(esh_fn_append_instr(esh, ESH_INSTR_RET, 1, 0)) throw_err(ctx);
	}
	
	size_t n_locals;
	bool upval_locals;
	leave_fn_scope(ctx, &n_locals, &upval_locals);
	if(esh_fn_finalize(esh, n_args, opt_args, n_locals, upval_locals, false)) throw_err(ctx);
	
	uint64_t imm;
	if(esh_fn_add_imm(esh, &imm)) throw_err(ctx);
	
	if(esh_fn_append_instr(esh, ESH_INSTR_CLOSURE, imm, 0)) throw_err(ctx);
}

static void compile_s_term(esh_state *esh, compile_ctx *ctx) {
	lex_token token = pop_token(esh, ctx);
	
	if(esh_fn_line_directive(esh, token.line)) throw_err(ctx);
	
	switch(token.type) {
		case TOK_SIGIL: {
			lex_token word = expect_token(esh, ctx, TOK_WORD, "following sigil");
			compile_var_load(esh, ctx, word);
		} break;
		
		case TOK_STR_INTERP: {
			size_t n = 0;
			do {
				compile_word(esh, ctx, token);
				
				if(peek_token(esh, ctx).type == TOK_WORD) {
					lex_next_as_string(ctx);
					lex_token word = pop_token(esh, ctx);
					compile_var_load(esh, ctx, word);
				} else {
					expect_token(esh, ctx, TOK_OPEN_BRACKET, "following string interpolation sigil");
					compile_expression(esh, ctx);
					lex_next_as_string(ctx);
					expect_token(esh, ctx, TOK_CLOSE_BRACKET, "following expression");
				}
				n += 2;
			} while(accept_token(esh, ctx, TOK_STR_INTERP, &token));
			
			compile_word(esh, ctx, expect_token(esh, ctx, TOK_WORD, "following string interpolation terms"));
			n++;
			
			if(esh_fn_append_instr(esh, ESH_INSTR_CONCAT, n, 0)) throw_err(ctx);
		} break;
		
		case TOK_WITH: {
			compile_function(esh, ctx, NULL);	
		} break;
		
		case TOK_OPEN_BRACKET: {
			compile_expression(esh, ctx);
			expect_token(esh, ctx, TOK_CLOSE_BRACKET, "following expression");
		} break;
		
		case TOK_CONST:
		case TOK_OPEN_CURL: {
			bool is_const = token.type == TOK_CONST;
			if(is_const) expect_token(esh, ctx, TOK_OPEN_CURL, "following 'const'");
			
			size_t n = 0;
			long long index_counter = 0;
			while(!accept_token(esh, ctx, TOK_CLOSE_CURL, NULL)) {
				compile_expression(esh, ctx);
				if(accept_token(esh, ctx, TOK_ASSIGN, NULL)) {
					compile_expression(esh, ctx);
				} else {
					if(esh_push_int(esh, index_counter)) throw_err(ctx);
					uint64_t ref;
					if(esh_fn_add_imm(esh, &ref)) throw_err(ctx);
					if(esh_fn_append_instr(esh, ESH_INSTR_IMM, ref, 0)) throw_err(ctx);
					if(esh_fn_append_instr(esh, ESH_INSTR_SWAP, 0, 0)) throw_err(ctx);
					
					index_counter++;
				}
				n++;
				
				if(!accept_token(esh, ctx, TOK_COMMA, NULL)) {
					expect_token(esh, ctx, TOK_CLOSE_CURL, "following object keys and values");
					break;
				}
			}
			
			if(esh_fn_append_instr(esh, ESH_INSTR_NEW_OBJ, n, 0)) throw_err(ctx);
			if(is_const) if(esh_fn_append_instr(esh, ESH_INSTR_MAKE_CONST, 0, 0)) throw_err(ctx);
		} break;
		
		case TOK_NULL_LITERAL: {
			if(esh_fn_append_instr(esh, ESH_INSTR_PUSH_NULL, 0, 0)) throw_err(ctx);
		} break;
		
		default:
			compile_err(esh, ctx, "Unexpected token. Expected term", token.start, token.end);
	}
}

static void compile_term(esh_state *esh, compile_ctx *ctx) {
	lex_token word;
	if(accept_token(esh, ctx, TOK_WORD, &word)) {
		if(esh_fn_line_directive(esh, word.line)) throw_err(ctx);
		compile_word(esh, ctx, word);
	} else {
		compile_s_term(esh, ctx);
		while(accept_token(esh, ctx, TOK_COLON, NULL)) {
			lex_token word;
			if(accept_token(esh, ctx, TOK_WORD, &word)) compile_word(esh, ctx, word);
			else compile_s_term(esh, ctx);
			
			if(esh_fn_append_instr(esh, ESH_INSTR_INDEX, 0, 0)) throw_err(ctx);
		}
	}
}

static size_t compile_arg_list(esh_state *esh, compile_ctx *ctx) {
	size_t n_args = 0;
	while(is_start_of_term(peek_token(esh, ctx).type) && !next_is_newline(esh, ctx)) {
		compile_term(esh, ctx);
		n_args++;
	}
	
	return n_args;
}

static void compile_call_expression(esh_state *esh, compile_ctx *ctx, bool is_statement) {
	lex_token word;
	if(accept_token(esh, ctx, TOK_WORD, &word)) {
		bool is_local = false;
		if(compile_local_var_load(esh, ctx, word)) {
			is_local = true;
		} else {
			compile_word(esh, ctx, word);
		}
		
		size_t n = compile_arg_list(esh, ctx);
		bool excl = accept_token(esh, ctx, TOK_EXCL, NULL);
		
		bool next_is_pipe = peek_token(esh, ctx).type == TOK_PIPE;
		
		if(excl || n != 0 || is_statement) { // Command invocation (e.g WORD TERM+)
			if(is_local) {
				if(esh_fn_append_instr(esh, ESH_INSTR_CALL, n, 0)) throw_err(ctx);
			} else {
				if(esh_fn_append_instr(esh, ESH_INSTR_CMD, n, (is_statement && !next_is_pipe)? 0 : 1)) throw_err(ctx);
			}
		}
	} else {
		compile_term(esh, ctx);
		size_t n = compile_arg_list(esh, ctx);
		if(accept_token(esh, ctx, TOK_EXCL, NULL) || n != 0) {
			if(esh_fn_append_instr(esh, ESH_INSTR_CALL, n, 0)) throw_err(ctx);
		}
	}
	
	if(accept_token(esh, ctx, TOK_OPT, NULL)) if(esh_fn_append_instr(esh, ESH_INSTR_PROP, 0, 0)) throw_err(ctx);
	
	while(accept_token(esh, ctx, TOK_PIPE, NULL)) {
		lex_token word;
		if(accept_token(esh, ctx, TOK_WORD, &word)) {
			bool is_local = false;
			if(compile_local_var_load(esh, ctx, word)) {
				is_local = true;
			} else {
				compile_word(esh, ctx, word);
			}
			
			if(esh_fn_append_instr(esh, ESH_INSTR_SWAP, 0, 0)) throw_err(ctx);
			
			size_t n = compile_arg_list(esh, ctx);
			accept_token(esh, ctx, TOK_EXCL, NULL);
			
			bool next_is_pipe = peek_token(esh, ctx).type == TOK_PIPE;
			
			if(is_local) {
				if(esh_fn_append_instr(esh, ESH_INSTR_CALL, n, 0)) throw_err(ctx);
			} else {
				if(esh_fn_append_instr(esh, ESH_INSTR_CMD, n + 1, (is_statement && !next_is_pipe)? (2 | 0) : (2 | 1))) throw_err(ctx);
			}
		} else {
			compile_term(esh, ctx);
			if(esh_fn_append_instr(esh, ESH_INSTR_SWAP, 0, 0)) throw_err(ctx);
			size_t n = compile_arg_list(esh, ctx);
			accept_token(esh, ctx, TOK_EXCL, NULL);
			if(esh_fn_append_instr(esh, ESH_INSTR_CALL, n + 1, 0)) throw_err(ctx);
		}
		
		if(accept_token(esh, ctx, TOK_OPT, NULL)) if(esh_fn_append_instr(esh, ESH_INSTR_PROP, 0, 0)) throw_err(ctx);
	}
}

static void compile_unary_expression(esh_state *esh, compile_ctx *ctx) {
	bool not = accept_token(esh, ctx, TOK_NOT, NULL);
	compile_call_expression(esh, ctx, false);
	
	if(not) if(esh_fn_append_instr(esh, ESH_INSTR_NOT, 0, 0)) throw_err(ctx);
}

static void compile_mul_expression(esh_state *esh, compile_ctx *ctx) {
	compile_unary_expression(esh, ctx);
	
	lex_token op;
	while(
		accept_token(esh, ctx, TOK_MUL, &op) ||
		accept_token(esh, ctx, TOK_DIV, &op)
	) {
		if(esh_fn_line_directive(esh, op.line)) throw_err(ctx);
		compile_unary_expression(esh, ctx);
		if(esh_fn_append_instr(esh, op.type == TOK_MUL? ESH_INSTR_MUL : ESH_INSTR_DIV, 0, 0)) throw_err(ctx);
	}
}

static void compile_add_expression(esh_state *esh, compile_ctx *ctx) {
	compile_mul_expression(esh, ctx);
	lex_token op;
	while(
		accept_token(esh, ctx, TOK_ADD, &op) ||
		accept_token(esh, ctx, TOK_SUB, &op)
	) {
		if(esh_fn_line_directive(esh, op.line)) throw_err(ctx);
		
		compile_mul_expression(esh, ctx);
		if(esh_fn_append_instr(esh, op.type == TOK_ADD? ESH_INSTR_ADD : ESH_INSTR_SUB, 0, 0)) throw_err(ctx);
	}
}

static void compile_cmp_expression(esh_state *esh, compile_ctx *ctx) {
	compile_add_expression(esh, ctx);
	lex_token op;
	while(
		accept_token(esh, ctx, TOK_EQUALS, &op) ||
		accept_token(esh, ctx, TOK_NEQUALS, &op) ||
		accept_token(esh, ctx, TOK_LESS, &op) ||
		accept_token(esh, ctx, TOK_GREATER, &op)
	) {
		if(esh_fn_line_directive(esh, op.line)) throw_err(ctx);
		
		compile_add_expression(esh, ctx);
		
		esh_opcode opcode;
		switch(op.type) {
			case TOK_EQUALS:
				opcode = ESH_INSTR_EQ;
				break;
			case TOK_NEQUALS:
				opcode = ESH_INSTR_NEQ;
				break;
			case TOK_LESS:
				opcode = ESH_INSTR_LESS;
				break;
			case TOK_GREATER:
				opcode = ESH_INSTR_GREATER;
				break;
			
			
			default:
				assert(false);
		}
		
		if(esh_fn_append_instr(esh, opcode, 0, 0)) throw_err(ctx);
	}
}

static void compile_and_expression(esh_state *esh, compile_ctx *ctx) {
	compile_cmp_expression(esh, ctx);
	lex_token op;
	while(
		accept_token(esh, ctx, TOK_AND, &op) ||
		accept_token(esh, ctx, TOK_OR, &op)
	) {
		if(esh_fn_line_directive(esh, op.line)) throw_err(ctx);
		
		uint64_t label;
		if(esh_fn_new_label(esh, &label)) throw_err(ctx);
		if(esh_fn_append_instr(esh, ESH_INSTR_DUP, 0, 0)) throw_err(ctx);
		if(esh_fn_append_instr(esh, op.type == TOK_AND? ESH_INSTR_JMP_IFN : ESH_INSTR_JMP_IF, label, 0)) throw_err(ctx);
		if(esh_fn_append_instr(esh, ESH_INSTR_POP, 0, 0)) throw_err(ctx);
		compile_cmp_expression(esh, ctx);
		
		if(esh_fn_put_label(esh, label)) throw_err(ctx);
	}
}

static void compile_expression(esh_state *esh, compile_ctx *ctx) {
	compile_and_expression(esh, ctx);
}

// Returns true if the statement leaves a value on the stack
static bool compile_statement(esh_state *esh, compile_ctx *ctx, bool top_level) {
	(void) top_level;
	lex_token tok = peek_token(esh, ctx);
	if(esh_fn_line_directive(esh, tok.line)) throw_err(ctx);
	
	switch(tok.type) {
		case TOK_WORD: {
			pop_token(esh, ctx);
			
			if(peek_token(esh, ctx).type == TOK_COLON) {
				compile_var_load(esh, ctx, tok);
				
				bool first = true;
				while(accept_token(esh, ctx, TOK_COLON, NULL)) {
					if(!first) if(esh_fn_append_instr(esh, ESH_INSTR_INDEX, 0, 0)) throw_err(ctx);
					first = false;
					
					lex_token word;
					if(accept_token(esh, ctx, TOK_WORD, &word)) compile_word(esh, ctx, word);
					else compile_s_term(esh, ctx);
				}
				
				expect_token(esh, ctx, TOK_ASSIGN, "following assign indices");
				
				compile_expression(esh, ctx);
				
				if(esh_fn_append_instr(esh, ESH_INSTR_SET_INDEX, 0, 0)) throw_err(ctx);
				return false;
			} else if(peek_token(esh, ctx).type == TOK_ASSIGN || peek_token(esh, ctx).type == TOK_COMMA) {
				size_t n = 1;
				token_stack_push(esh, ctx, tok);
				while(accept_token(esh, ctx, TOK_COMMA, NULL)) {
					lex_token word = expect_token(esh, ctx, TOK_WORD, "following comma");
					token_stack_push(esh, ctx, word);
					n++;
				}
				
				expect_token(esh, ctx, TOK_ASSIGN, "following variable name(s)");
				compile_expression(esh, ctx);
				
				if(n != 1) if(esh_fn_append_instr(esh, ESH_INSTR_UNPACK, n, 0)) throw_err(ctx);
				
				lex_token *vars = token_stack_pop(esh, ctx, n);
				for(size_t i = n; i > 0; i--) {
					lex_token var = vars[i - 1];
					size_t local_index, local_uplevel;
					bool is_const;
					if(find_local(ctx, var, &local_index, &local_uplevel, &is_const)) {
						if(is_const) compile_err(esh, ctx, "Attempting to redefine constant variable", var.start, var.end);
						upval_locals(ctx, local_uplevel);
						if(esh_fn_append_instr(esh, ESH_INSTR_STORE, local_index, local_uplevel)) throw_err(ctx);
					} else {
						uint64_t ref = add_str_imm(esh, ctx, var);
						if(esh_fn_append_instr(esh, ESH_INSTR_STORE_G, ref, 0)) throw_err(ctx);
					}
				}
				return false;
			} else {
				push_token(esh, ctx, tok);
				compile_call_expression(esh, ctx, true);
				return true;
			}
		} break;
		
		case TOK_LOCAL: {
			pop_token(esh, ctx);
			if(top_level) {
				compile_err(esh, ctx, "Cannot declare local variables at top level", tok.start, tok.end);
			}
			
			bool is_const = accept_token(esh, ctx, TOK_CONST, NULL);
			
			size_t n = 0;
			do {
				lex_token word = expect_token(esh, ctx, TOK_WORD, "following 'local'");
				token_stack_push(esh, ctx, word);
				n++;
			} while(accept_token(esh, ctx, TOK_COMMA, NULL));
			
			expect_token(esh, ctx, TOK_ASSIGN, "following variable name(s)");
			
			compile_expression(esh, ctx);
			
			assert(n > 0);
			if(n != 1) if(esh_fn_append_instr(esh, ESH_INSTR_UNPACK, n, 0)) throw_err(ctx);
			
			lex_token *vars = token_stack_pop(esh, ctx, n);
			for(size_t i = n; i > 0; i--) {
				size_t index = new_local(esh, ctx, vars[i - 1], is_const);
				if(esh_fn_append_instr(esh, ESH_INSTR_STORE, index, 0)) throw_err(ctx);
			}
			return false;
		} break;
		
		case TOK_FUNCTION: {
			pop_token(esh, ctx);
			lex_token fn_name = expect_token(esh, ctx, TOK_WORD, "following 'function'");
			
			size_t local_index;
			if(!top_level) { // In case of a local declaration; add the variable first; so that the function can be self-recursive
				local_index = new_local(esh, ctx, fn_name, true);
			}
			
			expect_token(esh, ctx, TOK_WITH, "following function name");
			compile_function(esh, ctx, &fn_name);
			
			if(top_level) {
				uint64_t ref = add_str_imm(esh, ctx, fn_name);
				if(esh_fn_append_instr(esh, ESH_INSTR_STORE_G, ref, 0)) throw_err(ctx);
			} else {
				if(esh_fn_append_instr(esh, ESH_INSTR_STORE, local_index, 0)) throw_err(ctx);
			}
			return false;
		} break;
		
		case TOK_IF: {
			pop_token(esh, ctx);
			
			compile_expression(esh, ctx);
			
			expect_token(esh, ctx, TOK_THEN, "following 'if' condition");
			
			uint64_t end_label, next_label;
			if(esh_fn_new_label(esh, &end_label)) throw_err(ctx);
			if(esh_fn_new_label(esh, &next_label)) throw_err(ctx);
			
			if(esh_fn_append_instr(esh, ESH_INSTR_JMP_IFN, next_label, 0)) throw_err(ctx);
			
			new_block_scope(esh, ctx);
			
			bool next_must_be_end = false;
			while(true) {
				if(accept_token(esh, ctx, TOK_END, NULL)) {
					leave_block_scope(ctx);
					if(esh_fn_put_label(esh, next_label)) throw_err(ctx);
					break;
				}
				
				lex_token else_tok;
				if(accept_token(esh, ctx, TOK_ELSE, &else_tok)) {
					leave_block_scope(ctx);
					if(esh_fn_append_instr(esh, ESH_INSTR_JMP, end_label, 0)) throw_err(ctx);
					
					if(next_must_be_end) {
						compile_err(esh, ctx, "Cannot have multiple 'else' sections in a single 'if' statement",  else_tok.start, else_tok.end);
					}
					
					if(esh_fn_put_label(esh, next_label)) throw_err(ctx);
					if(esh_fn_new_label(esh, &next_label)) throw_err(ctx);
					
					if(!next_is_newline(esh, ctx) && accept_token(esh, ctx, TOK_IF, NULL)) {
						compile_expression(esh, ctx);
						expect_token(esh, ctx, TOK_THEN, "following 'else if' condition");
						new_block_scope(esh, ctx);
						if(esh_fn_append_instr(esh, ESH_INSTR_JMP_IFN, next_label, 0)) throw_err(ctx);
					} else {
						new_block_scope(esh, ctx);
						next_must_be_end = true;
					}
				}
				
				bool value = compile_statement(esh, ctx, top_level);
				if(value) if(esh_fn_append_instr(esh, ESH_INSTR_POP, 0, 0)) throw_err(ctx);
			}
			
			if(esh_fn_put_label(esh, end_label)) throw_err(ctx);
			
			return false;
		} break;
		
		case TOK_RETURN: {
			pop_token(esh, ctx);
			
			size_t n = 0;
			do {
				compile_expression(esh, ctx);
				n++;
			} while(accept_token(esh, ctx, TOK_COMMA, NULL));
			
			if(esh_fn_append_instr(esh, ESH_INSTR_RET, n, 0)) throw_err(ctx);
			
			return false;
		} break;
		
		default:
			compile_call_expression(esh, ctx, true);
			return true;
	}
}

static void compile_program(esh_state *esh, compile_ctx *ctx, bool interactive) {
	// A new function is pushed onto the stack
	if(esh_new_fn(esh, ctx->src_name, strlen(ctx->src_name))) throw_err(ctx);;
	
	new_fn_scope(esh, ctx);
	
	bool value = false;
	while(!accept_token(esh, ctx, TOK_EOF, NULL)) {
		if(value) if(esh_fn_append_instr(esh, ESH_INSTR_POP, 0, 0)) throw_err(ctx);
		value = compile_statement(esh, ctx, true);
	}
	expect_token(esh, ctx, TOK_EOF, "folowing end of statements");
	
	if(!value) {
		if(esh_fn_append_instr(esh, ESH_INSTR_PUSH_NULL, 0, 0)) throw_err(ctx);
	} else if(!interactive) { // If interactive, return the value produced by the last statement; otherwise return null
		if(esh_fn_append_instr(esh, ESH_INSTR_POP, 0, 0)) throw_err(ctx);	
		if(esh_fn_append_instr(esh, ESH_INSTR_PUSH_NULL, 0, 0)) throw_err(ctx);
	}

	if(esh_fn_append_instr(esh, ESH_INSTR_RET, 1, 0)) throw_err(ctx);
	
	bool upval_locals;
	size_t n_locals;
	leave_fn_scope(ctx, &n_locals, &upval_locals);
	assert(n_locals == 0 && !upval_locals);
	
	esh_fn_finalize(esh, 0, 0, 0, false, true);
}

int esh_compile_src(esh_state *esh, const char *name, const char *src, size_t len, bool interactive_mode) {
	compile_ctx ctx = {
		.src = src,
		.src_name = name,
		
		.at = src,
		.end = src + len,
		
		.line_counter = 1,
		
		.token_stack = NULL,
		.token_stack_len = 0,
		.token_stack_cap = 0,
		
		.fn_scopes = NULL,
		.fn_scopes_len = 0,
		.fn_scopes_cap = 0,
		
		.block_scopes = NULL,
		.block_scopes_len = 0,
		.block_scopes_cap = 0,
		
		.locals = NULL,
		.locals_len = 0,
		.locals_cap = 0,
		
		.pushed_token = { .type = TOK_NULL },
		
		.str_buff = NULL,
		.str_buff_len = 0,
		.str_buff_cap = 0,
		
		.lex_next_as_string = false
	};
	
	esh_save_stack(esh);
	
	int err = 0;
	if(setjmp(ctx.err_jmp) == 0) {
		next_token(esh, &ctx);
		compile_program(esh, &ctx, interactive_mode);
	} else {
		err = 1;
		esh_restore_stack(esh);
	}
	
	esh_free(esh, ctx.token_stack);
	esh_free(esh, ctx.fn_scopes);
	esh_free(esh, ctx.block_scopes);
	esh_free(esh, ctx.locals);
	esh_free(esh, ctx.str_buff);
	
	return err;
}
