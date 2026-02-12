#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "codegen.h"

/* Emit release calls for ref-counted fields inside a struct/class.
   prefix includes the trailing accessor, e.g. "self->" for the top level
   or "self->inner." for nested struct fields.  Recurses into nested
   value-type (TK_STRUCT) fields. Used by class/object release functions. */
static void emit_nested_releases(CodegenContext *ctx, const char *prefix, StructDef *sd) {
    for (StructFieldDef *fd = sd->fields; fd; fd = fd->next) {
        Type *ft = fd->type;
        if (!ft) continue;
        if (ft->kind == TK_STRING) {
            cg_emitf(ctx, "        __zn_str_release(%s%s);\n", prefix, fd->name);
        } else if (ft->kind == TK_CLASS && ft->name) {
            cg_emitf(ctx, "        __%s_release(%s%s);\n", ft->name, prefix, fd->name);
        } else if (ft->kind == TK_STRUCT && ft->name) {
            StructDef *inner = lookup_struct(ctx->sem_ctx, ft->name);
            if (inner) {
                char nested[256];
                snprintf(nested, sizeof(nested), "%s%s.", prefix, fd->name);
                emit_nested_releases(ctx, nested, inner);
            }
        }
    }
}

/* Generate struct typedef to header */
void gen_struct_def(CodegenContext *ctx, ASTNode *node) {
    const char *name = node->data.type_def.name;
    StructDef *sd = lookup_struct(ctx->sem_ctx, name);
    if (!sd) return;

    fprintf(ctx->h_file, "typedef struct {\n");
    for (StructFieldDef *fd = sd->fields; fd; fd = fd->next) {
        if (fd->type->kind == TK_STRUCT && fd->type->name) {
            fprintf(ctx->h_file, "    %s %s;\n", fd->type->name, fd->name);
        } else {
            fprintf(ctx->h_file, "    %s %s;\n", type_to_c(fd->type->kind), fd->name);
        }
    }
    fprintf(ctx->h_file, "} %s;\n\n", name);
}

/* Generate class typedef (to header) and ARC alloc/retain/release functions (to C file) */
void gen_class_def(CodegenContext *ctx, ASTNode *node) {
    const char *name = node->data.type_def.name;
    StructDef *sd = lookup_struct(ctx->sem_ctx, name);
    if (!sd) return;

    /* Typedef to header (named struct tag for self-referential types) */
    fprintf(ctx->h_file, "typedef struct %s {\n", name);
    fprintf(ctx->h_file, "    int _rc;\n");
    for (StructFieldDef *fd = sd->fields; fd; fd = fd->next) {
        if (fd->type->kind == TK_STRING) {
            fprintf(ctx->h_file, "    ZnString *%s;\n", fd->name);
        } else if (fd->type->kind == TK_CLASS && fd->type->name) {
            fprintf(ctx->h_file, "    struct %s *%s;\n", fd->type->name, fd->name);
        } else if (fd->type->kind == TK_STRUCT && fd->type->name) {
            fprintf(ctx->h_file, "    %s %s;\n", fd->type->name, fd->name);
        } else {
            fprintf(ctx->h_file, "    %s %s;\n", type_to_c(fd->type->kind), fd->name);
        }
    }
    fprintf(ctx->h_file, "} %s;\n\n", name);

    /* Alloc function */
    cg_emitf(ctx, "static %s* __%s_alloc(void) {\n", name, name);
    cg_emitf(ctx, "    %s *self = calloc(1, sizeof(%s));\n", name, name);
    cg_emit(ctx, "    self->_rc = 1;\n");
    cg_emit(ctx, "    return self;\n");
    cg_emit(ctx, "}\n\n");

    /* Retain function */
    cg_emitf(ctx, "static void __%s_retain(%s *self) {\n", name, name);
    cg_emit(ctx, "    if (self) self->_rc++;\n");
    cg_emit(ctx, "}\n\n");

    /* Release function */
    cg_emitf(ctx, "static void __%s_release(%s *self) {\n", name, name);
    cg_emit(ctx, "    if (self && --(self->_rc) == 0) {\n");
    emit_nested_releases(ctx, "self->", sd);
    cg_emit(ctx, "        free(self);\n");
    cg_emit(ctx, "    }\n");
    cg_emit(ctx, "}\n\n");
}

/* Generate tuple typedefs (anonymous struct types registered by semantic analysis) */
void gen_tuple_typedefs(CodegenContext *ctx) {
    for (int _bi = 0; _bi < STRUCT_BUCKETS; _bi++)
    for (StructDef *sd = ctx->sem_ctx->struct_buckets[_bi]; sd; sd = sd->next) {
        if (strncmp(sd->name, "__ZnTuple", 9) == 0) {
            fprintf(ctx->h_file, "typedef struct {\n");
            for (StructFieldDef *fd = sd->fields; fd; fd = fd->next) {
                if (fd->type->kind == TK_STRUCT && fd->type->name) {
                    fprintf(ctx->h_file, "    %s %s;\n", fd->type->name, fd->name);
                } else {
                    fprintf(ctx->h_file, "    %s %s;\n", type_to_c(fd->type->kind), fd->name);
                }
            }
            fprintf(ctx->h_file, "} %s;\n\n", sd->name);
        }
    }
}

/* Generate anonymous object typedefs + ARC functions (names start with __obj) */
void gen_object_typedefs(CodegenContext *ctx) {
    for (int _bi = 0; _bi < STRUCT_BUCKETS; _bi++)
    for (StructDef *sd = ctx->sem_ctx->struct_buckets[_bi]; sd; sd = sd->next) {
        if (strncmp(sd->name, "__obj", 4) == 0) {
            /* Typedef to header (like class: with _rc field) */
            fprintf(ctx->h_file, "typedef struct %s {\n", sd->name);
            fprintf(ctx->h_file, "    int _rc;\n");
            for (StructFieldDef *fd = sd->fields; fd; fd = fd->next) {
                if (fd->type->kind == TK_STRING) {
                    fprintf(ctx->h_file, "    ZnString *%s;\n", fd->name);
                } else if (fd->type->kind == TK_CLASS && fd->type->name) {
                    fprintf(ctx->h_file, "    struct %s *%s;\n", fd->type->name, fd->name);
                } else if (fd->type->kind == TK_STRUCT && fd->type->name) {
                    fprintf(ctx->h_file, "    %s %s;\n", fd->type->name, fd->name);
                } else {
                    fprintf(ctx->h_file, "    %s %s;\n", type_to_c(fd->type->kind), fd->name);
                }
            }
            fprintf(ctx->h_file, "} %s;\n\n", sd->name);

            /* Alloc function */
            cg_emitf(ctx, "static %s* __%s_alloc(void) {\n", sd->name, sd->name);
            cg_emitf(ctx, "    %s *self = calloc(1, sizeof(%s));\n", sd->name, sd->name);
            cg_emit(ctx, "    self->_rc = 1;\n");
            cg_emit(ctx, "    return self;\n");
            cg_emit(ctx, "}\n\n");

            /* Retain function */
            cg_emitf(ctx, "static void __%s_retain(%s *self) {\n", sd->name, sd->name);
            cg_emit(ctx, "    if (self) self->_rc++;\n");
            cg_emit(ctx, "}\n\n");

            /* Release function */
            cg_emitf(ctx, "static void __%s_release(%s *self) {\n", sd->name, sd->name);
            cg_emit(ctx, "    if (self && --(self->_rc) == 0) {\n");
            emit_nested_releases(ctx, "self->", sd);
            cg_emit(ctx, "        free(self);\n");
            cg_emit(ctx, "    }\n");
            cg_emit(ctx, "}\n\n");
        }
    }
}
