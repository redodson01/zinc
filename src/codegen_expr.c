#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "codegen.h"

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

void gen_block(CodegenContext *ctx, ASTNode *block) {
    if (!block || block->type != NODE_BLOCK) return;
    cg_emit(ctx, "{\n");
    ctx->indent_level++;
    gen_stmts(ctx, block->data.block.stmts);
    ctx->indent_level--;
    cg_emit_indent(ctx);
    cg_emit(ctx, "}");
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
        cg_emit(ctx, "(");
        gen_expr(ctx, expr->data.binop.left);
        cg_emitf(ctx, " %s ", op_to_str(expr->data.binop.op));
        gen_expr(ctx, expr->data.binop.right);
        cg_emit(ctx, ")");
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
        /* Regular function call */
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
    case NODE_IF: {
        TypeKind rt = expr->resolved_type ? expr->resolved_type->kind : TK_UNKNOWN;
        if (rt == TK_UNKNOWN || rt == TK_VOID) break;

        int t = ctx->temp_counter++;

        /* Non-optional if/else expression */
        cg_emitf(ctx, "({ %s __if_%d; ", type_to_c(rt), t);
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
        ctx->loop_expr_temp = t;
        cg_emitf(ctx, "({ %s __loop_%d; ", type_to_c(rt), t);
        cg_emit(ctx, "while (");
        gen_expr(ctx, expr->data.while_expr.cond);
        cg_emit(ctx, ") ");
        gen_block(ctx, expr->data.while_expr.body);
        cg_emitf(ctx, " __loop_%d; })", t);
        ctx->loop_expr_temp = saved_let;
        break;
    }
    case NODE_FOR: {
        TypeKind rt = expr->resolved_type ? expr->resolved_type->kind : TK_UNKNOWN;
        if (rt == TK_UNKNOWN || rt == TK_VOID) break;
        int t = ctx->temp_counter++;
        int saved_let = ctx->loop_expr_temp;
        ctx->loop_expr_temp = t;
        cg_emitf(ctx, "({ %s __loop_%d; ", type_to_c(rt), t);
        gen_for_header(ctx, expr);
        gen_block(ctx, expr->data.for_expr.body);
        cg_emitf(ctx, " __loop_%d; })", t);
        ctx->loop_expr_temp = saved_let;
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
        cg_emitf(ctx, "%s%s %s = ", cq, type_to_c(t), name);
        gen_expr(ctx, value);
        cg_emit(ctx, ";\n");
        break;
    }
    case NODE_IF:
        cg_emit(ctx, "if (");
        gen_expr(ctx, node->data.if_expr.cond);
        cg_emit(ctx, ") ");
        gen_block(ctx, node->data.if_expr.then_b);
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
    case NODE_WHILE:
        cg_emit(ctx, "while (");
        gen_expr(ctx, node->data.while_expr.cond);
        cg_emit(ctx, ") ");
        gen_block(ctx, node->data.while_expr.body);
        cg_emit(ctx, "\n");
        break;
    case NODE_FOR:
        gen_for_header(ctx, node);
        gen_block(ctx, node->data.for_expr.body);
        cg_emit(ctx, "\n");
        break;
    case NODE_BREAK:
        if (ctx->loop_expr_temp >= 0 && node->data.break_expr.value) {
            cg_emitf(ctx, "__loop_%d = ", ctx->loop_expr_temp);
            gen_expr(ctx, node->data.break_expr.value);
            cg_emit(ctx, ";\n");
            cg_emit_indent(ctx);
        }
        cg_emit(ctx, "break;\n");
        break;
    case NODE_CONTINUE:
        if (ctx->loop_expr_temp >= 0 && node->data.continue_expr.value) {
            cg_emitf(ctx, "__loop_%d = ", ctx->loop_expr_temp);
            gen_expr(ctx, node->data.continue_expr.value);
            cg_emit(ctx, ";\n");
            cg_emit_indent(ctx);
        }
        cg_emit(ctx, "continue;\n");
        break;
    case NODE_RETURN: {
        ASTNode *rv = node->data.ret.value;
        if (!rv) {
            cg_emit(ctx, "return;\n");
        } else {
            cg_emit(ctx, "return ");
            gen_expr(ctx, rv);
            cg_emit(ctx, ";\n");
        }
        break;
    }
    case NODE_ASSIGN: {
        gen_expr(ctx, node);
        cg_emit(ctx, ";\n");
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
    if (strcmp(func->data.func_def.name, "main") == 0) {
        ret_str = "int";
    } else {
        ret_str = type_to_c(ret_type);
    }

    fprintf(out, "%s %s(", ret_str, func->data.func_def.name);

    int first = 1;
    for (NodeList *p = func->data.func_def.params; p; p = p->next) {
        if (!first) fprintf(out, ", ");
        TypeInfo *ti = p->node->data.param.type_info;
        fprintf(out, "const %s %s", type_to_c(ti->kind),
                p->node->data.param.name);
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

    NodeList *stmts = block->data.block.stmts;
    if (!stmts) {
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
        } else {
            cg_emit_indent(ctx);
            cg_emitf(ctx, "return ");
            gen_expr(ctx, last_node);
            cg_emit(ctx, ";\n");
        }
    }

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
