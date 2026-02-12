%code top {
/* Suppress warning - yynerrs is used internally by Bison's error recovery */
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
}

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
%}

%code requires {
    typedef void *yyscan_t;
}

%define api.pure full
%locations
%define parse.error verbose
%expect 7  /* Shift/reduce conflicts: dangling else (3) + expression ambiguities (4) */

%param { yyscan_t scanner }
%parse-param { ASTNode **result }
%parse-param { int *nerrs }

%code {
    extern int yylex(YYSTYPE *yylval_param, YYLTYPE *yylloc_param, yyscan_t scanner);
    void yyerror(YYLTYPE *loc, yyscan_t scanner, ASTNode **result, int *nerrs, const char *s);
}

%union {
    int64_t ival;
    double dval;
    int bval;
    char cval;
    char *sval;
    ASTNode *node;
    NodeList *list;
    TypeInfo *type;
}

%token <ival> INT_LIT
%token <dval> FLOAT_LIT
%token <bval> BOOL_LIT
%token <cval> CHAR_LIT
%token <sval> STRING_LIT STRING_PART STRING_TAIL IDENTIFIER
%token LET VAR
%token TYPE_INT TYPE_FLOAT TYPE_STRING TYPE_BOOL TYPE_CHAR
%token IF UNLESS ELSE
%token WHILE UNTIL FOR
%token BREAK CONTINUE
%token FUNC RETURN STRUCT
%token EQ NE LE GE AND OR
%token PLUS_ASSIGN MINUS_ASSIGN STAR_ASSIGN SLASH_ASSIGN PERCENT_ASSIGN
%token INCREMENT DECREMENT
%token LPAREN RPAREN LBRACE RBRACE LBRACKET RBRACKET
%token COMMA SEMICOLON COLON DOT QUESTION
%token PLUS MINUS STAR SLASH PERCENT LT GT ASSIGN NOT

%type <node> program expr primary block interp_string
%type <node> if_expr unless_expr while_expr until_expr for_expr
%type <node> func_def param for_init for_update
%type <node> struct_def struct_field arg_or_named
%type <node> interp_parts
%type <list> top_level_list expr_list param_list arg_list struct_field_list

%type <type> type_spec

%right BREAK CONTINUE RETURN
%right ASSIGN PLUS_ASSIGN MINUS_ASSIGN STAR_ASSIGN SLASH_ASSIGN PERCENT_ASSIGN
%left OR
%left AND
%left EQ NE
%left LT GT LE GE
%left PLUS MINUS
%left STAR SLASH PERCENT
%right NOT UMINUS UPLUS PREFIX_INCDEC
%left INCREMENT DECREMENT
%left DOT LBRACKET QUESTION

%%

program:
    top_level_list              { *result = make_program($1); }
    ;

top_level_list:
    func_def                        { $$ = make_list($1); }
    | struct_def                    { $$ = make_list($1); }
    | top_level_list func_def       { $$ = list_append($1, $2); }
    | top_level_list struct_def     { $$ = list_append($1, $2); }
    ;

func_def:
    FUNC IDENTIFIER LPAREN param_list RPAREN block
        { $$ = make_func_def($2, $4, $6); $$->line = @1.first_line; }
    ;

param_list:
    /* empty */                         { $$ = NULL; }
    | param                             { $$ = make_list($1); }
    | param_list COMMA param            { $$ = list_append($1, $3); }
    ;

param:
    IDENTIFIER COLON type_spec          { $$ = make_typed_param($1, $3); $$->line = @1.first_line; }
    ;

type_spec:
    TYPE_INT                            { $$ = make_type_info(TK_INT); }
    | TYPE_FLOAT                        { $$ = make_type_info(TK_FLOAT); }
    | TYPE_STRING                       { $$ = make_type_info(TK_STRING); }
    | TYPE_BOOL                         { $$ = make_type_info(TK_BOOL); }
    | TYPE_CHAR                         { $$ = make_type_info(TK_CHAR); }
    | IDENTIFIER                        { $$ = make_struct_type_info($1); }
    | type_spec QUESTION                { $$ = make_optional_type($1); }
    ;

block:
    LBRACE expr_list RBRACE             { $$ = make_block($2); }
    ;

expr_list:
    expr                                { $$ = make_list($1); }
    | expr_list expr                    { $$ = list_append($1, $2); }
    ;

expr:
    primary                             { $$ = $1; }
    /* Variable declarations */
    | VAR IDENTIFIER ASSIGN expr        { $$ = make_decl($2, $4, 0); $$->line = @1.first_line; }
    | LET IDENTIFIER ASSIGN expr        { $$ = make_decl($2, $4, 1); $$->line = @1.first_line; }
    /* Assignments */
    | expr ASSIGN expr                  { $$ = make_assign($1, $3); $$->line = @1.first_line; }
    | expr PLUS_ASSIGN expr  { $$ = make_compound_assign($1, OP_ADD_ASSIGN, $3); $$->line = @1.first_line; }
    | expr MINUS_ASSIGN expr { $$ = make_compound_assign($1, OP_SUB_ASSIGN, $3); $$->line = @1.first_line; }
    | expr STAR_ASSIGN expr  { $$ = make_compound_assign($1, OP_MUL_ASSIGN, $3); $$->line = @1.first_line; }
    | expr SLASH_ASSIGN expr { $$ = make_compound_assign($1, OP_DIV_ASSIGN, $3); $$->line = @1.first_line; }
    | expr PERCENT_ASSIGN expr { $$ = make_compound_assign($1, OP_MOD_ASSIGN, $3); $$->line = @1.first_line; }
    /* Increment/decrement (restricted to l-values via primary) */
    | INCREMENT primary %prec PREFIX_INCDEC  { $$ = make_incdec($2, OP_INC, 1); $$->line = @1.first_line; }
    | DECREMENT primary %prec PREFIX_INCDEC  { $$ = make_incdec($2, OP_DEC, 1); $$->line = @1.first_line; }
    | primary INCREMENT                 { $$ = make_incdec($1, OP_INC, 0); $$->line = @1.first_line; }
    | primary DECREMENT                 { $$ = make_incdec($1, OP_DEC, 0); $$->line = @1.first_line; }
    /* Binary operators */
    | expr PLUS expr                    { $$ = make_binop($1, OP_ADD, $3); $$->line = @$.first_line; }
    | expr MINUS expr                   { $$ = make_binop($1, OP_SUB, $3); $$->line = @$.first_line; }
    | expr STAR expr                    { $$ = make_binop($1, OP_MUL, $3); $$->line = @$.first_line; }
    | expr SLASH expr                   { $$ = make_binop($1, OP_DIV, $3); $$->line = @$.first_line; }
    | expr PERCENT expr                 { $$ = make_binop($1, OP_MOD, $3); $$->line = @$.first_line; }
    | expr LT expr                      { $$ = make_binop($1, OP_LT, $3); $$->line = @$.first_line; }
    | expr GT expr                      { $$ = make_binop($1, OP_GT, $3); $$->line = @$.first_line; }
    | expr LE expr                      { $$ = make_binop($1, OP_LE, $3); $$->line = @$.first_line; }
    | expr GE expr                      { $$ = make_binop($1, OP_GE, $3); $$->line = @$.first_line; }
    | expr EQ expr                      { $$ = make_binop($1, OP_EQ, $3); $$->line = @$.first_line; }
    | expr NE expr                      { $$ = make_binop($1, OP_NE, $3); $$->line = @$.first_line; }
    | expr AND expr                     { $$ = make_binop($1, OP_AND, $3); $$->line = @$.first_line; }
    | expr OR expr                      { $$ = make_binop($1, OP_OR, $3); $$->line = @$.first_line; }
    /* Unary operators */
    | MINUS expr %prec UMINUS           { $$ = make_unaryop(OP_NEG, $2); $$->line = @1.first_line; }
    | PLUS expr %prec UPLUS             { $$ = make_unaryop(OP_POS, $2); $$->line = @1.first_line; }
    | NOT expr                          { $$ = make_unaryop(OP_NOT, $2); $$->line = @1.first_line; }
    /* Index access */
    | expr LBRACKET expr RBRACKET       { $$ = make_index_access($1, $3); $$->line = @$.first_line; }
    /* Field access */
    | expr DOT IDENTIFIER               { $$ = make_field_access($1, $3); $$->line = @$.first_line; }
    /* Optional check */
    | expr QUESTION                     { $$ = make_optional_check($1); $$->line = @$.first_line; }
    /* Function call */
    | IDENTIFIER LPAREN arg_list RPAREN { $$ = make_call($1, $3); $$->line = @1.first_line; }
    /* Parenthesized expression */
    | LPAREN expr RPAREN                { $$ = $2; }
    /* Control flow expressions */
    | if_expr                           { $$ = $1; }
    | unless_expr                       { $$ = $1; }
    | while_expr                        { $$ = $1; }
    | until_expr                        { $$ = $1; }
    | for_expr                          { $$ = $1; }
    /* Control flow keywords with required expressions */
    | BREAK expr %prec BREAK            { $$ = make_break_expr($2); $$->line = @1.first_line; }
    | CONTINUE expr %prec CONTINUE      { $$ = make_continue_expr($2); $$->line = @1.first_line; }
    | RETURN expr %prec RETURN          { $$ = make_return($2); $$->line = @1.first_line; }
    ;

/* Desugared at parse time: unless → if with negated condition (#1) */
if_expr:
    IF expr block                 { $$ = make_if($2, $3, NULL); $$->line = @1.first_line; }
    | IF expr block ELSE block    { $$ = make_if($2, $3, $5); $$->line = @1.first_line; }
    | IF expr block ELSE if_expr  { $$ = make_if($2, $3, $5); $$->line = @1.first_line; }
    ;

unless_expr:
    UNLESS expr block                 { $$ = make_if(make_unaryop(OP_NOT, $2), $3, NULL); $$->line = @1.first_line; }
    | UNLESS expr block ELSE block    { $$ = make_if(make_unaryop(OP_NOT, $2), $3, $5); $$->line = @1.first_line; }
    ;

/* Desugared at parse time: until → while with negated condition (#1) */
while_expr:
    WHILE expr block      { $$ = make_while($2, $3); $$->line = @1.first_line; }
    ;

until_expr:
    UNTIL expr block      { $$ = make_while(make_unaryop(OP_NOT, $2), $3); $$->line = @1.first_line; }
    ;

for_expr:
    FOR for_init SEMICOLON expr SEMICOLON for_update block
        { $$ = make_for($2, $4, $6, $7); $$->line = @1.first_line; }
    ;

for_init:
    /* empty */                         { $$ = NULL; }
    | VAR IDENTIFIER ASSIGN expr        { $$ = make_decl($2, $4, 0); $$->line = @1.first_line; }
    | LET IDENTIFIER ASSIGN expr        { $$ = make_decl($2, $4, 1); $$->line = @1.first_line; }
    | IDENTIFIER ASSIGN expr            { $$ = make_assign(make_ident($1), $3); $$->line = @1.first_line; }
    ;

for_update:
    /* empty */                         { $$ = NULL; }
    | IDENTIFIER ASSIGN expr            { $$ = make_assign(make_ident($1), $3); $$->line = @1.first_line; }
    | IDENTIFIER PLUS_ASSIGN expr       { $$ = make_compound_assign(make_ident($1), OP_ADD_ASSIGN, $3); $$->line = @1.first_line; }
    | IDENTIFIER MINUS_ASSIGN expr      { $$ = make_compound_assign(make_ident($1), OP_SUB_ASSIGN, $3); $$->line = @1.first_line; }
    | IDENTIFIER STAR_ASSIGN expr       { $$ = make_compound_assign(make_ident($1), OP_MUL_ASSIGN, $3); $$->line = @1.first_line; }
    | IDENTIFIER SLASH_ASSIGN expr      { $$ = make_compound_assign(make_ident($1), OP_DIV_ASSIGN, $3); $$->line = @1.first_line; }
    | IDENTIFIER PERCENT_ASSIGN expr    { $$ = make_compound_assign(make_ident($1), OP_MOD_ASSIGN, $3); $$->line = @1.first_line; }
    | INCREMENT IDENTIFIER              { $$ = make_incdec(make_ident($2), OP_INC, 1); $$->line = @1.first_line; }
    | DECREMENT IDENTIFIER              { $$ = make_incdec(make_ident($2), OP_DEC, 1); $$->line = @1.first_line; }
    | IDENTIFIER INCREMENT              { $$ = make_incdec(make_ident($1), OP_INC, 0); $$->line = @1.first_line; }
    | IDENTIFIER DECREMENT              { $$ = make_incdec(make_ident($1), OP_DEC, 0); $$->line = @1.first_line; }
    ;

/* Struct definition */
struct_def:
    STRUCT IDENTIFIER LBRACE struct_field_list RBRACE
        { $$ = make_type_def($2, $4, 0); $$->line = @1.first_line; }
    ;

struct_field_list:
    struct_field                        { $$ = make_list($1); }
    | struct_field_list struct_field    { $$ = list_append($1, $2); }
    ;

struct_field:
    LET IDENTIFIER COLON type_spec
        { $$ = make_struct_field($2, $4, NULL, 1); $$->line = @1.first_line; }
    | VAR IDENTIFIER COLON type_spec
        { $$ = make_struct_field($2, $4, NULL, 0); $$->line = @1.first_line; }
    | LET IDENTIFIER ASSIGN expr
        { $$ = make_struct_field($2, NULL, $4, 1); $$->line = @1.first_line; }
    | VAR IDENTIFIER ASSIGN expr
        { $$ = make_struct_field($2, NULL, $4, 0); $$->line = @1.first_line; }
    ;

arg_list:
    /* empty */                         { $$ = NULL; }
    | arg_or_named                      { $$ = make_list($1); }
    | arg_list COMMA arg_or_named       { $$ = list_append($1, $3); }
    ;

arg_or_named:
    expr                                { $$ = $1; }
    | IDENTIFIER COLON expr             { $$ = make_named_arg($1, $3); }
    ;

primary:
    INT_LIT                             { $$ = make_int($1); $$->line = @1.first_line; }
    | FLOAT_LIT                         { $$ = make_float($1); $$->line = @1.first_line; }
    | STRING_LIT                        { $$ = make_string($1); $$->line = @1.first_line; }
    | BOOL_LIT                          { $$ = make_bool($1); $$->line = @1.first_line; }
    | CHAR_LIT                          { $$ = make_char($1); $$->line = @1.first_line; }
    | IDENTIFIER                        { $$ = make_ident($1); $$->line = @1.first_line; }
    | interp_string                     { $$ = $1; }
    ;

/* String interpolation: "hello ${name}!" desugars to ("hello " + name) + "!" */
interp_string:
    interp_parts expr STRING_TAIL {
        ASTNode *acc;
        if ($1) {
            acc = make_binop($1, OP_ADD, $2);
        } else {
            acc = $2;
        }
        if (strlen($3) > 0) {
            $$ = make_binop(acc, OP_ADD, make_string($3));
        } else {
            free($3);
            $$ = acc;
        }
    }
    ;

interp_parts:
    STRING_PART {
        if (strlen($1) > 0) {
            $$ = make_string($1);
        } else {
            free($1);
            $$ = NULL;
        }
    }
    | interp_parts expr STRING_PART {
        ASTNode *acc;
        if ($1) {
            acc = make_binop($1, OP_ADD, $2);
        } else {
            acc = $2;
        }
        if (strlen($3) > 0) {
            $$ = make_binop(acc, OP_ADD, make_string($3));
        } else {
            free($3);
            $$ = acc;
        }
    }
    ;

%%

void yyerror(YYLTYPE *loc, yyscan_t scanner, ASTNode **result, int *nerrs, const char *s) {
    (void)scanner;
    (void)result;
    (*nerrs)++;
    fprintf(stderr, "Parse error at line %d: %s\n", loc->first_line, s);
}
