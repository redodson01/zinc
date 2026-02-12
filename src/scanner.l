%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "scanner_extra.h"
#include "parser.h"

#define YY_USER_ACTION \
    yylloc->first_line = yylloc->last_line = yylineno; \
    yylloc->first_column = yyextra->column; \
    yylloc->last_column = yyextra->column + yyleng - 1; \
    yyextra->column += yyleng;
%}

%option reentrant
%option bison-bridge
%option bison-locations
%option noyywrap
%option yylineno
%option nounput
%option noinput
%option extra-type="ScannerExtra*"

%%

[ \t]+          { /* skip whitespace */ }
\n              { yyextra->column = 1; }
"#".*           { /* skip comments */ }

"let"           { return LET; }
"var"           { return VAR; }
"if"            { return IF; }
"unless"        { return UNLESS; }
"else"          { return ELSE; }
"while"         { return WHILE; }
"until"         { return UNTIL; }
"for"           { return FOR; }
"break"         { return BREAK; }
"continue"      { return CONTINUE; }
"func"          { return FUNC; }
"return"        { return RETURN; }
"true"          { yylval->bval = 1; return BOOL_LIT; }
"false"         { yylval->bval = 0; return BOOL_LIT; }
"int"           { return TYPE_INT; }
"float"         { return TYPE_FLOAT; }
"bool"          { return TYPE_BOOL; }
"char"          { return TYPE_CHAR; }

0[xX][0-9a-fA-F]+           { yylval->ival = (int64_t)strtoll(yytext, NULL, 16); return INT_LIT; }
[0-9]+\.[0-9]+([eE][+-]?[0-9]+)?  { yylval->dval = strtod(yytext, NULL); return FLOAT_LIT; }
[0-9]+[eE][+-]?[0-9]+       { yylval->ival = (int64_t)strtod(yytext, NULL); return INT_LIT; }
[0-9]+                      { yylval->ival = (int64_t)strtoll(yytext, NULL, 10); return INT_LIT; }

'\\.' {
    switch(yytext[2]) {
        case 'n':  yylval->cval = '\n'; break;
        case 't':  yylval->cval = '\t'; break;
        case 'r':  yylval->cval = '\r'; break;
        case '0':  yylval->cval = '\0'; break;
        case '\'': yylval->cval = '\''; break;
        case '\\': yylval->cval = '\\'; break;
        default:   yylval->cval = yytext[2]; break;
    }
    return CHAR_LIT;
}

'[^'\\]' { yylval->cval = yytext[1]; return CHAR_LIT; }

[a-zA-Z_][a-zA-Z0-9_]* {
    yylval->sval = strdup(yytext);
    return IDENTIFIER;
}

"+="            { return PLUS_ASSIGN; }
"-="            { return MINUS_ASSIGN; }
"*="            { return STAR_ASSIGN; }
"/="            { return SLASH_ASSIGN; }
"%="            { return PERCENT_ASSIGN; }
"++"            { return INCREMENT; }
"--"            { return DECREMENT; }

"=="            { return EQ; }
"!="            { return NE; }
"<="            { return LE; }
">="            { return GE; }
"&&"            { return AND; }
"||"            { return OR; }

"("             { return LPAREN; }
")"             { return RPAREN; }
"{"             { return LBRACE; }
"}"             { return RBRACE; }
","             { return COMMA; }
";"             { return SEMICOLON; }
":"             { return COLON; }
"+"             { return PLUS; }
"-"             { return MINUS; }
"*"             { return STAR; }
"/"             { return SLASH; }
"%"             { return PERCENT; }
"<"             { return LT; }
">"             { return GT; }
"="             { return ASSIGN; }
"!"             { return NOT; }

.               { fprintf(stderr, "Unknown char '%c' at line %d\n", yytext[0], yylineno); }

%%
