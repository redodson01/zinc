#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "semantic.h"

/* Variadic error reporting with line numbers */
static void semantic_errorf(SemanticContext *ctx, int line, const char *fmt, ...) {
    fprintf(stderr, "Semantic error at line %d: ", line);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    ctx->error_count++;
}

/* Hash function for symbol names (djb2) */
static unsigned int hash_name(const char *name) {
    unsigned int h = 5381;
    for (const char *p = name; *p; p++)
        h = ((h << 5) + h) ^ (unsigned char)*p;
    return h % SCOPE_BUCKETS;
}

/* Scope management */
static Scope *scope_create(Scope *parent) {
    Scope *s = calloc(1, sizeof(Scope));
    s->parent = parent;
    return s;
}

static void scope_free(Scope *s) {
    for (int i = 0; i < SCOPE_BUCKETS; i++) {
        Symbol *sym = s->buckets[i];
        while (sym) {
            Symbol *next = sym->next;
            free(sym->name);
            type_free(sym->type);
            if (sym->param_types) {
                for (int j = 0; j < sym->param_count; j++)
                    type_free(sym->param_types[j]);
                free(sym->param_types);
            }
            free(sym);
            sym = next;
        }
    }
    free(s);
}

static void push_scope(SemanticContext *ctx) {
    ctx->current_scope = scope_create(ctx->current_scope);
}

static void pop_scope(SemanticContext *ctx) {
    Scope *old = ctx->current_scope;
    ctx->current_scope = old->parent;
    scope_free(old);
}

/* Symbol table operations */
static Symbol *lookup_local(Scope *scope, const char *name) {
    unsigned int idx = hash_name(name);
    for (Symbol *s = scope->buckets[idx]; s; s = s->next) {
        if (strcmp(s->name, name) == 0) return s;
    }
    return NULL;
}

Symbol *lookup(SemanticContext *ctx, const char *name) {
    for (Scope *s = ctx->current_scope; s; s = s->parent) {
        Symbol *sym = lookup_local(s, name);
        if (sym) return sym;
    }
    return NULL;
}

static Symbol *add_symbol(SemanticContext *ctx, int line, const char *name,
                           Type *type, int is_const) {
    if (lookup_local(ctx->current_scope, name)) {
        semantic_errorf(ctx, line, "variable '%s' already declared in this scope", name);
        return NULL;
    }
    Symbol *sym = calloc(1, sizeof(Symbol));
    sym->name = strdup(name);
    sym->type = type_clone(type);
    sym->is_const = is_const;
    unsigned int idx = hash_name(name);
    sym->next = ctx->current_scope->buckets[idx];
    ctx->current_scope->buckets[idx] = sym;
    return sym;
}

static Symbol *add_function(SemanticContext *ctx, int line, const char *name,
                             Type *return_type, int param_count,
                             Type **param_types) {
    if (lookup_local(ctx->current_scope, name)) {
        semantic_errorf(ctx, line, "function '%s' already declared in this scope", name);
        return NULL;
    }
    Symbol *sym = calloc(1, sizeof(Symbol));
    sym->name = strdup(name);
    sym->type = type_clone(return_type);
    sym->is_function = 1;
    sym->param_count = param_count;
    if (param_count > 0) {
        sym->param_types = malloc(param_count * sizeof(Type*));
        for (int i = 0; i < param_count; i++)
            sym->param_types[i] = type_clone(param_types[i]);
    }
    unsigned int idx = hash_name(name);
    sym->next = ctx->current_scope->buckets[idx];
    ctx->current_scope->buckets[idx] = sym;
    return sym;
}

/* Forward declarations */
static void analyze_stmt(SemanticContext *ctx, ASTNode *node);
static void analyze_stmts(SemanticContext *ctx, NodeList *stmts);

/* Check if a condition is always true (for infinite loops).
   Handles both `true` literal and `!false` (from desugared `until false`). */
static int is_always_true(ASTNode *expr) {
    if (!expr) return 0;
    /* Direct `true` literal */
    if (expr->type == NODE_BOOL && expr->data.bval) return 1;
    /* `!false` — produced by `until false` -> `while (!false)` */
    if (expr->type == NODE_UNARYOP && expr->data.unaryop.op == OP_NOT) {
        ASTNode *inner = expr->data.unaryop.operand;
        if (inner && inner->type == NODE_BOOL && !inner->data.bval) return 1;
    }
    return 0;
}

/* Human-readable type names for error messages */
static const char *type_kind_name(TypeKind t) {
    switch (t) {
    case TK_INT:    return "int";
    case TK_FLOAT:  return "float";
    case TK_BOOL:   return "bool";
    case TK_CHAR:   return "char";
    case TK_VOID:   return "void";
    default:        return "unknown";
    }
}

/* Type inference — sets resolved_type on nodes */
Type *get_expr_type(SemanticContext *ctx, ASTNode *expr) {
    static Type void_type = {TK_VOID};
    if (!expr) return &void_type;

    /* Return cached type if already resolved */
    if (expr->resolved_type) {
        return expr->resolved_type;
    }

    TypeKind result = TK_UNKNOWN;

    switch (expr->type) {
    case NODE_INT:    result = TK_INT; break;
    case NODE_FLOAT:  result = TK_FLOAT; break;
    case NODE_BOOL:   result = TK_BOOL; break;
    case NODE_CHAR:   result = TK_CHAR; break;
    case NODE_IDENT: {
        Symbol *sym = lookup(ctx, expr->data.ident.name);
        if (!sym) {
            semantic_errorf(ctx, expr->line, "undefined variable '%s'",
                            expr->data.ident.name);
            result = TK_UNKNOWN;
        } else {
            type_free(expr->resolved_type);
            expr->resolved_type = type_clone(sym->type);
            return expr->resolved_type;
        }
        break;
    }
    case NODE_BINOP: {
        TypeKind left = get_expr_type(ctx, expr->data.binop.left)->kind;
        TypeKind right = get_expr_type(ctx, expr->data.binop.right)->kind;
        OpKind op = expr->data.binop.op;

        /* Comparison operators return bool */
        if (op == OP_EQ || op == OP_NE ||
            op == OP_LT || op == OP_GT ||
            op == OP_LE || op == OP_GE) {
            result = TK_BOOL;
        }
        /* Logical operators return bool */
        else if (op == OP_AND || op == OP_OR) {
            result = TK_BOOL;
        }
        /* Arithmetic: if either is float, result is float */
        else if (left == TK_FLOAT || right == TK_FLOAT) {
            result = TK_FLOAT;
        } else {
            result = TK_INT;
        }
        break;
    }
    case NODE_UNARYOP: {
        OpKind op = expr->data.unaryop.op;
        if (op == OP_NOT) {
            result = TK_BOOL;
        } else {
            result = get_expr_type(ctx, expr->data.unaryop.operand)->kind;
        }
        break;
    }
    case NODE_CALL: {
        Symbol *sym = lookup(ctx, expr->data.call.name);
        if (!sym) {
            semantic_errorf(ctx, expr->line, "undefined function '%s'",
                            expr->data.call.name);
            result = TK_UNKNOWN;
        } else if (!sym->is_function) {
            semantic_errorf(ctx, expr->line, "'%s' is not a function",
                            expr->data.call.name);
            result = TK_UNKNOWN;
        } else {
            type_free(expr->resolved_type);
            expr->resolved_type = type_clone(sym->type);
            return expr->resolved_type;
        }
        break;
    }
    case NODE_ASSIGN:
        result = get_expr_type(ctx, expr->data.assign.value)->kind;
        break;
    case NODE_COMPOUND_ASSIGN:
        result = get_expr_type(ctx, expr->data.compound_assign.value)->kind;
        break;
    case NODE_INCDEC:
        result = TK_INT;
        break;
    case NODE_IF:
    case NODE_WHILE:
    case NODE_FOR:
        result = expr->resolved_type ? expr->resolved_type->kind : TK_UNKNOWN;
        break;
    case NODE_BREAK:
        if (expr->data.break_expr.value) {
            result = get_expr_type(ctx, expr->data.break_expr.value)->kind;
        }
        break;
    case NODE_CONTINUE:
        if (expr->data.continue_expr.value) {
            result = get_expr_type(ctx, expr->data.continue_expr.value)->kind;
        }
        break;
    default:
        result = TK_UNKNOWN;
        break;
    }

    if (!expr->resolved_type)
        expr->resolved_type = type_new(result);
    else
        expr->resolved_type->kind = result;
    return expr->resolved_type;
}

/* Validate that an expression is a legal assignment target.
   Reports errors for undefined variables, constants, and non-lvalues. */
static void check_lvalue(SemanticContext *ctx, ASTNode *tgt, int line, const char *verb) {
    if (tgt->type == NODE_IDENT) {
        Symbol *sym = lookup(ctx, tgt->data.ident.name);
        if (!sym) {
            semantic_errorf(ctx, line, "undefined variable '%s'", tgt->data.ident.name);
        } else if (sym->is_const) {
            semantic_errorf(ctx, line, "cannot %s constant '%s'", verb, tgt->data.ident.name);
        }
    } else {
        semantic_errorf(ctx, line, "invalid assignment target");
    }
}

static void analyze_expr(SemanticContext *ctx, ASTNode *expr) {
    if (!expr) return;

    switch (expr->type) {
    case NODE_IDENT: {
        Symbol *sym = lookup(ctx, expr->data.ident.name);
        if (!sym) {
            semantic_errorf(ctx, expr->line, "undefined variable '%s'",
                            expr->data.ident.name);
        }
        break;
    }
    case NODE_BINOP:
        analyze_expr(ctx, expr->data.binop.left);
        analyze_expr(ctx, expr->data.binop.right);
        break;
    case NODE_UNARYOP:
        analyze_expr(ctx, expr->data.unaryop.operand);
        break;
    case NODE_ASSIGN: {
        analyze_expr(ctx, expr->data.assign.target);
        analyze_expr(ctx, expr->data.assign.value);
        check_lvalue(ctx, expr->data.assign.target, expr->line, "assign to");
        break;
    }
    case NODE_COMPOUND_ASSIGN: {
        analyze_expr(ctx, expr->data.compound_assign.target);
        analyze_expr(ctx, expr->data.compound_assign.value);
        check_lvalue(ctx, expr->data.compound_assign.target, expr->line, "assign to");
        break;
    }
    case NODE_INCDEC: {
        analyze_expr(ctx, expr->data.incdec.target);
        check_lvalue(ctx, expr->data.incdec.target, expr->line, "modify");
        break;
    }
    case NODE_CALL: {
        const char *name = expr->data.call.name;
        Symbol *sym = lookup(ctx, name);

        if (!sym) {
            semantic_errorf(ctx, expr->line, "undefined function '%s'", name);
        } else if (!sym->is_function) {
            semantic_errorf(ctx, expr->line, "'%s' is not a function", name);
        }

        /* Analyze arguments and resolve their types */
        int arg_count = 0;
        for (NodeList *arg = expr->data.call.args; arg; arg = arg->next) {
            analyze_expr(ctx, arg->node);
            get_expr_type(ctx, arg->node);
            arg_count++;
        }

        /* Arity and type checking for known functions */
        if (sym && sym->is_function && sym->param_count >= 0) {
            if (arg_count != sym->param_count) {
                semantic_errorf(ctx, expr->line,
                    "function '%s' expects %d argument(s), got %d",
                    name, sym->param_count, arg_count);
            } else if (sym->param_types) {
                int i = 0;
                for (NodeList *arg = expr->data.call.args; arg; arg = arg->next, i++) {
                    TypeKind expected = sym->param_types[i]->kind;
                    TypeKind actual = arg->node->resolved_type
                        ? arg->node->resolved_type->kind : TK_UNKNOWN;
                    if (actual != TK_UNKNOWN && expected != TK_UNKNOWN && actual != expected) {
                        semantic_errorf(ctx, expr->line,
                            "argument %d of '%s' expects %s, got %s",
                            i + 1, name,
                            type_kind_name(expected), type_kind_name(actual));
                    }
                }
            }
        }
        break;
    }
    case NODE_IF:
    case NODE_WHILE:
    case NODE_FOR:
    case NODE_BREAK:
    case NODE_CONTINUE:
        analyze_stmt(ctx, expr);
        break;
    default:
        break;
    }

    /* Ensure resolved_type is set for this expression */
    get_expr_type(ctx, expr);
}

static void analyze_block(SemanticContext *ctx, ASTNode *block) {
    if (!block || block->type != NODE_BLOCK) return;
    push_scope(ctx);
    analyze_stmts(ctx, block->data.block.stmts);
    pop_scope(ctx);
}

static void analyze_stmt(SemanticContext *ctx, ASTNode *node) {
    if (!node) return;

    switch (node->type) {
    case NODE_DECL: {
        analyze_expr(ctx, node->data.decl.value);
        Type *type = get_expr_type(ctx, node->data.decl.value);
        add_symbol(ctx, node->line, node->data.decl.name, type, node->data.decl.is_const);
        break;
    }
    case NODE_IF: {
        analyze_expr(ctx, node->data.if_expr.cond);
        analyze_block(ctx, node->data.if_expr.then_b);
        if (node->data.if_expr.else_b) {
            if (node->data.if_expr.else_b->type == NODE_IF) {
                analyze_stmt(ctx, node->data.if_expr.else_b);
            } else {
                analyze_block(ctx, node->data.if_expr.else_b);
            }
            /* Compute expression type from matching branch types */
            ASTNode *then_b = node->data.if_expr.then_b;
            ASTNode *else_b = node->data.if_expr.else_b;
            TypeKind then_t = TK_UNKNOWN, else_t = TK_UNKNOWN;
            if (then_b && then_b->type == NODE_BLOCK && then_b->data.block.stmts) {
                NodeList *last = then_b->data.block.stmts;
                while (last->next) last = last->next;
                then_t = get_expr_type(ctx, last->node)->kind;
            }
            if (else_b && else_b->type == NODE_IF) {
                else_t = else_b->resolved_type ? else_b->resolved_type->kind : TK_UNKNOWN;
            } else if (else_b && else_b->type == NODE_BLOCK &&
                       else_b->data.block.stmts) {
                NodeList *last = else_b->data.block.stmts;
                while (last->next) last = last->next;
                else_t = get_expr_type(ctx, last->node)->kind;
            }
            if (then_t != TK_UNKNOWN && then_t != TK_VOID && then_t == else_t) {
                if (!node->resolved_type) node->resolved_type = type_new(then_t);
                else node->resolved_type->kind = then_t;
            }
        }
        break;
    }
    case NODE_WHILE: {
        analyze_expr(ctx, node->data.while_expr.cond);
        Type *saved_lrt = ctx->loop_result_type;
        int saved_lrs = ctx->loop_result_set;
        ctx->loop_result_type = NULL;
        ctx->loop_result_set = 0;
        ctx->in_loop++;
        analyze_block(ctx, node->data.while_expr.body);
        ctx->in_loop--;
        if (ctx->loop_result_set && ctx->loop_result_type) {
            type_free(node->resolved_type);
            node->resolved_type = type_clone(ctx->loop_result_type);
        }
        type_free(ctx->loop_result_type);
        ctx->loop_result_type = saved_lrt;
        ctx->loop_result_set = saved_lrs;
        break;
    }
    case NODE_FOR: {
        push_scope(ctx);
        if (node->data.for_expr.init) {
            analyze_stmt(ctx, node->data.for_expr.init);
        }
        analyze_expr(ctx, node->data.for_expr.cond);
        if (node->data.for_expr.update) {
            analyze_expr(ctx, node->data.for_expr.update);
        }
        Type *saved_lrt = ctx->loop_result_type;
        int saved_lrs = ctx->loop_result_set;
        ctx->loop_result_type = NULL;
        ctx->loop_result_set = 0;
        ctx->in_loop++;
        if (node->data.for_expr.body &&
            node->data.for_expr.body->type == NODE_BLOCK) {
            analyze_stmts(ctx, node->data.for_expr.body->data.block.stmts);
        }
        ctx->in_loop--;
        if (ctx->loop_result_set && ctx->loop_result_type) {
            type_free(node->resolved_type);
            node->resolved_type = type_clone(ctx->loop_result_type);
        }
        type_free(ctx->loop_result_type);
        ctx->loop_result_type = saved_lrt;
        ctx->loop_result_set = saved_lrs;
        pop_scope(ctx);
        break;
    }
    case NODE_BREAK:
        if (!ctx->in_loop) {
            semantic_errorf(ctx, node->line, "'break' outside of loop");
        }
        if (node->data.break_expr.value) {
            analyze_expr(ctx, node->data.break_expr.value);
            Type *bt = get_expr_type(ctx, node->data.break_expr.value);
            if (bt->kind != TK_UNKNOWN && bt->kind != TK_VOID) {
                if (!ctx->loop_result_set) {
                    ctx->loop_result_type = type_clone(bt);
                    ctx->loop_result_set = 1;
                } else if (!type_eq(ctx->loop_result_type, bt)) {
                    semantic_errorf(ctx, node->line,
                        "break/continue value type does not match previous");
                }
            }
        }
        break;
    case NODE_CONTINUE:
        if (!ctx->in_loop) {
            semantic_errorf(ctx, node->line, "'continue' outside of loop");
        }
        if (node->data.continue_expr.value) {
            analyze_expr(ctx, node->data.continue_expr.value);
            Type *ct = get_expr_type(ctx, node->data.continue_expr.value);
            if (ct->kind != TK_UNKNOWN && ct->kind != TK_VOID) {
                if (!ctx->loop_result_set) {
                    ctx->loop_result_type = type_clone(ct);
                    ctx->loop_result_set = 1;
                } else if (!type_eq(ctx->loop_result_type, ct)) {
                    semantic_errorf(ctx, node->line,
                        "break/continue value type does not match previous");
                }
            }
        }
        break;
    case NODE_FUNC_DEF: {
        /* Collect parameter types */
        int param_count = 0;
        for (NodeList *p = node->data.func_def.params; p; p = p->next) {
            param_count++;
        }
        Type **param_types = NULL;
        if (param_count > 0) {
            param_types = malloc(param_count * sizeof(Type*));
            int i = 0;
            for (NodeList *p = node->data.func_def.params; p; p = p->next, i++) {
                param_types[i] = type_new(p->node->data.param.type_info->kind);
            }
        }

        /* Add function to current scope (before body for recursion) */
        Type void_type = { .kind = TK_VOID };
        Symbol *func_sym = add_function(ctx, node->line, node->data.func_def.name,
                                         &void_type, param_count, param_types);
        if (param_types) {
            for (int i = 0; i < param_count; i++)
                type_free(param_types[i]);
            free(param_types);
        }

        /* Analyze function body in new scope */
        push_scope(ctx);

        /* Add parameters to function scope (const by default) */
        for (NodeList *p = node->data.func_def.params; p; p = p->next) {
            TypeInfo *ti = p->node->data.param.type_info;
            Type *ptype = type_new(ti->kind);
            add_symbol(ctx, p->node->line, p->node->data.param.name, ptype, 1);
            type_free(ptype);
        }

        int old_in_function = ctx->in_function;
        Type *old_return_type = ctx->current_func_return_type;
        ctx->in_function = 1;
        ctx->current_func_return_type = NULL;

        if (node->data.func_def.body &&
            node->data.func_def.body->type == NODE_BLOCK) {
            analyze_stmts(ctx, node->data.func_def.body->data.block.stmts);
            /* Infer return type from last expression if not already set */
            if (!ctx->current_func_return_type) {
                NodeList *stmts = node->data.func_def.body->data.block.stmts;
                NodeList *last = stmts;
                while (last && last->next) last = last->next;
                if (last && last->node) {
                    Type *lt = get_expr_type(ctx, last->node);
                    if (lt->kind != TK_UNKNOWN && lt->kind != TK_VOID) {
                        ctx->current_func_return_type = type_clone(lt);
                    }
                }
            }
        }

        /* Update function return type based on what we found */
        if (func_sym && ctx->current_func_return_type) {
            type_free(func_sym->type);
            func_sym->type = type_clone(ctx->current_func_return_type);
        }

        ctx->in_function = old_in_function;
        type_free(ctx->current_func_return_type);
        ctx->current_func_return_type = old_return_type;
        pop_scope(ctx);
        break;
    }
    case NODE_RETURN:
        if (!ctx->in_function) {
            semantic_errorf(ctx, node->line, "'return' outside of function");
        } else if (node->data.ret.value) {
            analyze_expr(ctx, node->data.ret.value);
            Type *ret_type = get_expr_type(ctx, node->data.ret.value);
            if (!ctx->current_func_return_type) {
                if (ret_type->kind != TK_UNKNOWN && ret_type->kind != TK_VOID) {
                    ctx->current_func_return_type = type_clone(ret_type);
                }
            }
        }
        break;
    case NODE_BLOCK:
        analyze_block(ctx, node);
        break;
    default:
        /* Expression statement */
        analyze_expr(ctx, node);
        break;
    }
}

static void analyze_stmts(SemanticContext *ctx, NodeList *stmts) {
    for (NodeList *s = stmts; s; s = s->next) {
        analyze_stmt(ctx, s->node);
    }
}

SemanticContext *semantic_init(void) {
    SemanticContext *ctx = calloc(1, sizeof(SemanticContext));
    ctx->current_scope = scope_create(NULL);
    return ctx;
}

void semantic_free(SemanticContext *ctx) {
    while (ctx->current_scope) {
        pop_scope(ctx);
    }
    type_free(ctx->current_func_return_type);
    type_free(ctx->loop_result_type);
    free(ctx);
}

int analyze(SemanticContext *ctx, ASTNode *root) {
    if (!root || root->type != NODE_PROGRAM) return 1;

    analyze_stmts(ctx, root->data.program.stmts);

    return ctx->error_count;
}
