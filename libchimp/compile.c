/*****************************************************************************
 *                                                                           *
 * Copyright 2012 Thomas Lee                                                 *
 *                                                                           *
 * Licensed under the Apache License, Version 2.0 (the "License");           *
 * you may not use this file except in compliance with the License.          *
 * You may obtain a copy of the License at                                   *
 *                                                                           *
 *     http://www.apache.org/licenses/LICENSE-2.0                            *
 *                                                                           *
 * Unless required by applicable law or agreed to in writing, software       *
 * distributed under the License is distributed on an "AS IS" BASIS,         *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  *
 * See the License for the specific language governing permissions and       *
 * limitations under the License.                                            *
 *                                                                           *
 *****************************************************************************/

#include <sys/stat.h>
#include <sys/types.h>

#include "chimp/compile.h"
#include "chimp/str.h"
#include "chimp/ast.h"
#include "chimp/code.h"
#include "chimp/array.h"
#include "chimp/int.h"
#include "chimp/method.h"
#include "chimp/object.h"
#include "chimp/task.h"
#include "chimp/_parser.h"
#include "chimp/symtable.h"

typedef enum _ChimpUnitType {
    CHIMP_UNIT_TYPE_CODE,
    CHIMP_UNIT_TYPE_MODULE,
    CHIMP_UNIT_TYPE_CLASS
} ChimpUnitType;

typedef struct _ChimpCodeUnit {
    ChimpUnitType type;
    /* XXX not entirely sure why I made this a union. */
    union {
        ChimpRef *code;
        ChimpRef *module;
        ChimpRef *class;
        ChimpRef *value;
    };
    struct _ChimpCodeUnit *next;
    ChimpRef              *ste;
} ChimpCodeUnit;

/* track (possibly nested) loops so we can compute jumps for 'break' */

typedef struct _ChimpLoopStack {
    ChimpLabel *items;
    size_t      size;
    size_t      capacity;
} ChimpLoopStack;

typedef struct _ChimpCodeCompiler {
    ChimpCodeUnit  *current_unit;
    ChimpLoopStack  loop_stack;
    ChimpRef       *symtable;
} ChimpCodeCompiler;

/* the ChimpBind* structures are used for pattern matching */

typedef struct _ChimpBindPathItem {
    enum {
        CHIMP_BIND_PATH_ITEM_TYPE_SIMPLE,
        CHIMP_BIND_PATH_ITEM_TYPE_ARRAY,
        CHIMP_BIND_PATH_ITEM_TYPE_HASH
    } type;
    union {
        size_t    array_index;
        ChimpRef *hash_key;
    };
} ChimpBindPathItem;

#define MAX_BIND_PATH_SIZE 16

typedef struct _ChimpBindPath {
    ChimpBindPathItem items[16];
    size_t            size;
} ChimpBindPath;

typedef struct _ChimpBindVar {
    ChimpRef      *id;
    ChimpBindPath  path;
} ChimpBindVar;

#define MAX_BIND_VARS_SIZE 16

typedef struct _ChimpBindVars {
    ChimpBindVar    items[MAX_BIND_VARS_SIZE];
    size_t          size;
} ChimpBindVars;

#define CHIMP_BIND_PATH_ITEM_ARRAY(index) { CHIMP_BIND_PATH_ARRAY, (index) }
#define CHIMP_BIND_PATH_ITEM_HASH(key)    { CHIMP_BIND_PATH_HASH, (key) }

#define CHIMP_BIND_PATH_INIT(stack) memset ((stack), 0, sizeof(*(stack)))
#define CHIMP_BIND_PATH_PUSH_ARRAY(stack) \
    do { \
        if ((stack)->size >= MAX_BIND_PATH_SIZE) \
            CHIMP_BUG ("bindpath limit reached"); \
        (stack)->items[(stack)->size].type = CHIMP_BIND_PATH_ITEM_TYPE_ARRAY; \
        (stack)->items[(stack)->size].array_index = 0; \
        (stack)->size++; \
    } while (0)
#define CHIMP_BIND_PATH_PUSH_HASH(stack) \
    do { \
        if ((stack)->size >= MAX_BIND_PATH_SIZE) \
            CHIMP_BUG ("bindpath limit reached"); \
        (stack)->items[(stack)->size].type = CHIMP_BIND_PATH_ITEM_TYPE_HASH; \
        (stack)->items[(stack)->size].hash_key = NULL; \
        (stack)->size++; \
    } while (0)
#define CHIMP_BIND_PATH_POP(stack) (stack)->size--
#define CHIMP_BIND_PATH_ARRAY_INDEX(stack, i) \
    do { \
        if ((stack)->size == 0) { \
            CHIMP_BUG ("array index on empty bind path"); \
        } \
        if ((stack)->items[(stack)->size-1].type != CHIMP_BIND_PATH_ITEM_TYPE_ARRAY) { \
            CHIMP_BUG ("array index on path item that is not an array"); \
        } \
        (stack)->items[(stack)->size-1].array_index = (i); \
    } while (0)
#define CHIMP_BIND_PATH_HASH_KEY(stack, key) \
    do { \
        if ((stack)->size == 0) { \
            CHIMP_BUG ("hash key on empty bind path"); \
        } \
        if ((stack)->items[(stack)->size-1].type != CHIMP_BIND_PATH_ITEM_TYPE_HASH) { \
            CHIMP_BUG ("hash key on path item that is not a hash"); \
        } \
        (stack)->items[(stack)->size-1].hash_key = (key); \
    } while (0)
#define CHIMP_BIND_PATH_COPY(dest, src) memcpy ((dest), (src), sizeof(*dest));

#define CHIMP_BIND_VARS_INIT(vars) memset ((vars), 0, sizeof(*(vars)))
#define CHIMP_BIND_VARS_ADD(vars, id, p) \
    do { \
        if ((vars)->size >= MAX_BIND_VARS_SIZE) \
            CHIMP_BUG ("bindvar limit reached"); \
        (vars)->items[(vars)->size].id = id; \
        memcpy (&(vars)->items[(vars)->size].path, (p), sizeof(*p)); \
        /* XXX HACK path of zero elements means a top-level binding */ \
        if ((p)->size == 0) { \
            (vars)->items[(vars)->size].path.size++; \
            (vars)->items[(vars)->size].path.items[0].type = \
                CHIMP_BIND_PATH_ITEM_TYPE_SIMPLE; \
        } \
        (vars)->size++; \
    } while (0)

#define CHIMP_COMPILER_CODE(c) ((c)->current_unit)->code
#define CHIMP_COMPILER_MODULE(c) ((c)->current_unit)->module
#define CHIMP_COMPILER_CLASS(c) ((c)->current_unit)->class
#define CHIMP_COMPILER_SCOPE(c) ((c)->current_unit)->value

#define CHIMP_COMPILER_IN_MODULE(c) (((c)->current_unit)->type == CHIMP_UNIT_TYPE_MODULE)
#define CHIMP_COMPILER_IN_CODE(c) (((c)->current_unit)->type == CHIMP_UNIT_TYPE_CODE)
#define CHIMP_COMPILER_IN_CLASS(c) (((c)->current_unit)->type == CHIMP_UNIT_TYPE_CLASS)
#define CHIMP_COMPILER_SYMTABLE_ENTRY(c) (((c)->current_unit))->ste

static chimp_bool_t
chimp_compile_ast_decl (ChimpCodeCompiler *c, ChimpRef *decl);

static chimp_bool_t
chimp_compile_ast_stmt (ChimpCodeCompiler *c, ChimpRef *stmt);

static chimp_bool_t
chimp_compile_ast_expr (ChimpCodeCompiler *c, ChimpRef *expr);

static chimp_bool_t
chimp_compile_ast_expr_ident (ChimpCodeCompiler *c, ChimpRef *expr);

static chimp_bool_t
chimp_compile_ast_expr_array (ChimpCodeCompiler *c, ChimpRef *expr);

static chimp_bool_t
chimp_compile_ast_expr_hash (ChimpCodeCompiler *c, ChimpRef *expr);

static chimp_bool_t
chimp_compile_ast_expr_str (ChimpCodeCompiler *c, ChimpRef *expr);

static chimp_bool_t
chimp_compile_ast_expr_int_ (ChimpCodeCompiler *c, ChimpRef *expr);

static chimp_bool_t
chimp_compile_ast_expr_float_ (ChimpCodeCompiler *c, ChimpRef *expr);

static chimp_bool_t
chimp_compile_ast_expr_bool (ChimpCodeCompiler *c, ChimpRef *expr);

static chimp_bool_t
chimp_compile_ast_expr_call (ChimpCodeCompiler *c, ChimpRef *expr);

static chimp_bool_t
chimp_compile_ast_expr_fn (ChimpCodeCompiler *c, ChimpRef *expr);

static chimp_bool_t
chimp_compile_ast_expr_spawn (ChimpCodeCompiler *c, ChimpRef *expr);

static chimp_bool_t
chimp_compile_ast_expr_not (ChimpCodeCompiler *c, ChimpRef *expr);

static chimp_bool_t
chimp_compile_ast_expr_getattr (ChimpCodeCompiler *c, ChimpRef *expr);

static chimp_bool_t
chimp_compile_ast_expr_getitem (ChimpCodeCompiler *c, ChimpRef *expr);

static chimp_bool_t
chimp_compile_ast_expr_binop (ChimpCodeCompiler *c, ChimpRef *binop);

static chimp_bool_t
chimp_compile_ast_stmt_pattern_test (
    ChimpCodeCompiler *c,
    ChimpRef *test,
    ChimpBindPath *path,
    ChimpBindVars *vars,
    ChimpLabel *next_label
);

static void
chimp_code_compiler_cleanup (ChimpCodeCompiler *c)
{
    while (c->current_unit != NULL) {
        ChimpCodeUnit *unit = c->current_unit;
        ChimpCodeUnit *next = unit->next;
        CHIMP_FREE (unit);
        unit = next;
        c->current_unit = NULL;
    }
    if (c->loop_stack.items != NULL) {
        free (c->loop_stack.items);
        c->loop_stack.items = NULL;
    }
}

static ChimpLabel *
chimp_code_compiler_begin_loop (ChimpCodeCompiler *c)
{
    ChimpLoopStack *stack = &c->loop_stack;
    if (stack->capacity < stack->size + 1) {
        const size_t new_capacity =
            (stack->capacity == 0 ? 8 : stack->capacity * 2);
        ChimpLabel *labels = realloc (
            stack->items, new_capacity * sizeof(*stack->items));
        if (labels == NULL) {
            return NULL;
        }
        stack->items = labels;
        stack->capacity = new_capacity;
    }
    /* TODO CHIMP_LABEL_INIT (rename macro to CHIMP_LABEL_INIT_STATIC) */
    memset (stack->items + stack->size, 0, sizeof(*stack->items));
    stack->size++;
    return stack->items + (stack->size - 1);
}

static void
chimp_code_compiler_end_loop (ChimpCodeCompiler *c)
{
    ChimpLoopStack *stack = &c->loop_stack;
    if (stack->size == 0) {
        CHIMP_BUG ("end_loop with zero stack size");
        return;
    }
    /* TODO ensure we're in a code block */
    stack->size--;
    chimp_code_use_label (CHIMP_COMPILER_CODE(c), stack->items + stack->size);
}

static ChimpLabel *
chimp_code_compiler_get_loop_label (ChimpCodeCompiler *c)
{
    ChimpLoopStack *stack = &c->loop_stack;
    if (stack->size == 0) {
        CHIMP_BUG ("get_loop_label with zero stack size");
        return NULL;
    }
    return &stack->items[stack->size-1];
}

static ChimpRef *
chimp_code_compiler_push_unit (
    ChimpCodeCompiler *c, ChimpUnitType type, ChimpRef *scope, ChimpRef *value)
{
    ChimpCodeUnit *unit = CHIMP_MALLOC(ChimpCodeUnit, sizeof(*unit));
    if (unit == NULL) {
        return NULL;
    }
    memset (unit, 0, sizeof(*unit));
    switch (type) {
        case CHIMP_UNIT_TYPE_CODE:
            unit->code = chimp_code_new ();
            break;
        case CHIMP_UNIT_TYPE_MODULE:
            unit->module = chimp_module_new (chimp_nil, NULL);
            break;
        case CHIMP_UNIT_TYPE_CLASS:
            /* XXX the value parameter is a total hack */
            unit->class = value;
            break;
        default:
            CHIMP_BUG ("unknown unit type: %d", type);
            CHIMP_FREE (unit);
            return NULL;
    };
    if (unit->value == NULL) {
        CHIMP_FREE (unit);
        return NULL;
    }
    unit->type = type;
    unit->next = c->current_unit;
    unit->ste = chimp_symtable_lookup (c->symtable, scope);
    if (unit->ste == NULL) {
        CHIMP_FREE (unit);
        CHIMP_BUG ("symtable lookup error for scope %p", scope);
        return NULL;
    }
    if (unit->ste == chimp_nil) {
        CHIMP_FREE (unit);
        CHIMP_BUG ("symtable lookup failed for scope %p", scope);
        return NULL;
    }
    c->current_unit = unit;
    return unit->value;
}

inline static ChimpRef *
chimp_code_compiler_push_code_unit (ChimpCodeCompiler *c, ChimpRef *scope)
{
    return chimp_code_compiler_push_unit (c, CHIMP_UNIT_TYPE_CODE, scope, NULL);
}

inline static ChimpRef *
chimp_code_compiler_push_module_unit (ChimpCodeCompiler *c, ChimpRef *scope)
{
    return chimp_code_compiler_push_unit (c, CHIMP_UNIT_TYPE_MODULE, scope, NULL);
}

inline static ChimpRef *
chimp_code_compiler_push_class_unit (ChimpCodeCompiler *c, ChimpRef *scope, ChimpRef *klass)
{
    return chimp_code_compiler_push_unit (c, CHIMP_UNIT_TYPE_CLASS, scope, klass);
}

static ChimpRef *
chimp_code_compiler_pop_unit (ChimpCodeCompiler *c, ChimpUnitType expected)
{
    ChimpCodeUnit *unit = c->current_unit;
    if (unit == NULL) {
        CHIMP_BUG ("NULL code unit?");
        return NULL;
    }
    if (unit->type != expected) {
        CHIMP_BUG ("unexpected unit type!");
        return NULL;
    }
    c->current_unit = unit->next;
    CHIMP_FREE(unit);
    if (c->current_unit != NULL) {
        return c->current_unit->value;
    }
    else {
        return chimp_true;
    }
}

inline static ChimpRef *
chimp_code_compiler_pop_code_unit (ChimpCodeCompiler *c)
{
    return chimp_code_compiler_pop_unit (c, CHIMP_UNIT_TYPE_CODE);
}

inline static ChimpRef *
chimp_code_compiler_pop_module_unit (ChimpCodeCompiler *c)
{
    return chimp_code_compiler_pop_unit (c, CHIMP_UNIT_TYPE_MODULE);
}

inline static ChimpRef *
chimp_code_compiler_pop_class_unit (ChimpCodeCompiler *c)
{
    return chimp_code_compiler_pop_unit (c, CHIMP_UNIT_TYPE_CLASS);
}

static chimp_bool_t
chimp_compile_ast_stmts (ChimpCodeCompiler *c, ChimpRef *stmts)
{
    size_t i;

    for (i = 0; i < CHIMP_ARRAY_SIZE(stmts); i++) {
        if (CHIMP_ANY_CLASS(CHIMP_ARRAY_ITEM(stmts, i)) == chimp_ast_stmt_class) {
            if (!chimp_compile_ast_stmt (c, CHIMP_ARRAY_ITEM(stmts, i))) {
                /* TODO error message? */
                return CHIMP_FALSE;
            }
        }
        else if (CHIMP_ANY_CLASS(CHIMP_ARRAY_ITEM(stmts, i)) == chimp_ast_decl_class) {
            if (!chimp_compile_ast_decl (c, CHIMP_ARRAY_ITEM(stmts, i))) {
                return CHIMP_FALSE;
            }
        }
    }
    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_decls (ChimpCodeCompiler *c, ChimpRef *decls)
{
    size_t i;

    for (i = 0; i < CHIMP_ARRAY_SIZE(decls); i++) {
        if (!chimp_compile_ast_decl (c, CHIMP_ARRAY_ITEM(decls, i))) {
            /* TODO error message? */
            return CHIMP_FALSE;
        }
    }
    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_mod (ChimpCodeCompiler *c, ChimpRef *mod)
{
    ChimpRef *uses = CHIMP_AST_MOD(mod)->root.uses;
    ChimpRef *body = CHIMP_AST_MOD(mod)->root.body;

    if (!chimp_compile_ast_decls (c, uses)) {
        return CHIMP_FALSE;
    }

    /* TODO check CHIMP_AST_MOD_* type */
    if (!chimp_compile_ast_decls (c, body)) {
        return CHIMP_FALSE;
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_stmt_assign (ChimpCodeCompiler *c, ChimpRef *stmt)
{
    ChimpRef *name =
        CHIMP_AST_EXPR(CHIMP_AST_STMT(stmt)->assign.target)->ident.id;
    ChimpRef *code = CHIMP_COMPILER_CODE(c);

    if (!chimp_compile_ast_expr (c, CHIMP_AST_STMT(stmt)->assign.value)) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_storename (code, name)) {
        return CHIMP_FALSE;
    }
    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_stmt_while_ (ChimpCodeCompiler *c, ChimpRef *stmt)
{
    ChimpRef *code = CHIMP_COMPILER_CODE(c);
    ChimpLabel start_body = CHIMP_LABEL_INIT;
    ChimpLabel *end_body = NULL;
    
    chimp_code_use_label (code, &start_body);

    if (!chimp_compile_ast_expr (c, CHIMP_AST_STMT(stmt)->while_.expr))
        goto error;

    end_body = chimp_code_compiler_begin_loop (c);

    if (!chimp_code_jumpiffalse (code, end_body))
        goto error;

    if (!chimp_compile_ast_stmts (c, CHIMP_AST_STMT(stmt)->while_.body))
        goto error;

    if (!chimp_code_jump (code, &start_body))
        goto error;

    chimp_code_compiler_end_loop (c);

    return CHIMP_TRUE;

error:
    chimp_label_free (&start_body);
    if (end_body != NULL) {
        chimp_code_compiler_end_loop (c);
    }
    return CHIMP_FALSE;
}

static chimp_bool_t
chimp_compile_ast_stmt_array_pattern_test (
    ChimpCodeCompiler *c,
    ChimpRef *array,
    ChimpBindPath *path,
    ChimpBindVars *vars,
    ChimpLabel *next_label
)
{
    ChimpRef *size;
    size_t i;
    ChimpRef *code = CHIMP_COMPILER_CODE(c);
    ChimpLabel pop_label = CHIMP_LABEL_INIT;
    ChimpLabel end_label = CHIMP_LABEL_INIT;

    /* XXX need to free labels when things go bad */

    /* is the value an array ? */
    if (!chimp_code_dup (code)) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_getclass (code)) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_pushname (code, CHIMP_STR_NEW("array"))) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_eq (code)) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_jumpiffalse (code, next_label)) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_dup (code)) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_getattr (code, CHIMP_STR_NEW("size"))) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_call (code, 0)) {
        return CHIMP_FALSE;
    }

    size = chimp_int_new (CHIMP_ARRAY_SIZE(array));
    if (size == NULL) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_pushconst (code, size)) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_eq (code)) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_jumpiffalse (code, next_label)) {
        return CHIMP_FALSE;
    }

    for (i = 0; i < CHIMP_ARRAY_SIZE(array); i++) {
        ChimpRef *item = CHIMP_ARRAY_ITEM(array, i);

        if (!chimp_code_dup (code)) {
            return CHIMP_FALSE;
        }

        if (!chimp_code_pushconst (code, chimp_int_new (i))) {
            return CHIMP_FALSE;
        }

        if (!chimp_code_getitem (code)) {
            return CHIMP_FALSE;
        }

        CHIMP_BIND_PATH_ARRAY_INDEX(path, i);

        if (!chimp_compile_ast_stmt_pattern_test (
                    c, item, path, vars, &pop_label)) {
            return CHIMP_FALSE;
        }

        if (!chimp_code_pop (code)) {
            return CHIMP_FALSE;
        }
    }

    if (!chimp_code_jump (code, &end_label)) {
        return CHIMP_FALSE;
    }

    chimp_code_use_label (code, &pop_label);

    if (!chimp_code_pop (code)) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_jump (code, next_label)) {
        return CHIMP_FALSE;
    }

    chimp_code_use_label (code, &end_label);

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_stmt_hash_pattern_test (
    ChimpCodeCompiler *c,
    ChimpRef *hash,
    ChimpBindPath *path,
    ChimpBindVars *vars,
    ChimpLabel *next_label
)
{
    ChimpRef *size;
    size_t i;
    ChimpRef *code = CHIMP_COMPILER_CODE(c);

    /* is the value a hash ? */
    if (!chimp_code_dup (code)) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_getclass (code)) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_pushname (code, CHIMP_STR_NEW("hash"))) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_eq (code)) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_jumpiffalse (code, next_label)) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_dup (code)) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_getattr (code, CHIMP_STR_NEW("size"))) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_call (code, 0)) {
        return CHIMP_FALSE;
    }

    /* divide by two because we have an array repr of a hash:
     * [key1, value1, key2, value2]
     */
    size = chimp_int_new (CHIMP_ARRAY_SIZE(hash) / 2);
    if (size == NULL) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_pushconst (code, size)) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_eq (code)) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_jumpiffalse (code, next_label)) {
        return CHIMP_FALSE;
    }

    /* XXX hashes are encoded as arrays in the AST */
    for (i = 0; i < CHIMP_ARRAY_SIZE(hash); i += 2) {
        ChimpRef *key = CHIMP_ARRAY(hash)->items[i];
        ChimpRef *value = CHIMP_ARRAY(hash)->items[i+1];
        ChimpRef *realkey;

        switch (CHIMP_AST_EXPR_TYPE(key)) {
            case CHIMP_AST_EXPR_IDENT:
                {
                    CHIMP_BUG (
                        "pattern matcher does not support unpacking by key");
                    return CHIMP_FALSE;
                }
            case CHIMP_AST_EXPR_ARRAY:
                {
                    CHIMP_BUG ("pattern matcher does not support array keys");
                    return CHIMP_FALSE;
                }
            case CHIMP_AST_EXPR_HASH:
                {
                    CHIMP_BUG ("pattern matcher does not support hash keys");
                    return CHIMP_FALSE;
                }
            case CHIMP_AST_EXPR_STR:
                {
                    realkey = CHIMP_AST_EXPR(key)->str.value;
                    break;
                }
            case CHIMP_AST_EXPR_INT_:
                {
                    realkey = CHIMP_AST_EXPR(key)->int_.value;
                    break;
                }
            case CHIMP_AST_EXPR_FLOAT_:
                {
                    realkey = CHIMP_AST_EXPR(key)->float_.value;
                    break;
                }
            case CHIMP_AST_EXPR_BOOL:
                {
                    realkey = CHIMP_AST_EXPR(key)->bool.value;
                    break;
                }
            case CHIMP_AST_EXPR_NIL:
                {
                    realkey = chimp_nil;
                    break;
                }
            case CHIMP_AST_EXPR_WILDCARD:
                {
                    CHIMP_BUG (
                        "pattern matcher doesn't support wildcard in hash key");
                    return CHIMP_FALSE;
                }
            default:
                CHIMP_BUG (
                    "pattern matcher found unknown AST expr type in hash key");
                return CHIMP_FALSE;
        }

        if (!chimp_code_dup (code)) {
            return CHIMP_FALSE;
        }

        if (!chimp_code_pushconst (code, realkey)) {
            return CHIMP_FALSE;
        }

        if (!chimp_code_getitem (code)) {
            return CHIMP_FALSE;
        }

        CHIMP_BIND_PATH_HASH_KEY(path, realkey);

        if (!chimp_compile_ast_stmt_pattern_test (
                    c, value, path, vars, next_label)) {
            return CHIMP_FALSE;
        }

        if (!chimp_code_pop (code)) {
            return CHIMP_FALSE;
        }
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_stmt_simple_pattern_test (
    ChimpCodeCompiler *c,
    const char *class_name,
    ChimpRef *value,
    ChimpLabel *next_label
)
{
    ChimpRef *code = CHIMP_COMPILER_CODE(c);

    /* XXX the class comparison code path should be moved elsewhere:
     *     pretty sure it's only used by a pattern test for `nil`
     */
    if (value == NULL) {
        /*
         * no value provided. do the types match?
         */
        if (!chimp_code_dup (code)) {
            return CHIMP_FALSE;
        }

        if (!chimp_code_getclass (code)) {
            return CHIMP_FALSE;
        }

        /* XXX the `nil` class isn't exposed at the language level */
        if (strcmp (class_name, "nil") == 0) {
            if (!chimp_code_pushconst (code, chimp_nil_class)) {
                return CHIMP_FALSE;
            }
        }
        else if (!chimp_code_pushname (code,
                chimp_str_new (class_name, strlen(class_name)))) {
            return CHIMP_FALSE;
        }

        if (!chimp_code_eq (code)) {
            return CHIMP_FALSE;
        }

        if (!chimp_code_jumpiffalse (code, next_label)) {
            return CHIMP_FALSE;
        }

    }
    else /* if (value != NULL) */ {
        /*
         * okay, so the types match. are the values equivalent?
         */
        if (!chimp_code_dup (code)) {
            return CHIMP_FALSE;
        }

        if (!chimp_code_pushconst (code, value)) {
            return CHIMP_FALSE;
        }

        if (!chimp_code_eq (code)) {
            return CHIMP_FALSE;
        }

        if (!chimp_code_jumpiffalse (code, next_label)) {
            return CHIMP_FALSE;
        }
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_stmt_pattern_test (
    ChimpCodeCompiler *c,
    ChimpRef *test,
    ChimpBindPath *path,
    ChimpBindVars *vars,
    ChimpLabel *next_label
)
{
    switch (CHIMP_AST_EXPR(test)->type) {
        case CHIMP_AST_EXPR_INT_:
            {
                ChimpRef *value = CHIMP_AST_EXPR(test)->int_.value;
                if (!chimp_compile_ast_stmt_simple_pattern_test (
                        c, "int", value, next_label)) {
                    return CHIMP_FALSE;
                }

                break;
            }
        case CHIMP_AST_EXPR_FLOAT_:
            {
                ChimpRef *value = CHIMP_AST_EXPR(test)->float_.value;
                if (!chimp_compile_ast_stmt_simple_pattern_test(
                        c, "float", value, next_label )) {
                    return CHIMP_FALSE;
                }

               break; 
            }
        case CHIMP_AST_EXPR_STR:
            {
                ChimpRef *value = CHIMP_AST_EXPR(test)->str.value;
                if (!chimp_compile_ast_stmt_simple_pattern_test (
                        c, "str", value, next_label)) {
                    return CHIMP_FALSE;
                }

                break;
            }
        case CHIMP_AST_EXPR_BOOL:
            {
                ChimpRef *value = CHIMP_AST_EXPR(test)->bool.value;
                if (!chimp_compile_ast_stmt_simple_pattern_test (
                        c, "bool", value, next_label)) {
                    return CHIMP_FALSE;
                }

                break;
            }
        case CHIMP_AST_EXPR_NIL:
            {
                if (!chimp_compile_ast_stmt_simple_pattern_test (
                        c, "nil", NULL, next_label)) {
                    return CHIMP_FALSE;
                }

                break;
            }
        case CHIMP_AST_EXPR_IDENT:
            {
                ChimpRef *id = CHIMP_AST_EXPR(test)->ident.id;

                CHIMP_BIND_VARS_ADD(vars, id, path);

                break;
            }
        case CHIMP_AST_EXPR_ARRAY:
            {
                ChimpRef *value = CHIMP_AST_EXPR(test)->array.value;

                CHIMP_BIND_PATH_PUSH_ARRAY(path);

                if (!chimp_compile_ast_stmt_array_pattern_test (
                        c, value, path, vars, next_label)) {
                    return CHIMP_FALSE;
                }

                CHIMP_BIND_PATH_POP(path);

                break;
            }
        case CHIMP_AST_EXPR_HASH:
            {
                ChimpRef *value = CHIMP_AST_EXPR(test)->hash.value;

                CHIMP_BIND_PATH_PUSH_HASH(path);

                if (!chimp_compile_ast_stmt_hash_pattern_test (
                        c, value, path, vars, next_label)) {
                    return CHIMP_FALSE;
                }

                CHIMP_BIND_PATH_POP(path);

                break;
            }
        case CHIMP_AST_EXPR_WILDCARD:
            {
                break;
            }
        default:
            CHIMP_BUG ("TODO");
            return CHIMP_FALSE;
    };

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_stmt_match (ChimpCodeCompiler *c, ChimpRef *stmt)
{
    size_t i;
    ChimpLabel end_label = CHIMP_LABEL_INIT;
    ChimpRef *code = CHIMP_COMPILER_CODE(c);
    ChimpRef *expr = CHIMP_AST_STMT(stmt)->match.expr;
    ChimpRef *patterns = CHIMP_AST_STMT(stmt)->match.body;
    const size_t size = CHIMP_ARRAY_SIZE(patterns);

    if (!chimp_compile_ast_expr (c, expr)) {
        chimp_label_free (&end_label);
        return CHIMP_FALSE;
    }

    for (i = 0; i < size; i++) {
        ChimpLabel next_label = CHIMP_LABEL_INIT;
        ChimpRef *pattern = CHIMP_ARRAY_ITEM(patterns, i);
        ChimpRef *test = CHIMP_AST_STMT(pattern)->pattern.test;
        ChimpRef *body = CHIMP_AST_STMT(pattern)->pattern.body;
        size_t j, k;
        ChimpBindPath path;
        ChimpBindVars bound;

        CHIMP_BIND_PATH_INIT(&path);
        CHIMP_BIND_VARS_INIT(&bound);

        /* try to match the value against this test. if we fail, jump to next_label */
        if (!chimp_compile_ast_stmt_pattern_test (c, test, &path, &bound, &next_label)) {
            chimp_label_free (&end_label);
            return CHIMP_FALSE;
        }

        /* pattern test succeeded: bind pattern vars (if any) */
        for (j = 0; j < bound.size; j++) {
            const ChimpBindPath *var_path = &bound.items[j].path;

            if (!chimp_code_dup (code)) {
                return CHIMP_FALSE;
            }

            for (k = 0; k < var_path->size; k++) {
                if (var_path->items[k].type == CHIMP_BIND_PATH_ITEM_TYPE_ARRAY) {
                    size_t index = var_path->items[k].array_index;

                    if (!chimp_code_pushconst (code, chimp_int_new (index))) {
                        return CHIMP_FALSE;
                    }

                    if (!chimp_code_getitem (code)) {
                        return CHIMP_FALSE;
                    }
                }
                else if (var_path->items[k].type == CHIMP_BIND_PATH_ITEM_TYPE_HASH) {
                    ChimpRef *key = var_path->items[k].hash_key;

                    if (!chimp_code_pushconst (code, key)) {
                        return CHIMP_FALSE;
                    }

                    if (!chimp_code_getitem (code)) {
                        return CHIMP_FALSE;
                    }
                }
                else {
                    /* simple binding: we're storing the matched value itself */
                }
            }
            if (!chimp_code_storename (code, bound.items[j].id)) {
                return CHIMP_FALSE;
            }
        }

        /* pop the value we're testing off the stack */
        if (!chimp_code_pop (code)) {
            return CHIMP_FALSE;
        }

        /* successful match: execute the body */
        if (!chimp_compile_ast_stmts (c, body)) {
            return CHIMP_FALSE;
        }

        if (!chimp_code_jump (code, &end_label)) {
            return CHIMP_FALSE;
        }

        chimp_code_use_label (code, &next_label);
    }

    chimp_code_use_label (code, &end_label);

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_stmt_if_ (ChimpCodeCompiler *c, ChimpRef *stmt)
{
    ChimpRef *code = CHIMP_COMPILER_CODE(c);
    ChimpLabel end_body = CHIMP_LABEL_INIT;
    ChimpLabel end_orelse = CHIMP_LABEL_INIT;

    if (!chimp_compile_ast_expr (c, CHIMP_AST_STMT(stmt)->if_.expr))
        goto error;

    if (!chimp_code_jumpiffalse (code, &end_body))
        goto error;

    if (!chimp_compile_ast_stmts (c, CHIMP_AST_STMT(stmt)->if_.body))
        goto error;

    if (CHIMP_AST_STMT(stmt)->if_.orelse != NULL) {

        if (!chimp_code_jump (code, &end_orelse))
            goto error;

        chimp_code_use_label (code, &end_body);

        if (!chimp_compile_ast_stmts (c, CHIMP_AST_STMT(stmt)->if_.orelse))
            goto error;

        chimp_code_use_label (code, &end_orelse);
    }
    else {
        chimp_code_use_label (code, &end_body);
    }

    return CHIMP_TRUE;

error:
    chimp_label_free (&end_body);
    chimp_label_free (&end_orelse);
    return CHIMP_FALSE;
}

static chimp_bool_t
chimp_compile_ast_stmt_ret (ChimpCodeCompiler *c, ChimpRef *stmt)
{
    ChimpRef *code = CHIMP_COMPILER_CODE(c);
    ChimpRef *expr = CHIMP_AST_STMT(stmt)->ret.expr;
    if (expr != NULL) {
        if (!chimp_compile_ast_expr (c, expr)) {
            return CHIMP_FALSE;
        }
    }
    else if (!chimp_code_pushnil (code)) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_ret (code)) {
        return CHIMP_FALSE;
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_stmt_break_ (ChimpCodeCompiler *c, ChimpRef *stmt)
{
    ChimpRef *code = CHIMP_COMPILER_CODE(c);
    ChimpLabel *label = chimp_code_compiler_get_loop_label (c);
    if (label == NULL) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_jump (code, label)) {
        return CHIMP_FALSE;
    }

    return CHIMP_TRUE;
}

static ChimpRef *
chimp_compile_get_nearest_unit_with_type (
        ChimpCodeCompiler *c, ChimpUnitType type)
{
    if (c->current_unit->type == type) {
        return CHIMP_COMPILER_SCOPE(c);
    }
    else {
        ChimpCodeUnit *unit = c->current_unit;
        if (unit == NULL) return NULL;
        unit = unit->next;
        while (unit != NULL) {
            if (unit->type == type) {
                return unit->value;
            }
            unit = unit->next;
        }
        return NULL;
    }
}

static ChimpRef *
chimp_compile_get_current_class (ChimpCodeCompiler *c)
{
    return chimp_compile_get_nearest_unit_with_type (
            c, CHIMP_UNIT_TYPE_CLASS);
}

static ChimpRef *
chimp_compile_get_current_module (ChimpCodeCompiler *c)
{
    return chimp_compile_get_nearest_unit_with_type (
            c, CHIMP_UNIT_TYPE_MODULE);
}

static ChimpRef *
chimp_compile_bytecode_method (ChimpCodeCompiler *c, ChimpRef *fn, ChimpRef *args, ChimpRef *body)
{
    ChimpRef *mod;
    ChimpRef *func_code;
    ChimpRef *symbols;
    ChimpRef *method;
    ChimpRef *ste;
    size_t i;

    func_code = chimp_code_compiler_push_code_unit (c, fn);
    if (func_code == NULL) {
        return NULL;
    }

    ste = c->current_unit->ste;
    symbols = CHIMP_SYMTABLE_ENTRY(ste)->symbols;
    for (i = 0; i < CHIMP_HASH_SIZE(symbols); i++) {
        ChimpRef *key = CHIMP_HASH(symbols)->keys[i];
        ChimpRef *value = CHIMP_HASH(symbols)->values[i];
        if (CHIMP_INT(value)->value & CHIMP_SYM_DECL) {
            if (!chimp_array_push (CHIMP_CODE(func_code)->vars, key)) {
                CHIMP_BUG ("failed to push var name");
                return NULL;
            }
        }
        else if (CHIMP_INT(value)->value & CHIMP_SYM_FREE) {
            if (!chimp_array_push (CHIMP_CODE(func_code)->freevars, key)) {
                CHIMP_BUG ("failed to push freevar name");
                return NULL;
            }
        }
    }

    /* unpack arguments */
    for (i = 0; i < CHIMP_ARRAY_SIZE(args); i++) {
        ChimpRef *var_decl = CHIMP_ARRAY_ITEM(args, CHIMP_ARRAY_SIZE(args) - i - 1);
        if (!chimp_code_storename (func_code, CHIMP_AST_DECL(var_decl)->var.name)) {
            CHIMP_BUG ("failed to emit STORENAME instruction");
            return NULL;
        }
    }
    
    if (!chimp_compile_ast_stmts (c, body)) {
        return NULL;
    }

    if (!chimp_code_compiler_pop_code_unit (c)) {
        return NULL;
    }

    mod = chimp_compile_get_current_module (c);

    if (getenv ("CHIMP_DEBUG_MODE")) {
        fprintf (stderr, "%s\n", CHIMP_STR_DATA(chimp_code_dump (func_code)));
    }
    method = chimp_method_new_bytecode (mod, func_code);
    if (method == NULL) {
        return NULL;
    }

    return method;
}

static chimp_bool_t
chimp_compile_ast_decl_func (ChimpCodeCompiler *c, ChimpRef *decl)
{
    ChimpRef *method;
    ChimpRef *mod;

    method = chimp_compile_bytecode_method (
        c,
        decl,
        CHIMP_AST_DECL(decl)->func.args,
        CHIMP_AST_DECL(decl)->func.body
    );
    if (method == NULL) {
        return CHIMP_FALSE;
    }

    if (CHIMP_COMPILER_IN_CODE(c)) {
        ChimpRef *code = CHIMP_COMPILER_CODE(c);
        if (!chimp_code_pushconst (code, method)) {
            return CHIMP_FALSE;
        }

        if (!chimp_code_storename (code, CHIMP_AST_DECL(decl)->func.name)) {
            return CHIMP_FALSE;
        }
    }
    else if (CHIMP_COMPILER_IN_CLASS(c)) {
        ChimpRef *name = CHIMP_AST_DECL(decl)->func.name;
        mod = chimp_compile_get_current_class (c);
        if (!chimp_class_add_method (mod, name, method)) {
            return CHIMP_FALSE;
        }
    }
    else {
        ChimpRef *name = CHIMP_AST_DECL(decl)->func.name;
        mod = chimp_compile_get_current_module (c);
        if (!chimp_module_add_local (mod, name, method)) {
            return CHIMP_FALSE;
        }
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_decl_class (ChimpCodeCompiler *c, ChimpRef *decl)
{
    ChimpRef *mod;
    ChimpRef *name = CHIMP_AST_DECL(decl)->class.name;
    ChimpRef *base = CHIMP_AST_DECL(decl)->class.base;
    ChimpRef *body = CHIMP_AST_DECL(decl)->class.body;
    ChimpRef *base_ref = NULL;
    ChimpRef *klass;
    size_t i;

    if (CHIMP_COMPILER_IN_CODE(c)) {
        CHIMP_BUG ("Cannot declare classes inside functions");
        return CHIMP_FALSE;
    }
    else if (CHIMP_COMPILER_IN_CLASS(c)) {
        CHIMP_BUG ("Cannot declare nested classes");
    }

    mod = chimp_compile_get_current_module (c);
    if (mod == NULL) {
        CHIMP_BUG ("get_current_module failed");
        return CHIMP_FALSE;
    }

    /* resolve the base class */
    /* XXX this feels kind of hacky */
    if (CHIMP_ARRAY_SIZE(base) > 0) {
        if (chimp_is_builtin (chimp_array_first (base))) {
            int rc = chimp_hash_get (
                        chimp_builtins, chimp_array_first (base), &base_ref);
            if (rc < 0) {
                CHIMP_BUG ("is_builtin/hash_get failed");
                return CHIMP_FALSE;
            }
            else if (rc > 0) {
                return CHIMP_FALSE;
            }
        }
        else {
            base_ref = mod;
            for (i = 0; i < CHIMP_ARRAY_SIZE(base); i++) {
                int rc;
                ChimpRef *child;
                ChimpRef *name;
                if (i != CHIMP_ARRAY_SIZE(base)-1 && 
                    CHIMP_ANY_CLASS(base_ref) != chimp_module_class) {
                    CHIMP_BUG ("Cannot resolve class on non-module type: %s",
                        CHIMP_STR_DATA(name));
                    return CHIMP_FALSE;
                }
                name = CHIMP_ARRAY_ITEM(base, i);
                rc = chimp_hash_get (
                        CHIMP_MODULE_LOCALS(base_ref), name, &child);
                if (rc < 0) {
                    CHIMP_BUG ("Error");
                    return CHIMP_FALSE;
                }
                else if (rc > 0) {
                    CHIMP_BUG ("Module does not expose `%s`",
                        CHIMP_STR_DATA(CHIMP_ARRAY_ITEM(base_ref, i)));
                    return CHIMP_FALSE;
                }
                base_ref = child;
            }
        }
    }

    klass = chimp_class_new (name, base_ref, sizeof(ChimpObject));
    if (klass == NULL) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_compiler_push_class_unit (c, decl, klass)) {
        return CHIMP_FALSE;
    }
    if (!chimp_compile_ast_decls (c, body)) {
        return CHIMP_FALSE;
    }
    if (!chimp_code_compiler_pop_class_unit (c)) {
        return CHIMP_FALSE;
    }

    if (!chimp_module_add_local (mod, name, klass)) {
        CHIMP_BUG ("Failed to add new class to module");
        return CHIMP_FALSE;
    }
    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_decl_use (ChimpCodeCompiler *c, ChimpRef *decl)
{
    ChimpRef *module = CHIMP_COMPILER_MODULE(c);
    ChimpRef *import;
    ChimpRef *name;

    name = CHIMP_AST_DECL(decl)->use.name;
    import = chimp_module_load (name, chimp_module_path);

    if (import == NULL) {
        return CHIMP_FALSE;
    }

    if (!chimp_module_add_local (module, name, import)) {
        return CHIMP_FALSE;
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_decl_var (ChimpCodeCompiler *c, ChimpRef *decl)
{
    ChimpRef *scope = CHIMP_COMPILER_SCOPE(c);
    ChimpRef *name = CHIMP_AST_DECL(decl)->var.name;
    ChimpRef *value = CHIMP_AST_DECL(decl)->var.value;

    if (CHIMP_ANY_CLASS(scope) == chimp_module_class) {
        return chimp_module_add_local (scope, name, value == NULL ? chimp_nil : value);
    }
    else {
        /* TODO it's really about time we get ourselves a symbol table */
        if (value != NULL) {
            ChimpRef *code = CHIMP_COMPILER_CODE(c);

            if (!chimp_compile_ast_expr (c, value)) {
                return CHIMP_FALSE;
            }

            if (!chimp_code_storename (code, name)) {
                return CHIMP_FALSE;
            }
        }
    }
    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_decl (ChimpCodeCompiler *c, ChimpRef *decl)
{
    switch (CHIMP_AST_DECL_TYPE(decl)) {
        case CHIMP_AST_DECL_FUNC:
            return chimp_compile_ast_decl_func (c, decl);
        case CHIMP_AST_DECL_CLASS:
            return chimp_compile_ast_decl_class (c, decl);
        case CHIMP_AST_DECL_USE:
            return chimp_compile_ast_decl_use (c, decl);
        case CHIMP_AST_DECL_VAR:
            return chimp_compile_ast_decl_var (c, decl);
        default:
            CHIMP_BUG ("unknown AST stmt type: %d", CHIMP_AST_DECL_TYPE(decl));
            return CHIMP_FALSE;
    }
}

static chimp_bool_t
chimp_compile_ast_stmt (ChimpCodeCompiler *c, ChimpRef *stmt)
{
    switch (CHIMP_AST_STMT_TYPE(stmt)) {
        case CHIMP_AST_STMT_EXPR:
            return chimp_compile_ast_expr (c, CHIMP_AST_STMT(stmt)->expr.expr);
        case CHIMP_AST_STMT_ASSIGN:
            return chimp_compile_ast_stmt_assign (c, stmt);
        case CHIMP_AST_STMT_IF_:
            return chimp_compile_ast_stmt_if_ (c, stmt);
        case CHIMP_AST_STMT_WHILE_:
            return chimp_compile_ast_stmt_while_ (c, stmt);
        case CHIMP_AST_STMT_MATCH:
            return chimp_compile_ast_stmt_match (c, stmt);
        case CHIMP_AST_STMT_RET:
            return chimp_compile_ast_stmt_ret (c, stmt);
        case CHIMP_AST_STMT_BREAK_:
            return chimp_compile_ast_stmt_break_ (c, stmt);
        default:
            CHIMP_BUG ("unknown AST stmt type: %d", CHIMP_AST_STMT_TYPE(stmt));
            return CHIMP_FALSE;
    };
}

static chimp_bool_t
chimp_compile_ast_expr (ChimpCodeCompiler *c, ChimpRef *expr)
{
    switch (CHIMP_AST_EXPR_TYPE(expr)) {
        case CHIMP_AST_EXPR_CALL:
            return chimp_compile_ast_expr_call (c, expr);
        case CHIMP_AST_EXPR_GETATTR:
            return chimp_compile_ast_expr_getattr (c, expr);
        case CHIMP_AST_EXPR_GETITEM:
            return chimp_compile_ast_expr_getitem (c, expr);
        case CHIMP_AST_EXPR_ARRAY:
            return chimp_compile_ast_expr_array (c, expr);
        case CHIMP_AST_EXPR_HASH:
            return chimp_compile_ast_expr_hash (c, expr);
        case CHIMP_AST_EXPR_IDENT:
            return chimp_compile_ast_expr_ident (c, expr);
        case CHIMP_AST_EXPR_STR:
            return chimp_compile_ast_expr_str (c, expr);
        case CHIMP_AST_EXPR_BOOL:
            return chimp_compile_ast_expr_bool (c, expr);
        case CHIMP_AST_EXPR_NIL:
            return chimp_code_pushnil (CHIMP_COMPILER_CODE(c));
        case CHIMP_AST_EXPR_BINOP:
            return chimp_compile_ast_expr_binop (c, expr);
        case CHIMP_AST_EXPR_INT_:
            return chimp_compile_ast_expr_int_ (c, expr);
        case CHIMP_AST_EXPR_FLOAT_:
            return chimp_compile_ast_expr_float_ (c, expr);
        case CHIMP_AST_EXPR_FN:
            return chimp_compile_ast_expr_fn (c, expr);
        case CHIMP_AST_EXPR_SPAWN:
            return chimp_compile_ast_expr_spawn (c, expr);
        case CHIMP_AST_EXPR_NOT:
            return chimp_compile_ast_expr_not (c, expr);
        case CHIMP_AST_EXPR_WILDCARD:
            {
                CHIMP_BUG (
                    "wildcard can't be used outside of a match statement");
                return CHIMP_FALSE;
            }
        default:
            CHIMP_BUG ("unknown AST expr type: %d", CHIMP_AST_EXPR_TYPE(expr));
            return CHIMP_FALSE;
    };
}

static chimp_bool_t
chimp_compile_ast_expr_call (ChimpCodeCompiler *c, ChimpRef *expr)
{
    ChimpRef *target;
    ChimpRef *args;
    size_t i;
    ChimpRef *code = CHIMP_COMPILER_CODE(c);

    target = CHIMP_AST_EXPR(expr)->call.target;
    if (!chimp_compile_ast_expr (c, target)) {
        return CHIMP_FALSE;
    }

    args = CHIMP_AST_EXPR(expr)->call.args;
    for (i = 0; i < CHIMP_ARRAY_SIZE(args); i++) {
        if (!chimp_compile_ast_expr (c, CHIMP_ARRAY_ITEM(args, i))) {
            return CHIMP_FALSE;
        }
    }

    if (!chimp_code_call (code, CHIMP_ARRAY_SIZE(args))) {
        return CHIMP_FALSE;
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_expr_getattr (ChimpCodeCompiler *c, ChimpRef *expr)
{
    ChimpRef *target;
    ChimpRef *attr;
    ChimpRef *code = CHIMP_COMPILER_CODE(c);

    target = CHIMP_AST_EXPR(expr)->getattr.target;
    if (!chimp_compile_ast_expr (c, target)) {
        return CHIMP_FALSE;
    }

    attr = CHIMP_AST_EXPR(expr)->getattr.attr;
    if (attr == NULL) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_getattr (code, attr)) {
        return CHIMP_FALSE;
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_expr_getitem (ChimpCodeCompiler *c, ChimpRef *expr)
{
    ChimpRef *target;
    ChimpRef *key;
    ChimpRef *code = CHIMP_COMPILER_CODE(c);

    target = CHIMP_AST_EXPR(expr)->getitem.target;
    if (!chimp_compile_ast_expr (c, target)) {
        return CHIMP_FALSE;
    }

    key = CHIMP_AST_EXPR(expr)->getitem.key;
    if (key == NULL) {
        return CHIMP_FALSE;
    }

    if (!chimp_compile_ast_expr (c, key)) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_getitem (code)) {
        return CHIMP_FALSE;
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_expr_ident (ChimpCodeCompiler *c, ChimpRef *expr)
{
    ChimpRef *id = CHIMP_AST_EXPR(expr)->ident.id;
    const ChimpAstNodeLocation *loc = &CHIMP_AST_EXPR(expr)->location;
    ChimpRef *code = CHIMP_COMPILER_CODE(c);
    if (strcmp (CHIMP_STR_DATA (id), "__file__") == 0) {
        /* XXX loc->filename == NULL here. why?!? */
        if (!chimp_code_pushconst (code, CHIMP_SYMTABLE(c->symtable)->filename)) {
            return CHIMP_FALSE;
        }
    }
    else if (strcmp (CHIMP_STR_DATA (id), "__line__") == 0) {
        if (!chimp_code_pushconst (code, chimp_int_new (loc->first_line))) {
            return CHIMP_FALSE;
        }
    }
    else {
        if (!chimp_code_pushname (code, id)) {
            return CHIMP_FALSE;
        }
    }
    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_expr_array (ChimpCodeCompiler *c, ChimpRef *expr)
{
    size_t i;
    ChimpRef *code = CHIMP_COMPILER_CODE(c);
    ChimpRef *arr = CHIMP_AST_EXPR(expr)->array.value;
    for (i = 0; i < CHIMP_ARRAY_SIZE(arr); i++) {
        if (!chimp_compile_ast_expr (c, CHIMP_ARRAY_ITEM(arr, i))) {
            return CHIMP_FALSE;
        }
    }
    if (!chimp_code_makearray (code, CHIMP_ARRAY_SIZE(arr))) {
        return CHIMP_FALSE;
    }
    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_expr_hash (ChimpCodeCompiler *c, ChimpRef *expr)
{
    size_t i;
    ChimpRef *code = CHIMP_COMPILER_CODE(c);
    ChimpRef *arr = CHIMP_AST_EXPR(expr)->hash.value;
    /* TODO ensure array size is even (key/value pairs) */
    for (i = 0; i < CHIMP_ARRAY_SIZE(arr); i++) {
        if (!chimp_compile_ast_expr (c, CHIMP_ARRAY_ITEM(arr, i))) {
            return CHIMP_FALSE;
        }
    }
    if (!chimp_code_makehash (code, CHIMP_ARRAY_SIZE(arr) / 2)) {
        return CHIMP_FALSE;
    }
    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_expr_str (ChimpCodeCompiler *c, ChimpRef *expr)
{
    ChimpRef *code = CHIMP_COMPILER_CODE(c);
    if (chimp_code_pushconst (code, CHIMP_AST_EXPR(expr)->str.value) < 0) {
        return CHIMP_FALSE;
    }
    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_expr_bool (ChimpCodeCompiler *c, ChimpRef *expr)
{
    ChimpRef *code = CHIMP_COMPILER_CODE(c);
    if (chimp_code_pushconst (code, CHIMP_AST_EXPR(expr)->bool.value) < 0) {
        return CHIMP_FALSE;
    }
    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_expr_int_ (ChimpCodeCompiler *c, ChimpRef *expr)
{
    ChimpRef *code = CHIMP_COMPILER_CODE(c);
    if (chimp_code_pushconst (code, CHIMP_AST_EXPR(expr)->int_.value) < 0) {
        return CHIMP_FALSE;
    }
    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_expr_float_ (ChimpCodeCompiler *c, ChimpRef *expr)
{
    ChimpRef *code = CHIMP_COMPILER_CODE(c);
    if (chimp_code_pushconst (code, CHIMP_AST_EXPR(expr)->float_.value) < 0) {
        return CHIMP_FALSE;
    }
    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_expr_binop (ChimpCodeCompiler *c, ChimpRef *expr)
{
    ChimpRef *code = CHIMP_COMPILER_CODE(c);
    if (!chimp_compile_ast_expr (c, CHIMP_AST_EXPR(expr)->binop.left)) {
        return CHIMP_FALSE;
    }

    switch (CHIMP_AST_EXPR(expr)->binop.op) {
        case CHIMP_BINOP_EQ:
            {
                if (!chimp_compile_ast_expr (c, CHIMP_AST_EXPR(expr)->binop.right)) {
                    return CHIMP_FALSE;
                }

                if (!chimp_code_eq (code)) {
                    return CHIMP_FALSE;
                }
                break;
            }
        case CHIMP_BINOP_NEQ:
            {
                if (!chimp_compile_ast_expr (c, CHIMP_AST_EXPR(expr)->binop.right)) {
                    return CHIMP_FALSE;
                }

                if (!chimp_code_neq (code)) {
                    return CHIMP_FALSE;
                }
                break;
            }
        case CHIMP_BINOP_GT:
            {
                if (!chimp_compile_ast_expr (c, CHIMP_AST_EXPR(expr)->binop.right)) {
                    return CHIMP_FALSE;
                }

                if (!chimp_code_gt (code)) {
                    return CHIMP_FALSE;
                }
                break;
            }
        case CHIMP_BINOP_GTE:
            {
                if (!chimp_compile_ast_expr (c, CHIMP_AST_EXPR(expr)->binop.right)) {
                    return CHIMP_FALSE;
                }

                if (!chimp_code_gte (code)) {
                    return CHIMP_FALSE;
                }
                break;
            }
        case CHIMP_BINOP_LT:
            {
                if (!chimp_compile_ast_expr (c, CHIMP_AST_EXPR(expr)->binop.right)) {
                    return CHIMP_FALSE;
                }

                if (!chimp_code_lt (code)) {
                    return CHIMP_FALSE;
                }
                break;
            }
        case CHIMP_BINOP_LTE:
            {
                if (!chimp_compile_ast_expr (c, CHIMP_AST_EXPR(expr)->binop.right)) {
                    return CHIMP_FALSE;
                }

                if (!chimp_code_lte (code)) {
                    return CHIMP_FALSE;
                }
                break;
            }
        case CHIMP_BINOP_OR:
            {
                /* short-circuited logical OR */
                ChimpLabel right_label = CHIMP_LABEL_INIT;
                ChimpLabel end_label = CHIMP_LABEL_INIT;

                if (!chimp_code_jumpiffalse (code, &right_label))
                    goto or_error;

                if (!chimp_code_jump (code, &end_label))
                    goto or_error;

                chimp_code_use_label (code, &right_label);

                if (!chimp_compile_ast_expr (c, CHIMP_AST_EXPR(expr)->binop.right))
                    goto or_error;

                chimp_code_use_label (code, &end_label);

                break;

or_error:
                chimp_label_free (&right_label);
                chimp_label_free (&end_label);
                return CHIMP_FALSE;
            }
        case CHIMP_BINOP_AND:
            {
                /* short-circuited logical AND */

                ChimpLabel right_label = CHIMP_LABEL_INIT;
                ChimpLabel end_label = CHIMP_LABEL_INIT;

                if (!chimp_code_jumpiftrue (code, &right_label))
                    goto and_error;

                if (!chimp_code_jump (code, &end_label))
                    goto and_error;

                chimp_code_use_label (code, &right_label);

                /* JUMPIFTRUE will pop the value on our behalf */
                /*
                if (!chimp_code_pop (code)) {
                    return NULL;
                }
                */
                if (!chimp_compile_ast_expr (c, CHIMP_AST_EXPR(expr)->binop.right))
                    goto and_error;

                chimp_code_use_label (code, &end_label);

                break;
and_error:
                chimp_label_free (&right_label);
                chimp_label_free (&end_label);
                return CHIMP_FALSE;
            }
        case CHIMP_BINOP_ADD:
            {
                if (!chimp_compile_ast_expr (c, CHIMP_AST_EXPR(expr)->binop.right)) {
                    return CHIMP_FALSE;
                }
                
                if (!chimp_code_add (code)) {
                    return CHIMP_FALSE;
                }

                break;
            }
        case CHIMP_BINOP_SUB:
            {
                if (!chimp_compile_ast_expr (c, CHIMP_AST_EXPR(expr)->binop.right)) {
                    return CHIMP_FALSE;
                }

                if (!chimp_code_sub (code)) {
                    return CHIMP_FALSE;
                }

                break;
            }
        case CHIMP_BINOP_MUL:

            {
                if (!chimp_compile_ast_expr (c, CHIMP_AST_EXPR(expr)->binop.right)) {
                    return CHIMP_FALSE;
                }

                if (!chimp_code_mul (code)) {
                    return CHIMP_FALSE;
                }

                break;
            }
        case CHIMP_BINOP_DIV:
            {
                if (!chimp_compile_ast_expr (c, CHIMP_AST_EXPR(expr)->binop.right)) {
                    return CHIMP_FALSE;
                }

                if (!chimp_code_div (code)) {
                    return CHIMP_FALSE;
                }

                break;
            }
        default:
            CHIMP_BUG ("unknown binop type: %d", CHIMP_AST_EXPR(expr)->binop.op);
            return CHIMP_FALSE;
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_expr_fn (ChimpCodeCompiler *c, ChimpRef *expr)
{
    ChimpRef *method;
    ChimpRef *code = CHIMP_COMPILER_CODE(c);

    method = chimp_compile_bytecode_method (
        c,
        expr,
        CHIMP_AST_EXPR(expr)->fn.args,
        CHIMP_AST_EXPR(expr)->fn.body
    );
    if (method == NULL) {
        return CHIMP_FALSE;
    }
    
    if (!chimp_code_pushconst (code, method)) {
        return CHIMP_FALSE;
    }

    if (CHIMP_METHOD(method)->type == CHIMP_METHOD_TYPE_BYTECODE) {
        ChimpRef *method_code = CHIMP_METHOD(method)->bytecode.code;
        if (CHIMP_ARRAY_SIZE(CHIMP_CODE(method_code)->freevars) > 0) {
            if (!chimp_code_makeclosure (code)) {
                return CHIMP_FALSE;
            }
        }
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_expr_spawn (ChimpCodeCompiler *c, ChimpRef *expr)
{
    ChimpRef *method;
    ChimpRef *code = CHIMP_COMPILER_CODE(c);

    method = chimp_compile_bytecode_method (
        c,
        expr,
        CHIMP_AST_EXPR(expr)->fn.args,
        CHIMP_AST_EXPR(expr)->fn.body
    );
    if (method == NULL) {
        return CHIMP_FALSE;
    }
    
    if (!chimp_code_pushconst (code, method)) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_spawn (code)) {
        return CHIMP_FALSE;
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_compile_ast_expr_not (ChimpCodeCompiler *c, ChimpRef *expr)
{
    ChimpRef *code = CHIMP_COMPILER_CODE(c);
    if (!chimp_compile_ast_expr (c, CHIMP_AST_EXPR(expr)->not.value)) {
        return CHIMP_FALSE;
    }

    if (!chimp_code_not (code)) {
        return CHIMP_FALSE;
    }

    return CHIMP_TRUE;
}

ChimpRef *
chimp_compile_ast (ChimpRef *name, const char *filename, ChimpRef *ast)
{
    ChimpRef *module;
    ChimpCodeCompiler c;
    ChimpRef *filename_obj;
    memset (&c, 0, sizeof(c));

    filename_obj = chimp_str_new (filename, strlen (filename));
    if (filename_obj == NULL) {
        return NULL;
    }

    c.symtable = chimp_symtable_new_from_ast (filename_obj, ast);
    if (c.symtable == NULL) {
        goto error;
    }

    module = chimp_code_compiler_push_module_unit (&c, ast);
    if (module == NULL) {
        goto error;
    }
    CHIMP_MODULE(module)->name = name;

    if (CHIMP_ANY_CLASS(ast) == chimp_ast_mod_class) {
        if (!chimp_compile_ast_mod (&c, ast)) {
            goto error;
        }
    }
    else {
        CHIMP_BUG ("unknown top-level AST node type: %s",
                    CHIMP_STR_DATA(CHIMP_CLASS(CHIMP_ANY_CLASS(ast))->name));
        goto error;
    }

    if (!chimp_code_compiler_pop_unit (&c, CHIMP_UNIT_TYPE_MODULE)) {
        goto error;
    }

    chimp_code_compiler_cleanup (&c);

    return module;

error:
    chimp_code_compiler_cleanup (&c);
    return NULL;
}

extern int yyparse(void *scanner, ChimpRef *filename, ChimpRef **mod);
extern void yylex_init (void **scanner);
extern void yyset_in (FILE *stream, void *scanner);
extern void yylex_destroy(void *scanner);

static chimp_bool_t
is_file (const char *filename, chimp_bool_t *result)
{
    struct stat st;

    if (stat (filename, &st) != 0) {
        CHIMP_BUG ("stat() failed");
        return CHIMP_FALSE;
    }

    *result = S_ISREG(st.st_mode);

    return CHIMP_TRUE;
}

ChimpRef *
chimp_compile_file (ChimpRef *name, const char *filename)
{
    int rc;
    ChimpRef *filename_obj;
    ChimpRef *mod;
    chimp_bool_t isreg;
    void *scanner;
    FILE *input;

    if (!is_file (filename, &isreg)) {
        return NULL;
    }

    if (!isreg) {
        CHIMP_BUG ("not a regular file: %s", filename);
        return NULL;
    }

    input = fopen (filename, "r");
    if (input == NULL) {
        return NULL;
    }
    filename_obj = chimp_str_new (filename, strlen (filename));
    if (filename_obj == NULL) {
        fclose (input);
        return NULL;
    }
    yylex_init (&scanner);
    yyset_in (input, scanner);
    rc = yyparse(scanner, filename_obj, &mod);
    fclose (input);
    yylex_destroy (scanner);
    if (rc == 0) {
        /* keep a ptr to main_mod on the stack so it doesn't get collected */
        if (name == NULL) {
            ssize_t slash = -1;
            ssize_t dot = -1;
            size_t i;
            size_t len = strlen (filename);

            /* XXX this is just a shit O(n) basename() impl */
            for (i = 0; i < len; i++) {
                if (filename[i] == '/') {
                    slash = i + 1;
                    dot = -1;
                }
                else if (filename[i] == '.' && dot == -1) {
                    dot = i;
                }
            }
            if (dot == -1) dot = len;
            if (slash == -1) slash = 0;

            name = chimp_str_new (filename + slash, dot - slash);
            if (name == NULL) {
                return NULL;
            }
        }
        return chimp_compile_ast (name, filename, mod);
    }
    else {
        return NULL;
    }
}

