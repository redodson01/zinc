#ifndef AST_H
#define AST_H

#include <stdint.h>

/* Operator kinds — replaces string-based operator representation */
typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_AND, OP_OR,
    OP_NOT, OP_NEG, OP_POS,
    OP_INC, OP_DEC,
    OP_ASSIGN,
    OP_ADD_ASSIGN, OP_SUB_ASSIGN, OP_MUL_ASSIGN, OP_DIV_ASSIGN, OP_MOD_ASSIGN,
} OpKind;

const char *op_to_str(OpKind op);

typedef enum {
    TK_UNKNOWN,
    TK_INT,
    TK_FLOAT,
    TK_BOOL,
    TK_CHAR,
    TK_VOID,
} TypeKind;

/* Resolved type representation — used by semantic analysis and codegen. */
typedef struct Type {
    TypeKind kind;
} Type;

/* Type helpers */
Type *type_new(TypeKind kind);
Type *type_clone(const Type *t);
void type_free(Type *t);
int type_eq(const Type *a, const Type *b);

/* TypeInfo for type specifications in function parameters */
typedef struct TypeInfo {
    TypeKind kind;
} TypeInfo;

typedef enum {
    NODE_PROGRAM,
    NODE_BLOCK,
    NODE_INT,
    NODE_FLOAT,
    NODE_BOOL,
    NODE_CHAR,
    NODE_IDENT,
    NODE_PARAM,
    NODE_BINOP,
    NODE_UNARYOP,
    NODE_ASSIGN,
    NODE_COMPOUND_ASSIGN,
    NODE_INCDEC,
    NODE_DECL,
    NODE_IF,
    NODE_WHILE,
    NODE_FOR,
    NODE_BREAK,
    NODE_CONTINUE,
    NODE_FUNC_DEF,
    NODE_CALL,
    NODE_RETURN,
} NodeType;

typedef struct ASTNode ASTNode;
typedef struct NodeList NodeList;

struct NodeList {
    ASTNode *node;
    NodeList *next;
    NodeList *tail;  /* Only valid on head node — enables O(1) list_append */
};

struct ASTNode {
    NodeType type;
    int line;                /* Source line number for error reporting */
    Type *resolved_type;     /* Filled in by semantic analysis */
    union {
        int64_t ival;
        double dval;
        int bval;
        char cval;

        struct { NodeList *stmts; } program;
        struct { NodeList *stmts; } block;
        struct { char *name; } ident;
        struct { TypeInfo *type_info; char *name; } param;
        struct { ASTNode *left; OpKind op; ASTNode *right; } binop;
        struct { OpKind op; ASTNode *operand; } unaryop;
        struct { ASTNode *target; ASTNode *value; } assign;
        struct { ASTNode *target; OpKind op; ASTNode *value; } compound_assign;
        struct { ASTNode *target; OpKind op; int is_prefix; } incdec;
        struct { char *name; ASTNode *value; int is_const; } decl;
        struct { ASTNode *cond; ASTNode *then_b; ASTNode *else_b; } if_expr;
        struct { ASTNode *cond; ASTNode *body; } while_expr;
        struct { ASTNode *init; ASTNode *cond; ASTNode *update; ASTNode *body; } for_expr;
        struct { char *name; NodeList *params; TypeInfo *return_type; ASTNode *body; } func_def;
        struct { char *name; NodeList *args; } call;
        struct { ASTNode *value; } ret;
        struct { ASTNode *value; } break_expr;
        struct { ASTNode *value; } continue_expr;
    } data;
};

/* Type info constructors */
TypeInfo *make_type_info(TypeKind kind);

/* Constructor functions */
ASTNode *make_program(NodeList *stmts);
ASTNode *make_block(NodeList *stmts);
ASTNode *make_int(int64_t val);
ASTNode *make_float(double val);
ASTNode *make_bool(int val);
ASTNode *make_char(char val);
ASTNode *make_ident(char *name);
ASTNode *make_typed_param(char *name, TypeInfo *type_info);
ASTNode *make_binop(ASTNode *left, OpKind op, ASTNode *right);
ASTNode *make_unaryop(OpKind op, ASTNode *operand);
ASTNode *make_assign(ASTNode *target, ASTNode *value);
ASTNode *make_compound_assign(ASTNode *target, OpKind op, ASTNode *value);
ASTNode *make_incdec(ASTNode *target, OpKind op, int is_prefix);
ASTNode *make_decl(char *name, ASTNode *value, int is_const);
ASTNode *make_if(ASTNode *cond, ASTNode *then_b, ASTNode *else_b);
ASTNode *make_while(ASTNode *cond, ASTNode *body);
ASTNode *make_for(ASTNode *init, ASTNode *cond, ASTNode *update, ASTNode *body);
ASTNode *make_break_expr(ASTNode *value);
ASTNode *make_continue_expr(ASTNode *value);
ASTNode *make_func_def(char *name, NodeList *params, ASTNode *body);
ASTNode *make_call(char *name, NodeList *args);
ASTNode *make_return(ASTNode *value);

/* List utilities — O(1) append via tail pointer */
NodeList *make_list(ASTNode *node);
NodeList *list_append(NodeList *list, ASTNode *node);

/* Print AST */
void print_ast(ASTNode *node, int indent);

/* Cleanup */
void free_type_info(TypeInfo *ti);
void free_ast(ASTNode *node);
void free_list(NodeList *list);

#endif
