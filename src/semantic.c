#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "semantic.h"

/* Variadic error reporting with line numbers (#4, #10) */
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

StructDef *lookup_struct(SemanticContext *ctx, const char *name) {
    for (StructDef *s = ctx->struct_defs; s; s = s->next) {
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
                           TypeKind type, int is_const) {
    if (lookup_local(ctx->current_scope, name)) {
        semantic_errorf(ctx, line, "variable '%s' already declared in this scope", name);
        return NULL;
    }
    Symbol *sym = calloc(1, sizeof(Symbol));
    sym->name = strdup(name);
    sym->type = type_new(type);
    sym->is_const = is_const;
    unsigned int idx = hash_name(name);
    sym->next = ctx->current_scope->buckets[idx];
    ctx->current_scope->buckets[idx] = sym;
    return sym;
}

static Symbol *add_function(SemanticContext *ctx, int line, const char *name,
                             TypeKind return_type, int param_count,
                             Type **param_types, int is_extern) {
    if (lookup_local(ctx->current_scope, name)) {
        semantic_errorf(ctx, line, "function '%s' already declared in this scope", name);
        return NULL;
    }
    Symbol *sym = calloc(1, sizeof(Symbol));
    sym->name = strdup(name);
    sym->type = type_new(return_type);
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

static Symbol *add_extern_var(SemanticContext *ctx, int line, const char *name,
                               TypeKind type, int is_const) {
    if (lookup_local(ctx->current_scope, name)) {
        semantic_errorf(ctx, line, "symbol '%s' already declared in this scope", name);
        return NULL;
    }
    Symbol *sym = calloc(1, sizeof(Symbol));
    sym->name = strdup(name);
    sym->type = type_new(type);
    sym->is_const = is_const;
    sym->is_extern = 1;
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
    if (expr->type == NODE_UNARYOP && strcmp(expr->data.unaryop.op, "!") == 0) {
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

/* Short suffix for building canonical type names (tuples, objects) */
static const char *type_kind_suffix(TypeKind t) {
    switch (t) {
    case TK_INT:    return "int";
    case TK_FLOAT:  return "float";
    case TK_STRING: return "str";
    case TK_BOOL:   return "bool";
    case TK_CHAR:   return "char";
    default:        return "unk";
    }
}

/* Type inference — sets resolved_type on nodes */
TypeKind get_expr_type(SemanticContext *ctx, ASTNode *expr) {
    if (!expr) return TK_VOID;

    /* Return cached type if already resolved */
    if (expr->resolved_type) {
        return expr->resolved_type->kind;
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
            return sym->type->kind;  /* already set resolved_type */
        }
        break;
    }
    case NODE_BINOP: {
        TypeKind left = get_expr_type(ctx, expr->data.binop.left);
        TypeKind right = get_expr_type(ctx, expr->data.binop.right);
        const char *op = expr->data.binop.op;

        /* Comparison operators return bool */
        if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
            strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
            strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0) {
            result = TK_BOOL;
        }
        /* Logical operators return bool */
        else if (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
            result = TK_BOOL;
        }
        /* String concatenation: if either side is string, result is string (auto-coercion) */
        else if (strcmp(op, "+") == 0 && (left == TK_STRING || right == TK_STRING)) {
            result = TK_STRING;
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
        const char *op = expr->data.unaryop.op;
        if (strcmp(op, "!") == 0) {
            result = TK_BOOL;
        } else {
            result = get_expr_type(ctx, expr->data.unaryop.operand);
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
            return sym->type->kind;  /* already set resolved_type */
        }
        break;
    }
    case NODE_ASSIGN:
        result = get_expr_type(ctx, expr->data.assign.value);
        break;
    case NODE_COMPOUND_ASSIGN:
        result = get_expr_type(ctx, expr->data.compound_assign.value);
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
    case NODE_ARRAY_LITERAL:
    case NODE_TYPED_EMPTY_ARRAY:
        result = TK_ARRAY;
        break;
    case NODE_HASH_LITERAL:
    case NODE_TYPED_EMPTY_HASH:
        result = TK_HASH;
        break;
    case NODE_INDEX_ASSIGN:
        result = get_expr_type(ctx, expr->data.index_assign.value);
        break;
    case NODE_OPTIONAL_CHECK:
        result = TK_BOOL;
        break;
    case NODE_STRUCT_INIT:
    case NODE_TUPLE:
    case NODE_OBJECT_LITERAL:
        result = TK_STRUCT;
        break;
    case NODE_FIELD_ASSIGN:
        result = get_expr_type(ctx, expr->data.field_assign.value);
        break;
    case NODE_NAMED_ARG:
        result = get_expr_type(ctx, expr->data.named_arg.value);
        break;
    case NODE_IF:
    case NODE_WHILE:
    case NODE_FOR:
        result = expr->resolved_type ? expr->resolved_type->kind : TK_UNKNOWN;
        break;
    case NODE_BREAK:
        if (expr->data.break_expr.value) {
            result = get_expr_type(ctx, expr->data.break_expr.value);
        }
        break;
    case NODE_CONTINUE:
        if (expr->data.continue_expr.value) {
            result = get_expr_type(ctx, expr->data.continue_expr.value);
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
    return result;
}

/* Resolve a TypeInfo with object fields into a registered StructDef.
   Object types ({ name: type, ... }) become anonymous classes with __obj_ prefix (#5). */
static void resolve_type_info(SemanticContext *ctx, TypeInfo *ti) {
    if (!ti || !ti->fields || !ti->is_object) return;

    /* Recursively resolve nested object types */
    for (TypeInfoField *f = ti->fields; f; f = f->next) {
        resolve_type_info(ctx, f->type);
    }

    /* Build canonical name: __obj_field1_type1_field2_type2_... */
    char buf[1024];
    int pos = snprintf(buf, sizeof(buf), "__obj");
    for (TypeInfoField *f = ti->fields; f; f = f->next) {
        if (f->type->kind == TK_STRUCT && f->type->struct_name) {
            pos += snprintf(buf + pos, sizeof(buf) - pos, "_%s_%s",
                            f->name, f->type->struct_name);
        } else {
            pos += snprintf(buf + pos, sizeof(buf) - pos, "_%s_%s",
                            f->name, type_kind_suffix(f->type->kind));
        }
    }
    char *type_name = strdup(buf);

    /* Register as anonymous class if not already present */
    StructDef *sd = lookup_struct(ctx, type_name);
    if (!sd) {
        sd = calloc(1, sizeof(StructDef));
        sd->name = strdup(type_name);
        sd->is_class = 1;
        sd->next = ctx->struct_defs;
        ctx->struct_defs = sd;

        StructFieldDef *fields_head = NULL, *fields_tail = NULL;
        int count = 0;
        for (TypeInfoField *f = ti->fields; f; f = f->next) {
            StructFieldDef *fd = calloc(1, sizeof(StructFieldDef));
            fd->name = strdup(f->name);
            fd->type = type_new(f->type->kind);
            if (f->type->kind == TK_STRUCT && f->type->struct_name)
                fd->type->name = strdup(f->type->struct_name);
            if (fields_tail) fields_tail->next = fd;
            else fields_head = fd;
            fields_tail = fd;
            count++;
        }
        sd->fields = fields_head;
        sd->field_count = count;
    }

    ti->kind = TK_STRUCT;
    ti->struct_name = type_name;
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
        Symbol *sym = lookup(ctx, expr->data.assign.name);
        if (!sym) {
            semantic_errorf(ctx, expr->line, "undefined variable '%s'",
                            expr->data.assign.name);
        } else if (sym->is_const) {
            semantic_errorf(ctx, expr->line, "cannot assign to constant '%s'",
                            expr->data.assign.name);
        } else if (sym->is_extern) {
            semantic_errorf(ctx, expr->line, "cannot assign to extern '%s'",
                            expr->data.assign.name);
        }
        analyze_expr(ctx, expr->data.assign.value);
        check_not_void(ctx, expr->line, expr->data.assign.value, "in assignment");
        break;
    }
    case NODE_COMPOUND_ASSIGN: {
        Symbol *sym = lookup(ctx, expr->data.compound_assign.name);
        if (!sym) {
            semantic_errorf(ctx, expr->line, "undefined variable '%s'",
                            expr->data.compound_assign.name);
        } else if (sym->is_const) {
            semantic_errorf(ctx, expr->line, "cannot assign to constant '%s'",
                            expr->data.compound_assign.name);
        } else if (sym->is_extern) {
            semantic_errorf(ctx, expr->line, "cannot assign to extern '%s'",
                            expr->data.compound_assign.name);
        }
        analyze_expr(ctx, expr->data.compound_assign.value);
        check_not_void(ctx, expr->line, expr->data.compound_assign.value, "in assignment");
        break;
    }
    case NODE_INCDEC: {
        Symbol *sym = lookup(ctx, expr->data.incdec.name);
        if (!sym) {
            semantic_errorf(ctx, expr->line, "undefined variable '%s'",
                            expr->data.incdec.name);
        } else if (sym->is_const) {
            semantic_errorf(ctx, expr->line, "cannot modify constant '%s'",
                            expr->data.incdec.name);
        }
        break;
    }
    case NODE_CALL: {
        const char *name = expr->data.call.name;
        Symbol *sym = lookup(ctx, name);

        /* Check if this is a struct instantiation */
        StructDef *sd = lookup_struct(ctx, name);
        if (sd) {
            /* Re-tag as struct init */
            expr->type = NODE_STRUCT_INIT;
            /* struct_init has same layout as call, no data movement needed */

            /* Validate named args match struct fields */
            for (NodeList *arg = expr->data.struct_init.args; arg; arg = arg->next) {
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
                    for (NodeList *arg = expr->data.struct_init.args; arg; arg = arg->next) {
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

            if (!expr->resolved_type) expr->resolved_type = type_new(TK_STRUCT);
            else expr->resolved_type->kind = TK_STRUCT;
            expr->resolved_type->name = strdup(name);
            break;
        }

        if (!sym) {
            semantic_errorf(ctx, expr->line, "undefined function '%s'", name);
        } else if (!sym->is_function) {
            semantic_errorf(ctx, expr->line, "'%s' is not a function", name);
        }

        /* Analyze arguments and resolve their types */
        for (NodeList *arg = expr->data.call.args; arg; arg = arg->next) {
            analyze_expr(ctx, arg->node);
            get_expr_type(ctx, arg->node);
            check_not_void(ctx, expr->line, arg->node, "as function argument");
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

        /* Array/Hash .length */
        if ((obj_kind == TK_ARRAY || obj_kind == TK_HASH) &&
            strcmp(field, "length") == 0) {
            if (!expr->resolved_type) expr->resolved_type = type_new(TK_INT);
            else expr->resolved_type->kind = TK_INT;
            break;
        }

        /* Struct field access */
        const char *obj_struct_name = (obj->resolved_type) ? obj->resolved_type->name : NULL;
        if (obj_kind != TK_STRUCT || !obj_struct_name) {
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
        TypeKind obj_type = get_expr_type(ctx, expr->data.index_access.object);
        TypeKind idx_type = get_expr_type(ctx, expr->data.index_access.index);
        if (obj_type == TK_ARRAY) {
            Type *obj_t = expr->data.index_access.object->resolved_type;
            TypeKind elem_kind = (obj_t && obj_t->elem) ? obj_t->elem->kind : TK_UNKNOWN;
            if (!expr->resolved_type) expr->resolved_type = type_new(elem_kind);
            else expr->resolved_type->kind = elem_kind;
            if (idx_type != TK_INT) {
                semantic_errorf(ctx, expr->line, "array index must be an int");
            }
        } else if (obj_type == TK_HASH) {
            Type *obj_t = expr->data.index_access.object->resolved_type;
            TypeKind elem_kind = (obj_t && obj_t->elem) ? obj_t->elem->kind : TK_UNKNOWN;
            if (!expr->resolved_type) expr->resolved_type = type_new(elem_kind);
            else expr->resolved_type->kind = elem_kind;
        } else if (obj_type == TK_STRING) {
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
    case NODE_FIELD_ASSIGN: {
        analyze_expr(ctx, expr->data.field_assign.object);
        get_expr_type(ctx, expr->data.field_assign.object);
        analyze_expr(ctx, expr->data.field_assign.value);
        get_expr_type(ctx, expr->data.field_assign.value);

        ASTNode *obj = expr->data.field_assign.object;
        TypeKind obj_kind = obj->resolved_type ? obj->resolved_type->kind : TK_UNKNOWN;
        const char *obj_struct_name = (obj->resolved_type) ? obj->resolved_type->name : NULL;
        if (obj_kind != TK_STRUCT || !obj_struct_name) {
            semantic_errorf(ctx, expr->line, "field assignment on non-struct type");
            break;
        }

        StructDef *fsd = lookup_struct(ctx, obj_struct_name);
        if (!fsd) {
            semantic_errorf(ctx, expr->line, "undefined struct type '%s'", obj_struct_name);
            break;
        }

        StructFieldDef *fd = lookup_struct_field(fsd, expr->data.field_assign.field);
        if (!fd) {
            semantic_errorf(ctx, expr->line, "struct '%s' has no field '%s'",
                            obj_struct_name, expr->data.field_assign.field);
            break;
        }

        /* Check 1: Field-level immutability (let field) */
        if (fd->is_const) {
            semantic_errorf(ctx, expr->line, "cannot assign to immutable field '%s'",
                            expr->data.field_assign.field);
        }

        /* Check 2: Binding-level immutability for value types only.
           Reference types (classes): let binding only prevents reassignment,
           not field mutation. */
        if (!fsd->is_class) {
            ASTNode *cur = obj;
            while (cur->type == NODE_FIELD_ACCESS) {
                cur = cur->data.field_access.object;
            }
            if (cur->type == NODE_IDENT) {
                Symbol *sym = lookup(ctx, cur->data.ident.name);
                if (sym && sym->is_const) {
                    semantic_errorf(ctx, expr->line, "cannot modify field of immutable variable '%s'",
                                    cur->data.ident.name);
                }
            }
        }

        type_free(expr->resolved_type);
        expr->resolved_type = type_clone(fd->type);
        break;
    }
    case NODE_NAMED_ARG:
        analyze_expr(ctx, expr->data.named_arg.value);
        get_expr_type(ctx, expr->data.named_arg.value);
        break;
    case NODE_TUPLE: {
        NodeList *elems = expr->data.tuple.elements;

        /* Analyze all elements */
        for (NodeList *e = elems; e; e = e->next) {
            if (e->node->type == NODE_NAMED_ARG) {
                analyze_expr(ctx, e->node->data.named_arg.value);
                get_expr_type(ctx, e->node->data.named_arg.value);
            } else {
                analyze_expr(ctx, e->node);
                get_expr_type(ctx, e->node);
            }
        }

        /* Build canonical name: __ZnTuple_int_int or __ZnTuple_x_int_y_int */
        char canonical[256];
        int pos = snprintf(canonical, sizeof(canonical), "__ZnTuple");
        int is_named = (elems && elems->node->type == NODE_NAMED_ARG);

        for (NodeList *e = elems; e && pos < (int)sizeof(canonical) - 1; e = e->next) {
            if (is_named) {
                ASTNode *na = e->node;
                TypeKind et = get_expr_type(ctx, na->data.named_arg.value);
                const char *suffix = (et == TK_STRUCT && na->data.named_arg.value->resolved_type
                    && na->data.named_arg.value->resolved_type->name)
                    ? na->data.named_arg.value->resolved_type->name
                    : type_kind_suffix(et);
                pos += snprintf(canonical + pos, sizeof(canonical) - pos, "_%s_%s",
                                na->data.named_arg.name, suffix);
            } else {
                TypeKind et = get_expr_type(ctx, e->node);
                const char *suffix = (et == TK_STRUCT && e->node->resolved_type
                    && e->node->resolved_type->name)
                    ? e->node->resolved_type->name
                    : type_kind_suffix(et);
                pos += snprintf(canonical + pos, sizeof(canonical) - pos, "_%s", suffix);
            }
        }

        /* Find or register anonymous type (#5 — shared pattern for tuples/objects) */
        StructDef *sd = lookup_struct(ctx, canonical);
        if (!sd) {
            sd = calloc(1, sizeof(StructDef));
            sd->name = strdup(canonical);
            sd->next = ctx->struct_defs;
            ctx->struct_defs = sd;

            StructFieldDef *fields_head = NULL, *fields_tail = NULL;
            int idx = 0;
            for (NodeList *e = elems; e; e = e->next, idx++) {
                StructFieldDef *fd = calloc(1, sizeof(StructFieldDef));
                if (is_named) {
                    fd->name = strdup(e->node->data.named_arg.name);
                    TypeKind tk = get_expr_type(ctx, e->node->data.named_arg.value);
                    fd->type = type_new(tk);
                    if (tk == TK_STRUCT && e->node->data.named_arg.value->resolved_type
                        && e->node->data.named_arg.value->resolved_type->name)
                        fd->type->name = strdup(e->node->data.named_arg.value->resolved_type->name);
                } else {
                    char fname[32];
                    snprintf(fname, sizeof(fname), "_%d", idx);
                    fd->name = strdup(fname);
                    TypeKind tk = get_expr_type(ctx, e->node);
                    fd->type = type_new(tk);
                    if (tk == TK_STRUCT && e->node->resolved_type
                        && e->node->resolved_type->name)
                        fd->type->name = strdup(e->node->resolved_type->name);
                }
                fd->is_const = 0;  /* Tuple fields are implicitly var */
                if (fields_tail) { fields_tail->next = fd; } else { fields_head = fd; }
                fields_tail = fd;
                sd->field_count++;
            }
            sd->fields = fields_head;
        }

        if (!expr->resolved_type) expr->resolved_type = type_new(TK_STRUCT);
        else expr->resolved_type->kind = TK_STRUCT;
        expr->resolved_type->name = strdup(canonical);
        break;
    }
    case NODE_OBJECT_LITERAL: {
        NodeList *fields = expr->data.object_literal.fields;

        /* All fields are NODE_NAMED_ARG (enforced by grammar) */
        for (NodeList *f = fields; f; f = f->next) {
            ASTNode *na = f->node;
            analyze_expr(ctx, na->data.named_arg.value);
            get_expr_type(ctx, na->data.named_arg.value);
        }

        /* Build canonical name: __obj_field1_type1_field2_type2 */
        char buf[1024];
        int pos = snprintf(buf, sizeof(buf), "__obj");
        for (NodeList *f = fields; f; f = f->next) {
            ASTNode *na = f->node;
            TypeKind tk = na->data.named_arg.value->resolved_type
                ? na->data.named_arg.value->resolved_type->kind : TK_UNKNOWN;
            if (tk == TK_STRUCT && na->data.named_arg.value->resolved_type
                && na->data.named_arg.value->resolved_type->name) {
                pos += snprintf(buf + pos, sizeof(buf) - pos, "_%s_%s",
                                na->data.named_arg.name, na->data.named_arg.value->resolved_type->name);
            } else {
                pos += snprintf(buf + pos, sizeof(buf) - pos, "_%s_%s",
                                na->data.named_arg.name, type_kind_suffix(tk));
            }
        }
        char *type_name = strdup(buf);

        /* Register anonymous class if not already present (#5 — shared pattern) */
        StructDef *sd = lookup_struct(ctx, type_name);
        if (!sd) {
            sd = calloc(1, sizeof(StructDef));
            sd->name = strdup(type_name);
            sd->is_class = 1;
            sd->next = ctx->struct_defs;
            ctx->struct_defs = sd;

            StructFieldDef *fields_head = NULL, *fields_tail = NULL;
            int count = 0;
            for (NodeList *f = fields; f; f = f->next) {
                ASTNode *na = f->node;
                StructFieldDef *fd = calloc(1, sizeof(StructFieldDef));
                fd->name = strdup(na->data.named_arg.name);
                TypeKind tk = na->data.named_arg.value->resolved_type
                    ? na->data.named_arg.value->resolved_type->kind : TK_UNKNOWN;
                fd->type = type_new(tk);
                if (tk == TK_STRUCT && na->data.named_arg.value->resolved_type
                    && na->data.named_arg.value->resolved_type->name)
                    fd->type->name = strdup(na->data.named_arg.value->resolved_type->name);
                if (fields_tail) fields_tail->next = fd;
                else fields_head = fd;
                fields_tail = fd;
                count++;
            }
            sd->fields = fields_head;
            sd->field_count = count;
        }

        if (!expr->resolved_type) expr->resolved_type = type_new(TK_STRUCT);
        else expr->resolved_type->kind = TK_STRUCT;
        expr->resolved_type->name = strdup(type_name);
        free(type_name);
        break;
    }
    case NODE_ARRAY_LITERAL: {
        TypeKind elem_type = TK_UNKNOWN;
        for (NodeList *e = expr->data.array_literal.elems; e; e = e->next) {
            analyze_expr(ctx, e->node);
            TypeKind et = get_expr_type(ctx, e->node);
            if (elem_type == TK_UNKNOWN) {
                elem_type = et;
            } else if (et != elem_type) {
                semantic_errorf(ctx, expr->line, "array elements must all have the same type");
            }
        }
        if (!expr->resolved_type) expr->resolved_type = type_new(TK_ARRAY);
        else expr->resolved_type->kind = TK_ARRAY;
        expr->resolved_type->elem = type_new(elem_type);
        break;
    }
    case NODE_HASH_LITERAL: {
        TypeKind key_type = TK_UNKNOWN;
        TypeKind val_type = TK_UNKNOWN;
        for (NodeList *p = expr->data.hash_literal.pairs; p; p = p->next) {
            ASTNode *pair = p->node;
            analyze_expr(ctx, pair->data.hash_pair.key);
            analyze_expr(ctx, pair->data.hash_pair.value);
            TypeKind kt = get_expr_type(ctx, pair->data.hash_pair.key);
            TypeKind vt = get_expr_type(ctx, pair->data.hash_pair.value);
            if (key_type == TK_UNKNOWN) {
                key_type = kt;
            } else if (key_type != kt) {
                semantic_errorf(ctx, expr->line, "hash keys must all have the same type");
            }
            if (val_type == TK_UNKNOWN) {
                val_type = vt;
            } else if (val_type != vt) {
                semantic_errorf(ctx, expr->line, "hash values must all have the same type");
            }
        }
        if (!expr->resolved_type) expr->resolved_type = type_new(TK_HASH);
        else expr->resolved_type->kind = TK_HASH;
        expr->resolved_type->key = type_new(key_type);
        expr->resolved_type->elem = type_new(val_type);
        break;
    }
    case NODE_TYPED_EMPTY_ARRAY: {
        if (!expr->resolved_type) expr->resolved_type = type_new(TK_ARRAY);
        else expr->resolved_type->kind = TK_ARRAY;
        expr->resolved_type->elem = type_new(expr->data.typed_empty_array.elem_type);
        break;
    }
    case NODE_TYPED_EMPTY_HASH: {
        if (!expr->resolved_type) expr->resolved_type = type_new(TK_HASH);
        else expr->resolved_type->kind = TK_HASH;
        expr->resolved_type->key = type_new(expr->data.typed_empty_hash.key_type);
        expr->resolved_type->elem = type_new(expr->data.typed_empty_hash.value_type);
        break;
    }
    case NODE_INDEX_ASSIGN: {
        analyze_expr(ctx, expr->data.index_assign.object);
        analyze_expr(ctx, expr->data.index_assign.index);
        analyze_expr(ctx, expr->data.index_assign.value);
        TypeKind obj_type = get_expr_type(ctx, expr->data.index_assign.object);
        TypeKind idx_type = get_expr_type(ctx, expr->data.index_assign.index);
        if (obj_type == TK_STRING) {
            semantic_errorf(ctx, expr->line, "strings are immutable");
        } else if (obj_type == TK_ARRAY) {
            if (idx_type != TK_INT) {
                semantic_errorf(ctx, expr->line, "array index must be an int");
            }
        } else if (obj_type == TK_HASH) {
            /* Hash keys can be any primitive type */
        } else if (obj_type != TK_UNKNOWN) {
            semantic_errorf(ctx, expr->line, "index assignment requires an array or hash");
        }
        break;
    }
    case NODE_OPTIONAL_CHECK: {
        analyze_expr(ctx, expr->data.optional_check.operand);
        ASTNode *operand = expr->data.optional_check.operand;
        TypeKind ot = get_expr_type(ctx, operand);

        /* Check if operand is optional or a reference type */
        int is_opt = operand->resolved_type && operand->resolved_type->is_optional;
        if (!is_opt && operand->type == NODE_IDENT) {
            Symbol *sym = lookup(ctx, operand->data.ident.name);
            if (sym) is_opt = sym->type->is_optional;
        }

        /* Reference types (String) are always checkable (NULL-based) */
        if (!is_opt && ot != TK_STRING) {
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
    case NODE_VAR_DECL: {
        analyze_expr(ctx, node->data.var_decl.value);
        check_not_void(ctx, node->line, node->data.var_decl.value, "as initializer");
        TypeKind type = get_expr_type(ctx, node->data.var_decl.value);
        Symbol *sym = add_symbol(ctx, node->line, node->data.var_decl.name, type, 0);
        if (sym && node->data.var_decl.value->resolved_type) {
            type_free(sym->type);
            sym->type = type_clone(node->data.var_decl.value->resolved_type);
        }
        break;
    }
    case NODE_LET_DECL: {
        analyze_expr(ctx, node->data.let_decl.value);
        check_not_void(ctx, node->line, node->data.let_decl.value, "as initializer");
        TypeKind type = get_expr_type(ctx, node->data.let_decl.value);
        Symbol *sym = add_symbol(ctx, node->line, node->data.let_decl.name, type, 1);
        if (sym && node->data.let_decl.value->resolved_type) {
            type_free(sym->type);
            sym->type = type_clone(node->data.let_decl.value->resolved_type);
        }
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
                    Symbol *shadow = add_symbol(ctx, node->line, narrow_name, orig->type->kind, orig->is_const);
                    if (shadow) shadow->type->is_optional = 0;  /* narrowed to non-optional */
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
                then_t = get_expr_type(ctx, last->node);
            }
            if (else_b && else_b->type == NODE_IF) {
                else_t = else_b->resolved_type ? else_b->resolved_type->kind : TK_UNKNOWN;
            } else if (else_b && else_b->type == NODE_BLOCK &&
                       else_b->data.block.stmts) {
                NodeList *last = else_b->data.block.stmts;
                while (last->next) last = last->next;
                else_t = get_expr_type(ctx, last->node);
            }
            if (then_t != TK_UNKNOWN && then_t != TK_VOID && then_t == else_t) {
                if (!node->resolved_type) node->resolved_type = type_new(then_t);
                else node->resolved_type->kind = then_t;
                /* Propagate struct name for struct-typed if/else */
                if (then_t == TK_STRUCT && then_b->type == NODE_BLOCK && then_b->data.block.stmts) {
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
                TypeKind then_t = get_expr_type(ctx, last->node);
                if (then_t != TK_UNKNOWN && then_t != TK_VOID) {
                    if (!node->resolved_type) node->resolved_type = type_new(then_t);
                    else node->resolved_type->kind = then_t;
                    node->resolved_type->is_optional = 1;
                }
            }
        }
        break;
    }
    case NODE_WHILE: {
        analyze_expr(ctx, node->data.while_expr.cond);
        check_not_void(ctx, node->line, node->data.while_expr.cond, "as condition");
        TypeKind saved_lrt = ctx->loop_result_type;
        int saved_lrs = ctx->loop_result_set;
        ctx->loop_result_type = TK_UNKNOWN;
        ctx->loop_result_set = 0;
        ctx->in_loop++;
        analyze_block(ctx, node->data.while_expr.body);
        ctx->in_loop--;
        if (ctx->loop_result_set) {
            if (!node->resolved_type) node->resolved_type = type_new(ctx->loop_result_type);
            else node->resolved_type->kind = ctx->loop_result_type;
            /* Infinite loops (while true, until false) produce non-optional.
               Conditional loops produce optional. */
            if (!is_always_true(node->data.while_expr.cond)) {
                node->resolved_type->is_optional = 1;
            }
        }
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
        TypeKind saved_lrt = ctx->loop_result_type;
        int saved_lrs = ctx->loop_result_set;
        ctx->loop_result_type = TK_UNKNOWN;
        ctx->loop_result_set = 0;
        ctx->in_loop++;
        if (node->data.for_expr.body &&
            node->data.for_expr.body->type == NODE_BLOCK) {
            analyze_stmts(ctx, node->data.for_expr.body->data.block.stmts);
        }
        ctx->in_loop--;
        if (ctx->loop_result_set) {
            if (!node->resolved_type) node->resolved_type = type_new(ctx->loop_result_type);
            else node->resolved_type->kind = ctx->loop_result_type;
            node->resolved_type->is_optional = 1;  /* for loops are always conditional */
        }
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
            TypeKind bt = get_expr_type(ctx, node->data.break_expr.value);
            if (bt != TK_UNKNOWN && bt != TK_VOID) {
                if (!ctx->loop_result_set) {
                    ctx->loop_result_type = bt;
                    ctx->loop_result_set = 1;
                } else if (ctx->loop_result_type != bt) {
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
            TypeKind ct = get_expr_type(ctx, node->data.continue_expr.value);
            if (ct != TK_UNKNOWN && ct != TK_VOID) {
                if (!ctx->loop_result_set) {
                    ctx->loop_result_type = ct;
                    ctx->loop_result_set = 1;
                } else if (ctx->loop_result_type != ct) {
                    semantic_errorf(ctx, node->line,
                        "break/continue value type does not match previous");
                }
            }
        }
        break;
    case NODE_STRUCT_DEF: {
        /* Shared registration helper for struct/class (#2) */
        if (lookup_struct(ctx, node->data.struct_def.name)) {
            semantic_errorf(ctx, node->line, "struct '%s' already defined",
                            node->data.struct_def.name);
            break;
        }
        StructDef *sd = calloc(1, sizeof(StructDef));
        sd->name = strdup(node->data.struct_def.name);
        sd->next = ctx->struct_defs;
        ctx->struct_defs = sd;

        int field_count = 0;
        StructFieldDef *fields_head = NULL;
        StructFieldDef *fields_tail = NULL;

        for (NodeList *f = node->data.struct_def.fields; f; f = f->next) {
            ASTNode *field = f->node;

            /* Check for duplicate fields */
            for (StructFieldDef *existing = fields_head; existing; existing = existing->next) {
                if (strcmp(existing->name, field->data.struct_field.name) == 0) {
                    semantic_errorf(ctx, field->line, "duplicate field '%s' in struct '%s'",
                                    field->data.struct_field.name, node->data.struct_def.name);
                }
            }

            StructFieldDef *fd = calloc(1, sizeof(StructFieldDef));
            fd->name = strdup(field->data.struct_field.name);
            fd->is_const = field->data.struct_field.is_const;

            if (field->data.struct_field.type_info) {
                fd->type = type_new(field->data.struct_field.type_info->kind);
                if (fd->type->kind == TK_STRUCT && field->data.struct_field.type_info->struct_name) {
                    fd->type->name = strdup(field->data.struct_field.type_info->struct_name);
                    if (!lookup_struct(ctx, fd->type->name)) {
                        semantic_errorf(ctx, field->line, "undefined struct type '%s'",
                                        fd->type->name);
                    }
                }
                fd->has_default = 0;
            } else if (field->data.struct_field.default_value) {
                analyze_expr(ctx, field->data.struct_field.default_value);
                TypeKind tk = get_expr_type(ctx, field->data.struct_field.default_value);
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
    case NODE_CLASS_DEF: {
        /* Classes reuse the struct registration helper (#2), with is_class=1 */
        if (lookup_struct(ctx, node->data.struct_def.name)) {
            semantic_errorf(ctx, node->line, "class '%s' already defined",
                            node->data.struct_def.name);
            break;
        }
        StructDef *sd = calloc(1, sizeof(StructDef));
        sd->name = strdup(node->data.struct_def.name);
        sd->is_class = 1;
        sd->next = ctx->struct_defs;
        ctx->struct_defs = sd;

        int field_count = 0;
        StructFieldDef *fields_head = NULL;
        StructFieldDef *fields_tail = NULL;

        for (NodeList *f = node->data.struct_def.fields; f; f = f->next) {
            ASTNode *field = f->node;

            for (StructFieldDef *existing = fields_head; existing; existing = existing->next) {
                if (strcmp(existing->name, field->data.struct_field.name) == 0) {
                    semantic_errorf(ctx, field->line, "duplicate field '%s' in class '%s'",
                                    field->data.struct_field.name, node->data.struct_def.name);
                }
            }

            StructFieldDef *fd = calloc(1, sizeof(StructFieldDef));
            fd->name = strdup(field->data.struct_field.name);
            fd->is_const = field->data.struct_field.is_const;

            if (field->data.struct_field.type_info) {
                fd->type = type_new(field->data.struct_field.type_info->kind);
                if (fd->type->kind == TK_STRUCT && field->data.struct_field.type_info->struct_name) {
                    fd->type->name = strdup(field->data.struct_field.type_info->struct_name);
                }
                fd->has_default = 0;
            } else if (field->data.struct_field.default_value) {
                analyze_expr(ctx, field->data.struct_field.default_value);
                TypeKind tk = get_expr_type(ctx, field->data.struct_field.default_value);
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
        /* Resolve object types in parameters */
        for (NodeList *p = node->data.func_def.params; p; p = p->next) {
            resolve_type_info(ctx, p->node->data.param.type_info);
        }
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
        Symbol *func_sym = add_function(ctx, node->line, node->data.func_def.name,
                                         TK_VOID, param_count, param_types, 0);
        if (param_types) {
            for (int i = 0; i < param_count; i++)
                type_free(param_types[i]);
            free(param_types);
        }

        /* Store param struct names on function symbol */
        if (func_sym && param_count > 0) {
            int i = 0;
            for (NodeList *p = node->data.func_def.params; p; p = p->next, i++) {
                TypeInfo *ti = p->node->data.param.type_info;
                if (ti->kind == TK_STRUCT && ti->struct_name) {
                    func_sym->param_types[i]->name = strdup(ti->struct_name);
                }
            }
        }

        /* Analyze function body in new scope */
        push_scope(ctx);

        /* Add parameters to function scope (const by default) */
        for (NodeList *p = node->data.func_def.params; p; p = p->next) {
            TypeInfo *ti = p->node->data.param.type_info;
            Symbol *psym = add_symbol(ctx, p->node->line, p->node->data.param.name, ti->kind, 1);
            if (psym) {
                if (ti->is_optional) psym->type->is_optional = 1;
                if (ti->kind == TK_STRUCT && ti->struct_name)
                    psym->type->name = strdup(ti->struct_name);
            }
        }

        int old_in_function = ctx->in_function;
        TypeKind old_return_type = ctx->current_func_return_type;
        ctx->in_function = 1;
        ctx->current_func_return_type = TK_VOID;

        if (node->data.func_def.body &&
            node->data.func_def.body->type == NODE_BLOCK) {
            analyze_stmts(ctx, node->data.func_def.body->data.block.stmts);
            /* Infer return type from last expression if not already set */
            if (ctx->current_func_return_type == TK_VOID) {
                NodeList *stmts = node->data.func_def.body->data.block.stmts;
                NodeList *last = stmts;
                while (last && last->next) last = last->next;
                if (last && last->node) {
                    TypeKind lt = get_expr_type(ctx, last->node);
                    if (lt != TK_UNKNOWN && lt != TK_VOID) {
                        ctx->current_func_return_type = lt;
                        /* Capture struct name for struct return types */
                        if (lt == TK_STRUCT && last->node->resolved_type
                            && last->node->resolved_type->name && func_sym) {
                            func_sym->type->name = strdup(last->node->resolved_type->name);
                        }
                    }
                }
            }
        }

        /* Update function return type based on what we found */
        if (func_sym) {
            func_sym->type->kind = ctx->current_func_return_type;
        }

        ctx->in_function = old_in_function;
        ctx->current_func_return_type = old_return_type;
        pop_scope(ctx);
        break;
    }
    case NODE_EXTERN_BLOCK:
        for (NodeList *d = node->data.extern_block.decls; d; d = d->next) {
            analyze_stmt(ctx, d->node);
        }
        break;
    case NODE_EXTERN_FUNC: {
        int param_count = 0;
        for (NodeList *p = node->data.extern_func.params; p; p = p->next) {
            param_count++;
        }
        Type **param_types = NULL;
        if (param_count > 0) {
            param_types = malloc(param_count * sizeof(Type*));
            int i = 0;
            for (NodeList *p = node->data.extern_func.params; p; p = p->next, i++) {
                param_types[i] = type_new(p->node->data.param.type_info->kind);
            }
        }
        TypeKind ret_type = node->data.extern_func.return_type
            ? node->data.extern_func.return_type->kind
            : TK_VOID;
        add_function(ctx, node->line, node->data.extern_func.name,
                     ret_type, param_count, param_types, 1);
        if (param_types) {
            for (int i = 0; i < param_count; i++)
                type_free(param_types[i]);
            free(param_types);
        }
        break;
    }
    case NODE_EXTERN_VAR:
        add_extern_var(ctx, node->line, node->data.extern_var.name,
                       node->data.extern_var.type_info->kind, 0);
        break;
    case NODE_EXTERN_LET:
        add_extern_var(ctx, node->line, node->data.extern_let.name,
                       node->data.extern_let.type_info->kind, 1);
        break;
    case NODE_RETURN:
        if (!ctx->in_function) {
            semantic_errorf(ctx, node->line, "'return' outside of function");
        } else if (node->data.ret.value) {
            analyze_expr(ctx, node->data.ret.value);
            TypeKind ret_type = get_expr_type(ctx, node->data.ret.value);
            if (ctx->current_func_return_type == TK_VOID) {
                ctx->current_func_return_type = ret_type;
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
    StructDef *sd = ctx->struct_defs;
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
    free(ctx);
}

int analyze(SemanticContext *ctx, ASTNode *root) {
    if (!root || root->type != NODE_PROGRAM) return 1;

    analyze_stmts(ctx, root->data.program.stmts);

    return ctx->error_count;
}
