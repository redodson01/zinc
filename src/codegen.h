#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdio.h>
#include "ast.h"
#include "semantic.h"

/* Scope variable tracking for ARC */
typedef struct CGScopeVar {
    char *name;
    char *type_name;   /* "zn_str" for strings */
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
    ASTNode **string_nodes;      /* Codegen-side string literal tracking (#3) */
    int string_nodes_cap;
    const char *output_base;
    int loop_expr_temp;          /* >= 0 when loop is in expression context */
    int loop_expr_optional;      /* 1 when current loop expression produces optional */
    TypeKind loop_expr_type;     /* type of current loop expression result */
    CGScope *scope;
} CodegenContext;

/* Initialize/cleanup context */
CodegenContext *codegen_init(FILE *c_file, FILE *h_file, SemanticContext *sem_ctx, const char *output_base);
void codegen_free(CodegenContext *ctx);

/* Generate code from AST */
void generate(CodegenContext *ctx, ASTNode *root);

/* --- Shared emit helpers (codegen.c) --- */

void cg_emit(CodegenContext *ctx, const char *s);
void cg_emitf(CodegenContext *ctx, const char *fmt, ...);
void cg_emit_indent(CodegenContext *ctx);
void cg_emit_header(CodegenContext *ctx, const char *s);

/* --- Type helpers (codegen.c) --- */

const char *type_to_c(TypeKind t);
const char *opt_type_for(TypeKind t);
int is_class_type(CodegenContext *ctx, const char *name);
int is_ref_type(TypeKind t);
int is_fresh_string_alloc(ASTNode *expr);
int is_fresh_class_alloc(ASTNode *expr);
int is_fresh_array_alloc(ASTNode *expr);
int is_fresh_hash_alloc(ASTNode *expr);
int expr_is_string(ASTNode *expr);

/* --- ARC scope management (codegen.c) --- */

void cg_push_scope(CodegenContext *ctx, int is_loop);
void cg_pop_scope(CodegenContext *ctx);
void cg_scope_add_ref(CodegenContext *ctx, const char *name, const char *type_name);
void emit_scope_releases(CodegenContext *ctx);
void emit_all_scope_releases(CodegenContext *ctx);
CGScope *find_loop_scope(CodegenContext *ctx);

/* --- Expression generation (codegen_expr.c) --- */

const char *unbox_func_for(TypeKind t);
void gen_box_expr(CodegenContext *ctx, ASTNode *expr);
void gen_for_header(CodegenContext *ctx, ASTNode *node);
void gen_string_comparison(CodegenContext *ctx, ASTNode *left, const char *op, ASTNode *right);
void gen_coerce_to_string(CodegenContext *ctx, ASTNode *expr);
void gen_string_concat(CodegenContext *ctx, ASTNode *expr);
int find_string_id(CodegenContext *ctx, ASTNode *node);
void gen_block_with_scope(CodegenContext *ctx, ASTNode *block, int is_loop);
void gen_block(CodegenContext *ctx, ASTNode *block);
void gen_expr(CodegenContext *ctx, ASTNode *expr);
void gen_stmt(CodegenContext *ctx, ASTNode *node);
void gen_stmts(CodegenContext *ctx, NodeList *stmts);
void gen_func_proto(CodegenContext *ctx, ASTNode *func, int to_header);
void gen_func_body(CodegenContext *ctx, ASTNode *block, TypeKind ret_type);
void gen_func_def(CodegenContext *ctx, ASTNode *func);

/* --- Type layout emission (codegen_types.c) --- */

void gen_struct_def(CodegenContext *ctx, ASTNode *node);
void gen_class_def(CodegenContext *ctx, ASTNode *node);
void gen_tuple_typedefs(CodegenContext *ctx);
void gen_object_typedefs(CodegenContext *ctx);
void gen_extern_decl(CodegenContext *ctx, ASTNode *decl);
void gen_extern_block(CodegenContext *ctx, ASTNode *block);

#endif
