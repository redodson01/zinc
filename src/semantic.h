#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "ast.h"

/* Struct field definition (for struct registry) */
typedef struct StructFieldDef {
    char *name;
    Type *type;                /* resolved type (kind, name for structs) */
    int has_default;
    int is_const;              /* 1 for let fields, 0 for var fields */
    int is_weak;               /* 1 for weak fields (skip retain/release) */
    ASTNode *default_value;    /* pointer into AST, not owned */
    struct StructFieldDef *next;
} StructFieldDef;

/* Struct definition (for struct registry) */
typedef struct StructDef {
    char *name;
    StructFieldDef *fields;
    int field_count;
    int is_class;           /* 1 for class (reference type), 0 for struct (value type) */
    struct StructDef *next;
} StructDef;

/* Symbol table entry */
typedef struct Symbol {
    char *name;
    Type *type;          /* resolved type (includes kind, name, elem, key, is_optional) */
    int is_const;        /* 1 for let, 0 for var */
    int is_function;
    int is_extern;       /* 1 for extern declarations */
    int param_count;
    Type **param_types;  /* array of resolved types for each parameter */
    struct Symbol *next;
} Symbol;

/* Scope for nested scopes — hash table with chaining */
#define SCOPE_BUCKETS 64
#define STRUCT_BUCKETS 32

typedef struct Scope {
    Symbol *buckets[SCOPE_BUCKETS];
    struct Scope *parent;
} Scope;

/* Semantic analysis context */
typedef struct {
    Scope *current_scope;
    int error_count;
    int in_loop;         /* For break/continue validation */
    int in_function;     /* For return validation */
    Type *current_func_return_type;
    StructDef *struct_buckets[STRUCT_BUCKETS];  /* Registered struct definitions */
    /* Loop expression result tracking */
    Type *loop_result_type;
    int loop_result_set;
} SemanticContext;

/* Initialize/cleanup context */
SemanticContext *semantic_init(void);
void semantic_free(SemanticContext *ctx);

/* Main analysis function — returns error count */
int analyze(SemanticContext *ctx, ASTNode *root);

/* Get inferred type of an expression */
Type *get_expr_type(SemanticContext *ctx, ASTNode *expr);

/* Look up a symbol */
Symbol *lookup(SemanticContext *ctx, const char *name);

/* Look up a struct definition */
StructDef *lookup_struct(SemanticContext *ctx, const char *name);

#endif
