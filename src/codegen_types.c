#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "codegen.h"

/* Generate struct typedef to header */
void gen_struct_def(CodegenContext *ctx, ASTNode *node) {
    const char *name = node->data.struct_def.name;
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
    const char *name = node->data.struct_def.name;
    StructDef *sd = lookup_struct(ctx->sem_ctx, name);
    if (!sd) return;

    /* Typedef to header */
    fprintf(ctx->h_file, "typedef struct {\n");
    fprintf(ctx->h_file, "    int _rc;\n");
    for (StructFieldDef *fd = sd->fields; fd; fd = fd->next) {
        if (fd->type->kind == TK_STRING) {
            fprintf(ctx->h_file, "    ZnString *%s;\n", fd->name);
        } else if (fd->type->kind == TK_STRUCT && fd->type->name) {
            StructDef *fsd = lookup_struct(ctx->sem_ctx, fd->type->name);
            if (fsd && fsd->is_class) {
                fprintf(ctx->h_file, "    %s *%s;\n", fd->type->name, fd->name);
            } else {
                fprintf(ctx->h_file, "    %s %s;\n", fd->type->name, fd->name);
            }
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
    for (StructFieldDef *fd = sd->fields; fd; fd = fd->next) {
        if (fd->type->kind == TK_STRING) {
            cg_emitf(ctx, "        __zn_str_release(self->%s);\n", fd->name);
        } else if (fd->type->kind == TK_STRUCT && fd->type->name) {
            StructDef *fsd = lookup_struct(ctx->sem_ctx, fd->type->name);
            if (fsd && fsd->is_class) {
                cg_emitf(ctx, "        __%s_release(self->%s);\n", fd->type->name, fd->name);
            }
        }
    }
    cg_emit(ctx, "        free(self);\n");
    cg_emit(ctx, "    }\n");
    cg_emit(ctx, "}\n\n");
}

/* Generate tuple typedefs (anonymous struct types registered by semantic analysis) */
void gen_tuple_typedefs(CodegenContext *ctx) {
    for (StructDef *sd = ctx->sem_ctx->struct_defs; sd; sd = sd->next) {
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
    for (StructDef *sd = ctx->sem_ctx->struct_defs; sd; sd = sd->next) {
        if (strncmp(sd->name, "__obj", 4) == 0) {
            /* Typedef to header (like class: with _rc field) */
            fprintf(ctx->h_file, "typedef struct {\n");
            fprintf(ctx->h_file, "    int _rc;\n");
            for (StructFieldDef *fd = sd->fields; fd; fd = fd->next) {
                if (fd->type->kind == TK_STRING) {
                    fprintf(ctx->h_file, "    ZnString *%s;\n", fd->name);
                } else if (fd->type->kind == TK_STRUCT && fd->type->name) {
                    StructDef *fsd = lookup_struct(ctx->sem_ctx, fd->type->name);
                    if (fsd && fsd->is_class) {
                        fprintf(ctx->h_file, "    %s *%s;\n", fd->type->name, fd->name);
                    } else {
                        fprintf(ctx->h_file, "    %s %s;\n", fd->type->name, fd->name);
                    }
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
            for (StructFieldDef *fd = sd->fields; fd; fd = fd->next) {
                if (fd->type->kind == TK_STRING) {
                    cg_emitf(ctx, "        __zn_str_release(self->%s);\n", fd->name);
                } else if (fd->type->kind == TK_STRUCT && fd->type->name) {
                    StructDef *fsd = lookup_struct(ctx->sem_ctx, fd->type->name);
                    if (fsd && fsd->is_class) {
                        cg_emitf(ctx, "        __%s_release(self->%s);\n", fd->type->name, fd->name);
                    }
                }
            }
            cg_emit(ctx, "        free(self);\n");
            cg_emit(ctx, "    }\n");
            cg_emit(ctx, "}\n\n");
        }
    }
}

/* Generate individual extern declaration to header */
void gen_extern_decl(CodegenContext *ctx, ASTNode *decl) {
    switch (decl->type) {
    case NODE_EXTERN_FUNC: {
        TypeKind ret_type = decl->data.extern_func.return_type
            ? decl->data.extern_func.return_type->kind
            : TK_VOID;
        const char *ret_str = (ret_type == TK_STRING) ? "const char*" : type_to_c(ret_type);
        fprintf(ctx->h_file, "extern %s %s(", ret_str, decl->data.extern_func.name);
        int first = 1;
        for (NodeList *p = decl->data.extern_func.params; p; p = p->next) {
            if (!first) fprintf(ctx->h_file, ", ");
            TypeKind pk = p->node->data.param.type_info->kind;
            const char *pk_str = (pk == TK_STRING) ? "const char*" : type_to_c(pk);
            fprintf(ctx->h_file, "%s %s", pk_str, p->node->data.param.name);
            first = 0;
        }
        if (first) {
            fprintf(ctx->h_file, "void");
        }
        fprintf(ctx->h_file, ");\n");
        break;
    }
    case NODE_EXTERN_VAR: {
        TypeKind vk = decl->data.extern_var.type_info->kind;
        const char *vk_str = (vk == TK_STRING) ? "const char*" : type_to_c(vk);
        fprintf(ctx->h_file, "extern %s %s;\n", vk_str, decl->data.extern_var.name);
        break;
    }
    case NODE_EXTERN_LET: {
        TypeKind vk = decl->data.extern_let.type_info->kind;
        const char *vk_str = (vk == TK_STRING) ? "const char*" : type_to_c(vk);
        fprintf(ctx->h_file, "extern const %s %s;\n", vk_str, decl->data.extern_let.name);
        break;
    }
    default:
        break;
    }
}

/* Generate extern block — iterates declarations and emits each */
void gen_extern_block(CodegenContext *ctx, ASTNode *block) {
    for (NodeList *d = block->data.extern_block.decls; d; d = d->next) {
        gen_extern_decl(ctx, d->node);
    }
}
