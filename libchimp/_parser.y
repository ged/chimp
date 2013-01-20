%{

#include <inttypes.h>

#include "chimp/object.h"
#include "chimp/_parser_ext.h"

void yyerror(const ChimpAstNodeLocation *loc, void *scanner, ChimpRef *filename, ChimpRef **mod, const char *format, ...);

%}

%union {
    ChimpRef *ref;
}

/* token location support for tracking lineno & column info */
%locations

/* necessary to support %locations properly afaict */
/* Bison 2.4 adds api.pure & deprecates %pure-parser but OSX has 2.3 ... */
/*%define api.pure*/
%pure-parser

/* better error messages for free */
%error-verbose

%parse-param { void *scanner }
%parse-param { ChimpRef *filename }
/* We pass this around so we can avoid using a global */
/* XXX odd. doing this seems to trigger %locations for yyerror too */
/*     handy, but unexpected. */
%parse-param { ChimpRef **mod }
%lex-param { void *scanner }
%lex-param { ChimpRef *filename }
%lex-param { ChimpRef **mod }

/* setting a custom YYLTYPE means bison no longer initializes yylval for us */
%initial-action {
    memset (&@$, 0, sizeof(@$));
    @$.filename = filename;
    @$.first_line = @$.last_line = 1;
    @$.first_column = @$.last_column = 1;
}

%{
extern int yylex(YYSTYPE *lvalp, YYLTYPE *llocp, void *scanner, ChimpRef *filename, ChimpRef **mod);
%}

%expect 0

%token TOK_TRUE "`true`"
%token TOK_FALSE "`false`"
%token TOK_NIL "`nil`"
%token TOK_LBRACKET "`(`"
%token TOK_RBRACKET "`)`"
%token TOK_SEMICOLON "`;`"
%token TOK_COMMA "`,`"
%token TOK_COLON "`:`"
%token TOK_FULLSTOP "`.`"
%token TOK_LSQBRACKET "`[`"
%token TOK_RSQBRACKET "`]`"
%token TOK_LBRACE "`{`"
%token TOK_RBRACE "`}`"
%token TOK_PIPE "`|`"
%token TOK_ASSIGN "`=`"
%token TOK_IF "`if`"
%token TOK_ELSE "`else`"
%token TOK_USE "`use`"
%token TOK_RET "`ret`"
%token TOK_FN "`fn`"
%token TOK_VAR "`var`"
%token TOK_WHILE "`while`"
%token TOK_SPAWN "`spawn`"
%token TOK_NOT "`not`"
%token TOK_MATCH "`match`"
%token TOK_BREAK "`break`"
%token TOK_UNDERSCORE "`_`"

%token TOK_OR "`or`"
%token TOK_AND "`and`"
%token TOK_NEQ "`!=`"
%token TOK_EQ "`==`"
%token TOK_LT "`<`"
%token TOK_LTE "`<=`"
%token TOK_GT "`>`"
%token TOK_GTE "`>=`"
%token TOK_PLUS "`+`"
%token TOK_MINUS "`-`"
%token TOK_ASTERISK "`*`"
%token TOK_SLASH "`/`"
%token TOK_CLASS "`class`"

%token <ref> TOK_IDENT "identifier"
%token <ref> TOK_STR "string literal"
%token <ref> TOK_INT "integer literal"
%token <ref> TOK_FLOAT "float literal"

%right TOK_NOT
%nonassoc TOK_LSQBRACKET TOK_LBRACKET TOK_FULLSTOP
%left TOK_OR TOK_AND
%left TOK_NEQ TOK_EQ TOK_LT TOK_LTE TOK_GT TOK_GTE
%left TOK_PLUS TOK_MINUS
%left TOK_ASTERISK TOK_SLASH

%type <ref> module
%type <ref> stmt simple_stmt compound_stmt
%type <ref> var_decl assign
%type <ref> stmts opt_stmts block else
%type <ref> opt_expr expr expr2 expr3 expr4 expr5 
%type <ref> simple_tail
%type <ref> opt_decls opt_uses
%type <ref> use
%type <ref> func_decl class_decl func_ident
%type <ref> opt_extends type_name opt_class_decls
%type <ref> opt_params opt_params_tail param
%type <ref> opt_args args opt_args_tail
%type <ref> opt_patterns pattern pattern_test opt_pattern_array_elements
%type <ref> pattern_array_elements opt_pattern_array_elements_tail
%type <ref> opt_array_elements array_elements opt_array_elements_tail
%type <ref> hash_elements opt_hash_elements_tail
%type <ref> opt_pattern_hash_elements pattern_hash_elements
%type <ref> opt_pattern_hash_elements_tail
%type <ref> ident str array hash bool nil int float
%type <ref> ret break

%%

module : opt_uses opt_decls { *mod = chimp_ast_mod_new_root (CHIMP_STR_NEW("main"), $1, $2, &@$); }
       ;

opt_uses : use opt_uses { $$ = $2; chimp_array_unshift ($$, $1); }
         | /* empty */ { $$ = chimp_array_new (); }
         ;

use : TOK_USE ident TOK_SEMICOLON { $$ = chimp_ast_decl_new_use (CHIMP_AST_EXPR($2)->ident.id, &@$); }
    ;

opt_decls : func_decl opt_decls { $$ = $2; chimp_array_unshift ($$, $1); }
          | class_decl opt_decls { $$ = $2; chimp_array_unshift ($$, $1); }
          | /* empty */ { $$ = chimp_array_new (); }
          ;

opt_class_decls : func_decl opt_class_decls { $$ = $2; chimp_array_unshift ($$, $1); }
                | /* empty */ { $$ = chimp_array_new (); }
                ;

class_decl : TOK_CLASS ident opt_extends TOK_LBRACE opt_class_decls TOK_RBRACE {
            $$ = chimp_ast_decl_new_class (
                CHIMP_AST_EXPR($2)->ident.id, $3, $5, &@$);
           }
           ;

opt_extends : TOK_COLON type_name { $$ = $2; }
            | /* empty */ { $$ = chimp_array_new (); }
            ;

type_name : ident TOK_FULLSTOP type_name {
            $$ = $3;
            chimp_array_unshift ($$, CHIMP_AST_EXPR($1)->ident.id);
          }
          | ident { $$ = chimp_array_new_var (CHIMP_AST_EXPR($1)->ident.id, NULL); }
          ;

func_decl : func_ident opt_params TOK_LBRACE opt_stmts TOK_RBRACE {
            $$ = chimp_ast_decl_new_func (CHIMP_AST_EXPR($1)->ident.id, $2, $4, &@$);
          }
          ;

opt_params : param opt_params_tail { $$ = $2; chimp_array_unshift ($$, $1); }
           | /* empty */ { $$ = chimp_array_new (); }
           ;

opt_params_tail : TOK_COMMA param opt_params_tail { $$ = $3; chimp_array_unshift ($$, $2); }
                 | /* empty */ { $$ = chimp_array_new (); }
                 ;

param : ident { $$ = chimp_ast_decl_new_var (CHIMP_AST_EXPR($1)->ident.id, NULL, &@$); }
      ;

stmts: stmt opt_stmts { $$ = $2; chimp_array_unshift ($$, $1); }
     ;

opt_stmts : stmt opt_stmts { $$ = $2; chimp_array_unshift ($$, $1); }
          | /* empty */ { $$ = chimp_array_new (); }
          ;

stmt : simple_stmt TOK_SEMICOLON { $$ = $1; }
     | compound_stmt { $$ = $1; }
     ;

simple_stmt : expr { $$ = chimp_ast_stmt_new_expr ($1, &@$); }
            | var_decl { $$ = $1; }
            | assign { $$ = $1; }
            | ret { $$ = $1; }
            | break { $$ = $1; }
            ;

compound_stmt : TOK_IF expr block else { $$ = chimp_ast_stmt_new_if_ ($2, $3, $4, &@$); }
              | TOK_IF expr block { $$ = chimp_ast_stmt_new_if_ ($2, $3, NULL, &@$); }
              | TOK_WHILE expr block { $$ = chimp_ast_stmt_new_while_ ($2, $3, &@$); }
              | TOK_MATCH expr TOK_LBRACE opt_patterns TOK_RBRACE {
                    $$ = chimp_ast_stmt_new_match ($2, $4, &@$);
              }
              ;

else : TOK_ELSE block { $$ = $2; }

block : TOK_LBRACE stmts TOK_RBRACE { $$ = $2; }
      | TOK_LBRACE TOK_RBRACE { $$ = chimp_array_new (); }
      /* | stmt { $$ = chimp_array_new (); chimp_array_unshift ($$, $1); } */
      | TOK_SEMICOLON { $$ = chimp_array_new (); }
      ;

var_decl : TOK_VAR ident { $$ = chimp_ast_decl_new_var (CHIMP_AST_EXPR($2)->ident.id, NULL, &@$); }
         | TOK_VAR ident TOK_ASSIGN expr {
            $$ = chimp_ast_decl_new_var (CHIMP_AST_EXPR($2)->ident.id, $4, &@$);
         }
         ;

assign : ident TOK_ASSIGN expr { $$ = chimp_ast_stmt_new_assign ($1, $3, &@$); }
       ;

expr : expr2 TOK_OR expr  { $$ = chimp_ast_expr_new_binop (CHIMP_BINOP_OR, $1, $3, &@$); }
     | expr2 TOK_AND expr { $$ = chimp_ast_expr_new_binop (CHIMP_BINOP_AND, $1, $3, &@$); }
     | expr2 TOK_GT expr  { $$ = chimp_ast_expr_new_binop (CHIMP_BINOP_GT, $1, $3, &@$); }
     | expr2 TOK_GTE expr  { $$ = chimp_ast_expr_new_binop (CHIMP_BINOP_GTE, $1, $3, &@$); }
     | expr2 TOK_LT expr  { $$ = chimp_ast_expr_new_binop (CHIMP_BINOP_LT, $1, $3, &@$); }
     | expr2 TOK_LTE expr  { $$ = chimp_ast_expr_new_binop (CHIMP_BINOP_LTE, $1, $3, &@$); }
     | expr2 TOK_EQ expr  { $$ = chimp_ast_expr_new_binop (CHIMP_BINOP_EQ, $1, $3, &@$); }
     | expr2 TOK_NEQ expr { $$ = chimp_ast_expr_new_binop (CHIMP_BINOP_NEQ, $1, $3, &@$); }
     | expr2 TOK_PLUS expr     { $$ = chimp_ast_expr_new_binop (CHIMP_BINOP_ADD, $1, $3, &@$); }
     | expr2 TOK_MINUS expr    { $$ = chimp_ast_expr_new_binop (CHIMP_BINOP_SUB, $1, $3, &@$); }
     | expr2 TOK_ASTERISK expr { $$ = chimp_ast_expr_new_binop (CHIMP_BINOP_MUL, $1, $3, &@$); }
     | expr2 TOK_SLASH expr    { $$ = chimp_ast_expr_new_binop (CHIMP_BINOP_DIV, $1, $3, &@$); }
     | expr2 { $$ = $1; }
     ;

expr2: expr2 simple_tail {
        $$ = $2;
        CHIMP_AST_EXPR_TARGETABLE_INNER($$, $1);
     }
     | expr3 { $$ = $1; }
     ;

expr3: TOK_NOT expr2 { $$ = chimp_ast_expr_new_not ($2, &@$); }
     | TOK_FN TOK_LBRACE TOK_PIPE opt_params TOK_PIPE opt_stmts TOK_RBRACE {
        $$ = chimp_ast_expr_new_fn ($4, $6, &@$); }
     | TOK_FN TOK_LBRACE opt_stmts TOK_RBRACE {
        $$ = chimp_ast_expr_new_fn (chimp_array_new (), $3, &@$);
     }
     | TOK_SPAWN TOK_LBRACE opt_stmts TOK_RBRACE {
        $$ = chimp_ast_expr_new_spawn (chimp_array_new (), $3, &@$);
     }
     | expr4 { $$ = $1; };
     ;

expr4 : expr5 { $$ = $1; }
       | array { $$ = $1; }
       | hash { $$ = $1; }
       ;

expr5: nil { $$ = $1; }
       | str { $$ = $1; }
       | int { $$ = $1; }
       | float { $$ = $1; }
       | ident { $$ = $1; }
       | bool { $$ = $1; }
       ;

opt_patterns: pattern opt_patterns { $$ = $2; chimp_array_unshift ($$, $1); }
            | /* empty */ { $$ = chimp_array_new (); }
            ;

pattern: pattern_test block { $$ = chimp_ast_stmt_new_pattern ($1, $2, &@$); }
       ;

pattern_test: expr5 { $$ = $1; }
            | TOK_UNDERSCORE { $$ = chimp_ast_expr_new_wildcard (&@$); }
            | TOK_LSQBRACKET opt_pattern_array_elements TOK_RSQBRACKET {
                $$ = chimp_ast_expr_new_array ($2, &@$);
            }
            | TOK_LBRACE opt_pattern_hash_elements TOK_RBRACE {
                $$ = chimp_ast_expr_new_hash ($2, &@$);
            }
            ;

opt_pattern_array_elements : pattern_array_elements { $$ = $1; }
                           | /* empty */ { $$ = chimp_array_new (); }
                           ;

pattern_array_elements: pattern_test opt_pattern_array_elements_tail {
                          $$ = $2; chimp_array_unshift ($$, $1);
                      }
                      ;

opt_pattern_array_elements_tail:
            TOK_COMMA pattern_test opt_pattern_array_elements_tail {
              $$ = $3; chimp_array_unshift ($$, $2);
            }
            | /* empty */ { $$ = chimp_array_new (); }
            ;

opt_pattern_hash_elements : pattern_hash_elements { $$ = $1; }
                          | /* empty */ { $$ = chimp_array_new (); }
                          ;

pattern_hash_elements :
    pattern_test TOK_COLON pattern_test opt_pattern_hash_elements_tail {
        $$ = $4; chimp_array_unshift ($$, $3); chimp_array_unshift ($$, $1);
    }
    ;

opt_pattern_hash_elements_tail :
        TOK_COMMA pattern_test TOK_COLON pattern_test
            opt_pattern_hash_elements_tail
        {
            $$ = $5; chimp_array_unshift ($$, $4); chimp_array_unshift ($$, $2);
        }
        | /* empty */ { $$ = chimp_array_new (); }
        ;

simple_tail : TOK_LBRACKET opt_args TOK_RBRACKET {
                $$ = chimp_ast_expr_new_call (NULL, $2, &@$);
            }
            | TOK_LSQBRACKET expr TOK_RSQBRACKET {
                $$ = chimp_ast_expr_new_getitem (NULL, $2, &@$);
            }
            | TOK_FULLSTOP ident {
                $$ = chimp_ast_expr_new_getattr (NULL, CHIMP_AST_EXPR($2)->ident.id, &@$);
            }
            ;

opt_args : args        { $$ = $1; }
         | /* empty */ { $$ = chimp_array_new (); }
         ;

args : expr opt_args_tail { $$ = $2; chimp_array_unshift ($$, $1); }
     ;

opt_args_tail : TOK_COMMA expr opt_args_tail { $$ = $3; chimp_array_unshift ($$, $2); }
              | /* empty */ { $$ = chimp_array_new (); }
              ;

func_ident : TOK_IDENT { $$ = chimp_ast_expr_new_ident ($1, &@$); }
           ;

ident : TOK_IDENT { $$ = chimp_ast_expr_new_ident ($1, &@$); }
      | TOK_CLASS { $$ = chimp_ast_expr_new_ident (CHIMP_STR_NEW("class"), &@$); }
      ;

str : TOK_STR { $$ = chimp_ast_expr_new_str ($1, &@$); }
    ;

int : TOK_INT { $$ = chimp_ast_expr_new_int_ ($1, &@$); }
    ;

float : TOK_FLOAT { $$ = chimp_ast_expr_new_float_ ($1, &@$); }
    ;

nil : TOK_NIL { $$ = chimp_ast_expr_new_nil (&@$); }
    ;

bool : TOK_TRUE { $$ = chimp_ast_expr_new_bool (chimp_true, &@$); }
     | TOK_FALSE { $$ = chimp_ast_expr_new_bool (chimp_false, &@$); }
     ;

hash : TOK_LBRACE hash_elements TOK_RBRACE { $$ = chimp_ast_expr_new_hash ($2, &@$); }
     | TOK_LBRACE TOK_RBRACE { $$ = chimp_ast_expr_new_hash (chimp_array_new (), &@$); }
     ;

hash_elements : expr TOK_COLON expr opt_hash_elements_tail { $$ = $4; chimp_array_unshift ($$, $3); chimp_array_unshift ($$, $1); }
              ;

opt_hash_elements_tail : TOK_COMMA expr TOK_COLON expr opt_hash_elements_tail { $$ = $5; chimp_array_unshift ($$, $4); chimp_array_unshift ($$, $2); }
                       | /* empty */ { $$ = chimp_array_new (); }
                       ;

array : TOK_LSQBRACKET opt_array_elements TOK_RSQBRACKET { $$ = chimp_ast_expr_new_array ($2, &@$); }
      ;

opt_array_elements : array_elements { $$ = $1; }
                   | /* empty */ { $$ = chimp_array_new (); }
                   ;

array_elements : expr opt_array_elements_tail { $$ = $2; chimp_array_unshift ($$, $1); }
               ;

opt_array_elements_tail : TOK_COMMA expr opt_array_elements_tail { $$ = $3; chimp_array_unshift ($$, $2); }
                        | /* empty */ { $$ = chimp_array_new (); }
                        ;

opt_expr : expr { $$ = $1; }
         | /* empty */ { $$ = NULL; }
         ;

break : TOK_BREAK { $$ = chimp_ast_stmt_new_break_ (&@$); }
      ;

ret : TOK_RET opt_expr { $$ = chimp_ast_stmt_new_ret ($2, &@$); }
    ;

%%

void
yyerror (const ChimpAstNodeLocation *loc, void *scanner, ChimpRef *filename, ChimpRef **mod, const char *format, ...)
{
    va_list args;

    *mod = NULL;

    fprintf (stderr, "error:%s:%zu:%zu: ",
                        CHIMP_STR_DATA(loc->filename),
                        (intmax_t) loc->first_line, (intmax_t) loc->first_column);
    va_start (args, format);
    vfprintf (stderr, format, args);
    va_end (args);
    fprintf (stderr, "\n");
    exit (1);
}

