#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "semantic.h"

static int is_ref_type(TypeKind t) {
    return t == TK_STRING || t == TK_CLASS;
}

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

/* Hash function for struct names (djb2) */
static unsigned int hash_struct_name(const char *name) {
    unsigned int h = 5381;
    for (const char *p = name; *p; p++)
        h = ((h << 5) + h) ^ (unsigned char)*p;
    return h % STRUCT_BUCKETS;
}

StructDef *lookup_struct(SemanticContext *ctx, const char *name) {
    unsigned int idx = hash_struct_name(name);
    for (StructDef *s = ctx->struct_buckets[idx]; s; s = s->next) {
        if (strcmp(s->name, name) == 0) return s;
    }
    return NULL;
}

static StructFieldDef *lookup_struct_field(StructDef *sd, const char *name) {
    for (StructFieldDef *f = sd->fields; f; f = f->next) {
        if (strcmp(f->name, name) == 0) return f;
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
                             Type **param_types, int is_extern) {
    if (lookup_local(ctx->current_scope, name)) {
        semantic_errorf(ctx, line, "function '%s' already declared in this scope", name);
        return NULL;
    }
    Symbol *sym = calloc(1, sizeof(Symbol));
    sym->name = strdup(name);
    sym->type = type_clone(return_type);
    sym->is_function = 1;
    sym->is_extern = is_extern;
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
    /* `!false` — produced by `until false` → `while (!false)` */
    if (expr->type == NODE_UNARYOP && expr->data.unaryop.op == OP_NOT) {
        ASTNode *inner = expr->data.unaryop.operand;
        if (inner && inner->type == NODE_BOOL && !inner->data.bval) return 1;
    }
    return 0;
}

/* Check if expression is definitively void (extern void func) */
static int is_definitely_void(SemanticContext *ctx, ASTNode *expr) {
    if (!expr) return 0;
    if (expr->type != NODE_CALL) return 0;
    Symbol *sym = lookup(ctx, expr->data.call.name);
    return (sym && sym->is_function && sym->is_extern && sym->type->kind == TK_VOID);
}

static void check_not_void(SemanticContext *ctx, int line, ASTNode *expr, const char *context) {
    if (is_definitely_void(ctx, expr)) {
        semantic_errorf(ctx, line, "cannot use void expression %s", context);
    }
}

/* Human-readable type names for error messages */
static const char *type_kind_name(TypeKind t) {
    switch (t) {
    case TK_INT:    return "int";
    case TK_FLOAT:  return "float";
    case TK_STRING: return "string";
    case TK_BOOL:   return "bool";
    case TK_CHAR:   return "char";
    case TK_VOID:   return "void";
    case TK_STRUCT: return "struct";
    case TK_CLASS:  return "class";
    default:        return "unknown";
    }
}

/* Type inference — sets resolved_type on nodes */
Type *get_expr_type(SemanticContext *ctx, ASTNode *expr) {
    static Type void_type = {TK_VOID, 0, NULL};
    if (!expr) return &void_type;

    /* Return cached type if already resolved */
    if (expr->resolved_type) {
        return expr->resolved_type;
    }

    TypeKind result = TK_UNKNOWN;

    switch (expr->type) {
    case NODE_INT:    result = TK_INT; break;
    case NODE_FLOAT:  result = TK_FLOAT; break;
    case NODE_STRING: result = TK_STRING; break;
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
            return expr->resolved_type;  /* already set resolved_type */
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
        /* String concatenation: if either side is string, result is string (auto-coercion) */
        else if (op == OP_ADD && (left == TK_STRING || right == TK_STRING)) {
            result = TK_STRING;
            expr->is_fresh_alloc = 1;
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
        /* Built-in print already resolved by analyze_expr */
        if (strcmp(expr->data.call.name, "print") == 0) {
            result = TK_VOID;
            break;
        }
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
            TypeKind rk = sym->type->kind;
            if (rk == TK_STRING || rk == TK_CLASS)
                expr->is_fresh_alloc = 1;
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
    case NODE_FIELD_ACCESS:
        /* resolved_type already set during analyze_expr */
        result = expr->resolved_type ? expr->resolved_type->kind : TK_UNKNOWN;
        break;
    case NODE_INDEX:
        /* resolved_type already set during analyze_expr */
        result = expr->resolved_type ? expr->resolved_type->kind : TK_UNKNOWN;
        break;
    case NODE_OPTIONAL_CHECK:
        result = TK_BOOL;
        break;
    case NODE_NAMED_ARG:
        result = get_expr_type(ctx, expr->data.named_arg.value)->kind;
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
   Reports errors for undefined variables, constants, and non-lvalues.
   Returns the StructFieldDef if the target is a struct field access, NULL otherwise. */
static StructFieldDef *check_lvalue(SemanticContext *ctx, ASTNode *tgt, int line, const char *verb) {
    if (tgt->type == NODE_IDENT) {
        Symbol *sym = lookup(ctx, tgt->data.ident.name);
        if (!sym) {
            semantic_errorf(ctx, line, "undefined variable '%s'", tgt->data.ident.name);
        } else if (sym->is_const) {
            semantic_errorf(ctx, line, "cannot %s constant '%s'", verb, tgt->data.ident.name);
        } else if (sym->is_extern) {
            semantic_errorf(ctx, line, "cannot %s extern '%s'", verb, tgt->data.ident.name);
        }
    } else if (tgt->type == NODE_FIELD_ACCESS) {
        ASTNode *obj = tgt->data.field_access.object;
        TypeKind obj_kind = obj->resolved_type ? obj->resolved_type->kind : TK_UNKNOWN;
        const char *obj_sn = obj->resolved_type ? obj->resolved_type->name : NULL;
        if ((obj_kind == TK_STRUCT || obj_kind == TK_CLASS) && obj_sn) {
            StructDef *fsd = lookup_struct(ctx, obj_sn);
            if (fsd) {
                StructFieldDef *fd = lookup_struct_field(fsd, tgt->data.field_access.field);
                if (fd && fd->is_const)
                    semantic_errorf(ctx, line, "cannot %s immutable field '%s'",
                                    verb, tgt->data.field_access.field);
                if (fd && obj_kind != TK_CLASS) {
                    /* Binding immutability for value types */
                    ASTNode *cur = obj;
                    while (cur->type == NODE_FIELD_ACCESS)
                        cur = cur->data.field_access.object;
                    if (cur->type == NODE_IDENT) {
                        Symbol *sym = lookup(ctx, cur->data.ident.name);
                        if (sym && sym->is_const)
                            semantic_errorf(ctx, line, "cannot modify field of immutable variable '%s'",
                                            cur->data.ident.name);
                    }
                }
                return fd;
            }
        }
    } else if (tgt->type == NODE_INDEX) {
        TypeKind obj_type = tgt->data.index_access.object->resolved_type
            ? tgt->data.index_access.object->resolved_type->kind : TK_UNKNOWN;
        if (obj_type == TK_STRING) {
            semantic_errorf(ctx, line, "strings are immutable");
        }
    } else {
        semantic_errorf(ctx, line, "invalid assignment target");
    }
    return NULL;
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
        check_not_void(ctx, expr->line, expr->data.binop.left, "as operand");
        check_not_void(ctx, expr->line, expr->data.binop.right, "as operand");
        break;
    case NODE_UNARYOP:
        analyze_expr(ctx, expr->data.unaryop.operand);
        check_not_void(ctx, expr->line, expr->data.unaryop.operand, "as operand");
        break;
    case NODE_ASSIGN: {
        analyze_expr(ctx, expr->data.assign.target);
        analyze_expr(ctx, expr->data.assign.value);
        check_not_void(ctx, expr->line, expr->data.assign.value, "in assignment");
        StructFieldDef *fd = check_lvalue(ctx, expr->data.assign.target, expr->line, "assign to");
        if (fd) {
            type_free(expr->resolved_type);
            expr->resolved_type = type_clone(fd->type);
        }
        break;
    }
    case NODE_COMPOUND_ASSIGN: {
        analyze_expr(ctx, expr->data.compound_assign.target);
        analyze_expr(ctx, expr->data.compound_assign.value);
        check_not_void(ctx, expr->line, expr->data.compound_assign.value, "in assignment");
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

        /* Check if this is a struct instantiation */
        StructDef *sd = lookup_struct(ctx, name);
        if (sd) {
            expr->data.call.is_struct_init = 1;

            /* Validate named args match struct fields */
            for (NodeList *arg = expr->data.call.args; arg; arg = arg->next) {
                ASTNode *a = arg->node;
                if (a->type == NODE_NAMED_ARG) {
                    StructFieldDef *fd = lookup_struct_field(sd, a->data.named_arg.name);
                    if (!fd) {
                        semantic_errorf(ctx, expr->line, "struct '%s' has no field '%s'",
                                        name, a->data.named_arg.name);
                    }
                    analyze_expr(ctx, a->data.named_arg.value);
                    get_expr_type(ctx, a->data.named_arg.value);
                } else {
                    semantic_errorf(ctx, expr->line, "struct '%s' requires named arguments", name);
                    analyze_expr(ctx, a);
                    get_expr_type(ctx, a);
                }
            }

            /* Check that all required fields (without defaults) are provided */
            for (StructFieldDef *fd = sd->fields; fd; fd = fd->next) {
                if (!fd->has_default) {
                    int found = 0;
                    for (NodeList *arg = expr->data.call.args; arg; arg = arg->next) {
                        if (arg->node->type == NODE_NAMED_ARG &&
                            strcmp(arg->node->data.named_arg.name, fd->name) == 0) {
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        semantic_errorf(ctx, expr->line,
                            "missing required field '%s' for struct '%s'", fd->name, name);
                    }
                }
            }

            if (!expr->resolved_type) expr->resolved_type = type_new(sd->is_class ? TK_CLASS : TK_STRUCT);
            else expr->resolved_type->kind = sd->is_class ? TK_CLASS : TK_STRUCT;
            expr->resolved_type->name = strdup(name);
            if (sd->is_class) expr->is_fresh_alloc = 1;
            break;
        }

        /* Built-in print function */
        if (strcmp(name, "print") == 0) {
            int argc = 0;
            for (NodeList *arg = expr->data.call.args; arg; arg = arg->next) {
                analyze_expr(ctx, arg->node);
                get_expr_type(ctx, arg->node);
                argc++;
            }
            if (argc != 1) {
                semantic_errorf(ctx, expr->line, "print expects exactly 1 argument, got %d", argc);
            } else {
                ASTNode *arg = expr->data.call.args->node;
                TypeKind ak = arg->resolved_type ? arg->resolved_type->kind : TK_UNKNOWN;
                if (ak != TK_STRING && ak != TK_UNKNOWN) {
                    semantic_errorf(ctx, expr->line, "print argument must be a String");
                }
            }
            if (!expr->resolved_type) expr->resolved_type = type_new(TK_VOID);
            else expr->resolved_type->kind = TK_VOID;
            break;
        }

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
            check_not_void(ctx, expr->line, arg->node, "as function argument");
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
    case NODE_FIELD_ACCESS: {
        analyze_expr(ctx, expr->data.field_access.object);
        get_expr_type(ctx, expr->data.field_access.object);
        ASTNode *obj = expr->data.field_access.object;
        const char *field = expr->data.field_access.field;
        TypeKind obj_kind = obj->resolved_type ? obj->resolved_type->kind : TK_UNKNOWN;

        /* String .length */
        if (obj_kind == TK_STRING && strcmp(field, "length") == 0) {
            if (!expr->resolved_type) expr->resolved_type = type_new(TK_INT);
            else expr->resolved_type->kind = TK_INT;
            break;
        }
        if (obj_kind == TK_STRING) {
            semantic_errorf(ctx, expr->line, "string has no field '%s'", field);
            break;
        }

        /* Struct/class field access */
        const char *obj_struct_name = (obj->resolved_type) ? obj->resolved_type->name : NULL;
        if ((obj_kind != TK_STRUCT && obj_kind != TK_CLASS) || !obj_struct_name) {
            if (obj_kind != TK_UNKNOWN)
                semantic_errorf(ctx, expr->line, "field access on non-struct type");
            break;
        }

        StructDef *sd = lookup_struct(ctx, obj_struct_name);
        if (!sd) {
            semantic_errorf(ctx, expr->line, "undefined struct type '%s'", obj_struct_name);
            break;
        }

        StructFieldDef *fd = lookup_struct_field(sd, field);
        if (!fd) {
            semantic_errorf(ctx, expr->line, "struct '%s' has no field '%s'",
                            obj_struct_name, field);
            break;
        }

        type_free(expr->resolved_type);
        expr->resolved_type = type_clone(fd->type);
        break;
    }
    case NODE_INDEX: {
        analyze_expr(ctx, expr->data.index_access.object);
        analyze_expr(ctx, expr->data.index_access.index);
        TypeKind obj_type = get_expr_type(ctx, expr->data.index_access.object)->kind;
        TypeKind idx_type = get_expr_type(ctx, expr->data.index_access.index)->kind;
        if (obj_type == TK_STRING) {
            if (!expr->resolved_type) expr->resolved_type = type_new(TK_CHAR);
            else expr->resolved_type->kind = TK_CHAR;
            if (idx_type != TK_INT && idx_type != TK_UNKNOWN) {
                semantic_errorf(ctx, expr->line, "string index must be an integer");
            }
        } else if (obj_type != TK_UNKNOWN) {
            semantic_errorf(ctx, expr->line, "index operator requires an array, hash, or string");
        }
        break;
    }
    case NODE_NAMED_ARG:
        analyze_expr(ctx, expr->data.named_arg.value);
        get_expr_type(ctx, expr->data.named_arg.value);
        break;
    case NODE_OPTIONAL_CHECK: {
        analyze_expr(ctx, expr->data.optional_check.operand);
        ASTNode *operand = expr->data.optional_check.operand;
        TypeKind ot = get_expr_type(ctx, operand)->kind;

        /* Check if operand is optional or a reference type */
        int is_opt = operand->resolved_type && operand->resolved_type->is_optional;
        if (!is_opt && operand->type == NODE_IDENT) {
            Symbol *sym = lookup(ctx, operand->data.ident.name);
            if (sym) is_opt = sym->type->is_optional;
        }

        /* Reference types (String, classes) are always checkable (NULL-based) */
        if (!is_opt && ot != TK_STRING && ot != TK_CLASS) {
            semantic_errorf(ctx, expr->line, "cannot use '?' on non-optional type");
        }

        if (!expr->resolved_type) expr->resolved_type = type_new(TK_BOOL);
        else expr->resolved_type->kind = TK_BOOL;
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
        check_not_void(ctx, node->line, node->data.decl.value, "as initializer");
        Type *type = get_expr_type(ctx, node->data.decl.value);
        add_symbol(ctx, node->line, node->data.decl.name, type, node->data.decl.is_const);
        break;
    }
    case NODE_IF: {
        analyze_expr(ctx, node->data.if_expr.cond);
        check_not_void(ctx, node->line, node->data.if_expr.cond, "as condition");

        /* Type narrowing: if the condition is `x?` where x is optional,
           create a narrowed (non-optional) shadow in the then block */
        ASTNode *cond = node->data.if_expr.cond;
        int narrowing = 0;
        if (cond && cond->type == NODE_OPTIONAL_CHECK &&
            cond->data.optional_check.operand->type == NODE_IDENT) {
            const char *narrow_name = cond->data.optional_check.operand->data.ident.name;
            Symbol *orig = lookup(ctx, narrow_name);
            if (orig && orig->type->is_optional) {
                narrowing = 1;
                /* Manually analyze then block with narrowed scope */
                if (node->data.if_expr.then_b &&
                    node->data.if_expr.then_b->type == NODE_BLOCK) {
                    push_scope(ctx);
                    Type *narrowed = type_clone(orig->type);
                    narrowed->is_optional = 0;
                    add_symbol(ctx, node->line, narrow_name, narrowed, orig->is_const);
                    type_free(narrowed);
                    analyze_stmts(ctx, node->data.if_expr.then_b->data.block.stmts);
                    pop_scope(ctx);
                }
            }
        }

        if (!narrowing) {
            analyze_block(ctx, node->data.if_expr.then_b);
        }

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
                if (is_ref_type(then_t)) node->is_fresh_alloc = 1;
                /* Propagate struct/class name for struct/class-typed if/else */
                if ((then_t == TK_STRUCT || then_t == TK_CLASS) && then_b->type == NODE_BLOCK && then_b->data.block.stmts) {
                    NodeList *tl = then_b->data.block.stmts;
                    while (tl->next) tl = tl->next;
                    if (tl->node && tl->node->resolved_type && tl->node->resolved_type->name) {
                        node->resolved_type->name = strdup(tl->node->resolved_type->name);
                    }
                }
            }
        } else {
            /* If without else -> optional type */
            ASTNode *then_b = node->data.if_expr.then_b;
            if (then_b && then_b->type == NODE_BLOCK && then_b->data.block.stmts) {
                NodeList *last = then_b->data.block.stmts;
                while (last->next) last = last->next;
                TypeKind then_t = get_expr_type(ctx, last->node)->kind;
                if (then_t != TK_UNKNOWN && then_t != TK_VOID) {
                    if (!node->resolved_type) node->resolved_type = type_new(then_t);
                    else node->resolved_type->kind = then_t;
                    node->resolved_type->is_optional = 1;
                    if (is_ref_type(then_t)) node->is_fresh_alloc = 1;
                }
            }
        }
        break;
    }
    case NODE_WHILE: {
        analyze_expr(ctx, node->data.while_expr.cond);
        check_not_void(ctx, node->line, node->data.while_expr.cond, "as condition");
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
            /* Infinite loops (while true, until false) produce non-optional.
               Conditional loops produce optional. */
            if (!is_always_true(node->data.while_expr.cond)) {
                node->resolved_type->is_optional = 1;
            }
            if (is_ref_type(node->resolved_type->kind)) node->is_fresh_alloc = 1;
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
        check_not_void(ctx, node->line, node->data.for_expr.cond, "as condition");
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
            node->resolved_type->is_optional = 1;  /* for loops are always conditional */
            if (is_ref_type(node->resolved_type->kind)) node->is_fresh_alloc = 1;
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
    case NODE_TYPE_DEF: {
        int is_class = node->data.type_def.is_class;
        const char *def_name = node->data.type_def.name;
        if (lookup_struct(ctx, def_name)) {
            semantic_errorf(ctx, node->line, "%s '%s' already defined",
                            is_class ? "class" : "struct", def_name);
            break;
        }
        StructDef *sd = calloc(1, sizeof(StructDef));
        sd->name = strdup(def_name);
        sd->is_class = is_class;
        unsigned int si = hash_struct_name(def_name);
        sd->next = ctx->struct_buckets[si];
        ctx->struct_buckets[si] = sd;

        int field_count = 0;
        StructFieldDef *fields_head = NULL;
        StructFieldDef *fields_tail = NULL;

        for (NodeList *f = node->data.type_def.fields; f; f = f->next) {
            ASTNode *field = f->node;

            /* Check for duplicate fields */
            for (StructFieldDef *existing = fields_head; existing; existing = existing->next) {
                if (strcmp(existing->name, field->data.struct_field.name) == 0) {
                    semantic_errorf(ctx, field->line, "duplicate field '%s' in %s '%s'",
                                    field->data.struct_field.name,
                                    is_class ? "class" : "struct", def_name);
                }
            }

            StructFieldDef *fd = calloc(1, sizeof(StructFieldDef));
            fd->name = strdup(field->data.struct_field.name);
            fd->is_const = field->data.struct_field.is_const;

            if (field->data.struct_field.type_info) {
                TypeInfo *fti = field->data.struct_field.type_info;
                const char *sn = fti->name;
                if (sn) {
                    StructDef *fsd = lookup_struct(ctx, sn);
                    if (!fsd) {
                        semantic_errorf(ctx, field->line, "undefined type '%s'", sn);
                    }
                }
                fd->type = type_from_info(fti);
                /* Resolve struct that is actually a class */
                if (sn) {
                    StructDef *fsd = lookup_struct(ctx, sn);
                    if (fsd && fsd->is_class) fd->type->kind = TK_CLASS;
                }
                fd->has_default = 0;
            } else if (field->data.struct_field.default_value) {
                analyze_expr(ctx, field->data.struct_field.default_value);
                TypeKind tk = get_expr_type(ctx, field->data.struct_field.default_value)->kind;
                fd->type = type_new(tk);
                fd->has_default = 1;
                fd->default_value = field->data.struct_field.default_value;
            }

            if (fields_tail) {
                fields_tail->next = fd;
            } else {
                fields_head = fd;
            }
            fields_tail = fd;
            field_count++;
        }

        sd->fields = fields_head;
        sd->field_count = field_count;
        break;
    }
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
                param_types[i] = type_from_info(p->node->data.param.type_info);
                /* Resolve struct that is actually a class */
                if (param_types[i]->kind == TK_STRUCT && p->node->data.param.type_info->name) {
                    StructDef *psd = lookup_struct(ctx, p->node->data.param.type_info->name);
                    if (psd && psd->is_class) param_types[i]->kind = TK_CLASS;
                }
            }
        }

        /* Add function to current scope (before body for recursion) */
        Type void_type = { .kind = TK_VOID };
        Symbol *func_sym = add_function(ctx, node->line, node->data.func_def.name,
                                         &void_type, param_count, param_types, 0);
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
            Type *ptype = type_from_info(ti);
            /* Resolve struct that is actually a class */
            if (ptype->kind == TK_STRUCT && ti->name) {
                StructDef *psd = lookup_struct(ctx, ti->name);
                if (psd && psd->is_class) ptype->kind = TK_CLASS;
            }
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
    for (int i = 0; i < STRUCT_BUCKETS; i++) {
        StructDef *sd = ctx->struct_buckets[i];
        while (sd) {
            StructDef *next = sd->next;
            StructFieldDef *fd = sd->fields;
            while (fd) {
                StructFieldDef *fnext = fd->next;
                free(fd->name);
                type_free(fd->type);
                free(fd);
                fd = fnext;
            }
            free(sd->name);
            free(sd);
            sd = next;
        }
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
