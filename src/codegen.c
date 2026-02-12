/*
 * Code generation for Zinc -> C transpilation.
 *
 * Expression-oriented control flow uses GCC statement expressions ({ ... })
 * so that `if`, `while`, and `for` can appear in value positions.
 * This is a conscious design tradeoff: it ties the generated C to GCC/Clang
 * but keeps the codegen simple and the generated code readable. (#8)
 *
 * Split into three files:
 *   codegen.c       -- shared infrastructure, emit helpers, ARC scope, generate()
 *   codegen_types.c -- struct/class/tuple layout
 *   codegen_expr.c  -- expression/statement generation, function emission
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
    case TK_STRING: return "ZnString*";
    case TK_BOOL:   return "bool";
    case TK_CHAR:   return "char";
    case TK_VOID:   return "void";
    case TK_STRUCT: return "/* struct */";
    case TK_CLASS:  return "/* class */";
    default:        return "int64_t";
    }
}

const char *opt_type_for(TypeKind t) {
    switch (t) {
    case TK_INT:   return "ZnOpt_int";
    case TK_FLOAT: return "ZnOpt_float";
    case TK_BOOL:  return "ZnOpt_bool";
    case TK_CHAR:  return "ZnOpt_char";
    default:       return NULL;
    }
}

int is_ref_type(TypeKind t) {
    return t == TK_STRING || t == TK_CLASS;
}

int expr_is_string(ASTNode *expr) {
    return expr && expr->resolved_type && expr->resolved_type->kind == TK_STRING;
}

/* --- ARC scope management --- */

void cg_push_scope(CodegenContext *ctx, int is_loop) {
    CGScope *s = calloc(1, sizeof(CGScope));
    s->parent = ctx->scope;
    s->is_loop = is_loop;
    ctx->scope = s;
}

void cg_pop_scope(CodegenContext *ctx) {
    CGScope *old = ctx->scope;
    ctx->scope = old->parent;
    CGScopeVar *v = old->ref_vars;
    while (v) {
        CGScopeVar *next = v->next;
        free(v->name);
        free(v->type_name);
        free(v);
        v = next;
    }
    free(old);
}

void cg_scope_add_ref(CodegenContext *ctx, const char *name, const char *type_name) {
    CGScopeVar *v = calloc(1, sizeof(CGScopeVar));
    v->name = strdup(name);
    v->type_name = strdup(type_name);
    v->next = ctx->scope->ref_vars;
    ctx->scope->ref_vars = v;
}

void cg_scope_add_value_type(CodegenContext *ctx, const char *name, const char *struct_name) {
    CGScopeVar *v = calloc(1, sizeof(CGScopeVar));
    v->name = strdup(name);
    v->type_name = strdup(struct_name);
    v->is_value_type = 1;
    v->next = ctx->scope->ref_vars;
    ctx->scope->ref_vars = v;
}

static void emit_value_type_field_releases(CodegenContext *ctx, const char *prefix, StructDef *sd) {
    for (StructFieldDef *fd = sd->fields; fd; fd = fd->next) {
        Type *ft = fd->type;
        if (!ft) continue;
        if (ft->kind == TK_STRING) {
            cg_emit_indent(ctx);
            cg_emitf(ctx, "__zn_str_release(%s.%s);\n", prefix, fd->name);
        } else if (ft->kind == TK_CLASS && ft->name) {
            cg_emit_indent(ctx);
            cg_emitf(ctx, "__%s_release(%s.%s);\n", ft->name, prefix, fd->name);
        } else if (ft->kind == TK_STRUCT && ft->name) {
            StructDef *inner = lookup_struct(ctx->sem_ctx, ft->name);
            if (inner) {
                char nested[256];
                snprintf(nested, sizeof(nested), "%s.%s", prefix, fd->name);
                emit_value_type_field_releases(ctx, nested, inner);
            }
        }
    }
}

void emit_var_release(CodegenContext *ctx, CGScopeVar *v) {
    if (v->is_value_type) {
        StructDef *sd = lookup_struct(ctx->sem_ctx, v->type_name);
        if (sd) emit_value_type_field_releases(ctx, v->name, sd);
    } else {
        cg_emit_indent(ctx);
        cg_emitf(ctx, "__%s_release(%s);\n", v->type_name, v->name);
    }
}

void emit_scope_releases(CodegenContext *ctx) {
    if (!ctx->scope) return;
    for (CGScopeVar *v = ctx->scope->ref_vars; v; v = v->next) {
        emit_var_release(ctx, v);
    }
}

void emit_all_scope_releases(CodegenContext *ctx) {
    for (CGScope *s = ctx->scope; s; s = s->parent) {
        for (CGScopeVar *v = s->ref_vars; v; v = v->next) {
            emit_var_release(ctx, v);
        }
    }
}

CGScope *find_loop_scope(CodegenContext *ctx) {
    for (CGScope *s = ctx->scope; s; s = s->parent) {
        if (s->is_loop) return s;
    }
    return NULL;
}


/* --- ARC retain/release type dispatch helpers --- */

void emit_retain_call(CodegenContext *ctx, const char *expr, Type *type) {
    if (!type) return;
    switch (type->kind) {
    case TK_STRING: cg_emitf(ctx, "__zn_str_retain(%s)", expr); break;
    case TK_CLASS:  if (type->name) cg_emitf(ctx, "__%s_retain(%s)", type->name, expr); break;
    default: break;
    }
}

void emit_release_call(CodegenContext *ctx, const char *expr, Type *type) {
    if (!type) return;
    switch (type->kind) {
    case TK_STRING: cg_emitf(ctx, "__zn_str_release(%s)", expr); break;
    case TK_CLASS:  if (type->name) cg_emitf(ctx, "__%s_release(%s)", type->name, expr); break;
    default: break;
    }
}

/* Emit just the function name and opening paren — caller provides arg and closing paren */
void emit_retain_open(CodegenContext *ctx, Type *type) {
    if (!type) return;
    switch (type->kind) {
    case TK_STRING: cg_emit(ctx, "__zn_str_retain("); break;
    case TK_CLASS:  if (type->name) cg_emitf(ctx, "__%s_retain(", type->name); break;
    default: break;
    }
}

void emit_release_open(CodegenContext *ctx, Type *type) {
    if (!type) return;
    switch (type->kind) {
    case TK_STRING: cg_emit(ctx, "__zn_str_release("); break;
    case TK_CLASS:  if (type->name) cg_emitf(ctx, "__%s_release(", type->name); break;
    default: break;
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

static void emit_c_string_escaped(FILE *f, const char *s) {
    for (const char *p = s; *p; p++) {
        switch (*p) {
        case '\n': fputs("\\n", f); break;
        case '\t': fputs("\\t", f); break;
        case '\r': fputs("\\r", f); break;
        case '\\': fputs("\\\\", f); break;
        case '"':  fputs("\\\"", f); break;
        default:   fputc(*p, f); break;
        }
    }
}

/* --- AST walker for string literal collection (#6) --- */

typedef void (*ASTVisitor)(ASTNode *node, void *data);

static void ast_walk_list(NodeList *list, ASTVisitor visitor, void *data);

static void ast_walk(ASTNode *node, ASTVisitor visitor, void *data) {
    if (!node) return;
    visitor(node, data);
    switch (node->type) {
    case NODE_PROGRAM: ast_walk_list(node->data.program.stmts, visitor, data); break;
    case NODE_BLOCK:   ast_walk_list(node->data.block.stmts, visitor, data); break;
    case NODE_BINOP:
        ast_walk(node->data.binop.left, visitor, data);
        ast_walk(node->data.binop.right, visitor, data);
        break;
    case NODE_UNARYOP: ast_walk(node->data.unaryop.operand, visitor, data); break;
    case NODE_ASSIGN:
        ast_walk(node->data.assign.target, visitor, data);
        ast_walk(node->data.assign.value, visitor, data);
        break;
    case NODE_COMPOUND_ASSIGN:
        ast_walk(node->data.compound_assign.target, visitor, data);
        ast_walk(node->data.compound_assign.value, visitor, data);
        break;
    case NODE_INCDEC:
        ast_walk(node->data.incdec.target, visitor, data);
        break;
    case NODE_DECL: ast_walk(node->data.decl.value, visitor, data); break;
    case NODE_IF:
        ast_walk(node->data.if_expr.cond, visitor, data);
        ast_walk(node->data.if_expr.then_b, visitor, data);
        ast_walk(node->data.if_expr.else_b, visitor, data);
        break;
    case NODE_WHILE:
        ast_walk(node->data.while_expr.cond, visitor, data);
        ast_walk(node->data.while_expr.body, visitor, data);
        break;
    case NODE_FOR:
        ast_walk(node->data.for_expr.init, visitor, data);
        ast_walk(node->data.for_expr.cond, visitor, data);
        ast_walk(node->data.for_expr.update, visitor, data);
        ast_walk(node->data.for_expr.body, visitor, data);
        break;
    case NODE_FUNC_DEF: ast_walk(node->data.func_def.body, visitor, data); break;
    case NODE_CALL:     ast_walk_list(node->data.call.args, visitor, data); break;
    case NODE_RETURN:   ast_walk(node->data.ret.value, visitor, data); break;
    case NODE_BREAK:    ast_walk(node->data.break_expr.value, visitor, data); break;
    case NODE_CONTINUE: ast_walk(node->data.continue_expr.value, visitor, data); break;
    case NODE_FIELD_ACCESS: ast_walk(node->data.field_access.object, visitor, data); break;
    case NODE_TYPE_DEF: ast_walk_list(node->data.type_def.fields, visitor, data); break;
    case NODE_STRUCT_FIELD:
        ast_walk(node->data.struct_field.default_value, visitor, data);
        break;
    case NODE_NAMED_ARG: ast_walk(node->data.named_arg.value, visitor, data); break;
    case NODE_TUPLE: ast_walk_list(node->data.tuple.elements, visitor, data); break;
    case NODE_INDEX:
        ast_walk(node->data.index_access.object, visitor, data);
        ast_walk(node->data.index_access.index, visitor, data);
        break;
    case NODE_OPTIONAL_CHECK:
        ast_walk(node->data.optional_check.operand, visitor, data);
        break;
    default: break;
    }
}

static void ast_walk_list(NodeList *list, ASTVisitor visitor, void *data) {
    for (NodeList *l = list; l; l = l->next) {
        ast_walk(l->node, visitor, data);
    }
}

/* String literal visitor -- assigns codegen-side IDs and emits static structs (#3, #6) */
static void string_literal_visitor(ASTNode *node, void *data) {
    if (node->type != NODE_STRING) return;
    CodegenContext *ctx = data;

    int id = ctx->string_counter++;
    node->string_id = id;

    int len = (int)strlen(node->data.sval);
    fprintf(ctx->c_file,
            "static struct { int32_t _rc; int32_t _len; char _data[%d]; } __zn_str_%d = {-1, %d, \"",
            len + 1, id, len);
    emit_c_string_escaped(ctx->c_file, node->data.sval);
    fprintf(ctx->c_file, "\"};\n");
}

static void collect_string_literals(CodegenContext *ctx, ASTNode *root) {
    ast_walk(root, string_literal_visitor, ctx);
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

    /* Generate struct typedefs (to header) */
    for (NodeList *s = root->data.program.stmts; s; s = s->next) {
        if (s->node && s->node->type == NODE_TYPE_DEF && !s->node->data.type_def.is_class) {
            gen_struct_def(ctx, s->node);
        }
    }

    /* Generate class typedefs (to header) and ARC functions (to C file) */
    for (NodeList *s = root->data.program.stmts; s; s = s->next) {
        if (s->node && s->node->type == NODE_TYPE_DEF && s->node->data.type_def.is_class) {
            gen_class_def(ctx, s->node);
        }
    }

    /* Generate tuple typedefs */
    gen_tuple_typedefs(ctx);

    /* Collect string literals and emit static structs */
    collect_string_literals(ctx, root);
    cg_emit(ctx, "\n");

    /* Generate all functions */
    for (NodeList *s = root->data.program.stmts; s; s = s->next) {
        if (s->node && s->node->type == NODE_FUNC_DEF) {
            gen_func_def(ctx, s->node);
        }
    }

    cg_emit_header(ctx, "\n#endif\n");
}
