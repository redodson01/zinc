#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "codegen.h"

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
    default:        cg_emit(ctx, "__zn_val_int((int64_t)("); gen_expr(ctx, expr); cg_emit(ctx, "))"); break;
    }
}

/* Emit the for loop header: for (init; cond; update) */
void gen_for_header(CodegenContext *ctx, ASTNode *node) {
    cg_emit(ctx, "for (");
    ASTNode *init = node->data.for_expr.init;
    if (init) {
        if (init->type == NODE_VAR_DECL) {
            TypeKind t = init->data.var_decl.value->resolved_type->kind;
            cg_emitf(ctx, "%s %s = ", type_to_c(t), init->data.var_decl.name);
            gen_expr(ctx, init->data.var_decl.value);
        } else if (init->type == NODE_LET_DECL) {
            TypeKind t = init->data.let_decl.value->resolved_type->kind;
            cg_emitf(ctx, "const %s %s = ", type_to_c(t), init->data.let_decl.name);
            gen_expr(ctx, init->data.let_decl.value);
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
        && strcmp(expr->data.binop.op, "+") == 0) {
        return count_string_concat_parts(expr->data.binop.left) +
               count_string_concat_parts(expr->data.binop.right);
    }
    return 1;
}

/* Flatten a string concat tree into a linear sequence of leaves */
static void flatten_string_concat(ASTNode *expr, ASTNode **leaves, int *count) {
    if (expr->type == NODE_BINOP && expr->resolved_type && expr->resolved_type->kind == TK_STRING
        && strcmp(expr->data.binop.op, "+") == 0) {
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
    int base_temp = ctx->temp_counter;
    ctx->temp_counter += leaf_count - 1;

    for (int i = 0; i < leaf_count - 1; i++) {
        int t = base_temp + i;
        cg_emitf(ctx, "ZnString *__t%d = __zn_str_concat(", t);
        if (i == 0) {
            gen_coerce_to_string(ctx, leaves[0]);
        } else {
            cg_emitf(ctx, "__t%d", t - 1);
        }
        cg_emit(ctx, ", ");
        gen_coerce_to_string(ctx, leaves[i + 1]);
        cg_emit(ctx, "); ");

        /* Release coerced non-string temps */
        if (i == 0 && leaves[0]->resolved_type->kind != TK_STRING) {
            cg_emitf(ctx, "__zn_str_release(");
            gen_coerce_to_string(ctx, leaves[0]);
            cg_emit(ctx, "); ");
        }
        if (leaves[i + 1]->resolved_type->kind != TK_STRING) {
            cg_emitf(ctx, "__zn_str_release(");
            gen_coerce_to_string(ctx, leaves[i + 1]);
            cg_emit(ctx, "); ");
        }

        /* Release previous intermediate */
        if (i > 0) {
            cg_emitf(ctx, "__zn_str_release(__t%d); ", t - 1);
        }
    }

    cg_emitf(ctx, "__t%d; })", base_temp + leaf_count - 2);
    free(leaves);
}

/* Look up the codegen-side string literal ID for a NODE_STRING node (#3) */
int find_string_id(CodegenContext *ctx, ASTNode *node) {
    for (int i = 0; i < ctx->string_counter; i++) {
        if (ctx->string_nodes[i] == node) return i;
    }
    return -1;
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
    case NODE_STRING: {
        int id = find_string_id(ctx, expr);
        cg_emitf(ctx, "(ZnString*)&__zn_str_%d", id);
        break;
    }
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
    case NODE_IDENT:
        cg_emit(ctx, expr->data.ident.name);
        break;
    case NODE_BINOP: {
        const char *op = expr->data.binop.op;
        int is_comparison = (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
                            strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
                            strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0);

        /* String concatenation */
        if (strcmp(op, "+") == 0 && expr->resolved_type && expr->resolved_type->kind == TK_STRING) {
            gen_string_concat(ctx, expr);
            break;
        }

        /* String comparison */
        if (is_comparison && (expr_is_string(expr->data.binop.left) ||
                             expr_is_string(expr->data.binop.right))) {
            gen_string_comparison(ctx, expr->data.binop.left, op, expr->data.binop.right);
        } else {
            cg_emit(ctx, "(");
            gen_expr(ctx, expr->data.binop.left);
            cg_emitf(ctx, " %s ", op);
            gen_expr(ctx, expr->data.binop.right);
            cg_emit(ctx, ")");
        }
        break;
    }
    case NODE_UNARYOP:
        cg_emit(ctx, "(");
        cg_emit(ctx, expr->data.unaryop.op);
        gen_expr(ctx, expr->data.unaryop.operand);
        cg_emit(ctx, ")");
        break;
    case NODE_ASSIGN:
        cg_emit(ctx, expr->data.assign.name);
        cg_emit(ctx, " = ");
        gen_expr(ctx, expr->data.assign.value);
        break;
    case NODE_COMPOUND_ASSIGN:
        cg_emit(ctx, expr->data.compound_assign.name);
        cg_emitf(ctx, " %s ", expr->data.compound_assign.op);
        gen_expr(ctx, expr->data.compound_assign.value);
        break;
    case NODE_INCDEC:
        if (expr->data.incdec.is_prefix) {
            cg_emit(ctx, expr->data.incdec.op);
            cg_emit(ctx, expr->data.incdec.name);
        } else {
            cg_emit(ctx, expr->data.incdec.name);
            cg_emit(ctx, expr->data.incdec.op);
        }
        break;
    case NODE_CALL: {
        cg_emit(ctx, expr->data.call.name);
        cg_emit(ctx, "(");
        int first = 1;
        for (NodeList *arg = expr->data.call.args; arg; arg = arg->next) {
            if (!first) cg_emit(ctx, ", ");
            gen_expr(ctx, arg->node);
            first = 0;
        }
        cg_emit(ctx, ")");
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
        if (is_class_type(ctx, expr->data.field_access.object->resolved_type ? expr->data.field_access.object->resolved_type->name : NULL)) {
            cg_emitf(ctx, "->%s", expr->data.field_access.field);
        } else {
            cg_emitf(ctx, ".%s", expr->data.field_access.field);
        }
        break;
    case NODE_FIELD_ASSIGN:
        gen_expr(ctx, expr->data.field_assign.object);
        if (is_class_type(ctx, expr->data.field_assign.object->resolved_type ? expr->data.field_assign.object->resolved_type->name : NULL)) {
            cg_emitf(ctx, "->%s = ", expr->data.field_assign.field);
        } else {
            cg_emitf(ctx, ".%s = ", expr->data.field_assign.field);
        }
        gen_expr(ctx, expr->data.field_assign.value);
        break;
    case NODE_STRUCT_INIT: {
        const char *name = expr->data.struct_init.name;
        StructDef *sd = lookup_struct(ctx->sem_ctx, name);

        if (sd && sd->is_class) {
            /* Class init: heap allocate via __ClassName_alloc() */
            int t = ctx->temp_counter++;
            cg_emitf(ctx, "({ %s *__ci_%d = __%s_alloc(); ", name, t, name);

            for (StructFieldDef *fd = sd->fields; fd; fd = fd->next) {
                ASTNode *val = NULL;
                for (NodeList *arg = expr->data.struct_init.args; arg; arg = arg->next) {
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
                if (fd->type->kind == TK_STRING) {
                    if (!val || !is_fresh_string_alloc(val)) {
                        cg_emitf(ctx, "__zn_str_retain(__ci_%d->%s); ", t, fd->name);
                    }
                } else if (fd->type->kind == TK_STRUCT && fd->type->name &&
                           is_class_type(ctx, fd->type->name)) {
                    if (!is_fresh_class_alloc(val)) {
                        cg_emitf(ctx, "__%s_retain(__ci_%d->%s); ", fd->type->name, t, fd->name);
                    }
                }
            }

            cg_emitf(ctx, "__ci_%d; })", t);
        } else {
            /* Struct init: value type with C99 designators */
            cg_emitf(ctx, "(%s){", name);
            int first = 1;
            for (StructFieldDef *fd = sd->fields; fd; fd = fd->next) {
                if (!first) cg_emit(ctx, ", ");
                cg_emitf(ctx, ".%s = ", fd->name);
                int found = 0;
                for (NodeList *arg = expr->data.struct_init.args; arg; arg = arg->next) {
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
        break;
    }
    case NODE_TUPLE: {
        const char *name = expr->resolved_type ? expr->resolved_type->name : NULL;
        StructDef *sd = lookup_struct(ctx->sem_ctx, name);
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
            TypeKind ft = na->data.named_arg.value->resolved_type ? na->data.named_arg.value->resolved_type->kind : TK_UNKNOWN;
            if (ft == TK_STRING) {
                if (!is_fresh_string_alloc(na->data.named_arg.value)) {
                    cg_emitf(ctx, "__zn_str_retain(__t%d->%s); ", t, na->data.named_arg.name);
                }
            } else if (ft == TK_STRUCT && na->data.named_arg.value->resolved_type &&
                       na->data.named_arg.value->resolved_type->name &&
                       is_class_type(ctx, na->data.named_arg.value->resolved_type->name)) {
                if (!is_fresh_class_alloc(na->data.named_arg.value)) {
                    cg_emitf(ctx, "__%s_retain(__t%d->%s); ",
                           na->data.named_arg.value->resolved_type->name, t, na->data.named_arg.name);
                }
            }
        }
        cg_emitf(ctx, "__t%d; })", t);
        break;
    }
    case NODE_INDEX:
        if (expr->data.index_access.object->resolved_type &&
            expr->data.index_access.object->resolved_type->kind == TK_ARRAY) {
            /* Array indexing: unbox ZnValue from __zn_arr_get */
            cg_emitf(ctx, "%s(", unbox_func_for(expr->resolved_type ? expr->resolved_type->kind : TK_UNKNOWN));
            cg_emit(ctx, "__zn_arr_get(");
            gen_expr(ctx, expr->data.index_access.object);
            cg_emit(ctx, ", ");
            gen_expr(ctx, expr->data.index_access.index);
            cg_emit(ctx, "))");
        } else if (expr->data.index_access.object->resolved_type &&
                   expr->data.index_access.object->resolved_type->kind == TK_HASH) {
            /* Hash indexing: box key, unbox value from __zn_hash_get */
            cg_emitf(ctx, "%s(", unbox_func_for(expr->resolved_type ? expr->resolved_type->kind : TK_UNKNOWN));
            cg_emit(ctx, "__zn_hash_get(");
            gen_expr(ctx, expr->data.index_access.object);
            cg_emit(ctx, ", ");
            gen_box_expr(ctx, expr->data.index_access.index);
            cg_emit(ctx, "))");
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
        cg_emitf(ctx, "({ ZnArray *__t%d = __zn_arr_alloc(%d); ", t, n > 0 ? n : 4);
        for (NodeList *e = expr->data.array_literal.elems; e; e = e->next) {
            cg_emitf(ctx, "__zn_arr_push(__t%d, ", t);
            gen_box_expr(ctx, e->node);
            cg_emit(ctx, "); ");
        }
        cg_emitf(ctx, "__t%d; })", t);
        break;
    }
    case NODE_HASH_LITERAL: {
        int n = 0;
        for (NodeList *p = expr->data.hash_literal.pairs; p; p = p->next) n++;
        int t = ctx->temp_counter++;
        cg_emitf(ctx, "({ ZnHash *__t%d = __zn_hash_alloc(%d); ", t, n > 0 ? n * 2 : 8);
        for (NodeList *p = expr->data.hash_literal.pairs; p; p = p->next) {
            ASTNode *pair = p->node;
            cg_emitf(ctx, "__zn_hash_set(__t%d, ", t);
            gen_box_expr(ctx, pair->data.hash_pair.key);
            cg_emit(ctx, ", ");
            gen_box_expr(ctx, pair->data.hash_pair.value);
            cg_emit(ctx, "); ");
        }
        cg_emitf(ctx, "__t%d; })", t);
        break;
    }
    case NODE_TYPED_EMPTY_ARRAY: {
        int t = ctx->temp_counter++;
        cg_emitf(ctx, "({ ZnArray *__t%d = __zn_arr_alloc(0); __t%d; })", t, t);
        break;
    }
    case NODE_TYPED_EMPTY_HASH: {
        int t = ctx->temp_counter++;
        cg_emitf(ctx, "({ ZnHash *__t%d = __zn_hash_alloc(8); __t%d; })", t, t);
        break;
    }
    case NODE_OPTIONAL_CHECK: {
        ASTNode *operand = expr->data.optional_check.operand;
        TypeKind ot = operand->resolved_type ? operand->resolved_type->kind : TK_UNKNOWN;
        if (is_ref_type(ot)) {
            /* Reference types: check != NULL */
            cg_emit(ctx, "(");
            gen_expr(ctx, operand);
            cg_emit(ctx, " != NULL)");
        } else {
            /* Value types: check ._has on the __opt_ variable */
            if (operand->type == NODE_IDENT) {
                cg_emitf(ctx, "(__opt_%s._has)", operand->data.ident.name);
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
                cg_emitf(ctx, "({ %s __if_%d = NULL; ", type_to_c(rt), t);
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

        /* Non-optional if/else expression */
        if (rt == TK_STRUCT && expr->resolved_type && expr->resolved_type->name) {
            cg_emitf(ctx, "({ %s __if_%d; ", expr->resolved_type->name, t);
        } else {
            cg_emitf(ctx, "({ %s __if_%d; ", type_to_c(rt), t);
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
        cg_emit(ctx, "} else { ");
        ASTNode *else_b = expr->data.if_expr.else_b;
        if (else_b && else_b->type == NODE_IF) {
            cg_emitf(ctx, "__if_%d = ", t);
            gen_expr(ctx, else_b);
            cg_emit(ctx, "; ");
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
            } else {
                cg_emitf(ctx, "({ %s __loop_%d = NULL; ", type_to_c(rt), t);
            }
        } else {
            cg_emitf(ctx, "({ %s __loop_%d; ", type_to_c(rt), t);
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
    case NODE_VAR_DECL: {
        TypeKind t = node->data.var_decl.value->resolved_type ? node->data.var_decl.value->resolved_type->kind : TK_UNKNOWN;
        /* Optional value type: use ZnOpt_T wrapper */
        int val_is_optional = node->data.var_decl.value->resolved_type ? node->data.var_decl.value->resolved_type->is_optional : 0;
        if (val_is_optional && opt_type_for(t)) {
            cg_emitf(ctx, "%s __opt_%s = ", opt_type_for(t), node->data.var_decl.name);
            gen_expr(ctx, node->data.var_decl.value);
            cg_emit(ctx, ";\n");
        } else if (t == TK_STRUCT && node->data.var_decl.value->resolved_type &&
                   node->data.var_decl.value->resolved_type->name &&
                   is_class_type(ctx, node->data.var_decl.value->resolved_type->name)) {
            const char *sn = node->data.var_decl.value->resolved_type->name;
            cg_emitf(ctx, "%s *%s = ", sn, node->data.var_decl.name);
            gen_expr(ctx, node->data.var_decl.value);
            cg_emit(ctx, ";\n");
            if (!is_fresh_class_alloc(node->data.var_decl.value)) {
                cg_emit_indent(ctx);
                cg_emitf(ctx, "__%s_retain(%s);\n", sn, node->data.var_decl.name);
            }
            if (ctx->scope) {
                cg_scope_add_ref(ctx, node->data.var_decl.name, sn);
            }
        } else if (t == TK_STRUCT && node->data.var_decl.value->resolved_type &&
                   node->data.var_decl.value->resolved_type->name) {
            cg_emitf(ctx, "%s %s = ", node->data.var_decl.value->resolved_type->name,
                   node->data.var_decl.name);
            gen_expr(ctx, node->data.var_decl.value);
            cg_emit(ctx, ";\n");
        } else if (t == TK_STRING) {
            cg_emitf(ctx, "ZnString *%s = ", node->data.var_decl.name);
            gen_expr(ctx, node->data.var_decl.value);
            cg_emit(ctx, ";\n");
            if (!is_fresh_string_alloc(node->data.var_decl.value)) {
                cg_emit_indent(ctx);
                cg_emitf(ctx, "__zn_str_retain(%s);\n", node->data.var_decl.name);
            }
            if (ctx->scope) {
                cg_scope_add_ref(ctx, node->data.var_decl.name, "zn_str");
            }
        } else if (t == TK_ARRAY) {
            cg_emitf(ctx, "ZnArray *%s = ", node->data.var_decl.name);
            gen_expr(ctx, node->data.var_decl.value);
            cg_emit(ctx, ";\n");
            if (!is_fresh_array_alloc(node->data.var_decl.value)) {
                cg_emit_indent(ctx);
                cg_emitf(ctx, "__zn_arr_retain(%s);\n", node->data.var_decl.name);
            }
            if (ctx->scope) {
                cg_scope_add_ref(ctx, node->data.var_decl.name, "zn_arr");
            }
        } else if (t == TK_HASH) {
            cg_emitf(ctx, "ZnHash *%s = ", node->data.var_decl.name);
            gen_expr(ctx, node->data.var_decl.value);
            cg_emit(ctx, ";\n");
            if (!is_fresh_hash_alloc(node->data.var_decl.value)) {
                cg_emit_indent(ctx);
                cg_emitf(ctx, "__zn_hash_retain(%s);\n", node->data.var_decl.name);
            }
            if (ctx->scope) {
                cg_scope_add_ref(ctx, node->data.var_decl.name, "zn_hash");
            }
        } else {
            cg_emitf(ctx, "%s %s = ", type_to_c(t), node->data.var_decl.name);
            gen_expr(ctx, node->data.var_decl.value);
            cg_emit(ctx, ";\n");
        }
        break;
    }
    case NODE_LET_DECL: {
        TypeKind t = node->data.let_decl.value->resolved_type ? node->data.let_decl.value->resolved_type->kind : TK_UNKNOWN;
        /* Optional value type: use ZnOpt_T wrapper */
        int val_is_optional = node->data.let_decl.value->resolved_type ? node->data.let_decl.value->resolved_type->is_optional : 0;
        if (val_is_optional && opt_type_for(t)) {
            cg_emitf(ctx, "const %s __opt_%s = ", opt_type_for(t), node->data.let_decl.name);
            gen_expr(ctx, node->data.let_decl.value);
            cg_emit(ctx, ";\n");
        } else if (t == TK_STRUCT && node->data.let_decl.value->resolved_type &&
                   node->data.let_decl.value->resolved_type->name &&
                   is_class_type(ctx, node->data.let_decl.value->resolved_type->name)) {
            const char *sn = node->data.let_decl.value->resolved_type->name;
            /* let binding = const pointer (prevents reassignment, but fields are mutable) */
            cg_emitf(ctx, "%s *const %s = ", sn, node->data.let_decl.name);
            gen_expr(ctx, node->data.let_decl.value);
            cg_emit(ctx, ";\n");
            if (!is_fresh_class_alloc(node->data.let_decl.value)) {
                cg_emit_indent(ctx);
                cg_emitf(ctx, "__%s_retain(%s);\n", sn, node->data.let_decl.name);
            }
            if (ctx->scope) {
                cg_scope_add_ref(ctx, node->data.let_decl.name, sn);
            }
        } else if (t == TK_STRUCT && node->data.let_decl.value->resolved_type &&
                   node->data.let_decl.value->resolved_type->name) {
            cg_emitf(ctx, "const %s %s = ", node->data.let_decl.value->resolved_type->name,
                   node->data.let_decl.name);
            gen_expr(ctx, node->data.let_decl.value);
            cg_emit(ctx, ";\n");
        } else if (t == TK_STRING) {
            /* String let: not const pointer (we need to release it) */
            cg_emitf(ctx, "ZnString *%s = ", node->data.let_decl.name);
            gen_expr(ctx, node->data.let_decl.value);
            cg_emit(ctx, ";\n");
            if (!is_fresh_string_alloc(node->data.let_decl.value)) {
                cg_emit_indent(ctx);
                cg_emitf(ctx, "__zn_str_retain(%s);\n", node->data.let_decl.name);
            }
            if (ctx->scope) {
                cg_scope_add_ref(ctx, node->data.let_decl.name, "zn_str");
            }
        } else if (t == TK_ARRAY) {
            cg_emitf(ctx, "ZnArray *%s = ", node->data.let_decl.name);
            gen_expr(ctx, node->data.let_decl.value);
            cg_emit(ctx, ";\n");
            if (!is_fresh_array_alloc(node->data.let_decl.value)) {
                cg_emit_indent(ctx);
                cg_emitf(ctx, "__zn_arr_retain(%s);\n", node->data.let_decl.name);
            }
            if (ctx->scope) {
                cg_scope_add_ref(ctx, node->data.let_decl.name, "zn_arr");
            }
        } else if (t == TK_HASH) {
            cg_emitf(ctx, "ZnHash *%s = ", node->data.let_decl.name);
            gen_expr(ctx, node->data.let_decl.value);
            cg_emit(ctx, ";\n");
            if (!is_fresh_hash_alloc(node->data.let_decl.value)) {
                cg_emit_indent(ctx);
                cg_emitf(ctx, "__zn_hash_retain(%s);\n", node->data.let_decl.name);
            }
            if (ctx->scope) {
                cg_scope_add_ref(ctx, node->data.let_decl.name, "zn_hash");
            }
        } else {
            cg_emitf(ctx, "const %s %s = ", type_to_c(t), node->data.let_decl.name);
            gen_expr(ctx, node->data.let_decl.value);
            cg_emit(ctx, ";\n");
        }
        break;
    }
    case NODE_IF: {
        /* Check for type narrowing: if x? { ... uses narrowed x ... } */
        ASTNode *cond = node->data.if_expr.cond;
        int narrowing = 0;
        const char *narrow_name = NULL;
        TypeKind narrow_type = TK_UNKNOWN;

        if (cond && cond->type == NODE_OPTIONAL_CHECK &&
            cond->data.optional_check.operand->type == NODE_IDENT) {
            ASTNode *operand = cond->data.optional_check.operand;
            int operand_is_optional = operand->resolved_type && operand->resolved_type->is_optional;
            TypeKind operand_kind = operand->resolved_type ? operand->resolved_type->kind : TK_UNKNOWN;
            if (operand_is_optional && !is_ref_type(operand_kind)) {
                narrowing = 1;
                narrow_name = operand->data.ident.name;
                narrow_type = operand_kind;
            }
        }

        cg_emit(ctx, "if (");
        gen_expr(ctx, cond);
        cg_emit(ctx, ") ");

        if (narrowing && node->data.if_expr.then_b &&
            node->data.if_expr.then_b->type == NODE_BLOCK) {
            /* Emit narrowed block with shadow variable */
            cg_emit(ctx, "{\n");
            ctx->indent_level++;
            cg_push_scope(ctx, 0);
            cg_emit_indent(ctx);
            cg_emitf(ctx, "%s %s = __opt_%s._val;\n", type_to_c(narrow_type),
                  narrow_name, narrow_name);
            gen_stmts(ctx, node->data.if_expr.then_b->data.block.stmts);
            emit_scope_releases(ctx);
            cg_pop_scope(ctx);
            ctx->indent_level--;
            cg_emit_indent(ctx);
            cg_emit(ctx, "}");
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
                    cg_emit_indent(ctx);
                    cg_emitf(ctx, "__%s_release(%s);\n", v->type_name, v->name);
                }
                if (s == loop) break;
            }
            cg_emit_indent(ctx);
        }
        if (ctx->loop_expr_temp >= 0 && node->data.break_expr.value) {
            if (ctx->loop_expr_optional && opt_type_for(ctx->loop_expr_type)) {
                cg_emitf(ctx, "__loop_%d._has = true; __loop_%d._val = ",
                      ctx->loop_expr_temp, ctx->loop_expr_temp);
            } else {
                cg_emitf(ctx, "__loop_%d = ", ctx->loop_expr_temp);
            }
            gen_expr(ctx, node->data.break_expr.value);
            cg_emit(ctx, ";\n");
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
                    cg_emit_indent(ctx);
                    cg_emitf(ctx, "__%s_release(%s);\n", v->type_name, v->name);
                }
                if (s == loop) break;
            }
            cg_emit_indent(ctx);
        }
        if (ctx->loop_expr_temp >= 0 && node->data.continue_expr.value) {
            if (ctx->loop_expr_optional && opt_type_for(ctx->loop_expr_type)) {
                cg_emitf(ctx, "__loop_%d._has = true; __loop_%d._val = ",
                      ctx->loop_expr_temp, ctx->loop_expr_temp);
            } else {
                cg_emitf(ctx, "__loop_%d = ", ctx->loop_expr_temp);
            }
            gen_expr(ctx, node->data.continue_expr.value);
            cg_emit(ctx, ";\n");
            cg_emit_indent(ctx);
        }
        cg_emit(ctx, "continue;\n");
        break;
    }
    case NODE_RETURN: {
        if (node->data.ret.value &&
            node->data.ret.value->resolved_type &&
            node->data.ret.value->resolved_type->kind == TK_STRING) {
            int t = ctx->temp_counter++;
            cg_emitf(ctx, "ZnString *__ret%d = ", t);
            gen_expr(ctx, node->data.ret.value);
            cg_emit(ctx, ";\n");
            if (!is_fresh_string_alloc(node->data.ret.value)) {
                cg_emit_indent(ctx);
                cg_emitf(ctx, "__zn_str_retain(__ret%d);\n", t);
            }
            emit_all_scope_releases(ctx);
            cg_emit_indent(ctx);
            cg_emitf(ctx, "return __ret%d;\n", t);
        } else if (node->data.ret.value &&
                   node->data.ret.value->resolved_type &&
                   node->data.ret.value->resolved_type->kind == TK_ARRAY) {
            int t = ctx->temp_counter++;
            cg_emitf(ctx, "ZnArray *__ret%d = ", t);
            gen_expr(ctx, node->data.ret.value);
            cg_emit(ctx, ";\n");
            if (!is_fresh_array_alloc(node->data.ret.value)) {
                cg_emit_indent(ctx);
                cg_emitf(ctx, "__zn_arr_retain(__ret%d);\n", t);
            }
            emit_all_scope_releases(ctx);
            cg_emit_indent(ctx);
            cg_emitf(ctx, "return __ret%d;\n", t);
        } else if (node->data.ret.value &&
                   node->data.ret.value->resolved_type &&
                   node->data.ret.value->resolved_type->kind == TK_HASH) {
            int t = ctx->temp_counter++;
            cg_emitf(ctx, "ZnHash *__ret%d = ", t);
            gen_expr(ctx, node->data.ret.value);
            cg_emit(ctx, ";\n");
            if (!is_fresh_hash_alloc(node->data.ret.value)) {
                cg_emit_indent(ctx);
                cg_emitf(ctx, "__zn_hash_retain(__ret%d);\n", t);
            }
            emit_all_scope_releases(ctx);
            cg_emit_indent(ctx);
            cg_emitf(ctx, "return __ret%d;\n", t);
        } else if (node->data.ret.value) {
            TypeKind rt = node->data.ret.value->resolved_type ? node->data.ret.value->resolved_type->kind : TK_UNKNOWN;
            if (rt == TK_STRUCT && node->data.ret.value->resolved_type &&
                node->data.ret.value->resolved_type->name &&
                is_class_type(ctx, node->data.ret.value->resolved_type->name)) {
                const char *cn = node->data.ret.value->resolved_type->name;
                int t = ctx->temp_counter++;
                cg_emitf(ctx, "%s *__ret%d = ", cn, t);
                gen_expr(ctx, node->data.ret.value);
                cg_emit(ctx, ";\n");
                if (!is_fresh_class_alloc(node->data.ret.value)) {
                    cg_emit_indent(ctx);
                    cg_emitf(ctx, "__%s_retain(__ret%d);\n", cn, t);
                }
                emit_all_scope_releases(ctx);
                cg_emit_indent(ctx);
                cg_emitf(ctx, "return __ret%d;\n", t);
            } else if (rt == TK_STRUCT && node->data.ret.value->resolved_type &&
                       node->data.ret.value->resolved_type->name) {
                int t = ctx->temp_counter++;
                cg_emitf(ctx, "%s __ret%d = ", node->data.ret.value->resolved_type->name, t);
                gen_expr(ctx, node->data.ret.value);
                cg_emit(ctx, ";\n");
                emit_all_scope_releases(ctx);
                cg_emit_indent(ctx);
                cg_emitf(ctx, "return __ret%d;\n", t);
            } else if (rt != TK_VOID && rt != TK_UNKNOWN) {
                int t = ctx->temp_counter++;
                cg_emitf(ctx, "%s __ret%d = ", type_to_c(rt), t);
                gen_expr(ctx, node->data.ret.value);
                cg_emit(ctx, ";\n");
                emit_all_scope_releases(ctx);
                cg_emit_indent(ctx);
                cg_emitf(ctx, "return __ret%d;\n", t);
            } else {
                emit_all_scope_releases(ctx);
                cg_emit_indent(ctx);
                cg_emit(ctx, "return ");
                gen_expr(ctx, node->data.ret.value);
                cg_emit(ctx, ";\n");
            }
        } else {
            emit_all_scope_releases(ctx);
            cg_emit_indent(ctx);
            cg_emit(ctx, "return;\n");
        }
        break;
    }
    case NODE_ASSIGN: {
        ASTNode *val = node->data.assign.value;
        TypeKind val_kind = val->resolved_type ? val->resolved_type->kind : TK_UNKNOWN;
        if (val_kind == TK_STRING) {
            int fresh = is_fresh_string_alloc(val);
            cg_emitf(ctx, "__zn_str_release(%s);\n", node->data.assign.name);
            cg_emit_indent(ctx);
            cg_emitf(ctx, "%s = ", node->data.assign.name);
            gen_expr(ctx, val);
            cg_emit(ctx, ";\n");
            if (!fresh) {
                cg_emit_indent(ctx);
                cg_emitf(ctx, "__zn_str_retain(%s);\n", node->data.assign.name);
            }
        } else if (val_kind == TK_STRUCT && val->resolved_type &&
                   val->resolved_type->name &&
                   is_class_type(ctx, val->resolved_type->name)) {
            const char *cn = val->resolved_type->name;
            if (!is_fresh_class_alloc(val)) {
                /* Retain new value first (in case old == new) */
                cg_emitf(ctx, "__%s_retain(", cn);
                gen_expr(ctx, val);
                cg_emit(ctx, ");\n");
                cg_emit_indent(ctx);
            }
            cg_emitf(ctx, "__%s_release(%s);\n", cn, node->data.assign.name);
            cg_emit_indent(ctx);
            cg_emitf(ctx, "%s = ", node->data.assign.name);
            gen_expr(ctx, val);
            cg_emit(ctx, ";\n");
        } else if (val_kind == TK_ARRAY) {
            int fresh = is_fresh_array_alloc(val);
            cg_emitf(ctx, "__zn_arr_release(%s);\n", node->data.assign.name);
            cg_emit_indent(ctx);
            cg_emitf(ctx, "%s = ", node->data.assign.name);
            gen_expr(ctx, val);
            cg_emit(ctx, ";\n");
            if (!fresh) {
                cg_emit_indent(ctx);
                cg_emitf(ctx, "__zn_arr_retain(%s);\n", node->data.assign.name);
            }
        } else if (val_kind == TK_HASH) {
            int fresh = is_fresh_hash_alloc(val);
            cg_emitf(ctx, "__zn_hash_release(%s);\n", node->data.assign.name);
            cg_emit_indent(ctx);
            cg_emitf(ctx, "%s = ", node->data.assign.name);
            gen_expr(ctx, val);
            cg_emit(ctx, ";\n");
            if (!fresh) {
                cg_emit_indent(ctx);
                cg_emitf(ctx, "__zn_hash_retain(%s);\n", node->data.assign.name);
            }
        } else {
            gen_expr(ctx, node);
            cg_emit(ctx, ";\n");
        }
        break;
    }
    case NODE_INDEX_ASSIGN: {
        ASTNode *obj = node->data.index_assign.object;
        TypeKind obj_kind = obj->resolved_type ? obj->resolved_type->kind : TK_UNKNOWN;
        if (obj_kind == TK_ARRAY) {
            cg_emit(ctx, "__zn_arr_set(");
            gen_expr(ctx, obj);
            cg_emit(ctx, ", ");
            gen_expr(ctx, node->data.index_assign.index);
            cg_emit(ctx, ", ");
            gen_box_expr(ctx, node->data.index_assign.value);
            cg_emit(ctx, ");\n");
        } else if (obj_kind == TK_HASH) {
            cg_emit(ctx, "__zn_hash_set(");
            gen_expr(ctx, obj);
            cg_emit(ctx, ", ");
            gen_box_expr(ctx, node->data.index_assign.index);
            cg_emit(ctx, ", ");
            gen_box_expr(ctx, node->data.index_assign.value);
            cg_emit(ctx, ");\n");
        }
        break;
    }
    case NODE_FUNC_DEF:
        break;
    case NODE_FIELD_ASSIGN: {
        ASTNode *obj = node->data.field_assign.object;
        ASTNode *val = node->data.field_assign.value;
        const char *field = node->data.field_assign.field;
        const char *obj_struct_name = obj->resolved_type ? obj->resolved_type->name : NULL;

        if (obj_struct_name && is_class_type(ctx, obj_struct_name)) {
            /* Check if field is a string type for ARC */
            StructDef *sd = lookup_struct(ctx->sem_ctx, obj_struct_name);
            StructFieldDef *fd = NULL;
            if (sd) {
                for (StructFieldDef *f = sd->fields; f; f = f->next) {
                    if (strcmp(f->name, field) == 0) { fd = f; break; }
                }
            }

            if (fd && fd->type->kind == TK_STRING) {
                /* String field: release old, assign new, retain if not fresh */
                cg_emitf(ctx, "__zn_str_release(");
                gen_expr(ctx, obj);
                cg_emitf(ctx, "->%s);\n", field);
                cg_emit_indent(ctx);
                gen_expr(ctx, obj);
                cg_emitf(ctx, "->%s = ", field);
                gen_expr(ctx, val);
                cg_emit(ctx, ";\n");
                if (!is_fresh_string_alloc(val)) {
                    cg_emit_indent(ctx);
                    cg_emitf(ctx, "__zn_str_retain(");
                    gen_expr(ctx, obj);
                    cg_emitf(ctx, "->%s);\n", field);
                }
            } else {
                gen_expr(ctx, node);
                cg_emit(ctx, ";\n");
            }
        } else {
            gen_expr(ctx, node);
            cg_emit(ctx, ";\n");
        }
        break;
    }
    default:
        gen_expr(ctx, node);
        cg_emit(ctx, ";\n");
        break;
    }
}

void gen_stmts(CodegenContext *ctx, NodeList *stmts) {
    for (NodeList *s = stmts; s; s = s->next) {
        if (s->node && s->node->type != NODE_FUNC_DEF) {
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
    } else if (ret_type == TK_STRUCT && sym && sym->type->name) {
        ret_is_class = is_class_type(ctx, sym->type->name);
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
        if (ti->kind == TK_STRUCT && ti->struct_name && is_class_type(ctx, ti->struct_name)) {
            fprintf(out, "const %s *%s", ti->struct_name, p->node->data.param.name);
        } else if (ti->kind == TK_STRUCT && ti->struct_name) {
            fprintf(out, "const %s %s", ti->struct_name, p->node->data.param.name);
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
            if (!is_fresh_string_alloc(last_node)) {
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
            if (!is_fresh_array_alloc(last_node)) {
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
            if (!is_fresh_hash_alloc(last_node)) {
                cg_emit_indent(ctx);
                cg_emitf(ctx, "__zn_hash_retain(__ret%d);\n", t);
            }
            emit_scope_releases(ctx);
            cg_emit_indent(ctx);
            cg_emitf(ctx, "return __ret%d;\n", t);
        } else if (ret_type == TK_STRUCT && last_node->resolved_type &&
                   last_node->resolved_type->name &&
                   is_class_type(ctx, last_node->resolved_type->name)) {
            int t = ctx->temp_counter++;
            cg_emit_indent(ctx);
            cg_emitf(ctx, "%s *__ret%d = ", last_node->resolved_type->name, t);
            gen_expr(ctx, last_node);
            cg_emit(ctx, ";\n");
            if (!is_fresh_class_alloc(last_node)) {
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
