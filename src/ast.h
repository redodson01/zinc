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
    TK_STRING,
    TK_BOOL,
    TK_CHAR,
    TK_VOID,
    TK_STRUCT,
    TK_CLASS,
    TK_ARRAY,
    TK_HASH,
} TypeKind;

/* Resolved type representation — used by semantic analysis and codegen.
   TypeInfo (below) is the parser-side type specification; Type is what the
   compiler resolved.  Semantic analysis converts TypeInfo → Type. */
typedef struct Type {
    TypeKind kind;
    int is_optional;
    char *name;           /* struct/class/tuple/object canonical name */
    struct Type *elem;    /* array element type, hash value type */
    struct Type *key;     /* hash key type */
} Type;

/* Type helpers */
Type *type_new(TypeKind kind);
Type *type_clone(const Type *t);
void type_free(Type *t);
int type_eq(const Type *a, const Type *b);

/* Named field in a type specification: { name: type, ... } for object types */
typedef struct TypeInfoField {
    char *name;
    struct TypeInfo *type;
    struct TypeInfoField *next;
} TypeInfoField;

/* TypeInfo for type specifications in function parameters */
typedef struct TypeInfo {
    TypeKind kind;
    int is_optional;        /* 1 if T?, 0 otherwise */
    char *name;             /* struct/class name, NULL for non-struct types */
    TypeInfoField *fields;  /* For object types { name: type, ... } */
    int is_object;          /* 1 for object types, 0 otherwise */
    int is_tuple;           /* 1 for tuple type annotations */
    struct TypeInfo *elem;  /* array element type / hash value type */
    struct TypeInfo *key;   /* hash key type */
} TypeInfo;

Type *type_from_info(TypeInfo *ti);
void free_type_info(TypeInfo *ti);

typedef enum {
    NODE_PROGRAM,
    NODE_BLOCK,
    NODE_INT,
    NODE_FLOAT,
    NODE_STRING,
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
    NODE_FIELD_ACCESS,
    NODE_INDEX,
    NODE_OPTIONAL_CHECK,
    NODE_TYPE_DEF,
    NODE_STRUCT_FIELD,

    NODE_NAMED_ARG,
    NODE_TUPLE,
    NODE_OBJECT_LITERAL,
    NODE_ARRAY_LITERAL,
    NODE_HASH_LITERAL,
    NODE_HASH_PAIR,
    NODE_EXTERN_BLOCK,
    NODE_EXTERN_FUNC,
    NODE_EXTERN_VAR,
    NODE_EXTERN_LET,
    NODE_TYPED_EMPTY_ARRAY,
    NODE_TYPED_EMPTY_HASH,
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
    int string_id;           /* Codegen-side string literal ID, -1 if not a string */
    int is_fresh_alloc;      /* 1 if this expression produces a fresh ref-counted allocation */
    Type *resolved_type;     /* Filled in by semantic analysis */
    union {
        int64_t ival;
        double dval;
        int bval;
        char cval;
        char *sval;

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
        struct { char *name; NodeList *args; int is_struct_init; } call;
        struct { ASTNode *value; } ret;
        struct { ASTNode *value; } break_expr;
        struct { ASTNode *value; } continue_expr;
        struct { ASTNode *object; char *field; int is_dot_int; } field_access;
        struct { ASTNode *object; ASTNode *index; } index_access;
        struct { ASTNode *operand; } optional_check;
        struct { char *name; NodeList *fields; int is_class; } type_def;
        struct { char *name; TypeInfo *type_info; ASTNode *default_value; int is_const; int is_weak; } struct_field;
        struct { char *name; ASTNode *value; } named_arg;
        struct { NodeList *elements; } tuple;
        struct { NodeList *fields; } object_literal;
        struct { NodeList *elems; } array_literal;
        struct { NodeList *pairs; } hash_literal;
        struct { ASTNode *key; ASTNode *value; } hash_pair;
        struct { NodeList *decls; } extern_block;
        struct { char *name; NodeList *params; TypeInfo *return_type; } extern_func;
        struct { char *name; TypeInfo *type_info; } extern_var;
        struct { char *name; TypeInfo *type_info; } extern_let;
        struct { TypeKind elem_type; char *elem_name; } typed_empty_array;
        struct { TypeKind key_type; TypeKind value_type; char *value_name; } typed_empty_hash;
    } data;
};

/* Type info constructors */
TypeInfo *make_type_info(TypeKind kind);
TypeInfo *make_optional_type(TypeInfo *base);

/* Constructor functions */
ASTNode *make_program(NodeList *stmts);
ASTNode *make_block(NodeList *stmts);
ASTNode *make_int(int64_t val);
ASTNode *make_float(double val);
ASTNode *make_string(char *val);
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
ASTNode *make_field_access(ASTNode *object, char *field);
ASTNode *make_index_access(ASTNode *object, ASTNode *index);
ASTNode *make_array_literal(NodeList *elems);
ASTNode *make_hash_literal(NodeList *pairs);
ASTNode *make_hash_pair(ASTNode *key, ASTNode *value);
ASTNode *make_extern_block(NodeList *decls);
ASTNode *make_extern_func(char *name, NodeList *params, TypeInfo *return_type);
ASTNode *make_extern_var(char *name, TypeInfo *type_info);
ASTNode *make_extern_let(char *name, TypeInfo *type_info);
ASTNode *make_optional_check(ASTNode *operand);
ASTNode *make_typed_empty_array(TypeKind elem_type);
ASTNode *make_typed_empty_array_named(char *type_name);
ASTNode *make_typed_empty_hash(TypeKind key_type, TypeKind value_type);
ASTNode *make_typed_empty_hash_named(TypeKind key_type, char *value_name);
ASTNode *make_type_def(char *name, NodeList *fields, int is_class);
ASTNode *make_struct_field(char *name, TypeInfo *type_info, ASTNode *default_value, int is_const);
ASTNode *make_weak_struct_field(char *name, TypeInfo *type_info, int is_const);
ASTNode *make_named_arg(char *name, ASTNode *value);
ASTNode *make_tuple(NodeList *elements);
ASTNode *make_object_literal(NodeList *fields);

/* Object type helpers */
TypeInfoField *make_type_info_field(char *name, TypeInfo *type);
TypeInfoField *type_info_field_append(TypeInfoField *list, TypeInfoField *field);
TypeInfo *make_object_type_info(TypeInfoField *fields);
TypeInfo *make_struct_type_info(char *name);
TypeInfo *make_hash_type_info(TypeInfo *key, TypeInfo *value);
TypeInfo *make_tuple_type_info(TypeInfoField *fields);

/* List utilities — O(1) append via tail pointer */
NodeList *make_list(ASTNode *node);
NodeList *list_append(NodeList *list, ASTNode *node);

/* Print AST */
void print_ast(ASTNode *node, int indent);

/* Cleanup */
void free_ast(ASTNode *node);
void free_list(NodeList *list);

#endif
