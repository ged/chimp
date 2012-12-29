%{

#include <inttypes.h>

#include "chimp/str.h"
#include "chimp/_parser_ext.h"
#include "chimp/_parser.h"
#include "chimp/int.h"

#define CHIMP_REF_TOKEN(token, r) yylval->ref = (r); return (token);
#define CHIMP_TOKEN(token) return (token);

#define YY_NO_INPUT /* silence 'warning: ‘input’ defined but not used' */

%}

/* silence warnings / errors */
%option nounput
%option noyywrap

%option 8bit

/* enable bison location info */
%option bison-locations

/* prefer a fast scanner to a small one */
%option full

%%

[#][^\n]*                   { /* do nothing (comment) */ }
[\n]                        {
                                yylloc->first_line++;
                                yylloc->last_line = yylloc->first_line;
                                yylloc->first_column = 0;
                                yylloc->last_column = 0;
                            }

"true"                      { CHIMP_TOKEN(TOK_TRUE) }
"false"                     { CHIMP_TOKEN(TOK_FALSE) }
"nil"                       { CHIMP_TOKEN(TOK_NIL) }
"or"                        { CHIMP_TOKEN(TOK_OR) }
"and"                       { CHIMP_TOKEN(TOK_AND) }
"use"                       { CHIMP_TOKEN(TOK_USE) }
"ret"                       { CHIMP_TOKEN(TOK_RET) }
"fn"                        { CHIMP_TOKEN(TOK_FN) }
"var"                       { CHIMP_TOKEN(TOK_VAR) }
"not"                       { CHIMP_TOKEN(TOK_NOT) }
"match"                     { CHIMP_TOKEN(TOK_MATCH) }
"class"                     { CHIMP_TOKEN(TOK_CLASS) }
[ \t\r]                     { /* do nothing */ }
"."                         { CHIMP_TOKEN(TOK_FULLSTOP) }
"_"                         { CHIMP_TOKEN(TOK_UNDERSCORE) }
"("                         { CHIMP_TOKEN(TOK_LBRACKET) }
")"                         { CHIMP_TOKEN(TOK_RBRACKET) }
";"                         { CHIMP_TOKEN(TOK_SEMICOLON) }
":"                         { CHIMP_TOKEN(TOK_COLON) }
","                         { CHIMP_TOKEN(TOK_COMMA) }
"|"                         { CHIMP_TOKEN(TOK_PIPE) }
"["                         { CHIMP_TOKEN(TOK_LSQBRACKET) }
"]"                         { CHIMP_TOKEN(TOK_RSQBRACKET) }
"="                         { CHIMP_TOKEN(TOK_ASSIGN) }
"=="                        { CHIMP_TOKEN(TOK_EQ) }
"!="                        { CHIMP_TOKEN(TOK_NEQ) }
"{"                         { CHIMP_TOKEN(TOK_LBRACE) }
"}"                         { CHIMP_TOKEN(TOK_RBRACE) }
"<"                         { CHIMP_TOKEN(TOK_LT) }
"<="                        { CHIMP_TOKEN(TOK_LTE) }
">"                         { CHIMP_TOKEN(TOK_GT) }
">="                        { CHIMP_TOKEN(TOK_GTE) }
"+"                         { CHIMP_TOKEN(TOK_PLUS) }
"-"                         { CHIMP_TOKEN(TOK_MINUS) }
"*"                         { CHIMP_TOKEN(TOK_ASTERISK) }
"/"                         { CHIMP_TOKEN(TOK_SLASH) }
"if"                        { CHIMP_TOKEN(TOK_IF) }
"else"                      { CHIMP_TOKEN(TOK_ELSE) }
"while"                     { CHIMP_TOKEN(TOK_WHILE) }
"break"                     { CHIMP_TOKEN(TOK_BREAK) }
["][^"]*["]                 { CHIMP_REF_TOKEN(TOK_STR, chimp_str_new (yytext + 1, yyleng - 2)) }
[a-zA-Z_][a-zA-Z_0-9]*[!?]? { CHIMP_REF_TOKEN(TOK_IDENT, chimp_str_new (yytext, yyleng)) }
[0-9]+                      { CHIMP_REF_TOKEN(TOK_INT, chimp_int_new (strtoll(yytext, NULL, 0))) }
