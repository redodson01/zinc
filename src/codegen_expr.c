#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "codegen.h"

/* --- Collection callback helpers --- */

static void emit_elem_retain_cb(CodegenContext *ctx, Type *elem) {
    if (!elem) { cg_emit(ctx, "NULL"); return; }
    if (elem->kind == TK_STRING) { cg_emit(ctx, "(ZnElemFn)__zn_str_retain_v"); }
    else if (elem->kind == TK_ARRAY) { cg_emit(ctx, "(ZnElemFn)__zn_arr_retain_v"); }
    else if (elem->kind == TK_HASH) { cg_emit(ctx, "(ZnElemFn)__zn_hash_retain_v"); }
    else if (elem->kind == TK_CLASS && elem->name) {
        cg_emitf(ctx, "(ZnElemFn)__zn_ret_%s", elem->name);
    } else { cg_emit(ctx, "NULL"); }
}

static void emit_elem_release_cb(CodegenContext *ctx, Type *elem) {
    if (!elem) { cg_emit(ctx, "NULL"); return; }
    if (elem->kind == TK_STRING) { cg_emit(ctx, "(ZnElemFn)__zn_str_release_v"); }
    else if (elem->kind == TK_ARRAY) { cg_emit(ctx, "(ZnElemFn)__zn_arr_release_v"); }
    else if (elem->kind == TK_HASH) { cg_emit(ctx, "(ZnElemFn)__zn_hash_release_v"); }
    else if (elem->kind == TK_CLASS && elem->name) {
        cg_emitf(ctx, "(ZnElemFn)__zn_rel_%s", elem->name);
    } else if (elem->kind == TK_STRUCT && elem->name) {
        cg_emitf(ctx, "(ZnElemFn)__zn_val_rel_%s", elem->name);
    } else { cg_emit(ctx, "NULL"); }
}

static void emit_hashcode_cb(CodegenContext *ctx, Type *elem) {
    if (!elem) { cg_emit(ctx, "__zn_default_hashcode"); return; }
    if (elem->kind == TK_STRUCT && elem->name) {
        cg_emitf(ctx, "__zn_hash_%s", elem->name);
    } else { cg_emit(ctx, "__zn_default_hashcode"); }
}

static void emit_equals_cb(CodegenContext *ctx, Type *elem) {
    if (!elem) { cg_emit(ctx, "__zn_default_equals"); return; }
    if (elem->kind == TK_STRUCT && elem->name) {
        cg_emitf(ctx, "__zn_eq_%s", elem->name);
    } else { cg_emit(ctx, "__zn_default_equals"); }
}

static void emit_arr_callbacks(CodegenContext *ctx, Type *elem) {
    cg_emit(ctx, ", ");
    emit_elem_retain_cb(ctx, elem);
    cg_emit(ctx, ", ");
    emit_elem_release_cb(ctx, elem);
    cg_emit(ctx, ", ");
    emit_hashcode_cb(ctx, elem);
    cg_emit(ctx, ", ");
    emit_equals_cb(ctx, elem);
}

static void emit_hash_callbacks(CodegenContext *ctx, Type *key, Type *val) {
    cg_emit(ctx, ", ");
    emit_elem_retain_cb(ctx, key);
    cg_emit(ctx, ", ");
    emit_elem_release_cb(ctx, key);
    cg_emit(ctx, ", ");
    emit_hashcode_cb(ctx, key);
    cg_emit(ctx, ", ");
    emit_equals_cb(ctx, key);
    cg_emit(ctx, ", ");
    emit_elem_retain_cb(ctx, val);
    cg_emit(ctx, ", ");
    emit_elem_release_cb(ctx, val);
}

/* Emit a retain call for a named variable of the given ref type.
   Skips if value is a fresh allocation. */
static void emit_retain(CodegenContext *ctx, const char *name, ASTNode *value, Type *type) {
    if (!type || (value && value->is_fresh_alloc)) return;
    if (is_ref_type(type->kind)) {
        cg_emit_indent(ctx);
        emit_retain_call(ctx, name, type);
        cg_emit(ctx, ";\n");
    }
}

/* Emit an inline retain for an expression temp (no newline/indent).
   Used inside GCC statement expressions for if/break/continue results. */
static void emit_inline_retain(CodegenContext *ctx, int temp_id, const char *prefix,
                                ASTNode *value, Type *type) {
    if (!type || !value || !is_ref_type(type->kind) || value->is_fresh_alloc) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "%s%d", prefix, temp_id);
    emit_retain_call(ctx, buf, type);
    cg_emit(ctx, "; ");
}

/* Check if a struct has any ref-counted fields (recursively) */
static int struct_has_rc_fields(StructDef *sd, SemanticContext *sem_ctx) {
    for (StructFieldDef *fd = sd->fields; fd; fd = fd->next) {
        if (!fd->type) continue;
        if (fd->type->kind == TK_STRING || fd->type->kind == TK_ARRAY ||
            fd->type->kind == TK_HASH || fd->type->kind == TK_CLASS) return 1;
        if (fd->type->kind == TK_STRUCT && fd->type->name) {
            StructDef *inner = lookup_struct(sem_ctx, fd->type->name);
            if (inner && struct_has_rc_fields(inner, sem_ctx)) return 1;
        }
    }
    return 0;
}

/* Emit a temp variable declaration for a ref type (handles class pointer types) */
static void emit_ref_temp_decl(CodegenContext *ctx, const char *name, Type *type) {
    if (type->kind == TK_CLASS && type->name) {
        cg_emitf(ctx, "%s *%s = ", type->name, name);
    } else {
        cg_emitf(ctx, "%s %s = ", type_to_c(type->kind), name);
    }
}

/* Add a ref-type variable to the ARC scope for release tracking. */
static void scope_track_ref(CodegenContext *ctx, const char *name, Type *type) {
    if (!ctx->scope || !type) return;
    switch (type->kind) {
    case TK_STRING: cg_scope_add_ref(ctx, name, "zn_str"); break;
    case TK_CLASS:  if (type->name) cg_scope_add_ref(ctx, name, type->name); break;
    case TK_ARRAY:  cg_scope_add_ref(ctx, name, "zn_arr"); break;
    case TK_HASH:   cg_scope_add_ref(ctx, name, "zn_hash"); break;
    case TK_STRUCT:
        if (type->name) {
            StructDef *sd = lookup_struct(ctx->sem_ctx, type->name);
            if (sd && struct_has_rc_fields(sd, ctx->sem_ctx))
                cg_scope_add_value_type(ctx, name, type->name);
        }
        break;
    default: break;
    }
}

/* Get the unboxing function name for a given type */
const char *unbox_func_for(TypeKind t) {
    switch (t) {
    case TK_INT:    return "__zn_val_as_int";
    case TK_FLOAT:  return "__zn_val_as_float";
    case TK_BOOL:   return "__zn_val_as_bool";
    case TK_CHAR:   return "__zn_val_as_char";
    case TK_STRING: return "__zn_val_as_string";
    default:        return "__zn_val_as_int";
    }
}

/* Box an expression into a ZnValue */
void gen_box_expr(CodegenContext *ctx, ASTNode *expr) {
    TypeKind t = expr->resolved_type->kind;
    switch (t) {
    case TK_INT:    cg_emit(ctx, "__zn_val_int("); gen_expr(ctx, expr); cg_emit(ctx, ")"); break;
    case TK_FLOAT:  cg_emit(ctx, "__zn_val_float("); gen_expr(ctx, expr); cg_emit(ctx, ")"); break;
    case TK_BOOL:   cg_emit(ctx, "__zn_val_bool("); gen_expr(ctx, expr); cg_emit(ctx, ")"); break;
    case TK_CHAR:   cg_emit(ctx, "__zn_val_char("); gen_expr(ctx, expr); cg_emit(ctx, ")"); break;
    case TK_STRING: cg_emit(ctx, "__zn_val_string("); gen_expr(ctx, expr); cg_emit(ctx, ")"); break;
    case TK_ARRAY:  cg_emit(ctx, "__zn_val_array((ZnArray*)("); gen_expr(ctx, expr); cg_emit(ctx, "))"); break;
    case TK_HASH:   cg_emit(ctx, "__zn_val_hash((ZnHash*)("); gen_expr(ctx, expr); cg_emit(ctx, "))"); break;
    case TK_CLASS:
        /* Class (reference type): wrap pointer */
        cg_emit(ctx, "__zn_val_ref("); gen_expr(ctx, expr); cg_emit(ctx, ")");
        break;
    case TK_STRUCT:
        if (expr->resolved_type->name) {
            /* Value type (struct/tuple): heap-copy then wrap */
            const char *name = expr->resolved_type->name;
            cg_emitf(ctx, "__zn_val_val(({ %s *__cp = malloc(sizeof(%s)); *__cp = (", name, name);
            gen_expr(ctx, expr);
            cg_emit(ctx, "); __cp; }))");
        } else {
            cg_emit(ctx, "__zn_val_int((int64_t)("); gen_expr(ctx, expr); cg_emit(ctx, "))");
        }
        break;
    default:        cg_emit(ctx, "__zn_val_int((int64_t)("); gen_expr(ctx, expr); cg_emit(ctx, "))"); break;
    }
}

/* Emit the for loop header: for (init; cond; update) */
void gen_for_header(CodegenContext *ctx, ASTNode *node) {
    cg_emit(ctx, "for (");
    ASTNode *init = node->data.for_expr.init;
    if (init) {
        if (init->type == NODE_DECL) {
            TypeKind t = init->data.decl.value->resolved_type->kind;
            if (init->data.decl.is_const)
                cg_emitf(ctx, "const %s %s = ", type_to_c(t), init->data.decl.name);
            else
                cg_emitf(ctx, "%s %s = ", type_to_c(t), init->data.decl.name);
            gen_expr(ctx, init->data.decl.value);
        } else {
            gen_expr(ctx, init);
        }
    }
    cg_emit(ctx, "; ");
    gen_expr(ctx, node->data.for_expr.cond);
    cg_emit(ctx, "; ");
    if (node->data.for_expr.update) {
        gen_expr(ctx, node->data.for_expr.update);
    }
    cg_emit(ctx, ") ");
}

/* Generate string comparison using strcmp */
void gen_string_comparison(CodegenContext *ctx, ASTNode *left, const char *op, ASTNode *right) {
    cg_emit(ctx, "(strcmp((");
    gen_expr(ctx, left);
    cg_emit(ctx, ")->_data, (");
    gen_expr(ctx, right);
    cg_emitf(ctx, ")->_data) %s 0)", op);
}

/* Emit coercion wrapper for non-string operand in concat */
void gen_coerce_to_string(CodegenContext *ctx, ASTNode *expr) {
    TypeKind t = expr->resolved_type->kind;
    if (t == TK_STRING) {
        gen_expr(ctx, expr);
        return;
    }
    switch (t) {
    case TK_INT:   cg_emit(ctx, "__zn_str_from_int("); break;
    case TK_FLOAT: cg_emit(ctx, "__zn_str_from_float("); break;
    case TK_BOOL:  cg_emit(ctx, "__zn_str_from_bool("); break;
    case TK_CHAR:  cg_emit(ctx, "__zn_str_from_char("); break;
    default:       gen_expr(ctx, expr); return;
    }
    gen_expr(ctx, expr);
    cg_emit(ctx, ")");
}

/* Count string concat operands */
static int count_string_concat_parts(ASTNode *expr) {
    if (!expr) return 0;
    if (expr->type == NODE_BINOP && expr->resolved_type && expr->resolved_type->kind == TK_STRING
        && expr->data.binop.op == OP_ADD) {
        return count_string_concat_parts(expr->data.binop.left) +
               count_string_concat_parts(expr->data.binop.right);
    }
    return 1;
}

/* Flatten a string concat tree into a linear sequence of leaves */
static void flatten_string_concat(ASTNode *expr, ASTNode **leaves, int *count) {
    if (expr->type == NODE_BINOP && expr->resolved_type && expr->resolved_type->kind == TK_STRING
        && expr->data.binop.op == OP_ADD) {
        flatten_string_concat(expr->data.binop.left, leaves, count);
        flatten_string_concat(expr->data.binop.right, leaves, count);
    } else {
        leaves[(*count)++] = expr;
    }
}

/* Generate string concatenation using GCC statement expressions.
   Releases intermediate temporaries. */
void gen_string_concat(CodegenContext *ctx, ASTNode *expr) {
    int n = count_string_concat_parts(expr);
    ASTNode **leaves = malloc(n * sizeof(ASTNode *));
    int leaf_count = 0;
    flatten_string_concat(expr, leaves, &leaf_count);

    cg_emit(ctx, "({ ");

    /* Pre-evaluate non-string leaves into coercion temps */
    int *coerce_temp = malloc(leaf_count * sizeof(int));
    for (int i = 0; i < leaf_count; i++) {
        if (leaves[i]->resolved_type->kind != TK_STRING) {
            int c = ctx->temp_counter++;
            coerce_temp[i] = c;
            cg_emitf(ctx, "ZnString *__c%d = ", c);
            gen_coerce_to_string(ctx, leaves[i]);
            cg_emit(ctx, "; ");
        } else {
            coerce_temp[i] = -1;
        }
    }

    int base_temp = ctx->temp_counter;
    ctx->temp_counter += leaf_count - 1;

    for (int i = 0; i < leaf_count - 1; i++) {
        int t = base_temp + i;
        cg_emitf(ctx, "ZnString *__t%d = __zn_str_concat(", t);
        if (i == 0) {
            if (coerce_temp[0] >= 0)
                cg_emitf(ctx, "__c%d", coerce_temp[0]);
            else
                gen_expr(ctx, leaves[0]);
        } else {
            cg_emitf(ctx, "__t%d", t - 1);
        }
        cg_emit(ctx, ", ");
        if (coerce_temp[i + 1] >= 0)
            cg_emitf(ctx, "__c%d", coerce_temp[i + 1]);
        else
            gen_expr(ctx, leaves[i + 1]);
        cg_emit(ctx, "); ");

        /* Release coerced non-string temps */
        if (i == 0 && coerce_temp[0] >= 0) {
            cg_emitf(ctx, "__zn_str_release(__c%d); ", coerce_temp[0]);
        }
        if (coerce_temp[i + 1] >= 0) {
            cg_emitf(ctx, "__zn_str_release(__c%d); ", coerce_temp[i + 1]);
        }

        /* Release previous intermediate */
        if (i > 0) {
            cg_emitf(ctx, "__zn_str_release(__t%d); ", t - 1);
        }
    }

    cg_emitf(ctx, "__t%d; })", base_temp + leaf_count - 2);
    free(coerce_temp);
    free(leaves);
}

void gen_block_with_scope(CodegenContext *ctx, ASTNode *block, int is_loop) {
    if (!block || block->type != NODE_BLOCK) return;
    cg_emit(ctx, "{\n");
    ctx->indent_level++;
    cg_push_scope(ctx, is_loop);
    gen_stmts(ctx, block->data.block.stmts);
    emit_scope_releases(ctx);
    cg_pop_scope(ctx);
    ctx->indent_level--;
    cg_emit_indent(ctx);
    cg_emit(ctx, "}");
}

void gen_block(CodegenContext *ctx, ASTNode *block) {
    gen_block_with_scope(ctx, block, 0);
}

void gen_expr(CodegenContext *ctx, ASTNode *expr) {
    if (!expr) return;

    switch (expr->type) {
    case NODE_INT:
        cg_emitf(ctx, "%lld", (long long)expr->data.ival);
        break;
    case NODE_FLOAT:
        cg_emitf(ctx, "%g", expr->data.dval);
        break;
    case NODE_STRING:
        cg_emitf(ctx, "(ZnString*)&__zn_str_%d", expr->string_id);
        break;
    case NODE_BOOL:
        cg_emit(ctx, expr->data.bval ? "true" : "false");
        break;
    case NODE_CHAR:
        cg_emit(ctx, "'");
        switch (expr->data.cval) {
        case '\n': cg_emit(ctx, "\\n"); break;
        case '\t': cg_emit(ctx, "\\t"); break;
        case '\r': cg_emit(ctx, "\\r"); break;
        case '\\': cg_emit(ctx, "\\\\"); break;
        case '\'': cg_emit(ctx, "\\'"); break;
        case '\0': cg_emit(ctx, "\\0"); break;
        default:   cg_emitf(ctx, "%c", expr->data.cval); break;
        }
        cg_emit(ctx, "'");
        break;
    case NODE_IDENT: {
        const char *id_name = expr->data.ident.name;
        /* Check if this variable is narrowed (optional value type) */
        int is_narrowed = 0;
        for (CGNarrow *n = ctx->narrowed; n; n = n->next) {
            if (strcmp(n->name, id_name) == 0) { is_narrowed = 1; break; }
        }
        if (is_narrowed) {
            cg_emitf(ctx, "%s._val", id_name);
        } else {
            cg_emit(ctx, id_name);
        }
        break;
    }
    case NODE_BINOP: {
        OpKind op = expr->data.binop.op;
        int is_comparison = (op == OP_EQ || op == OP_NE ||
                            op == OP_LT || op == OP_GT ||
                            op == OP_LE || op == OP_GE);

        /* String concatenation */
        if (op == OP_ADD && expr->resolved_type && expr->resolved_type->kind == TK_STRING) {
            gen_string_concat(ctx, expr);
            break;
        }

        /* String comparison */
        if (is_comparison && (expr_is_string(expr->data.binop.left) ||
                             expr_is_string(expr->data.binop.right))) {
            gen_string_comparison(ctx, expr->data.binop.left, op_to_str(op), expr->data.binop.right);
        } else {
            cg_emit(ctx, "(");
            gen_expr(ctx, expr->data.binop.left);
            cg_emitf(ctx, " %s ", op_to_str(op));
            gen_expr(ctx, expr->data.binop.right);
            cg_emit(ctx, ")");
        }
        break;
    }
    case NODE_UNARYOP:
        cg_emit(ctx, "(");
        cg_emit(ctx, op_to_str(expr->data.unaryop.op));
        gen_expr(ctx, expr->data.unaryop.operand);
        cg_emit(ctx, ")");
        break;
    case NODE_ASSIGN:
        gen_expr(ctx, expr->data.assign.target);
        cg_emit(ctx, " = ");
        gen_expr(ctx, expr->data.assign.value);
        break;
    case NODE_COMPOUND_ASSIGN:
        gen_expr(ctx, expr->data.compound_assign.target);
        cg_emitf(ctx, " %s ", op_to_str(expr->data.compound_assign.op));
        gen_expr(ctx, expr->data.compound_assign.value);
        break;
    case NODE_INCDEC:
        if (expr->data.incdec.is_prefix) {
            cg_emit(ctx, op_to_str(expr->data.incdec.op));
            gen_expr(ctx, expr->data.incdec.target);
        } else {
            gen_expr(ctx, expr->data.incdec.target);
            cg_emit(ctx, op_to_str(expr->data.incdec.op));
        }
        break;
    case NODE_CALL: {
        /* Built-in print function */
        if (strcmp(expr->data.call.name, "print") == 0) {
            cg_emit(ctx, "({ fputs((");
            if (expr->data.call.args)
                gen_expr(ctx, expr->data.call.args->node);
            cg_emit(ctx, ")->_data, stdout); })");
            break;
        }
        if (expr->data.call.is_struct_init) {
            const char *name = expr->data.call.name;
            StructDef *sd = lookup_struct(ctx->sem_ctx, name);

            if (sd && sd->is_class) {
                /* Class init: heap allocate via __ClassName_alloc() */
                int t = ctx->temp_counter++;
                cg_emitf(ctx, "({ %s *__ci_%d = __%s_alloc(); ", name, t, name);

                for (StructFieldDef *fd = sd->fields; fd; fd = fd->next) {
                    ASTNode *val = NULL;
                    for (NodeList *arg = expr->data.call.args; arg; arg = arg->next) {
                        if (arg->node->type == NODE_NAMED_ARG &&
                            strcmp(arg->node->data.named_arg.name, fd->name) == 0) {
                            val = arg->node->data.named_arg.value;
                            break;
                        }
                    }

                    cg_emitf(ctx, "__ci_%d->%s = ", t, fd->name);
                    if (val) {
                        gen_expr(ctx, val);
                    } else if (fd->default_value) {
                        gen_expr(ctx, fd->default_value);
                    } else {
                        cg_emit(ctx, "0");
                    }
                    cg_emit(ctx, "; ");

                    /* Retain reference-type fields */
                    if (is_ref_type(fd->type->kind)) {
                        if (!val || !val->is_fresh_alloc) {
                            char buf[128];
                            snprintf(buf, sizeof(buf), "__ci_%d->%s", t, fd->name);
                            emit_retain_call(ctx, buf, fd->type);
                            cg_emit(ctx, "; ");
                        }
                    }
                }

                cg_emitf(ctx, "__ci_%d; })", t);
            } else {
                /* Struct init: value type with C99 designators */
                /* Check if any field needs ARC retain */
                int needs_arc = 0;
                for (StructFieldDef *fd = sd->fields; fd; fd = fd->next) {
                    if (fd->type && is_ref_type(fd->type->kind)) {
                        needs_arc = 1;
                        break;
                    }
                }

                if (needs_arc) {
                    int t = ctx->temp_counter++;
                    cg_emitf(ctx, "({ %s __vt%d = (%s){", expr->data.call.name, t, expr->data.call.name);
                    int first = 1;
                    for (StructFieldDef *fd = sd->fields; fd; fd = fd->next) {
                        if (!first) cg_emit(ctx, ", ");
                        cg_emitf(ctx, ".%s = ", fd->name);
                        int found = 0;
                        for (NodeList *arg = expr->data.call.args; arg; arg = arg->next) {
                            if (arg->node->type == NODE_NAMED_ARG &&
                                strcmp(arg->node->data.named_arg.name, fd->name) == 0) {
                                gen_expr(ctx, arg->node->data.named_arg.value);
                                found = 1;
                                break;
                            }
                        }
                        if (!found && fd->default_value) {
                            gen_expr(ctx, fd->default_value);
                        } else if (!found) {
                            cg_emit(ctx, "0");
                        }
                        first = 0;
                    }
                    cg_emit(ctx, "}; ");
                    /* Emit retains for ref-counted fields */
                    for (StructFieldDef *fd = sd->fields; fd; fd = fd->next) {
                        if (!fd->type) continue;
                        ASTNode *val = NULL;
                        for (NodeList *arg = expr->data.call.args; arg; arg = arg->next) {
                            if (arg->node->type == NODE_NAMED_ARG &&
                                strcmp(arg->node->data.named_arg.name, fd->name) == 0) {
                                val = arg->node->data.named_arg.value;
                                break;
                            }
                        }
                        if (is_ref_type(fd->type->kind) && !(val && val->is_fresh_alloc)) {
                            char buf[128];
                            snprintf(buf, sizeof(buf), "__vt%d.%s", t, fd->name);
                            emit_retain_call(ctx, buf, fd->type);
                            cg_emit(ctx, "; ");
                        }
                    }
                    cg_emitf(ctx, "__vt%d; })", t);
                } else {
                    cg_emitf(ctx, "(%s){", expr->data.call.name);
                    int first = 1;
                    for (StructFieldDef *fd = sd->fields; fd; fd = fd->next) {
                        if (!first) cg_emit(ctx, ", ");
                        cg_emitf(ctx, ".%s = ", fd->name);
                        int found = 0;
                        for (NodeList *arg = expr->data.call.args; arg; arg = arg->next) {
                            if (arg->node->type == NODE_NAMED_ARG &&
                                strcmp(arg->node->data.named_arg.name, fd->name) == 0) {
                                gen_expr(ctx, arg->node->data.named_arg.value);
                                found = 1;
                                break;
                            }
                        }
                        if (!found && fd->default_value) {
                            gen_expr(ctx, fd->default_value);
                        } else if (!found) {
                            cg_emit(ctx, "0");
                        }
                        first = 0;
                    }
                    cg_emit(ctx, "}");
                }
            }
        } else {
            /* Regular function call */
            cg_emit(ctx, expr->data.call.name);
            cg_emit(ctx, "(");
            Symbol *func_sym = lookup(ctx->sem_ctx, expr->data.call.name);
            int first = 1;
            int arg_idx = 0;
            for (NodeList *arg = expr->data.call.args; arg; arg = arg->next) {
                if (!first) cg_emit(ctx, ", ");
                /* Wrap non-optional value in optional struct if param expects it */
                int wrap_opt = 0;
                const char *opt_wrap = NULL;
                if (func_sym && arg_idx < func_sym->param_count &&
                    func_sym->param_types[arg_idx] &&
                    func_sym->param_types[arg_idx]->is_optional) {
                    Type *at = arg->node->resolved_type;
                    if (!at || !at->is_optional) {
                        opt_wrap = opt_type_for(func_sym->param_types[arg_idx]->kind);
                        if (opt_wrap) wrap_opt = 1;
                    }
                }
                if (wrap_opt) {
                    cg_emitf(ctx, "(%s){._has = true, ._val = ", opt_wrap);
                    gen_expr(ctx, arg->node);
                    cg_emit(ctx, "}");
                } else {
                    gen_expr(ctx, arg->node);
                }
                first = 0;
                arg_idx++;
            }
            cg_emit(ctx, ")");
        }
        break;
    }
    case NODE_FIELD_ACCESS:
        /* String .length */
        if (expr->data.field_access.object->resolved_type &&
            expr->data.field_access.object->resolved_type->kind == TK_STRING &&
            strcmp(expr->data.field_access.field, "length") == 0) {
            cg_emit(ctx, "(int64_t)((");
            gen_expr(ctx, expr->data.field_access.object);
            cg_emit(ctx, ")->_len)");
            break;
        }
        /* Array/Hash .length */
        if (expr->data.field_access.object->resolved_type &&
            (expr->data.field_access.object->resolved_type->kind == TK_ARRAY ||
             expr->data.field_access.object->resolved_type->kind == TK_HASH) &&
            strcmp(expr->data.field_access.field, "length") == 0) {
            cg_emit(ctx, "(int64_t)((");
            gen_expr(ctx, expr->data.field_access.object);
            cg_emit(ctx, ")->_len)");
            break;
        }
        /* Struct/class field access: -> for classes, . for value types */
        gen_expr(ctx, expr->data.field_access.object);
        if (expr->data.field_access.object->resolved_type &&
            expr->data.field_access.object->resolved_type->kind == TK_CLASS) {
            cg_emitf(ctx, "->%s", expr->data.field_access.field);
        } else {
            cg_emitf(ctx, ".%s", expr->data.field_access.field);
        }
        break;
    case NODE_TUPLE: {
        const char *name = expr->resolved_type ? expr->resolved_type->name : NULL;
        StructDef *sd = lookup_struct(ctx->sem_ctx, name);

        /* Check if any field needs ARC retain */
        int needs_arc = 0;
        if (sd) {
            for (StructFieldDef *fd = sd->fields; fd; fd = fd->next) {
                if (fd->type && is_ref_type(fd->type->kind)) {
                    needs_arc = 1;
                    break;
                }
            }
        }

        if (needs_arc) {
            int t = ctx->temp_counter++;
            cg_emitf(ctx, "({ %s __vt%d = (%s){", name, t, name);
            if (sd) {
                StructFieldDef *fd = sd->fields;
                int first = 1;
                for (NodeList *e = expr->data.tuple.elements; e && fd; e = e->next, fd = fd->next) {
                    if (!first) cg_emit(ctx, ", ");
                    cg_emitf(ctx, ".%s = ", fd->name);
                    if (e->node->type == NODE_NAMED_ARG) {
                        gen_expr(ctx, e->node->data.named_arg.value);
                    } else {
                        gen_expr(ctx, e->node);
                    }
                    first = 0;
                }
            }
            cg_emit(ctx, "}; ");
            /* Emit retains for ref-counted fields */
            if (sd) {
                StructFieldDef *fd = sd->fields;
                for (NodeList *e = expr->data.tuple.elements; e && fd; e = e->next, fd = fd->next) {
                    ASTNode *val = (e->node->type == NODE_NAMED_ARG) ? e->node->data.named_arg.value : e->node;
                    if (is_ref_type(fd->type->kind) && !(val && val->is_fresh_alloc)) {
                        char buf[128];
                        snprintf(buf, sizeof(buf), "__vt%d.%s", t, fd->name);
                        emit_retain_call(ctx, buf, fd->type);
                        cg_emit(ctx, "; ");
                    }
                }
            }
            cg_emitf(ctx, "__vt%d; })", t);
        } else {
            cg_emitf(ctx, "(%s){", name);
            if (sd) {
                StructFieldDef *fd = sd->fields;
                int first = 1;
                for (NodeList *e = expr->data.tuple.elements; e && fd; e = e->next, fd = fd->next) {
                    if (!first) cg_emit(ctx, ", ");
                    cg_emitf(ctx, ".%s = ", fd->name);
                    if (e->node->type == NODE_NAMED_ARG) {
                        gen_expr(ctx, e->node->data.named_arg.value);
                    } else {
                        gen_expr(ctx, e->node);
                    }
                    first = 0;
                }
            }
            cg_emit(ctx, "}");
        }
        break;
    }
    case NODE_OBJECT_LITERAL: {
        const char *type_name = expr->resolved_type ? expr->resolved_type->name : NULL;
        int t = ctx->temp_counter++;
        cg_emitf(ctx, "({ %s *__t%d = __%s_alloc(); ", type_name, t, type_name);
        for (NodeList *f = expr->data.object_literal.fields; f; f = f->next) {
            ASTNode *na = f->node;
            cg_emitf(ctx, "__t%d->%s = ", t, na->data.named_arg.name);
            gen_expr(ctx, na->data.named_arg.value);
            cg_emit(ctx, "; ");

            /* Retain reference-type fields */
            Type *ftype = na->data.named_arg.value->resolved_type;
            if (ftype && is_ref_type(ftype->kind) && !na->data.named_arg.value->is_fresh_alloc) {
                char buf[128];
                snprintf(buf, sizeof(buf), "__t%d->%s", t, na->data.named_arg.name);
                emit_retain_call(ctx, buf, ftype);
                cg_emit(ctx, "; ");
            }
        }
        cg_emitf(ctx, "__t%d; })", t);
        break;
    }
    case NODE_INDEX:
        if (expr->data.index_access.object->resolved_type &&
            expr->data.index_access.object->resolved_type->kind == TK_ARRAY) {
            /* Array indexing: unbox ZnValue from __zn_arr_get */
            Type *arr_elem = expr->resolved_type;
            if (arr_elem && arr_elem->kind == TK_ARRAY) {
                /* Element is an array: cast .as.ptr */
                cg_emit(ctx, "(ZnArray*)__zn_arr_get(");
                gen_expr(ctx, expr->data.index_access.object);
                cg_emit(ctx, ", ");
                gen_expr(ctx, expr->data.index_access.index);
                cg_emit(ctx, ").as.ptr");
            } else if (arr_elem && arr_elem->kind == TK_HASH) {
                /* Element is a hash: cast .as.ptr */
                cg_emit(ctx, "(ZnHash*)__zn_arr_get(");
                gen_expr(ctx, expr->data.index_access.object);
                cg_emit(ctx, ", ");
                gen_expr(ctx, expr->data.index_access.index);
                cg_emit(ctx, ").as.ptr");
            } else if (arr_elem && arr_elem->kind == TK_CLASS && arr_elem->name) {
                /* Element is a class: cast .as.ptr to class pointer */
                cg_emitf(ctx, "(%s*)__zn_arr_get(", arr_elem->name);
                gen_expr(ctx, expr->data.index_access.object);
                cg_emit(ctx, ", ");
                gen_expr(ctx, expr->data.index_access.index);
                cg_emit(ctx, ").as.ptr");
            } else if (arr_elem && arr_elem->kind == TK_STRUCT && arr_elem->name) {
                /* Element is a value struct: dereference .as.ptr */
                cg_emitf(ctx, "*(%s*)__zn_arr_get(", arr_elem->name);
                gen_expr(ctx, expr->data.index_access.object);
                cg_emit(ctx, ", ");
                gen_expr(ctx, expr->data.index_access.index);
                cg_emit(ctx, ").as.ptr");
            } else {
                /* Primitive element: use unbox function */
                cg_emitf(ctx, "%s(", unbox_func_for(arr_elem ? arr_elem->kind : TK_UNKNOWN));
                cg_emit(ctx, "__zn_arr_get(");
                gen_expr(ctx, expr->data.index_access.object);
                cg_emit(ctx, ", ");
                gen_expr(ctx, expr->data.index_access.index);
                cg_emit(ctx, "))");
            }
        } else if (expr->data.index_access.object->resolved_type &&
                   expr->data.index_access.object->resolved_type->kind == TK_HASH) {
            /* Hash indexing: box key, unbox value from __zn_hash_get */
            Type *hash_val = expr->resolved_type;
            if (hash_val && hash_val->kind == TK_ARRAY) {
                cg_emit(ctx, "(ZnArray*)__zn_hash_get(");
                gen_expr(ctx, expr->data.index_access.object);
                cg_emit(ctx, ", ");
                gen_box_expr(ctx, expr->data.index_access.index);
                cg_emit(ctx, ").as.ptr");
            } else if (hash_val && hash_val->kind == TK_HASH) {
                cg_emit(ctx, "(ZnHash*)__zn_hash_get(");
                gen_expr(ctx, expr->data.index_access.object);
                cg_emit(ctx, ", ");
                gen_box_expr(ctx, expr->data.index_access.index);
                cg_emit(ctx, ").as.ptr");
            } else if (hash_val && hash_val->kind == TK_CLASS && hash_val->name) {
                cg_emitf(ctx, "(%s*)__zn_hash_get(", hash_val->name);
                gen_expr(ctx, expr->data.index_access.object);
                cg_emit(ctx, ", ");
                gen_box_expr(ctx, expr->data.index_access.index);
                cg_emit(ctx, ").as.ptr");
            } else if (hash_val && hash_val->kind == TK_STRUCT && hash_val->name) {
                cg_emitf(ctx, "*(%s*)__zn_hash_get(", hash_val->name);
                gen_expr(ctx, expr->data.index_access.object);
                cg_emit(ctx, ", ");
                gen_box_expr(ctx, expr->data.index_access.index);
                cg_emit(ctx, ").as.ptr");
            } else {
                cg_emitf(ctx, "%s(", unbox_func_for(hash_val ? hash_val->kind : TK_UNKNOWN));
                cg_emit(ctx, "__zn_hash_get(");
                gen_expr(ctx, expr->data.index_access.object);
                cg_emit(ctx, ", ");
                gen_box_expr(ctx, expr->data.index_access.index);
                cg_emit(ctx, "))");
            }
        } else {
            /* String indexing */
            cg_emit(ctx, "(");
            gen_expr(ctx, expr->data.index_access.object);
            cg_emit(ctx, ")->_data[");
            gen_expr(ctx, expr->data.index_access.index);
            cg_emit(ctx, "]");
        }
        break;
    case NODE_ARRAY_LITERAL: {
        int n = 0;
        for (NodeList *e = expr->data.array_literal.elems; e; e = e->next) n++;
        int t = ctx->temp_counter++;
        cg_emitf(ctx, "({ ZnArray *__t%d = __zn_arr_alloc(%d", t, n > 0 ? n : 4);
        emit_arr_callbacks(ctx, expr->resolved_type ? expr->resolved_type->elem : NULL);
        cg_emit(ctx, "); ");
        for (NodeList *e = expr->data.array_literal.elems; e; e = e->next) {
            ASTNode *elem = e->node;
            if (elem->resolved_type && is_ref_type(elem->resolved_type->kind) && elem->is_fresh_alloc) {
                /* Fresh ref-type: pre-evaluate into temp, push (retains), then release temp */
                int pt = ctx->temp_counter++;
                char pname[64];
                snprintf(pname, sizeof(pname), "__pe%d", pt);
                emit_ref_temp_decl(ctx, pname, elem->resolved_type);
                gen_expr(ctx, elem);
                cg_emitf(ctx, "; __zn_arr_push(__t%d, ", t);
                emit_box_call(ctx, pname, elem->resolved_type);
                cg_emit(ctx, "); ");
                emit_release_call(ctx, pname, elem->resolved_type);
                cg_emit(ctx, "; ");
            } else {
                cg_emitf(ctx, "__zn_arr_push(__t%d, ", t);
                gen_box_expr(ctx, elem);
                cg_emit(ctx, "); ");
            }
        }
        cg_emitf(ctx, "__t%d; })", t);
        break;
    }
    case NODE_HASH_LITERAL: {
        int n = 0;
        for (NodeList *p = expr->data.hash_literal.pairs; p; p = p->next) n++;
        int t = ctx->temp_counter++;
        cg_emitf(ctx, "({ ZnHash *__t%d = __zn_hash_alloc(%d", t, n > 0 ? n * 2 : 8);
        emit_hash_callbacks(ctx,
            expr->resolved_type ? expr->resolved_type->key : NULL,
            expr->resolved_type ? expr->resolved_type->elem : NULL);
        cg_emit(ctx, "); ");
        for (NodeList *p = expr->data.hash_literal.pairs; p; p = p->next) {
            ASTNode *pair = p->node;
            ASTNode *hk = pair->data.hash_pair.key;
            ASTNode *hv = pair->data.hash_pair.value;
            int fresh_key = hk->resolved_type && is_ref_type(hk->resolved_type->kind) && hk->is_fresh_alloc;
            int fresh_val = hv->resolved_type && is_ref_type(hv->resolved_type->kind) && hv->is_fresh_alloc;
            char kname[64] = {0}, vname[64] = {0};
            if (fresh_key) {
                int kt = ctx->temp_counter++;
                snprintf(kname, sizeof(kname), "__pk%d", kt);
                emit_ref_temp_decl(ctx, kname, hk->resolved_type);
                gen_expr(ctx, hk);
                cg_emit(ctx, "; ");
            }
            if (fresh_val) {
                int vt = ctx->temp_counter++;
                snprintf(vname, sizeof(vname), "__pv%d", vt);
                emit_ref_temp_decl(ctx, vname, hv->resolved_type);
                gen_expr(ctx, hv);
                cg_emit(ctx, "; ");
            }
            cg_emitf(ctx, "__zn_hash_set(__t%d, ", t);
            if (fresh_key) emit_box_call(ctx, kname, hk->resolved_type);
            else gen_box_expr(ctx, hk);
            cg_emit(ctx, ", ");
            if (fresh_val) emit_box_call(ctx, vname, hv->resolved_type);
            else gen_box_expr(ctx, hv);
            cg_emit(ctx, "); ");
            if (fresh_key) { emit_release_call(ctx, kname, hk->resolved_type); cg_emit(ctx, "; "); }
            if (fresh_val) { emit_release_call(ctx, vname, hv->resolved_type); cg_emit(ctx, "; "); }
        }
        cg_emitf(ctx, "__t%d; })", t);
        break;
    }
    case NODE_TYPED_EMPTY_ARRAY: {
        int t = ctx->temp_counter++;
        cg_emitf(ctx, "({ ZnArray *__t%d = __zn_arr_alloc(0", t);
        emit_arr_callbacks(ctx, expr->resolved_type ? expr->resolved_type->elem : NULL);
        cg_emitf(ctx, "); __t%d; })", t);
        break;
    }
    case NODE_TYPED_EMPTY_HASH: {
        int t = ctx->temp_counter++;
        cg_emitf(ctx, "({ ZnHash *__t%d = __zn_hash_alloc(8", t);
        emit_hash_callbacks(ctx,
            expr->resolved_type ? expr->resolved_type->key : NULL,
            expr->resolved_type ? expr->resolved_type->elem : NULL);
        cg_emitf(ctx, "); __t%d; })", t);
        break;
    }
    case NODE_OPTIONAL_CHECK: {
        ASTNode *operand = expr->data.optional_check.operand;
        TypeKind ot = operand->resolved_type ? operand->resolved_type->kind : TK_UNKNOWN;
        int is_ref = is_ref_type(ot);
        if (is_ref) {
            /* Reference types: check != NULL */
            cg_emit(ctx, "(");
            gen_expr(ctx, operand);
            cg_emit(ctx, " != NULL)");
        } else {
            /* Value types: check ._has on the optional variable */
            if (operand->type == NODE_IDENT) {
                cg_emitf(ctx, "(%s._has)", operand->data.ident.name);
            } else {
                cg_emit(ctx, "(");
                gen_expr(ctx, operand);
                cg_emit(ctx, "._has)");
            }
        }
        break;
    }
    case NODE_IF: {
        TypeKind rt = expr->resolved_type ? expr->resolved_type->kind : TK_UNKNOWN;
        if (rt == TK_UNKNOWN || rt == TK_VOID) break;

        int t = ctx->temp_counter++;

        /* Optional if-without-else */
        int is_opt = expr->resolved_type ? expr->resolved_type->is_optional : 0;
        if (is_opt && !expr->data.if_expr.else_b) {
            const char *opt = opt_type_for(rt);
            if (opt) {
                /* Value type optional */
                cg_emitf(ctx, "({ %s __if_%d; ", opt, t);
                cg_emit(ctx, "if (");
                gen_expr(ctx, expr->data.if_expr.cond);
                cg_emit(ctx, ") { ");
                if (expr->data.if_expr.then_b &&
                    expr->data.if_expr.then_b->type == NODE_BLOCK) {
                    NodeList *stmts = expr->data.if_expr.then_b->data.block.stmts;
                    NodeList *last = stmts;
                    while (last && last->next) last = last->next;
                    for (NodeList *s = stmts; s != last; s = s->next) {
                        gen_stmt(ctx, s->node);
                    }
                    if (last && last->node) {
                        cg_emitf(ctx, "__if_%d._has = true; __if_%d._val = ", t, t);
                        gen_expr(ctx, last->node);
                        cg_emit(ctx, "; ");
                    }
                }
                cg_emitf(ctx, "} else { __if_%d._has = false; } __if_%d; })", t, t);
            } else {
                /* Reference type optional (NULL = none) */
                if (rt == TK_CLASS && expr->resolved_type && expr->resolved_type->name) {
                    cg_emitf(ctx, "({ %s *__if_%d = NULL; ", expr->resolved_type->name, t);
                } else {
                    cg_emitf(ctx, "({ %s __if_%d = NULL; ", type_to_c(rt), t);
                }
                cg_emit(ctx, "if (");
                gen_expr(ctx, expr->data.if_expr.cond);
                cg_emit(ctx, ") { ");
                if (expr->data.if_expr.then_b &&
                    expr->data.if_expr.then_b->type == NODE_BLOCK) {
                    NodeList *stmts = expr->data.if_expr.then_b->data.block.stmts;
                    NodeList *last = stmts;
                    while (last && last->next) last = last->next;
                    for (NodeList *s = stmts; s != last; s = s->next) {
                        gen_stmt(ctx, s->node);
                    }
                    if (last && last->node) {
                        cg_emitf(ctx, "__if_%d = ", t);
                        gen_expr(ctx, last->node);
                        cg_emit(ctx, "; ");
                    }
                }
                cg_emitf(ctx, "} __if_%d; })", t);
            }
            break;
        }

        /* Check for type narrowing in if-expression */
        ASTNode *expr_cond = expr->data.if_expr.cond;
        int expr_narrowing = 0;
        const char *expr_narrow_name = NULL;
        if (expr_cond && expr_cond->type == NODE_OPTIONAL_CHECK &&
            expr_cond->data.optional_check.operand->type == NODE_IDENT) {
            ASTNode *operand = expr_cond->data.optional_check.operand;
            if (operand->resolved_type && operand->resolved_type->is_optional &&
                !is_ref_type(operand->resolved_type->kind)) {
                expr_narrowing = 1;
                expr_narrow_name = operand->data.ident.name;
            }
        }

        /* Non-optional if/else expression */
        if (rt == TK_STRUCT && expr->resolved_type && expr->resolved_type->name) {
            cg_emitf(ctx, "({ %s __if_%d; ", expr->resolved_type->name, t);
        } else if (rt == TK_CLASS && expr->resolved_type && expr->resolved_type->name) {
            cg_emitf(ctx, "({ %s *__if_%d; ", expr->resolved_type->name, t);
        } else {
            cg_emitf(ctx, "({ %s __if_%d; ", type_to_c(rt), t);
        }
        cg_emit(ctx, "if (");
        gen_expr(ctx, expr->data.if_expr.cond);
        cg_emit(ctx, ") { ");
        if (expr->data.if_expr.then_b &&
            expr->data.if_expr.then_b->type == NODE_BLOCK) {
            CGNarrow *ne = NULL;
            if (expr_narrowing) {
                ne = malloc(sizeof(CGNarrow));
                ne->name = strdup(expr_narrow_name);
                ne->next = ctx->narrowed;
                ctx->narrowed = ne;
            }
            NodeList *stmts = expr->data.if_expr.then_b->data.block.stmts;
            NodeList *last = stmts;
            while (last && last->next) last = last->next;
            for (NodeList *s = stmts; s != last; s = s->next) {
                gen_stmt(ctx, s->node);
            }
            if (last && last->node) {
                cg_emitf(ctx, "__if_%d = ", t);
                gen_expr(ctx, last->node);
                cg_emit(ctx, "; ");
                emit_inline_retain(ctx, t, "__if_", last->node, expr->resolved_type);
            }
            if (ne) {
                ctx->narrowed = ne->next;
                free(ne->name);
                free(ne);
            }
        }
        cg_emit(ctx, "} else { ");
        ASTNode *else_b = expr->data.if_expr.else_b;
        if (else_b && else_b->type == NODE_IF) {
            cg_emitf(ctx, "__if_%d = ", t);
            gen_expr(ctx, else_b);
            cg_emit(ctx, "; ");
            emit_inline_retain(ctx, t, "__if_", else_b, expr->resolved_type);
        } else if (else_b && else_b->type == NODE_BLOCK) {
            NodeList *stmts = else_b->data.block.stmts;
            NodeList *last = stmts;
            while (last && last->next) last = last->next;
            for (NodeList *s = stmts; s != last; s = s->next) {
                gen_stmt(ctx, s->node);
            }
            if (last && last->node) {
                cg_emitf(ctx, "__if_%d = ", t);
                gen_expr(ctx, last->node);
                cg_emit(ctx, "; ");
                emit_inline_retain(ctx, t, "__if_", last->node, expr->resolved_type);
            }
        }
        cg_emitf(ctx, "} __if_%d; })", t);
        break;
    }
    case NODE_WHILE: {
        TypeKind rt = expr->resolved_type ? expr->resolved_type->kind : TK_UNKNOWN;
        if (rt == TK_UNKNOWN || rt == TK_VOID) break;
        int t = ctx->temp_counter++;
        int saved_let = ctx->loop_expr_temp;
        int saved_opt = ctx->loop_expr_optional;
        ctx->loop_expr_temp = t;
        ctx->loop_expr_type = rt;
        int is_opt = expr->resolved_type ? expr->resolved_type->is_optional : 0;
        ctx->loop_expr_optional = is_opt ? 1 : 0;

        if (is_opt) {
            const char *opt = opt_type_for(rt);
            if (opt) {
                cg_emitf(ctx, "({ %s __loop_%d; __loop_%d._has = false; ", opt, t, t);
            } else if (rt == TK_CLASS && expr->resolved_type && expr->resolved_type->name) {
                cg_emitf(ctx, "({ %s *__loop_%d = NULL; ", expr->resolved_type->name, t);
            } else {
                cg_emitf(ctx, "({ %s __loop_%d = NULL; ", type_to_c(rt), t);
            }
        } else {
            if (rt == TK_CLASS && expr->resolved_type && expr->resolved_type->name) {
                cg_emitf(ctx, "({ %s *__loop_%d = NULL; ", expr->resolved_type->name, t);
            } else if (is_ref_type(rt)) {
                cg_emitf(ctx, "({ %s __loop_%d = NULL; ", type_to_c(rt), t);
            } else {
                cg_emitf(ctx, "({ %s __loop_%d; ", type_to_c(rt), t);
            }
        }

        cg_emit(ctx, "while (");
        gen_expr(ctx, expr->data.while_expr.cond);
        cg_emit(ctx, ") ");
        gen_block_with_scope(ctx, expr->data.while_expr.body, 1);
        cg_emitf(ctx, " __loop_%d; })", t);
        ctx->loop_expr_temp = saved_let;
        ctx->loop_expr_optional = saved_opt;
        break;
    }
    case NODE_FOR: {
        TypeKind rt = expr->resolved_type ? expr->resolved_type->kind : TK_UNKNOWN;
        if (rt == TK_UNKNOWN || rt == TK_VOID) break;
        int t = ctx->temp_counter++;
        int saved_let = ctx->loop_expr_temp;
        int saved_opt = ctx->loop_expr_optional;
        ctx->loop_expr_temp = t;
        ctx->loop_expr_type = rt;
        ctx->loop_expr_optional = 1;  /* for loops are always optional */

        const char *opt = opt_type_for(rt);
        if (opt) {
            cg_emitf(ctx, "({ %s __loop_%d; __loop_%d._has = false; ", opt, t, t);
        } else if (rt == TK_CLASS && expr->resolved_type && expr->resolved_type->name) {
            cg_emitf(ctx, "({ %s *__loop_%d = NULL; ", expr->resolved_type->name, t);
        } else {
            cg_emitf(ctx, "({ %s __loop_%d = NULL; ", type_to_c(rt), t);
        }

        gen_for_header(ctx, expr);
        gen_block_with_scope(ctx, expr->data.for_expr.body, 1);
        cg_emitf(ctx, " __loop_%d; })", t);
        ctx->loop_expr_temp = saved_let;
        ctx->loop_expr_optional = saved_opt;
        break;
    }
    default:
        break;
    }
}

void gen_stmt(CodegenContext *ctx, ASTNode *node) {
    if (!node) return;

    cg_emit_indent(ctx);

    switch (node->type) {
    case NODE_DECL: {
        const char *name = node->data.decl.name;
        ASTNode *value = node->data.decl.value;
        int is_const = node->data.decl.is_const;
        const char *cq = is_const ? "const " : "";
        Type *vt = value->resolved_type;
        TypeKind t = vt ? vt->kind : TK_UNKNOWN;
        int val_is_optional = vt ? vt->is_optional : 0;
        if (val_is_optional && opt_type_for(t)) {
            cg_emitf(ctx, "%s%s %s = ", cq, opt_type_for(t), name);
        } else if (t == TK_CLASS && vt->name) {
            if (is_const)
                cg_emitf(ctx, "%s *const %s = ", vt->name, name);
            else
                cg_emitf(ctx, "%s *%s = ", vt->name, name);
        } else if (t == TK_STRUCT && vt->name) {
            cg_emitf(ctx, "%s%s %s = ", cq, vt->name, name);
        } else if (t == TK_STRING || t == TK_ARRAY || t == TK_HASH) {
            cg_emitf(ctx, "%s %s = ", type_to_c(t), name);
        } else {
            cg_emitf(ctx, "%s%s %s = ", cq, type_to_c(t), name);
        }
        gen_expr(ctx, value);
        cg_emit(ctx, ";\n");
        emit_retain(ctx, name, value, vt);
        scope_track_ref(ctx, name, vt);
        break;
    }
    case NODE_IF: {
        /* Check for type narrowing: if x? { ... uses narrowed x ... } */
        ASTNode *cond = node->data.if_expr.cond;
        int narrowing = 0;
        const char *narrow_name = NULL;

        if (cond && cond->type == NODE_OPTIONAL_CHECK &&
            cond->data.optional_check.operand->type == NODE_IDENT) {
            ASTNode *operand = cond->data.optional_check.operand;
            int operand_is_optional = operand->resolved_type && operand->resolved_type->is_optional;
            TypeKind operand_kind = operand->resolved_type ? operand->resolved_type->kind : TK_UNKNOWN;
            if (operand_is_optional && !is_ref_type(operand_kind)) {
                narrowing = 1;
                narrow_name = operand->data.ident.name;
            }
        }

        cg_emit(ctx, "if (");
        gen_expr(ctx, cond);
        cg_emit(ctx, ") ");

        if (narrowing && node->data.if_expr.then_b &&
            node->data.if_expr.then_b->type == NODE_BLOCK) {
            /* Push narrowing: references to narrow_name will emit name._val */
            CGNarrow *n = malloc(sizeof(CGNarrow));
            n->name = strdup(narrow_name);
            n->next = ctx->narrowed;
            ctx->narrowed = n;
            gen_block(ctx, node->data.if_expr.then_b);
            /* Pop narrowing */
            ctx->narrowed = n->next;
            free(n->name);
            free(n);
        } else {
            gen_block(ctx, node->data.if_expr.then_b);
        }

        if (node->data.if_expr.else_b) {
            cg_emit(ctx, " else ");
            if (node->data.if_expr.else_b->type == NODE_IF) {
                gen_stmt(ctx, node->data.if_expr.else_b);
            } else {
                gen_block(ctx, node->data.if_expr.else_b);
                cg_emit(ctx, "\n");
            }
        } else {
            cg_emit(ctx, "\n");
        }
        break;
    }
    case NODE_WHILE:
        cg_emit(ctx, "while (");
        gen_expr(ctx, node->data.while_expr.cond);
        cg_emit(ctx, ") ");
        gen_block_with_scope(ctx, node->data.while_expr.body, 1);
        cg_emit(ctx, "\n");
        break;
    case NODE_FOR:
        gen_for_header(ctx, node);
        gen_block_with_scope(ctx, node->data.for_expr.body, 1);
        cg_emit(ctx, "\n");
        break;
    case NODE_BREAK: {
        CGScope *loop = find_loop_scope(ctx);
        if (loop) {
            for (CGScope *s = ctx->scope; s; s = s->parent) {
                for (CGScopeVar *v = s->ref_vars; v; v = v->next) {
                    emit_var_release(ctx, v);
                }
                if (s == loop) break;
            }
            cg_emit_indent(ctx);
        }
        if (ctx->loop_expr_temp >= 0 && node->data.break_expr.value) {
            ASTNode *bv = node->data.break_expr.value;
            int is_opt = ctx->loop_expr_optional && opt_type_for(ctx->loop_expr_type);
            if (bv->resolved_type && is_ref_type(bv->resolved_type->kind)) {
                /* ARC: pre-eval value, retain-before-release, assign */
                int t_val = ctx->temp_counter++;
                char tname[32]; snprintf(tname, sizeof(tname), "__t%d", t_val);
                emit_ref_temp_decl(ctx, tname, bv->resolved_type);
                gen_expr(ctx, bv);
                cg_emit(ctx, ";\n");
                cg_emit_indent(ctx);
                if (!bv->is_fresh_alloc) {
                    emit_retain_call(ctx, tname, bv->resolved_type);
                    cg_emit(ctx, ";\n");
                    cg_emit_indent(ctx);
                }
                char lbuf[64];
                snprintf(lbuf, sizeof(lbuf), is_opt ? "__loop_%d._val" : "__loop_%d",
                         ctx->loop_expr_temp);
                emit_release_call(ctx, lbuf, bv->resolved_type);
                cg_emit(ctx, ";\n");
                cg_emit_indent(ctx);
                if (is_opt) {
                    cg_emitf(ctx, "__loop_%d._has = true; __loop_%d._val = %s;\n",
                          ctx->loop_expr_temp, ctx->loop_expr_temp, tname);
                } else {
                    cg_emitf(ctx, "__loop_%d = %s;\n", ctx->loop_expr_temp, tname);
                }
            } else {
                if (is_opt) {
                    cg_emitf(ctx, "__loop_%d._has = true; __loop_%d._val = ",
                          ctx->loop_expr_temp, ctx->loop_expr_temp);
                } else {
                    cg_emitf(ctx, "__loop_%d = ", ctx->loop_expr_temp);
                }
                gen_expr(ctx, bv);
                cg_emit(ctx, ";\n");
            }
            cg_emit_indent(ctx);
        }
        cg_emit(ctx, "break;\n");
        break;
    }
    case NODE_CONTINUE: {
        CGScope *loop = find_loop_scope(ctx);
        if (loop) {
            for (CGScope *s = ctx->scope; s; s = s->parent) {
                for (CGScopeVar *v = s->ref_vars; v; v = v->next) {
                    emit_var_release(ctx, v);
                }
                if (s == loop) break;
            }
            cg_emit_indent(ctx);
        }
        if (ctx->loop_expr_temp >= 0 && node->data.continue_expr.value) {
            ASTNode *cv = node->data.continue_expr.value;
            int is_opt = ctx->loop_expr_optional && opt_type_for(ctx->loop_expr_type);
            if (cv->resolved_type && is_ref_type(cv->resolved_type->kind)) {
                /* ARC: pre-eval value, retain-before-release, assign */
                int t_val = ctx->temp_counter++;
                char tname[32]; snprintf(tname, sizeof(tname), "__t%d", t_val);
                emit_ref_temp_decl(ctx, tname, cv->resolved_type);
                gen_expr(ctx, cv);
                cg_emit(ctx, ";\n");
                cg_emit_indent(ctx);
                if (!cv->is_fresh_alloc) {
                    emit_retain_call(ctx, tname, cv->resolved_type);
                    cg_emit(ctx, ";\n");
                    cg_emit_indent(ctx);
                }
                char lbuf[64];
                snprintf(lbuf, sizeof(lbuf), is_opt ? "__loop_%d._val" : "__loop_%d",
                         ctx->loop_expr_temp);
                emit_release_call(ctx, lbuf, cv->resolved_type);
                cg_emit(ctx, ";\n");
                cg_emit_indent(ctx);
                if (is_opt) {
                    cg_emitf(ctx, "__loop_%d._has = true; __loop_%d._val = %s;\n",
                          ctx->loop_expr_temp, ctx->loop_expr_temp, tname);
                } else {
                    cg_emitf(ctx, "__loop_%d = %s;\n", ctx->loop_expr_temp, tname);
                }
            } else {
                if (is_opt) {
                    cg_emitf(ctx, "__loop_%d._has = true; __loop_%d._val = ",
                          ctx->loop_expr_temp, ctx->loop_expr_temp);
                } else {
                    cg_emitf(ctx, "__loop_%d = ", ctx->loop_expr_temp);
                }
                gen_expr(ctx, cv);
                cg_emit(ctx, ";\n");
            }
            cg_emit_indent(ctx);
        }
        cg_emit(ctx, "continue;\n");
        break;
    }
    case NODE_RETURN: {
        ASTNode *rv = node->data.ret.value;
        if (!rv) {
            emit_all_scope_releases(ctx);
            cg_emit_indent(ctx);
            cg_emit(ctx, "return;\n");
        } else {
            Type *rt = rv->resolved_type;
            TypeKind rk = rt ? rt->kind : TK_UNKNOWN;
            if (rk == TK_VOID || rk == TK_UNKNOWN) {
                emit_all_scope_releases(ctx);
                cg_emit_indent(ctx);
                cg_emit(ctx, "return ");
                gen_expr(ctx, rv);
                cg_emit(ctx, ";\n");
            } else {
                /* Save to temp, retain if ref type, release scopes, return */
                int t = ctx->temp_counter++;
                if (rk == TK_CLASS && rt->name)
                    cg_emitf(ctx, "%s *__ret%d = ", rt->name, t);
                else if (rk == TK_STRUCT && rt->name)
                    cg_emitf(ctx, "%s __ret%d = ", rt->name, t);
                else if (rk == TK_STRING || rk == TK_ARRAY || rk == TK_HASH)
                    cg_emitf(ctx, "%s __ret%d = ", type_to_c(rk), t);
                else
                    cg_emitf(ctx, "%s __ret%d = ", type_to_c(rk), t);
                gen_expr(ctx, rv);
                cg_emit(ctx, ";\n");
                char tmp_name[32];
                snprintf(tmp_name, sizeof(tmp_name), "__ret%d", t);
                emit_retain(ctx, tmp_name, rv, rt);
                emit_all_scope_releases(ctx);
                cg_emit_indent(ctx);
                cg_emitf(ctx, "return __ret%d;\n", t);
            }
        }
        break;
    }
    case NODE_ASSIGN: {
        ASTNode *tgt = node->data.assign.target;
        ASTNode *val = node->data.assign.value;
        TypeKind val_kind = val->resolved_type ? val->resolved_type->kind : TK_UNKNOWN;

        if (tgt->type == NODE_INDEX) {
            /* Index assignment: arr[i] = val or hash[k] = val */
            ASTNode *obj = tgt->data.index_access.object;
            TypeKind obj_kind = obj->resolved_type ? obj->resolved_type->kind : TK_UNKNOWN;
            if (obj_kind == TK_ARRAY) {
                int fresh_val = val->resolved_type && is_ref_type(val->resolved_type->kind) && val->is_fresh_alloc;
                if (fresh_val) {
                    /* Pre-evaluate fresh value so we can release after set retains */
                    int pt = ctx->temp_counter++;
                    char pname[64];
                    snprintf(pname, sizeof(pname), "__ps%d", pt);
                    cg_emit(ctx, "{ ");
                    emit_ref_temp_decl(ctx, pname, val->resolved_type);
                    gen_expr(ctx, val);
                    cg_emit(ctx, "; __zn_arr_set(");
                    gen_expr(ctx, obj);
                    cg_emit(ctx, ", ");
                    gen_expr(ctx, tgt->data.index_access.index);
                    cg_emit(ctx, ", ");
                    emit_box_call(ctx, pname, val->resolved_type);
                    cg_emit(ctx, "); ");
                    emit_release_call(ctx, pname, val->resolved_type);
                    cg_emit(ctx, "; }\n");
                } else {
                    cg_emit(ctx, "__zn_arr_set(");
                    gen_expr(ctx, obj);
                    cg_emit(ctx, ", ");
                    gen_expr(ctx, tgt->data.index_access.index);
                    cg_emit(ctx, ", ");
                    gen_box_expr(ctx, val);
                    cg_emit(ctx, ");\n");
                }
            } else if (obj_kind == TK_HASH) {
                int fresh_val = val->resolved_type && is_ref_type(val->resolved_type->kind) && val->is_fresh_alloc;
                if (fresh_val) {
                    int pt = ctx->temp_counter++;
                    char pname[64];
                    snprintf(pname, sizeof(pname), "__ps%d", pt);
                    cg_emit(ctx, "{ ");
                    emit_ref_temp_decl(ctx, pname, val->resolved_type);
                    gen_expr(ctx, val);
                    cg_emit(ctx, "; __zn_hash_set(");
                    gen_expr(ctx, obj);
                    cg_emit(ctx, ", ");
                    gen_box_expr(ctx, tgt->data.index_access.index);
                    cg_emit(ctx, ", ");
                    emit_box_call(ctx, pname, val->resolved_type);
                    cg_emit(ctx, "); ");
                    emit_release_call(ctx, pname, val->resolved_type);
                    cg_emit(ctx, "; }\n");
                } else {
                    cg_emit(ctx, "__zn_hash_set(");
                    gen_expr(ctx, obj);
                    cg_emit(ctx, ", ");
                    gen_box_expr(ctx, tgt->data.index_access.index);
                    cg_emit(ctx, ", ");
                    gen_box_expr(ctx, val);
                    cg_emit(ctx, ");\n");
                }
            }
            break;
        }

        if (tgt->type == NODE_FIELD_ACCESS) {
            /* Field assignment: obj.field = val */
            ASTNode *obj = tgt->data.field_access.object;
            const char *field = tgt->data.field_access.field;
            const char *obj_sn = obj->resolved_type ? obj->resolved_type->name : NULL;
            TypeKind obj_kind = obj->resolved_type ? obj->resolved_type->kind : TK_UNKNOWN;

            /* Look up field def for struct/class types */
            StructFieldDef *fd = NULL;
            if ((obj_kind == TK_STRUCT || obj_kind == TK_CLASS) && obj_sn) {
                StructDef *sd = lookup_struct(ctx->sem_ctx, obj_sn);
                if (sd) {
                    for (StructFieldDef *f = sd->fields; f; f = f->next) {
                        if (strcmp(f->name, field) == 0) { fd = f; break; }
                    }
                }
            }

            if (fd && is_ref_type(fd->type->kind) && obj_kind == TK_CLASS) {
                /* Class ref-type field: pre-evaluate obj pointer and val */
                int t_obj = ctx->temp_counter++;
                int t_val = ctx->temp_counter++;
                cg_emitf(ctx, "struct %s *__t%d = ", obj_sn, t_obj);
                gen_expr(ctx, obj);
                cg_emit(ctx, ";\n");
                cg_emit_indent(ctx);
                if (fd->type->kind == TK_CLASS && fd->type->name)
                    cg_emitf(ctx, "struct %s *__t%d = ", fd->type->name, t_val);
                else
                    cg_emitf(ctx, "%s __t%d = ", type_to_c(fd->type->kind), t_val);
                gen_expr(ctx, val);
                cg_emit(ctx, ";\n");
                cg_emit_indent(ctx);
                /* retain-before-release for self-assignment safety */
                if (!val || !val->is_fresh_alloc) {
                    emit_retain_open(ctx, fd->type);
                    cg_emitf(ctx, "__t%d);\n", t_val);
                    cg_emit_indent(ctx);
                }
                emit_release_open(ctx, fd->type);
                cg_emitf(ctx, "__t%d->%s);\n", t_obj, field);
                cg_emit_indent(ctx);
                cg_emitf(ctx, "__t%d->%s = __t%d;\n", t_obj, field, t_val);
            } else if (fd && is_ref_type(fd->type->kind) && obj_kind == TK_STRUCT) {
                /* Struct ref-type field: obj is an lvalue, only pre-evaluate val */
                int t_val = ctx->temp_counter++;
                if (fd->type->kind == TK_CLASS && fd->type->name)
                    cg_emitf(ctx, "struct %s *__t%d = ", fd->type->name, t_val);
                else
                    cg_emitf(ctx, "%s __t%d = ", type_to_c(fd->type->kind), t_val);
                gen_expr(ctx, val);
                cg_emit(ctx, ";\n");
                cg_emit_indent(ctx);
                /* retain-before-release for self-assignment safety */
                if (!val || !val->is_fresh_alloc) {
                    emit_retain_open(ctx, fd->type);
                    cg_emitf(ctx, "__t%d);\n", t_val);
                    cg_emit_indent(ctx);
                }
                emit_release_open(ctx, fd->type);
                gen_expr(ctx, obj);
                cg_emitf(ctx, ".%s);\n", field);
                cg_emit_indent(ctx);
                gen_expr(ctx, obj);
                cg_emitf(ctx, ".%s = __t%d;\n", field, t_val);
            } else {
                gen_expr(ctx, node);
                cg_emit(ctx, ";\n");
            }
            break;
        }

        /* Simple variable assignment (NODE_IDENT target) */
        const char *name = tgt->data.ident.name;
        Type *vtype = val->resolved_type;
        if (vtype && is_ref_type(val_kind)) {
            if (val_kind == TK_CLASS) {
                /* Class: retain-before-release for self-assignment safety */
                if (!val->is_fresh_alloc) {
                    /* Pre-evaluate val to avoid double evaluation */
                    int t = ctx->temp_counter++;
                    cg_emitf(ctx, "struct %s *__t%d = ", vtype->name, t);
                    gen_expr(ctx, val);
                    cg_emit(ctx, ";\n");
                    cg_emit_indent(ctx);
                    emit_retain_open(ctx, vtype);
                    cg_emitf(ctx, "__t%d);\n", t);
                    cg_emit_indent(ctx);
                    emit_release_call(ctx, name, vtype);
                    cg_emit(ctx, ";\n");
                    cg_emit_indent(ctx);
                    cg_emitf(ctx, "%s = __t%d;\n", name, t);
                } else {
                    emit_release_call(ctx, name, vtype);
                    cg_emit(ctx, ";\n");
                    cg_emit_indent(ctx);
                    cg_emitf(ctx, "%s = ", name);
                    gen_expr(ctx, val);
                    cg_emit(ctx, ";\n");
                }
            } else {
                /* String/Array/Hash: release-assign-retain */
                emit_release_call(ctx, name, vtype);
                cg_emit(ctx, ";\n");
                cg_emit_indent(ctx);
                cg_emitf(ctx, "%s = ", name);
                gen_expr(ctx, val);
                cg_emit(ctx, ";\n");
                if (!val->is_fresh_alloc) {
                    cg_emit_indent(ctx);
                    emit_retain_call(ctx, name, vtype);
                    cg_emit(ctx, ";\n");
                }
            }
        } else {
            gen_expr(ctx, node);
            cg_emit(ctx, ";\n");
        }
        break;
    }
    case NODE_FUNC_DEF:
        break;
    default:
        gen_expr(ctx, node);
        cg_emit(ctx, ";\n");
        break;
    }
}

void gen_stmts(CodegenContext *ctx, NodeList *stmts) {
    for (NodeList *s = stmts; s; s = s->next) {
        if (s->node && s->node->type != NODE_FUNC_DEF) {
            cg_emit_line(ctx, s->node->line);
            gen_stmt(ctx, s->node);
        }
    }
}

/* Generate function prototype */
void gen_func_proto(CodegenContext *ctx, ASTNode *func, int to_header) {
    Symbol *sym = lookup(ctx->sem_ctx, func->data.func_def.name);
    TypeKind ret_type = sym ? sym->type->kind : TK_VOID;

    FILE *out = to_header ? ctx->h_file : ctx->c_file;

    const char *ret_str;
    int ret_is_class = 0;
    if (strcmp(func->data.func_def.name, "main") == 0) {
        ret_str = "int";
    } else if (ret_type == TK_CLASS && sym && sym->type->name) {
        ret_is_class = 1;
        ret_str = sym->type->name;
    } else if (ret_type == TK_STRUCT && sym && sym->type->name) {
        ret_str = sym->type->name;
    } else {
        ret_str = type_to_c(ret_type);
    }

    if (ret_is_class) {
        fprintf(out, "%s *%s(", ret_str, func->data.func_def.name);
    } else {
        fprintf(out, "%s %s(", ret_str, func->data.func_def.name);
    }

    int first = 1;
    for (NodeList *p = func->data.func_def.params; p; p = p->next) {
        if (!first) fprintf(out, ", ");
        TypeInfo *ti = p->node->data.param.type_info;
        const char *opt = ti->is_optional ? opt_type_for(ti->kind) : NULL;
        if (opt) {
            fprintf(out, "const %s %s", opt, p->node->data.param.name);
        } else if (ti->kind == TK_CLASS && ti->name) {
            /* Object type (resolve_type_info sets kind to TK_CLASS) */
            fprintf(out, "%s *%s", ti->name, p->node->data.param.name);
        } else if (ti->kind == TK_STRUCT && ti->name) {
            StructDef *psd = lookup_struct(ctx->sem_ctx, ti->name);
            if (psd && psd->is_class) {
                /* Named class (parser creates TK_STRUCT, semantic resolves to class) */
                fprintf(out, "%s *%s", ti->name, p->node->data.param.name);
            } else {
                fprintf(out, "const %s %s", ti->name, p->node->data.param.name);
            }
        } else {
            fprintf(out, "const %s %s", type_to_c(ti->kind),
                    p->node->data.param.name);
        }
        first = 0;
    }

    if (first) {
        fprintf(out, "void");
    }

    fprintf(out, ")");
}

/* Generate function body with implicit return for last expression */
void gen_func_body(CodegenContext *ctx, ASTNode *block, TypeKind ret_type) {
    if (!block || block->type != NODE_BLOCK) return;

    cg_emit(ctx, "{\n");
    ctx->indent_level++;
    cg_push_scope(ctx, 0);

    NodeList *stmts = block->data.block.stmts;
    if (!stmts) {
        cg_pop_scope(ctx);
        ctx->indent_level--;
        cg_emit_indent(ctx);
        cg_emit(ctx, "}");
        return;
    }

    NodeList *last = stmts;
    while (last->next) last = last->next;

    for (NodeList *s = stmts; s != last; s = s->next) {
        gen_stmt(ctx, s->node);
    }

    ASTNode *last_node = last->node;
    if (last_node) {
        TypeKind last_kind = last_node->resolved_type ? last_node->resolved_type->kind : TK_UNKNOWN;
        if (last_node->type == NODE_RETURN) {
            gen_stmt(ctx, last_node);
        } else if (ret_type == TK_VOID || last_kind == TK_VOID) {
            gen_stmt(ctx, last_node);
            emit_scope_releases(ctx);
        } else if (ret_type == TK_STRING) {
            int t = ctx->temp_counter++;
            cg_emit_indent(ctx);
            cg_emitf(ctx, "ZnString *__ret%d = ", t);
            gen_expr(ctx, last_node);
            cg_emit(ctx, ";\n");
            if (!last_node->is_fresh_alloc) {
                cg_emit_indent(ctx);
                cg_emitf(ctx, "__zn_str_retain(__ret%d);\n", t);
            }
            emit_scope_releases(ctx);
            cg_emit_indent(ctx);
            cg_emitf(ctx, "return __ret%d;\n", t);
        } else if (ret_type == TK_ARRAY) {
            int t = ctx->temp_counter++;
            cg_emit_indent(ctx);
            cg_emitf(ctx, "ZnArray *__ret%d = ", t);
            gen_expr(ctx, last_node);
            cg_emit(ctx, ";\n");
            if (!last_node->is_fresh_alloc) {
                cg_emit_indent(ctx);
                cg_emitf(ctx, "__zn_arr_retain(__ret%d);\n", t);
            }
            emit_scope_releases(ctx);
            cg_emit_indent(ctx);
            cg_emitf(ctx, "return __ret%d;\n", t);
        } else if (ret_type == TK_HASH) {
            int t = ctx->temp_counter++;
            cg_emit_indent(ctx);
            cg_emitf(ctx, "ZnHash *__ret%d = ", t);
            gen_expr(ctx, last_node);
            cg_emit(ctx, ";\n");
            if (!last_node->is_fresh_alloc) {
                cg_emit_indent(ctx);
                cg_emitf(ctx, "__zn_hash_retain(__ret%d);\n", t);
            }
            emit_scope_releases(ctx);
            cg_emit_indent(ctx);
            cg_emitf(ctx, "return __ret%d;\n", t);
        } else if (ret_type == TK_CLASS && last_node->resolved_type &&
                   last_node->resolved_type->name) {
            int t = ctx->temp_counter++;
            cg_emit_indent(ctx);
            cg_emitf(ctx, "%s *__ret%d = ", last_node->resolved_type->name, t);
            gen_expr(ctx, last_node);
            cg_emit(ctx, ";\n");
            if (!last_node->is_fresh_alloc) {
                cg_emit_indent(ctx);
                cg_emitf(ctx, "__%s_retain(__ret%d);\n", last_node->resolved_type->name, t);
            }
            emit_scope_releases(ctx);
            cg_emit_indent(ctx);
            cg_emitf(ctx, "return __ret%d;\n", t);
        } else if (ret_type == TK_STRUCT && last_node->resolved_type &&
                   last_node->resolved_type->name) {
            int t = ctx->temp_counter++;
            cg_emit_indent(ctx);
            cg_emitf(ctx, "%s __ret%d = ", last_node->resolved_type->name, t);
            gen_expr(ctx, last_node);
            cg_emit(ctx, ";\n");
            emit_scope_releases(ctx);
            cg_emit_indent(ctx);
            cg_emitf(ctx, "return __ret%d;\n", t);
        } else {
            int t = ctx->temp_counter++;
            cg_emit_indent(ctx);
            cg_emitf(ctx, "%s __ret%d = ", type_to_c(ret_type), t);
            gen_expr(ctx, last_node);
            cg_emit(ctx, ";\n");
            emit_scope_releases(ctx);
            cg_emit_indent(ctx);
            cg_emitf(ctx, "return __ret%d;\n", t);
        }
    } else {
        emit_scope_releases(ctx);
    }

    cg_pop_scope(ctx);
    ctx->indent_level--;
    cg_emit_indent(ctx);
    cg_emit(ctx, "}");
}

void gen_func_def(CodegenContext *ctx, ASTNode *func) {
    gen_func_proto(ctx, func, 1);
    cg_emit_header(ctx, ";\n");

    Symbol *sym = lookup(ctx->sem_ctx, func->data.func_def.name);
    TypeKind ret_type = sym ? sym->type->kind : TK_VOID;

    gen_func_proto(ctx, func, 0);
    cg_emit(ctx, " ");
    gen_func_body(ctx, func->data.func_def.body, ret_type);
    cg_emit(ctx, "\n\n");
}
