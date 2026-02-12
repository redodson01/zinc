/*
 * Code generation for Zinc -> C transpilation.
 *
 * Expression-oriented control flow uses GCC statement expressions ({ ... })
 * so that `if`, `while`, and `for` can appear in value positions.
 *
 * Split into two files:
 *   codegen.c      — shared infrastructure, emit helpers, generate()
 *   codegen_expr.c — expression/statement generation, function emission
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "codegen.h"

/* --- Emit helpers --- */

void cg_emit(CodegenContext *ctx, const char *s) {
    fputs(s, ctx->c_file);
}

void cg_emitf(CodegenContext *ctx, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(ctx->c_file, fmt, args);
    va_end(args);
}

void cg_emit_indent(CodegenContext *ctx) {
    for (int i = 0; i < ctx->indent_level; i++) {
        cg_emit(ctx, "    ");
    }
}

void cg_emit_header(CodegenContext *ctx, const char *s) {
    fputs(s, ctx->h_file);
}

void cg_emit_line(CodegenContext *ctx, int line) {
    if (line > 0 && ctx->source_file) {
        fprintf(ctx->c_file, "#line %d \"%s\"\n", line, ctx->source_file);
    }
}

/* --- Type helpers --- */

const char *type_to_c(TypeKind t) {
    switch (t) {
    case TK_INT:    return "int64_t";
    case TK_FLOAT:  return "double";
    case TK_BOOL:   return "bool";
    case TK_CHAR:   return "char";
    case TK_VOID:   return "void";
    default:        return "int64_t";
    }
}

/* --- Context lifecycle --- */

CodegenContext *codegen_init(FILE *c_file, FILE *h_file,
                              SemanticContext *sem_ctx,
                              const char *output_base,
                              const char *source_file) {
    CodegenContext *ctx = calloc(1, sizeof(CodegenContext));
    ctx->c_file = c_file;
    ctx->h_file = h_file;
    ctx->sem_ctx = sem_ctx;
    ctx->output_base = output_base;
    ctx->source_file = source_file;
    ctx->loop_expr_temp = -1;
    return ctx;
}

void codegen_free(CodegenContext *ctx) {
    free(ctx);
}

/* --- Utility helpers --- */

static const char *basename_of(const char *path) {
    const char *base = strrchr(path, '/');
    return base ? base + 1 : path;
}

/* --- Top-level code generation --- */

void generate(CodegenContext *ctx, ASTNode *root) {
    if (!root || root->type != NODE_PROGRAM) return;

    const char *base = basename_of(ctx->output_base);
    char guard[256];
    snprintf(guard, sizeof(guard), "%s_H", base);
    for (char *p = guard; *p; p++) {
        if (*p >= 'a' && *p <= 'z') *p -= 32;
        else if (*p == '-' || *p == '.') *p = '_';
    }

    fprintf(ctx->h_file, "#ifndef %s\n", guard);
    fprintf(ctx->h_file, "#define %s\n\n", guard);
    cg_emit_header(ctx, "#include <stdint.h>\n");
    cg_emit_header(ctx, "#include <stdbool.h>\n\n");
    cg_emit_header(ctx, "#include \"zinc_runtime.h\"\n\n");

    cg_emit(ctx, "#include <stdio.h>\n");
    cg_emit(ctx, "#include <stdlib.h>\n");
    cg_emit(ctx, "#include <string.h>\n");
    cg_emit(ctx, "#include <stdint.h>\n");
    cg_emit(ctx, "#include <inttypes.h>\n");
    cg_emit(ctx, "#include <stdbool.h>\n");
    fprintf(ctx->c_file, "#include \"%s.h\"\n\n", base);

    /* Generate all functions */
    for (NodeList *s = root->data.program.stmts; s; s = s->next) {
        if (s->node && s->node->type == NODE_FUNC_DEF) {
            gen_func_def(ctx, s->node);
        }
    }

    cg_emit_header(ctx, "\n#endif\n");
}
