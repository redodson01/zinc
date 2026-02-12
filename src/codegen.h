#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdio.h>
#include "ast.h"
#include "semantic.h"

/* Scope variable tracking for ARC */
typedef struct CGScopeVar {
    char *name;
    char *type_name;   /* "ZnString" for strings */
    struct CGScopeVar *next;
} CGScopeVar;

/* Codegen scope for ARC release tracking */
typedef struct CGScope {
    CGScopeVar *ref_vars;
    int is_loop;
    struct CGScope *parent;
} CGScope;

/* Code generation context */
typedef struct {
    FILE *c_file;
    FILE *h_file;
    SemanticContext *sem_ctx;
    int indent_level;
    int temp_counter;
    int string_counter;          /* Number of string literals collected */
    const char *output_base;
    int loop_expr_temp;          /* >= 0 when loop is in expression context */
    TypeKind loop_expr_type;     /* type of current loop expression result */
    CGScope *scope;
    const char *source_file;     /* Source file name for #line directives */
} CodegenContext;

/* Initialize/cleanup context */
CodegenContext *codegen_init(FILE *c_file, FILE *h_file, SemanticContext *sem_ctx, const char *output_base, const char *source_file);
void codegen_free(CodegenContext *ctx);

/* Generate code from AST */
void generate(CodegenContext *ctx, ASTNode *root);

/* --- Shared emit helpers (codegen.c) --- */

void cg_emit(CodegenContext *ctx, const char *s);
void cg_emitf(CodegenContext *ctx, const char *fmt, ...);
void cg_emit_line(CodegenContext *ctx, int line);
void cg_emit_indent(CodegenContext *ctx);
void cg_emit_header(CodegenContext *ctx, const char *s);

/* --- Type helpers (codegen.c) --- */

const char *type_to_c(TypeKind t);
int is_ref_type(TypeKind t);
int expr_is_string(ASTNode *expr);

/* --- ARC scope management (codegen.c) --- */

void cg_push_scope(CodegenContext *ctx, int is_loop);
void cg_pop_scope(CodegenContext *ctx);
void cg_scope_add_ref(CodegenContext *ctx, const char *name, const char *type_name);
void emit_scope_releases(CodegenContext *ctx);
void emit_all_scope_releases(CodegenContext *ctx);
void emit_retain_call(CodegenContext *ctx, const char *expr, Type *type);
void emit_release_call(CodegenContext *ctx, const char *expr, Type *type);
void emit_retain_open(CodegenContext *ctx, Type *type);
void emit_release_open(CodegenContext *ctx, Type *type);
CGScope *find_loop_scope(CodegenContext *ctx);

/* --- Expression generation (codegen_expr.c) --- */

void gen_for_header(CodegenContext *ctx, ASTNode *node);
void gen_string_comparison(CodegenContext *ctx, ASTNode *left, const char *op, ASTNode *right);
void gen_coerce_to_string(CodegenContext *ctx, ASTNode *expr);
void gen_string_concat(CodegenContext *ctx, ASTNode *expr);
void gen_block_with_scope(CodegenContext *ctx, ASTNode *block, int is_loop);
void gen_block(CodegenContext *ctx, ASTNode *block);
void gen_expr(CodegenContext *ctx, ASTNode *expr);
void gen_stmt(CodegenContext *ctx, ASTNode *node);
void gen_stmts(CodegenContext *ctx, NodeList *stmts);
void gen_func_proto(CodegenContext *ctx, ASTNode *func, int to_header);
void gen_func_body(CodegenContext *ctx, ASTNode *block, TypeKind ret_type);
void gen_func_def(CodegenContext *ctx, ASTNode *func);

#endif
