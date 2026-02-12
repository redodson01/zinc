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
        } else if (ft->kind == TK_ARRAY) {
            cg_emitf(ctx, "        __zn_arr_release(%s%s);\n", prefix, fd->name);
        } else if (ft->kind == TK_HASH) {
            cg_emitf(ctx, "        __zn_hash_release(%s%s);\n", prefix, fd->name);
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

/* Generate individual extern declaration to header */
void gen_extern_decl(CodegenContext *ctx, ASTNode *decl) {
    switch (decl->type) {
    case NODE_EXTERN_FUNC: {
        /* Return type */
        TypeInfo *ret_ti = decl->data.extern_func.return_type;
        fprintf(ctx->h_file, "extern ");
        if (!ret_ti) {
            fprintf(ctx->h_file, "void");
        } else if (ret_ti->kind == TK_STRING) {
            fprintf(ctx->h_file, "const char*");
        } else if (ret_ti->kind == TK_STRUCT && ret_ti->name) {
            StructDef *sd = lookup_struct(ctx->sem_ctx, ret_ti->name);
            if (sd && sd->is_class) {
                fprintf(ctx->h_file, "%s *", ret_ti->name);
            } else {
                fprintf(ctx->h_file, "%s", ret_ti->name);
            }
        } else {
            fprintf(ctx->h_file, "%s", type_to_c(ret_ti->kind));
        }
        fprintf(ctx->h_file, " %s(", decl->data.extern_func.name);

        /* Parameters */
        int first = 1;
        for (NodeList *p = decl->data.extern_func.params; p; p = p->next) {
            if (!first) fprintf(ctx->h_file, ", ");
            TypeInfo *ti = p->node->data.param.type_info;
            if (ti->kind == TK_STRING) {
                fprintf(ctx->h_file, "const char* %s", p->node->data.param.name);
            } else if (ti->kind == TK_STRUCT && ti->name) {
                StructDef *sd = lookup_struct(ctx->sem_ctx, ti->name);
                if (sd && sd->is_class) {
                    fprintf(ctx->h_file, "%s *%s", ti->name, p->node->data.param.name);
                } else {
                    fprintf(ctx->h_file, "%s %s", ti->name, p->node->data.param.name);
                }
            } else {
                fprintf(ctx->h_file, "%s %s", type_to_c(ti->kind), p->node->data.param.name);
            }
            first = 0;
        }
        if (first) {
            fprintf(ctx->h_file, "void");
        }
        fprintf(ctx->h_file, ");\n");
        break;
    }
    case NODE_EXTERN_VAR: {
        TypeInfo *ti = decl->data.extern_var.type_info;
        if (ti->kind == TK_STRING) {
            fprintf(ctx->h_file, "extern const char* %s;\n", decl->data.extern_var.name);
        } else if (ti->kind == TK_STRUCT && ti->name) {
            StructDef *sd = lookup_struct(ctx->sem_ctx, ti->name);
            if (sd && sd->is_class) {
                fprintf(ctx->h_file, "extern %s *%s;\n", ti->name, decl->data.extern_var.name);
            } else {
                fprintf(ctx->h_file, "extern %s %s;\n", ti->name, decl->data.extern_var.name);
            }
        } else {
            fprintf(ctx->h_file, "extern %s %s;\n", type_to_c(ti->kind), decl->data.extern_var.name);
        }
        break;
    }
    case NODE_EXTERN_LET: {
        TypeInfo *ti = decl->data.extern_let.type_info;
        if (ti->kind == TK_STRING) {
            fprintf(ctx->h_file, "extern const char* const %s;\n", decl->data.extern_let.name);
        } else if (ti->kind == TK_STRUCT && ti->name) {
            StructDef *sd = lookup_struct(ctx->sem_ctx, ti->name);
            if (sd && sd->is_class) {
                fprintf(ctx->h_file, "extern %s *const %s;\n", ti->name, decl->data.extern_let.name);
            } else {
                fprintf(ctx->h_file, "extern const %s %s;\n", ti->name, decl->data.extern_let.name);
            }
        } else {
            fprintf(ctx->h_file, "extern const %s %s;\n", type_to_c(ti->kind), decl->data.extern_let.name);
        }
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

/* Generate collection helper functions for all struct-like types */
void gen_collection_helpers(CodegenContext *ctx) {
    /* Pass 1: Forward declarations for all helpers */
    for (int _bi = 0; _bi < STRUCT_BUCKETS; _bi++)
    for (StructDef *sd = ctx->sem_ctx->struct_buckets[_bi]; sd; sd = sd->next) {
        const char *name = sd->name;
        bool is_class = sd->is_class;
        bool is_value = !sd->is_class;

        if (is_class) {
            cg_emitf(ctx, "static void __zn_ret_%s(void *p);\n", name);
            cg_emitf(ctx, "static void __zn_rel_%s(void *p);\n", name);
        }
        if (is_value) {
            cg_emitf(ctx, "static void __zn_val_rel_%s(void *p);\n", name);
        }
        cg_emitf(ctx, "static unsigned int __zn_hash_%s(ZnValue v);\n", name);
        cg_emitf(ctx, "static bool __zn_eq_%s(ZnValue a, ZnValue b);\n", name);
    }
    cg_emit(ctx, "\n");

    /* Pass 2: Implementations */
    for (int _bi = 0; _bi < STRUCT_BUCKETS; _bi++)
    for (StructDef *sd = ctx->sem_ctx->struct_buckets[_bi]; sd; sd = sd->next) {
        const char *name = sd->name;
        bool is_class = sd->is_class;
        bool is_value = !sd->is_class;
        StructFieldDef *fields = sd->fields;
        int field_count = sd->field_count;

        /* Retain/release wrappers for reference types */
        if (is_class) {
            cg_emitf(ctx, "static void __zn_ret_%s(void *p) { __%s_retain((%s*)p); }\n", name, name, name);
            cg_emitf(ctx, "static void __zn_rel_%s(void *p) { __%s_release((%s*)p); }\n", name, name, name);
        }

        /* Value-type release (free heap copy + release ref-counted fields) */
        if (is_value) {
            cg_emitf(ctx, "static void __zn_val_rel_%s(void *p) {\n", name);
            cg_emitf(ctx, "    %s *self = (%s*)p;\n", name, name);
            for (StructFieldDef *fd = fields; fd; fd = fd->next) {
                Type *ft = fd->type;
                if (!ft) continue;
                if (ft->kind == TK_STRING) {
                    cg_emitf(ctx, "    __zn_str_release(self->%s);\n", fd->name);
                } else if (ft->kind == TK_ARRAY) {
                    cg_emitf(ctx, "    __zn_arr_release(self->%s);\n", fd->name);
                } else if (ft->kind == TK_HASH) {
                    cg_emitf(ctx, "    __zn_hash_release(self->%s);\n", fd->name);
                } else if (ft->kind == TK_CLASS && ft->name) {
                    cg_emitf(ctx, "    __%s_release(self->%s);\n", ft->name, fd->name);
                } else if (ft->kind == TK_STRUCT && ft->name) {
                    StructDef *inner = lookup_struct(ctx->sem_ctx, ft->name);
                    if (inner) {
                        char nested[256];
                        snprintf(nested, sizeof(nested), "self->%s.", fd->name);
                        /* Reuse emit_nested_releases for recursive struct fields */
                        emit_nested_releases(ctx, nested, inner);
                    }
                }
            }
            cg_emit(ctx, "    free(self);\n");
            cg_emit(ctx, "}\n");
        }

        /* Hashcode — field-by-field djb2 */
        cg_emitf(ctx, "static unsigned int __zn_hash_%s(ZnValue v) {\n", name);
        cg_emitf(ctx, "    %s *self = (%s*)v.as.ptr;\n", name, name);
        cg_emit(ctx, "    unsigned int h = 5381;\n");
        for (StructFieldDef *fd = fields; fd; fd = fd->next) {
            Type *ft = fd->type;
            if (!ft) continue;
            const char *fname = fd->name;
            if (ft->kind == TK_INT) {
                cg_emitf(ctx, "    h = ((h << 5) + h) ^ (unsigned int)((uint64_t)self->%s ^ ((uint64_t)self->%s >> 32));\n", fname, fname);
            } else if (ft->kind == TK_FLOAT) {
                cg_emitf(ctx, "    { union { double d; uint64_t u; } __cv; __cv.d = self->%s; h = ((h << 5) + h) ^ (unsigned int)(__cv.u ^ (__cv.u >> 32)); }\n", fname);
            } else if (ft->kind == TK_BOOL) {
                cg_emitf(ctx, "    h = ((h << 5) + h) ^ (self->%s ? 1u : 0u);\n", fname);
            } else if (ft->kind == TK_CHAR) {
                cg_emitf(ctx, "    h = ((h << 5) + h) ^ (unsigned int)self->%s;\n", fname);
            } else if (ft->kind == TK_STRING) {
                cg_emitf(ctx, "    { ZnValue __sv = __zn_val_string(self->%s); h = ((h << 5) + h) ^ __zn_val_hashcode(__sv); }\n", fname);
            } else if (ft->kind == TK_CLASS && ft->name) {
                /* Class field: hash pointer identity */
                cg_emitf(ctx, "    h = ((h << 5) + h) ^ (unsigned int)((uintptr_t)self->%s);\n", fname);
            } else if (ft->kind == TK_STRUCT && ft->name) {
                /* Value type field: hash content */
                cg_emitf(ctx, "    { ZnValue __sv; __sv.tag = ZN_TAG_VAL; __sv.as.ptr = &self->%s; h = ((h << 5) + h) ^ __zn_hash_%s(__sv); }\n", fname, ft->name);
            } else if (ft->kind == TK_ARRAY || ft->kind == TK_HASH) {
                /* Array/Hash: hash pointer identity */
                cg_emitf(ctx, "    h = ((h << 5) + h) ^ (unsigned int)((uintptr_t)self->%s);\n", fname);
            }
        }
        cg_emit(ctx, "    return h;\n");
        cg_emit(ctx, "}\n");

        /* Equality — field-by-field */
        cg_emitf(ctx, "static bool __zn_eq_%s(ZnValue a, ZnValue b) {\n", name);
        cg_emitf(ctx, "    %s *pa = (%s*)a.as.ptr, *pb = (%s*)b.as.ptr;\n", name, name, name);
        cg_emit(ctx, "    return ");
        int fi = 0;
        for (StructFieldDef *fd = fields; fd; fd = fd->next, fi++) {
            if (fi > 0) cg_emit(ctx, " && ");
            Type *ft = fd->type;
            const char *fname = fd->name;
            if (ft && ft->kind == TK_STRING) {
                cg_emitf(ctx, "__zn_val_eq(__zn_val_string(pa->%s), __zn_val_string(pb->%s))", fname, fname);
            } else if (ft && ft->kind == TK_CLASS && ft->name) {
                /* Class field: pointer equality */
                cg_emitf(ctx, "pa->%s == pb->%s", fname, fname);
            } else if (ft && ft->kind == TK_STRUCT && ft->name) {
                /* Value type: content equality */
                cg_emitf(ctx, "({ ZnValue __a, __b; __a.as.ptr = &pa->%s; __b.as.ptr = &pb->%s; __zn_eq_%s(__a, __b); })", fname, fname, ft->name);
            } else if (ft && (ft->kind == TK_ARRAY || ft->kind == TK_HASH)) {
                cg_emitf(ctx, "pa->%s == pb->%s", fname, fname);
            } else {
                cg_emitf(ctx, "pa->%s == pb->%s", fname, fname);
            }
        }
        if (field_count == 0) cg_emit(ctx, "true");
        cg_emit(ctx, ";\n");
        cg_emit(ctx, "}\n\n");
    }
}
