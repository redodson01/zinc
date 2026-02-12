#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdio.h>
#include "ast.h"
#include "semantic.h"

/* Code generation context */
typedef struct {
    FILE *c_file;
    FILE *h_file;
    SemanticContext *sem_ctx;
    int indent_level;
    int temp_counter;
    int loop_expr_temp;       /* temp var index for break/continue value, -1 if not in loop expr */
    const char *output_base;
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

/* --- Expression generation (codegen_expr.c) --- */

void gen_for_header(CodegenContext *ctx, ASTNode *node);
void gen_block(CodegenContext *ctx, ASTNode *block);
void gen_expr(CodegenContext *ctx, ASTNode *expr);
void gen_stmt(CodegenContext *ctx, ASTNode *node);
void gen_stmts(CodegenContext *ctx, NodeList *stmts);
void gen_func_proto(CodegenContext *ctx, ASTNode *func, int to_header);
void gen_func_body(CodegenContext *ctx, ASTNode *block, TypeKind ret_type);
void gen_func_def(CodegenContext *ctx, ASTNode *func);

#endif
