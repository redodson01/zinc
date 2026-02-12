#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

const char *op_to_str(OpKind op) {
    switch (op) {
    case OP_ADD: return "+";
    case OP_SUB: return "-";
    case OP_MUL: return "*";
    case OP_DIV: return "/";
    case OP_MOD: return "%";
    case OP_EQ:  return "==";
    case OP_NE:  return "!=";
    case OP_LT:  return "<";
    case OP_GT:  return ">";
    case OP_LE:  return "<=";
    case OP_GE:  return ">=";
    case OP_AND: return "&&";
    case OP_OR:  return "||";
    case OP_NOT: return "!";
    case OP_NEG: return "-";
    case OP_POS: return "+";
    case OP_INC: return "++";
    case OP_DEC: return "--";
    case OP_ASSIGN: return "=";
    case OP_ADD_ASSIGN: return "+=";
    case OP_SUB_ASSIGN: return "-=";
    case OP_MUL_ASSIGN: return "*=";
    case OP_DIV_ASSIGN: return "/=";
    case OP_MOD_ASSIGN: return "%=";
    }
    return "?";
}

/* --- Type helpers --- */

Type *type_new(TypeKind kind) {
    Type *t = calloc(1, sizeof(Type));
    t->kind = kind;
    return t;
}

Type *type_clone(const Type *t) {
    if (!t) return NULL;
    Type *c = calloc(1, sizeof(Type));
    c->kind = t->kind;
    c->is_optional = t->is_optional;
    c->name = t->name ? strdup(t->name) : NULL;
    return c;
}

void type_free(Type *t) {
    if (!t) return;
    free(t->name);
    free(t);
}

int type_eq(const Type *a, const Type *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->kind != b->kind) return 0;
    if (a->is_optional != b->is_optional) return 0;
    if (a->kind == TK_STRUCT) {
        if (!a->name || !b->name) return a->name == b->name;
        return strcmp(a->name, b->name) == 0;
    }
    return 1;
}

static ASTNode *alloc_node(NodeType type) {
    ASTNode *n = calloc(1, sizeof(ASTNode));
    n->type = type;
    n->string_id = -1;
    n->is_fresh_alloc = 0;
    return n;
}

TypeInfo *make_type_info(TypeKind kind) {
    TypeInfo *t = calloc(1, sizeof(TypeInfo));
    t->kind = kind;
    return t;
}

TypeInfo *make_optional_type(TypeInfo *base) {
    if (base) base->is_optional = 1;
    return base;
}

ASTNode *make_program(NodeList *stmts) {
    ASTNode *n = alloc_node(NODE_PROGRAM);
    n->data.program.stmts = stmts;
    return n;
}

ASTNode *make_block(NodeList *stmts) {
    ASTNode *n = alloc_node(NODE_BLOCK);
    n->data.block.stmts = stmts;
    return n;
}

ASTNode *make_int(int64_t val) {
    ASTNode *n = alloc_node(NODE_INT);
    n->data.ival = val;
    return n;
}

ASTNode *make_float(double val) {
    ASTNode *n = alloc_node(NODE_FLOAT);
    n->data.dval = val;
    return n;
}

ASTNode *make_string(char *val) {
    ASTNode *n = alloc_node(NODE_STRING);
    n->data.sval = val;
    return n;
}

ASTNode *make_bool(int val) {
    ASTNode *n = alloc_node(NODE_BOOL);
    n->data.bval = val;
    return n;
}

ASTNode *make_char(char val) {
    ASTNode *n = alloc_node(NODE_CHAR);
    n->data.cval = val;
    return n;
}

ASTNode *make_ident(char *name) {
    ASTNode *n = alloc_node(NODE_IDENT);
    n->data.ident.name = name;
    return n;
}

ASTNode *make_typed_param(char *name, TypeInfo *type_info) {
    ASTNode *n = alloc_node(NODE_PARAM);
    n->data.param.name = name;
    n->data.param.type_info = type_info;
    return n;
}

ASTNode *make_binop(ASTNode *left, OpKind op, ASTNode *right) {
    ASTNode *n = alloc_node(NODE_BINOP);
    n->data.binop.left = left;
    n->data.binop.op = op;
    n->data.binop.right = right;
    return n;
}

ASTNode *make_unaryop(OpKind op, ASTNode *operand) {
    ASTNode *n = alloc_node(NODE_UNARYOP);
    n->data.unaryop.op = op;
    n->data.unaryop.operand = operand;
    return n;
}

ASTNode *make_assign(ASTNode *target, ASTNode *value) {
    ASTNode *n = alloc_node(NODE_ASSIGN);
    n->data.assign.target = target;
    n->data.assign.value = value;
    return n;
}

ASTNode *make_compound_assign(ASTNode *target, OpKind op, ASTNode *value) {
    ASTNode *n = alloc_node(NODE_COMPOUND_ASSIGN);
    n->data.compound_assign.target = target;
    n->data.compound_assign.op = op;
    n->data.compound_assign.value = value;
    return n;
}

ASTNode *make_incdec(ASTNode *target, OpKind op, int is_prefix) {
    ASTNode *n = alloc_node(NODE_INCDEC);
    n->data.incdec.target = target;
    n->data.incdec.op = op;
    n->data.incdec.is_prefix = is_prefix;
    return n;
}

ASTNode *make_decl(char *name, ASTNode *value, int is_const) {
    ASTNode *n = alloc_node(NODE_DECL);
    n->data.decl.name = name;
    n->data.decl.value = value;
    n->data.decl.is_const = is_const;
    return n;
}

ASTNode *make_if(ASTNode *cond, ASTNode *then_b, ASTNode *else_b) {
    ASTNode *n = alloc_node(NODE_IF);
    n->data.if_expr.cond = cond;
    n->data.if_expr.then_b = then_b;
    n->data.if_expr.else_b = else_b;
    return n;
}

ASTNode *make_while(ASTNode *cond, ASTNode *body) {
    ASTNode *n = alloc_node(NODE_WHILE);
    n->data.while_expr.cond = cond;
    n->data.while_expr.body = body;
    return n;
}

ASTNode *make_for(ASTNode *init, ASTNode *cond, ASTNode *update, ASTNode *body) {
    ASTNode *n = alloc_node(NODE_FOR);
    n->data.for_expr.init = init;
    n->data.for_expr.cond = cond;
    n->data.for_expr.update = update;
    n->data.for_expr.body = body;
    return n;
}

ASTNode *make_break_expr(ASTNode *value) {
    ASTNode *n = alloc_node(NODE_BREAK);
    n->data.break_expr.value = value;
    return n;
}

ASTNode *make_continue_expr(ASTNode *value) {
    ASTNode *n = alloc_node(NODE_CONTINUE);
    n->data.continue_expr.value = value;
    return n;
}

ASTNode *make_func_def(char *name, NodeList *params, ASTNode *body) {
    ASTNode *n = alloc_node(NODE_FUNC_DEF);
    n->data.func_def.name = name;
    n->data.func_def.params = params;
    n->data.func_def.return_type = NULL;
    n->data.func_def.body = body;
    return n;
}

ASTNode *make_call(char *name, NodeList *args) {
    ASTNode *n = alloc_node(NODE_CALL);
    n->data.call.name = name;
    n->data.call.args = args;
    return n;
}

ASTNode *make_return(ASTNode *value) {
    ASTNode *n = alloc_node(NODE_RETURN);
    n->data.ret.value = value;
    return n;
}

ASTNode *make_field_access(ASTNode *object, char *field) {
    ASTNode *n = alloc_node(NODE_FIELD_ACCESS);
    n->data.field_access.object = object;
    n->data.field_access.field = field;
    return n;
}

ASTNode *make_index_access(ASTNode *object, ASTNode *index) {
    ASTNode *n = alloc_node(NODE_INDEX);
    n->data.index_access.object = object;
    n->data.index_access.index = index;
    return n;
}

ASTNode *make_optional_check(ASTNode *operand) {
    ASTNode *n = alloc_node(NODE_OPTIONAL_CHECK);
    n->data.optional_check.operand = operand;
    return n;
}

ASTNode *make_type_def(char *name, NodeList *fields, int is_class) {
    ASTNode *n = alloc_node(NODE_TYPE_DEF);
    n->data.type_def.name = name;
    n->data.type_def.fields = fields;
    n->data.type_def.is_class = is_class;
    return n;
}

ASTNode *make_struct_field(char *name, TypeInfo *type_info, ASTNode *default_value, int is_const) {
    ASTNode *n = alloc_node(NODE_STRUCT_FIELD);
    n->data.struct_field.name = name;
    n->data.struct_field.type_info = type_info;
    n->data.struct_field.default_value = default_value;
    n->data.struct_field.is_const = is_const;
    return n;
}

ASTNode *make_named_arg(char *name, ASTNode *value) {
    ASTNode *n = alloc_node(NODE_NAMED_ARG);
    n->data.named_arg.name = name;
    n->data.named_arg.value = value;
    return n;
}

TypeInfo *make_struct_type_info(char *name) {
    TypeInfo *t = calloc(1, sizeof(TypeInfo));
    t->kind = TK_STRUCT;
    t->name = name;
    return t;
}

Type *type_from_info(TypeInfo *ti) {
    if (!ti) return type_new(TK_UNKNOWN);
    Type *t = type_new(ti->kind);
    t->is_optional = ti->is_optional;
    if (ti->name)
        t->name = strdup(ti->name);
    return t;
}

void free_type_info(TypeInfo *ti) {
    if (!ti) return;
    free(ti->name);
    free(ti);
}

/* O(1) list append using tail pointer (#15) */
NodeList *make_list(ASTNode *node) {
    NodeList *l = malloc(sizeof(NodeList));
    l->node = node;
    l->next = NULL;
    l->tail = l;
    return l;
}

NodeList *list_append(NodeList *list, ASTNode *node) {
    NodeList *new_item = make_list(node);
    if (!list) return new_item;
    list->tail->next = new_item;
    list->tail = new_item;
    return list;
}

/* Pretty printing */

static void indent_print(int indent) {
    for (int i = 0; i < indent; i++) printf("  ");
}

static void print_type_info(TypeInfo *ti) {
    if (!ti) { printf("(inferred)"); return; }
    switch (ti->kind) {
    case TK_INT:    printf("int"); break;
    case TK_FLOAT:  printf("float"); break;
    case TK_STRING: printf("String"); break;
    case TK_BOOL:   printf("bool"); break;
    case TK_CHAR:   printf("char"); break;
    case TK_VOID:   printf("void"); break;
    case TK_STRUCT:
        if (ti->name) printf("%s", ti->name);
        else printf("struct");
        break;
    default:        printf("unknown"); break;
    }
    if (ti->is_optional) printf("?");
}

void print_ast(ASTNode *node, int indent) {
    if (!node) return;
    indent_print(indent);

    switch (node->type) {
    case NODE_PROGRAM:
        printf("Program\n");
        for (NodeList *l = node->data.program.stmts; l; l = l->next)
            print_ast(l->node, indent + 1);
        break;
    case NODE_BLOCK:
        printf("Block\n");
        for (NodeList *l = node->data.block.stmts; l; l = l->next)
            print_ast(l->node, indent + 1);
        break;
    case NODE_INT:
        printf("Int: %lld\n", (long long)node->data.ival);
        break;
    case NODE_FLOAT:
        printf("Float: %g\n", node->data.dval);
        break;
    case NODE_STRING:
        printf("String: \"%s\"\n", node->data.sval);
        break;
    case NODE_BOOL:
        printf("Bool: %s\n", node->data.bval ? "true" : "false");
        break;
    case NODE_CHAR:
        printf("Char: '%c'\n", node->data.cval);
        break;
    case NODE_IDENT:
        printf("Ident: %s\n", node->data.ident.name);
        break;
    case NODE_PARAM:
        printf("Param: %s: ", node->data.param.name);
        print_type_info(node->data.param.type_info);
        printf("\n");
        break;
    case NODE_BINOP:
        printf("BinOp: %s\n", op_to_str(node->data.binop.op));
        print_ast(node->data.binop.left, indent + 1);
        print_ast(node->data.binop.right, indent + 1);
        break;
    case NODE_UNARYOP:
        printf("UnaryOp: %s\n", op_to_str(node->data.unaryop.op));
        print_ast(node->data.unaryop.operand, indent + 1);
        break;
    case NODE_ASSIGN:
        printf("Assign\n");
        indent_print(indent + 1); printf("Target:\n");
        print_ast(node->data.assign.target, indent + 2);
        indent_print(indent + 1); printf("Value:\n");
        print_ast(node->data.assign.value, indent + 2);
        break;
    case NODE_COMPOUND_ASSIGN:
        printf("CompoundAssign: %s\n", op_to_str(node->data.compound_assign.op));
        indent_print(indent + 1); printf("Target:\n");
        print_ast(node->data.compound_assign.target, indent + 2);
        indent_print(indent + 1); printf("Value:\n");
        print_ast(node->data.compound_assign.value, indent + 2);
        break;
    case NODE_INCDEC:
        printf("IncDec: %s %s\n", op_to_str(node->data.incdec.op),
               node->data.incdec.is_prefix ? "prefix" : "postfix");
        print_ast(node->data.incdec.target, indent + 1);
        break;
    case NODE_DECL:
        printf("%s: %s\n", node->data.decl.is_const ? "LetDecl" : "VarDecl",
               node->data.decl.name);
        print_ast(node->data.decl.value, indent + 1);
        break;
    case NODE_IF:
        printf("If\n");
        indent_print(indent + 1); printf("Cond:\n");
        print_ast(node->data.if_expr.cond, indent + 2);
        indent_print(indent + 1); printf("Then:\n");
        print_ast(node->data.if_expr.then_b, indent + 2);
        if (node->data.if_expr.else_b) {
            indent_print(indent + 1); printf("Else:\n");
            print_ast(node->data.if_expr.else_b, indent + 2);
        }
        break;
    case NODE_WHILE:
        printf("While\n");
        indent_print(indent + 1); printf("Cond:\n");
        print_ast(node->data.while_expr.cond, indent + 2);
        indent_print(indent + 1); printf("Body:\n");
        print_ast(node->data.while_expr.body, indent + 2);
        break;
    case NODE_FOR:
        printf("For\n");
        indent_print(indent + 1); printf("Init:\n");
        print_ast(node->data.for_expr.init, indent + 2);
        indent_print(indent + 1); printf("Cond:\n");
        print_ast(node->data.for_expr.cond, indent + 2);
        indent_print(indent + 1); printf("Update:\n");
        print_ast(node->data.for_expr.update, indent + 2);
        indent_print(indent + 1); printf("Body:\n");
        print_ast(node->data.for_expr.body, indent + 2);
        break;
    case NODE_BREAK:
        printf("Break\n");
        if (node->data.break_expr.value)
            print_ast(node->data.break_expr.value, indent + 1);
        break;
    case NODE_CONTINUE:
        printf("Continue\n");
        if (node->data.continue_expr.value)
            print_ast(node->data.continue_expr.value, indent + 1);
        break;
    case NODE_FUNC_DEF:
        printf("FuncDef: %s(", node->data.func_def.name);
        for (NodeList *l = node->data.func_def.params; l; l = l->next) {
            if (l->node->type == NODE_PARAM) {
                printf("%s: ", l->node->data.param.name);
                print_type_info(l->node->data.param.type_info);
            }
            if (l->next) printf(", ");
        }
        printf(")\n");
        print_ast(node->data.func_def.body, indent + 1);
        break;
    case NODE_CALL:
        printf("Call: %s\n", node->data.call.name);
        for (NodeList *l = node->data.call.args; l; l = l->next)
            print_ast(l->node, indent + 1);
        break;
    case NODE_RETURN:
        printf("Return\n");
        if (node->data.ret.value)
            print_ast(node->data.ret.value, indent + 1);
        break;
    case NODE_FIELD_ACCESS:
        printf("FieldAccess: .%s\n", node->data.field_access.field);
        print_ast(node->data.field_access.object, indent + 1);
        break;
    case NODE_INDEX:
        printf("Index\n");
        print_ast(node->data.index_access.object, indent + 1);
        print_ast(node->data.index_access.index, indent + 1);
        break;
    case NODE_OPTIONAL_CHECK:
        printf("OptionalCheck\n");
        print_ast(node->data.optional_check.operand, indent + 1);
        break;
    case NODE_TYPE_DEF:
        printf("StructDef: %s\n", node->data.type_def.name);
        for (NodeList *l = node->data.type_def.fields; l; l = l->next)
            print_ast(l->node, indent + 1);
        break;
    case NODE_STRUCT_FIELD:
        printf("StructField: %s%s", node->data.struct_field.is_const ? "let " : "var ",
               node->data.struct_field.name);
        if (node->data.struct_field.type_info) {
            printf(": ");
            print_type_info(node->data.struct_field.type_info);
        }
        printf("\n");
        if (node->data.struct_field.default_value)
            print_ast(node->data.struct_field.default_value, indent + 1);
        break;
    case NODE_NAMED_ARG:
        printf("NamedArg: %s\n", node->data.named_arg.name);
        print_ast(node->data.named_arg.value, indent + 1);
        break;
    }
}

void free_list(NodeList *list) {
    while (list) {
        NodeList *next = list->next;
        free_ast(list->node);
        free(list);
        list = next;
    }
}

void free_ast(ASTNode *node) {
    if (!node) return;
    switch (node->type) {
    case NODE_PROGRAM: free_list(node->data.program.stmts); break;
    case NODE_BLOCK: free_list(node->data.block.stmts); break;
    case NODE_INT: case NODE_FLOAT: case NODE_BOOL: case NODE_CHAR: break;
    case NODE_STRING: free(node->data.sval); break;
    case NODE_IDENT: free(node->data.ident.name); break;
    case NODE_PARAM:
        free(node->data.param.name);
        free_type_info(node->data.param.type_info);
        break;
    case NODE_BINOP:
        free_ast(node->data.binop.left);
        free_ast(node->data.binop.right);
        break;
    case NODE_UNARYOP:
        free_ast(node->data.unaryop.operand);
        break;
    case NODE_ASSIGN:
        free_ast(node->data.assign.target);
        free_ast(node->data.assign.value);
        break;
    case NODE_COMPOUND_ASSIGN:
        free_ast(node->data.compound_assign.target);
        free_ast(node->data.compound_assign.value);
        break;
    case NODE_INCDEC:
        free_ast(node->data.incdec.target);
        break;
    case NODE_DECL:
        free(node->data.decl.name);
        free_ast(node->data.decl.value);
        break;
    case NODE_IF:
        free_ast(node->data.if_expr.cond);
        free_ast(node->data.if_expr.then_b);
        free_ast(node->data.if_expr.else_b);
        break;
    case NODE_WHILE:
        free_ast(node->data.while_expr.cond);
        free_ast(node->data.while_expr.body);
        break;
    case NODE_FOR:
        free_ast(node->data.for_expr.init);
        free_ast(node->data.for_expr.cond);
        free_ast(node->data.for_expr.update);
        free_ast(node->data.for_expr.body);
        break;
    case NODE_BREAK: free_ast(node->data.break_expr.value); break;
    case NODE_CONTINUE: free_ast(node->data.continue_expr.value); break;
    case NODE_FUNC_DEF:
        free(node->data.func_def.name);
        free_list(node->data.func_def.params);
        free_type_info(node->data.func_def.return_type);
        free_ast(node->data.func_def.body);
        break;
    case NODE_CALL:
        free(node->data.call.name);
        free_list(node->data.call.args);
        break;
    case NODE_RETURN: free_ast(node->data.ret.value); break;
    case NODE_FIELD_ACCESS:
        free_ast(node->data.field_access.object);
        free(node->data.field_access.field);
        break;
    case NODE_INDEX:
        free_ast(node->data.index_access.object);
        free_ast(node->data.index_access.index);
        break;
    case NODE_OPTIONAL_CHECK:
        free_ast(node->data.optional_check.operand);
        break;
    case NODE_TYPE_DEF:
        free(node->data.type_def.name);
        free_list(node->data.type_def.fields);
        break;
    case NODE_STRUCT_FIELD:
        free(node->data.struct_field.name);
        free_type_info(node->data.struct_field.type_info);
        free_ast(node->data.struct_field.default_value);
        break;
    case NODE_NAMED_ARG:
        free(node->data.named_arg.name);
        free_ast(node->data.named_arg.value);
        break;
    }
    type_free(node->resolved_type);
    free(node);
}
