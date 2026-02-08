/*
 * Code generation for Zinc → C transpilation.
 *
 * Expression-oriented control flow uses GCC statement expressions ({ ... })
 * so that `if`, `while`, and `for` can appear in value positions.
 * This is a conscious design tradeoff: it ties the generated C to GCC/Clang
 * but keeps the codegen simple and the generated code readable. (#8)
 *
 * Split into three files:
 *   codegen.c       — shared infrastructure, emit helpers, ARC scope, generate()
 *   codegen_types.c — struct/class/tuple/object layout, extern declarations
 *   codegen_expr.c  — expression/statement generation, function emission
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "codegen.h"

/* Embedded runtime generated from src/zinc_runtime.h at build time (#7) */
#include "zinc_runtime_embed.h"

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
    case TK_ARRAY:  return "ZnArray*";
    case TK_HASH:   return "ZnHash*";
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

int is_class_type(CodegenContext *ctx, const char *name) {
    if (!name) return 0;
    StructDef *sd = lookup_struct(ctx->sem_ctx, name);
    return sd && sd->is_class;
}

int is_fresh_class_alloc(ASTNode *expr) {
    if (!expr) return 0;
    return (expr->type == NODE_STRUCT_INIT ||
            expr->type == NODE_OBJECT_LITERAL ||
            expr->type == NODE_CALL);
}

int is_fresh_array_alloc(ASTNode *expr) {
    if (!expr) return 0;
    return (expr->type == NODE_ARRAY_LITERAL ||
            (expr->type == NODE_CALL && expr->resolved_type && expr->resolved_type->kind == TK_ARRAY));
}

int is_fresh_hash_alloc(ASTNode *expr) {
    if (!expr) return 0;
    return (expr->type == NODE_HASH_LITERAL ||
            (expr->type == NODE_CALL && expr->resolved_type && expr->resolved_type->kind == TK_HASH));
}

int is_ref_type(TypeKind t) {
    return t == TK_STRING || t == TK_ARRAY || t == TK_HASH;
}

int is_fresh_string_alloc(ASTNode *expr) {
    if (!expr) return 0;
    if (expr->type == NODE_BINOP && expr->resolved_type && expr->resolved_type->kind == TK_STRING
        && strcmp(expr->data.binop.op, "+") == 0) return 1;
    if (expr->type == NODE_CALL && expr->resolved_type && expr->resolved_type->kind == TK_STRING) return 1;
    return 0;
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

void emit_scope_releases(CodegenContext *ctx) {
    if (!ctx->scope) return;
    for (CGScopeVar *v = ctx->scope->ref_vars; v; v = v->next) {
        cg_emit_indent(ctx);
        cg_emitf(ctx, "__%s_release(%s);\n", v->type_name, v->name);
    }
}

void emit_all_scope_releases(CodegenContext *ctx) {
    for (CGScope *s = ctx->scope; s; s = s->parent) {
        for (CGScopeVar *v = s->ref_vars; v; v = v->next) {
            cg_emit_indent(ctx);
            cg_emitf(ctx, "__%s_release(%s);\n", v->type_name, v->name);
        }
    }
}

CGScope *find_loop_scope(CodegenContext *ctx) {
    for (CGScope *s = ctx->scope; s; s = s->parent) {
        if (s->is_loop) return s;
    }
    return NULL;
}

/* --- Context lifecycle --- */

CodegenContext *codegen_init(FILE *c_file, FILE *h_file,
                              SemanticContext *sem_ctx,
                              const char *output_base) {
    CodegenContext *ctx = calloc(1, sizeof(CodegenContext));
    ctx->c_file = c_file;
    ctx->h_file = h_file;
    ctx->sem_ctx = sem_ctx;
    ctx->output_base = output_base;
    ctx->loop_expr_temp = -1;
    return ctx;
}

void codegen_free(CodegenContext *ctx) {
    free(ctx->string_nodes);
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
    case NODE_ASSIGN:  ast_walk(node->data.assign.value, visitor, data); break;
    case NODE_COMPOUND_ASSIGN: ast_walk(node->data.compound_assign.value, visitor, data); break;
    case NODE_VAR_DECL: ast_walk(node->data.var_decl.value, visitor, data); break;
    case NODE_LET_DECL: ast_walk(node->data.let_decl.value, visitor, data); break;
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
    case NODE_FIELD_ASSIGN:
        ast_walk(node->data.field_assign.object, visitor, data);
        ast_walk(node->data.field_assign.value, visitor, data);
        break;
    case NODE_STRUCT_DEF:
    case NODE_CLASS_DEF: ast_walk_list(node->data.struct_def.fields, visitor, data); break;
    case NODE_STRUCT_FIELD:
        ast_walk(node->data.struct_field.default_value, visitor, data);
        break;
    case NODE_STRUCT_INIT: ast_walk_list(node->data.struct_init.args, visitor, data); break;
    case NODE_NAMED_ARG: ast_walk(node->data.named_arg.value, visitor, data); break;
    case NODE_TUPLE: ast_walk_list(node->data.tuple.elements, visitor, data); break;
    case NODE_OBJECT_LITERAL: ast_walk_list(node->data.object_literal.fields, visitor, data); break;
    case NODE_INDEX:
        ast_walk(node->data.index_access.object, visitor, data);
        ast_walk(node->data.index_access.index, visitor, data);
        break;
    case NODE_ARRAY_LITERAL:
        ast_walk_list(node->data.array_literal.elems, visitor, data);
        break;
    case NODE_HASH_LITERAL:
        ast_walk_list(node->data.hash_literal.pairs, visitor, data);
        break;
    case NODE_HASH_PAIR:
        ast_walk(node->data.hash_pair.key, visitor, data);
        ast_walk(node->data.hash_pair.value, visitor, data);
        break;
    case NODE_INDEX_ASSIGN:
        ast_walk(node->data.index_assign.object, visitor, data);
        ast_walk(node->data.index_assign.index, visitor, data);
        ast_walk(node->data.index_assign.value, visitor, data);
        break;
    case NODE_OPTIONAL_CHECK:
        ast_walk(node->data.optional_check.operand, visitor, data);
        break;
    case NODE_EXTERN_BLOCK:
        ast_walk_list(node->data.extern_block.decls, visitor, data);
        break;
    default: break;
    }
}

static void ast_walk_list(NodeList *list, ASTVisitor visitor, void *data) {
    for (NodeList *l = list; l; l = l->next) {
        ast_walk(l->node, visitor, data);
    }
}

/* String literal visitor — assigns codegen-side IDs and emits static structs (#3, #6) */
static void string_literal_visitor(ASTNode *node, void *data) {
    if (node->type != NODE_STRING) return;
    CodegenContext *ctx = data;

    if (ctx->string_counter >= ctx->string_nodes_cap) {
        ctx->string_nodes_cap = ctx->string_nodes_cap ? ctx->string_nodes_cap * 2 : 16;
        ctx->string_nodes = realloc(ctx->string_nodes,
                                     ctx->string_nodes_cap * sizeof(ASTNode *));
    }
    int id = ctx->string_counter++;
    ctx->string_nodes[id] = node;

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

    /* ZnString typedef to header (uses int32_t for _rc/_len #18) */
    cg_emit_header(ctx, "typedef struct { int32_t _rc; int32_t _len; char _data[]; } ZnString;\n\n");

    /* ZnTag enum and ZnValue/ZnArray/ZnHash typedefs */
    cg_emit_header(ctx, "typedef enum { ZN_TAG_INT = 0, ZN_TAG_FLOAT = 1, ZN_TAG_BOOL = 2, ZN_TAG_CHAR = 3, ZN_TAG_STRING = 4, ZN_TAG_ARRAY = 5, ZN_TAG_HASH = 6, ZN_TAG_REF = 7, ZN_TAG_VAL = 8 } ZnTag;\n");
    cg_emit_header(ctx, "typedef struct { ZnTag tag; union { int64_t i; double f; bool b; char c; void *ptr; } as; } ZnValue;\n");
    cg_emit_header(ctx, "typedef void (*ZnElemFn)(void*);\n");
    cg_emit_header(ctx, "typedef unsigned int (*ZnHashFn)(ZnValue);\n");
    cg_emit_header(ctx, "typedef bool (*ZnEqFn)(ZnValue, ZnValue);\n");
    cg_emit_header(ctx, "typedef struct { int _rc; int32_t _len; int32_t _cap; ZnValue *_data; ZnElemFn _elem_retain; ZnElemFn _elem_release; ZnHashFn _elem_hashcode; ZnEqFn _elem_equals; } ZnArray;\n");
    cg_emit_header(ctx, "typedef struct ZnHashEntry { ZnValue key; ZnValue value; struct ZnHashEntry *next; } ZnHashEntry;\n");
    cg_emit_header(ctx, "typedef struct { int _rc; int32_t _len; int32_t _cap; ZnHashEntry **_buckets; ZnElemFn _key_retain; ZnElemFn _key_release; ZnHashFn _key_hashcode; ZnEqFn _key_equals; ZnElemFn _val_retain; ZnElemFn _val_release; } ZnHash;\n\n");

    /* Optional wrapper types for value types */
    cg_emit_header(ctx, "typedef struct { bool _has; int64_t _val; } ZnOpt_int;\n");
    cg_emit_header(ctx, "typedef struct { bool _has; double _val; } ZnOpt_float;\n");
    cg_emit_header(ctx, "typedef struct { bool _has; bool _val; } ZnOpt_bool;\n");
    cg_emit_header(ctx, "typedef struct { bool _has; char _val; } ZnOpt_char;\n\n");

    cg_emit(ctx, "#include <stdio.h>\n");
    cg_emit(ctx, "#include <stdlib.h>\n");
    cg_emit(ctx, "#include <string.h>\n");
    cg_emit(ctx, "#include <stdint.h>\n");
    cg_emit(ctx, "#include <stdbool.h>\n");
    fprintf(ctx->c_file, "#include \"%s.h\"\n\n", base);

    /* Emit embedded runtime (#7) */
    cg_emit(ctx, zinc_runtime_src);
    cg_emit(ctx, "\n");

    /* Generate struct typedefs (to header) */
    for (NodeList *s = root->data.program.stmts; s; s = s->next) {
        if (s->node && s->node->type == NODE_STRUCT_DEF) {
            gen_struct_def(ctx, s->node);
        }
    }

    /* Generate class typedefs (to header) and ARC functions (to C file) */
    for (NodeList *s = root->data.program.stmts; s; s = s->next) {
        if (s->node && s->node->type == NODE_CLASS_DEF) {
            gen_class_def(ctx, s->node);
        }
    }

    /* Generate tuple typedefs */
    gen_tuple_typedefs(ctx);

    /* Generate anonymous object typedefs + ARC functions */
    gen_object_typedefs(ctx);

    /* Generate collection helper functions (retain/release wrappers, hashcode, equals) */
    gen_collection_helpers(ctx);

    /* Collect string literals and emit static structs */
    collect_string_literals(ctx, root);
    cg_emit(ctx, "\n");

    /* Generate extern declarations (to header) */
    for (NodeList *s = root->data.program.stmts; s; s = s->next) {
        if (s->node && s->node->type == NODE_EXTERN_BLOCK) {
            gen_extern_block(ctx, s->node);
        }
    }

    /* Generate all functions */
    for (NodeList *s = root->data.program.stmts; s; s = s->next) {
        if (s->node && s->node->type == NODE_FUNC_DEF) {
            gen_func_def(ctx, s->node);
        }
    }

    cg_emit_header(ctx, "\n#endif\n");
}
