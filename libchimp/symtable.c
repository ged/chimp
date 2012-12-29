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

#include "chimp/hash.h"
#include "chimp/array.h"
#include "chimp/class.h"
#include "chimp/object.h"
#include "chimp/symtable.h"

#define CHIMP_SYMTABLE_INIT(ref) \
    CHIMP_ANY(ref)->type = CHIMP_VALUE_TYPE_SYMTABLE; \
    CHIMP_ANY(ref)->klass = chimp_symtable_class;

#define CHIMP_SYMTABLE_ENTRY_INIT(ref) \
    CHIMP_ANY(ref)->type = CHIMP_VALUE_TYPE_SYMTABLE_ENTRY; \
    CHIMP_ANY(ref)->klass = chimp_symtable_entry_class;

#define CHIMP_SYMTABLE_GET_CURRENT_ENTRY(ref) CHIMP_SYMTABLE(ref)->ste

#define CHIMP_SYMTABLE_ENTRY_CHECK_TYPE(ste, t) \
    ((CHIMP_SYMTABLE_ENTRY(ste)->flags & CHIMP_SYM_TYPE_MASK) == (t))

#define CHIMP_SYMTABLE_ENTRY_IS_MODULE(ste) \
    CHIMP_SYMTABLE_ENTRY_CHECK_TYPE(ste, CHIMP_SYM_MODULE)

#define CHIMP_SYMTABLE_ENTRY_IS_SPAWN(ste) \
    CHIMP_SYMTABLE_ENTRY_CHECK_TYPE(ste, CHIMP_SYM_SPAWN)

ChimpRef *chimp_symtable_class = NULL;
ChimpRef *chimp_symtable_entry_class = NULL;

static chimp_bool_t
chimp_symtable_visit_decls (ChimpRef *self, ChimpRef *decls);

static chimp_bool_t
chimp_symtable_visit_decl (ChimpRef *self, ChimpRef *decl);

static chimp_bool_t
chimp_symtable_visit_stmt (ChimpRef *self, ChimpRef *stmt);

static chimp_bool_t
chimp_symtable_visit_expr (ChimpRef *self, ChimpRef *expr);

chimp_bool_t
chimp_symtable_class_bootstrap (void)
{
    chimp_symtable_class =
        chimp_class_new (CHIMP_STR_NEW("symtable"), chimp_object_class);
    if (chimp_symtable_class == NULL) {
        return CHIMP_FALSE;
    }
    chimp_gc_make_root (NULL, chimp_symtable_class);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_symtable_entry_class_bootstrap (void)
{
    chimp_symtable_entry_class =
        chimp_class_new (CHIMP_STR_NEW("symtable.entry"), chimp_object_class);
    if (chimp_symtable_entry_class == NULL) {
        return CHIMP_FALSE;
    }
    chimp_gc_make_root (NULL, chimp_symtable_entry_class);
    return CHIMP_TRUE;
}

static ChimpRef *
chimp_symtable_entry_new (
    ChimpRef *symtable, ChimpRef *parent, ChimpRef *scope, int flags)
{
    ChimpRef *ref = chimp_gc_new_object (NULL);
    if (ref == NULL) {
        return NULL;
    }
    CHIMP_SYMTABLE_ENTRY_INIT(ref);
    CHIMP_SYMTABLE_ENTRY(ref)->flags = flags;
    CHIMP_SYMTABLE_ENTRY(ref)->symtable = symtable;
    CHIMP_SYMTABLE_ENTRY(ref)->scope = scope;
    CHIMP_SYMTABLE_ENTRY(ref)->parent = parent;
    CHIMP_SYMTABLE_ENTRY(ref)->symbols = chimp_hash_new ();
    if (CHIMP_SYMTABLE_ENTRY(ref)->symbols == NULL) {
        return NULL;
    }
    CHIMP_SYMTABLE_ENTRY(ref)->children = chimp_array_new ();
    if (CHIMP_SYMTABLE_ENTRY(ref)->children == NULL) {
        return NULL;
    }
    return ref;
}

static chimp_bool_t
chimp_symtable_enter_scope (ChimpRef *self, ChimpRef *scope, int flags)
{
    ChimpRef *ste = CHIMP_SYMTABLE_GET_CURRENT_ENTRY(self);
    ChimpRef *new_ste = chimp_symtable_entry_new (self, ste, scope, flags);
    if (new_ste == NULL) {
        return CHIMP_FALSE;
    }
    if (!chimp_hash_put (CHIMP_SYMTABLE(self)->lookup, scope, new_ste)) {
        return CHIMP_FALSE;
    }
    if (ste != NULL) {
        if (!chimp_array_push (CHIMP_SYMTABLE(self)->stack, ste)) {
            return CHIMP_FALSE;
        }
        if (!chimp_array_push (CHIMP_SYMTABLE_ENTRY_CHILDREN(ste), new_ste)) {
            return CHIMP_FALSE;
        }
    }
    CHIMP_SYMTABLE(self)->ste = new_ste;
    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_symtable_leave_scope (ChimpRef *self)
{
    ChimpRef *stack = CHIMP_SYMTABLE(self)->stack;
    if (CHIMP_ARRAY_SIZE(stack) > 0) {
        CHIMP_SYMTABLE(self)->ste = chimp_array_pop (stack);
    }
    return CHIMP_SYMTABLE_GET_CURRENT_ENTRY(self) != NULL;
}

static chimp_bool_t
chimp_symtable_add (ChimpRef *self, ChimpRef *name, int64_t flags)
{
    ChimpRef *ste = CHIMP_SYMTABLE_GET_CURRENT_ENTRY(self);
    ChimpRef *symbols = CHIMP_SYMTABLE_ENTRY(ste)->symbols;
    ChimpRef *fl = chimp_int_new (flags);
    if (fl == NULL) {
        return CHIMP_FALSE;
    }
    if (!chimp_hash_put (symbols, name, fl)) {
        return CHIMP_FALSE;
    }
    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_symtable_visit_stmts_or_decls (ChimpRef *self, ChimpRef *arr)
{
    size_t i;

    for (i = 0; i < CHIMP_ARRAY_SIZE(arr); i++) {
        ChimpRef *item = CHIMP_ARRAY_ITEM(arr, i);
        if (CHIMP_ANY_TYPE(item) == CHIMP_VALUE_TYPE_AST_STMT) {
            if (!chimp_symtable_visit_stmt (self, item)) {
                return CHIMP_FALSE;
            }
        }
        else if (CHIMP_ANY_TYPE(item) == CHIMP_VALUE_TYPE_AST_DECL) {
            if (!chimp_symtable_visit_decl (self, item)) {
                return CHIMP_FALSE;
            }
        }
        else {
            CHIMP_BUG ("dude");
            return CHIMP_FALSE;
        }
    }
    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_symtable_visit_decl_func (ChimpRef *self, ChimpRef *decl)
{
    ChimpRef *name = CHIMP_AST_DECL(decl)->func.name;
    ChimpRef *args = CHIMP_AST_DECL(decl)->func.args;
    ChimpRef *body = CHIMP_AST_DECL(decl)->func.body;
    ChimpRef *ste = CHIMP_SYMTABLE_GET_CURRENT_ENTRY(self);
    int type = CHIMP_SYMTABLE_ENTRY(ste)->flags & CHIMP_SYM_TYPE_MASK;

    if (!chimp_symtable_add (self, name, CHIMP_SYM_DECL | type)) {
        return CHIMP_FALSE;
    }

    if (!chimp_symtable_enter_scope (self, decl, CHIMP_SYM_FUNC)) {
        return CHIMP_FALSE;
    }

    if (!chimp_symtable_visit_decls (self, args)) {
        return CHIMP_FALSE;
    }

    if (!chimp_symtable_visit_stmts_or_decls (self, body)) {
        return CHIMP_FALSE;
    }

    if (!chimp_symtable_leave_scope (self)) {
        return CHIMP_FALSE;
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_symtable_visit_decl_class (ChimpRef *self, ChimpRef *decl)
{
    ChimpRef *name = CHIMP_AST_DECL(decl)->class.name;
    ChimpRef *base = CHIMP_AST_DECL(decl)->class.base;
    ChimpRef *body = CHIMP_AST_DECL(decl)->class.body;
    ChimpRef *ste = CHIMP_SYMTABLE_GET_CURRENT_ENTRY(self);
    int type = CHIMP_SYMTABLE_ENTRY(ste)->flags & CHIMP_SYM_TYPE_MASK;

    if (!chimp_symtable_add (self, name, CHIMP_SYM_DECL | type)) {
        return CHIMP_FALSE;
    }

    if (!chimp_symtable_enter_scope (self, decl, CHIMP_SYM_CLASS)) {
        return CHIMP_FALSE;
    }

    if (base != NULL) {
        /* XXX this sucks. */
        if (!chimp_symtable_add (self, chimp_array_first (base), 0)) {
            return CHIMP_FALSE;
        }
    }

    if (!chimp_symtable_visit_decls (self, body)) {
        return CHIMP_FALSE;
    }

    if (!chimp_symtable_leave_scope (self)) {
        return CHIMP_FALSE;
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_symtable_visit_decl_use (ChimpRef *self, ChimpRef *decl)
{
    ChimpRef *name = CHIMP_AST_DECL(decl)->use.name;
    ChimpRef *ste = CHIMP_SYMTABLE_GET_CURRENT_ENTRY(self);
    int type = CHIMP_SYMTABLE_ENTRY(ste)->flags & CHIMP_SYM_TYPE_MASK;

    if (!chimp_symtable_add (self, name, CHIMP_SYM_DECL | type)) {
        return CHIMP_FALSE;
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_symtable_visit_decl_var (ChimpRef *self, ChimpRef *decl)
{
    ChimpRef *name = CHIMP_AST_DECL(decl)->var.name;
    ChimpRef *value = CHIMP_AST_DECL(decl)->var.value;
    ChimpRef *ste = CHIMP_SYMTABLE_GET_CURRENT_ENTRY(self);
    int type = CHIMP_SYMTABLE_ENTRY(ste)->flags & CHIMP_SYM_TYPE_MASK;

    if (!chimp_symtable_add (self, name, CHIMP_SYM_DECL | type)) {
        return CHIMP_FALSE;
    }

    if (value != NULL) {
        if (!chimp_symtable_visit_expr (self, value)) {
            return CHIMP_FALSE;
        }
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_symtable_visit_decl (ChimpRef *self, ChimpRef *decl)
{
    switch (CHIMP_AST_DECL_TYPE(decl)) {
        case CHIMP_AST_DECL_FUNC:
            return chimp_symtable_visit_decl_func (self, decl);
        case CHIMP_AST_DECL_CLASS:
            return chimp_symtable_visit_decl_class (self, decl);
        case CHIMP_AST_DECL_USE:
            return chimp_symtable_visit_decl_use (self, decl);
        case CHIMP_AST_DECL_VAR:
            return chimp_symtable_visit_decl_var (self, decl);
        default:
            CHIMP_BUG ("unknown AST decl type: %d", CHIMP_AST_DECL_TYPE(decl));
            return CHIMP_FALSE;
    }
}

static chimp_bool_t
chimp_symtable_visit_expr_call (ChimpRef *self, ChimpRef *expr)
{
    ChimpRef *target = CHIMP_AST_EXPR(expr)->call.target;
    ChimpRef *args = CHIMP_AST_EXPR(expr)->call.args;
    size_t i;

    if (!chimp_symtable_visit_expr (self, target)) {
        return CHIMP_FALSE;
    }

    for (i = 0; i < CHIMP_ARRAY_SIZE(args); i++) {
        ChimpRef *item = CHIMP_ARRAY_ITEM(args, i);
        if (!chimp_symtable_visit_expr (self, item)) {
            return CHIMP_FALSE;
        }
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_symtable_visit_expr_getattr (ChimpRef *self, ChimpRef *expr)
{
    ChimpRef *target = CHIMP_AST_EXPR(expr)->getattr.target;

    if (!chimp_symtable_visit_expr (self, target)) {
        return CHIMP_FALSE;
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_symtable_visit_expr_getitem (ChimpRef *self, ChimpRef *expr)
{
    ChimpRef *target = CHIMP_AST_EXPR(expr)->getitem.target;

    if (!chimp_symtable_visit_expr (self, target)) {
        return CHIMP_FALSE;
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_symtable_visit_expr_array (ChimpRef *self, ChimpRef *expr)
{
    size_t i;
    ChimpRef *arr = CHIMP_AST_EXPR(expr)->array.value;
    for (i = 0; i < CHIMP_ARRAY_SIZE(arr); i++) {
        ChimpRef *item = CHIMP_ARRAY_ITEM(arr, i);
        if (!chimp_symtable_visit_expr (self, item)) {
            return CHIMP_FALSE;
        }
    }
    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_symtable_visit_expr_hash (ChimpRef *self, ChimpRef *expr)
{
    size_t i;
    ChimpRef *arr = CHIMP_AST_EXPR(expr)->hash.value;
    for (i = 0; i < CHIMP_ARRAY_SIZE(arr); i++) {
        ChimpRef *item = CHIMP_ARRAY_ITEM(arr, i);
        if (!chimp_symtable_visit_expr (self, item)) {
            return CHIMP_FALSE;
        }
    }
    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_symtable_visit_expr_ident (ChimpRef *self, ChimpRef *expr)
{
    ChimpRef *ste = CHIMP_SYMTABLE_GET_CURRENT_ENTRY(self);
    ChimpRef *name = CHIMP_AST_EXPR(expr)->ident.id;

    while (ste != NULL) {
        int rc;
        rc = chimp_hash_get (CHIMP_SYMTABLE_ENTRY(ste)->symbols, name, NULL);
        if (rc == 0) {
            int type = CHIMP_SYMTABLE_ENTRY(ste)->flags &
                        CHIMP_SYM_TYPE_MASK;
            if (ste != CHIMP_SYMTABLE_GET_CURRENT_ENTRY(self)) {
                return chimp_symtable_add (self, name, CHIMP_SYM_FREE | type);
            }
            return CHIMP_TRUE;
        }
        ste = CHIMP_SYMTABLE_ENTRY(ste)->parent;
    }

    /* XXX is this really the best way to handle builtins? */
    if (chimp_is_builtin (name)) {
        return chimp_symtable_add (self, name, CHIMP_SYM_BUILTIN);
    }

    CHIMP_BUG ("unknown symbol: %s", CHIMP_STR_DATA(name));
    return CHIMP_FALSE;
}

static chimp_bool_t
chimp_symtable_visit_expr_binop (ChimpRef *self, ChimpRef *expr)
{
    ChimpRef *left = CHIMP_AST_EXPR(expr)->binop.left;
    ChimpRef *right = CHIMP_AST_EXPR(expr)->binop.right;
    if (!chimp_symtable_visit_expr (self, left)) {
        return CHIMP_FALSE;
    }
    if (!chimp_symtable_visit_expr (self, right)) {
        return CHIMP_FALSE;
    }
    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_symtable_visit_expr_fn (ChimpRef *self, ChimpRef *expr)
{
    ChimpRef *args = CHIMP_AST_EXPR(expr)->fn.args;
    ChimpRef *body = CHIMP_AST_EXPR(expr)->fn.body;

    if (!chimp_symtable_enter_scope (self, expr, CHIMP_SYM_FUNC)) {
        return CHIMP_FALSE;
    }

    if (!chimp_symtable_visit_decls (self, args)) {
        return CHIMP_FALSE;
    }

    if (!chimp_symtable_visit_stmts_or_decls (self, body)) {
        return CHIMP_FALSE;
    }

    if (!chimp_symtable_leave_scope (self)) {
        return CHIMP_FALSE;
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_symtable_visit_expr_not (ChimpRef *self, ChimpRef *expr)
{
    return chimp_symtable_visit_expr (self, CHIMP_AST_EXPR(expr)->not.value);
}

static chimp_bool_t
chimp_symtable_visit_expr (ChimpRef *self, ChimpRef *expr)
{
    switch (CHIMP_AST_EXPR_TYPE(expr)) {
        case CHIMP_AST_EXPR_CALL:
            return chimp_symtable_visit_expr_call (self, expr);
        case CHIMP_AST_EXPR_GETATTR:
            return chimp_symtable_visit_expr_getattr (self, expr);
        case CHIMP_AST_EXPR_GETITEM:
            return chimp_symtable_visit_expr_getitem (self, expr);
        case CHIMP_AST_EXPR_ARRAY:
            return chimp_symtable_visit_expr_array (self, expr);
        case CHIMP_AST_EXPR_HASH:
            return chimp_symtable_visit_expr_hash (self, expr);
        case CHIMP_AST_EXPR_IDENT:
            return chimp_symtable_visit_expr_ident (self, expr);
        case CHIMP_AST_EXPR_STR:
            return CHIMP_TRUE;
        case CHIMP_AST_EXPR_BOOL:
            return CHIMP_TRUE;
        case CHIMP_AST_EXPR_NIL:
            return CHIMP_TRUE;
        case CHIMP_AST_EXPR_BINOP:
            return chimp_symtable_visit_expr_binop (self, expr);
        case CHIMP_AST_EXPR_INT_:
            return CHIMP_TRUE;
        case CHIMP_AST_EXPR_FN:
            return chimp_symtable_visit_expr_fn (self, expr);
        case CHIMP_AST_EXPR_NOT:
            return chimp_symtable_visit_expr_not (self, expr);
        default:
            CHIMP_BUG ("unknown AST expr type: %d", CHIMP_AST_EXPR_TYPE(expr));
            return CHIMP_FALSE;
    }
}

static chimp_bool_t
chimp_symtable_visit_stmt_assign (ChimpRef *self, ChimpRef *stmt)
{
    ChimpRef *target = CHIMP_AST_STMT(stmt)->assign.target;
    ChimpRef *value = CHIMP_AST_STMT(stmt)->assign.value;

    if (!chimp_symtable_visit_expr (self, target)) {
        return CHIMP_FALSE;
    }

    if (!chimp_symtable_visit_expr (self, value)) {
        return CHIMP_FALSE;
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_symtable_visit_stmt_if_ (ChimpRef *self, ChimpRef *stmt)
{
    ChimpRef *expr = CHIMP_AST_STMT(stmt)->if_.expr;
    ChimpRef *body = CHIMP_AST_STMT(stmt)->if_.body;
    ChimpRef *orelse = CHIMP_AST_STMT(stmt)->if_.orelse;

    if (!chimp_symtable_visit_expr (self, expr)) {
        return CHIMP_FALSE;
    }

    if (!chimp_symtable_visit_stmts_or_decls (self, body)) {
        return CHIMP_FALSE;
    }

    if (orelse != NULL) {
        if (!chimp_symtable_visit_stmts_or_decls (self, orelse)) {
            return CHIMP_FALSE;
        }
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_symtable_visit_stmt_while_ (ChimpRef *self, ChimpRef *stmt)
{
    ChimpRef *expr = CHIMP_AST_STMT(stmt)->while_.expr;
    ChimpRef *body = CHIMP_AST_STMT(stmt)->while_.body;

    if (!chimp_symtable_visit_expr (self, expr)) {
        return CHIMP_FALSE;
    }

    if (!chimp_symtable_visit_stmts_or_decls (self, body)) {
        return CHIMP_FALSE;
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_symtable_visit_stmt_pattern_test (ChimpRef *self, ChimpRef *test)
{
    switch (CHIMP_AST_EXPR(test)->type) {
        case CHIMP_AST_EXPR_IDENT:
            {
                ChimpRef *id = CHIMP_AST_EXPR(test)->ident.id;
                ChimpRef *ste = CHIMP_SYMTABLE_GET_CURRENT_ENTRY(self);
                int type = CHIMP_SYMTABLE_ENTRY(ste)->flags &
                            CHIMP_SYM_TYPE_MASK;
                if (!chimp_symtable_add (self, id, CHIMP_SYM_DECL | type)) {
                    return CHIMP_FALSE;
                }

                break;
            }
        case CHIMP_AST_EXPR_ARRAY:
            {
                ChimpRef *array = CHIMP_AST_EXPR(test)->array.value;
                size_t i;
                for (i = 0; i < CHIMP_ARRAY_SIZE(array); i++) {
                    ChimpRef *item = CHIMP_ARRAY_ITEM(array, i);
                    if (!chimp_symtable_visit_stmt_pattern_test (self, item)) {
                        return CHIMP_FALSE;
                    }
                }

                break;
            }
        case CHIMP_AST_EXPR_HASH:
            {
                ChimpRef *hash = CHIMP_AST_EXPR(test)->hash.value;
                size_t i;
                /* XXX hashes are encoded as arrays in the AST */
                for (i = 0; i < CHIMP_ARRAY_SIZE(hash); i += 2) {
                    ChimpRef *key = CHIMP_ARRAY(hash)->items[i];
                    ChimpRef *value = CHIMP_ARRAY(hash)->items[i+1];
                    if (!chimp_symtable_visit_stmt_pattern_test (self, key)) {
                        return CHIMP_FALSE;
                    }
                    if (!chimp_symtable_visit_stmt_pattern_test (self, value)) {
                        return CHIMP_FALSE;
                    }
                }
            }
        default:
            break;
    };

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_symtable_visit_stmt_patterns (ChimpRef *self, ChimpRef *patterns)
{
    size_t size = CHIMP_ARRAY_SIZE(patterns);
    size_t i;

    for (i = 0; i < size; i++) {
        ChimpRef *pattern = CHIMP_ARRAY_ITEM(patterns, i);
        ChimpRef *test = CHIMP_AST_STMT(pattern)->pattern.test;
        ChimpRef *body = CHIMP_AST_STMT(pattern)->pattern.body;

        if (!chimp_symtable_visit_stmt_pattern_test (self, test)) {
            return CHIMP_FALSE;
        }

        if (!chimp_symtable_visit_stmts_or_decls (self, body)) {
            return CHIMP_FALSE;
        }
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_symtable_visit_stmt_match (ChimpRef *self, ChimpRef *stmt)
{
    ChimpRef *expr = CHIMP_AST_STMT(stmt)->match.expr;
    ChimpRef *body = CHIMP_AST_STMT(stmt)->match.body;
    
    if (!chimp_symtable_visit_expr (self, expr)) {
        return CHIMP_FALSE;
    }

    if (!chimp_symtable_visit_stmt_patterns (self, body)) {
        return CHIMP_FALSE;
    }

    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_symtable_visit_stmt_ret (ChimpRef *self, ChimpRef *stmt)
{
    ChimpRef *expr = CHIMP_AST_STMT(stmt)->ret.expr;
    if (expr != NULL) {
        if (!chimp_symtable_visit_expr (self, expr)) {
            return CHIMP_FALSE;
        }
    }
    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_symtable_visit_stmt (ChimpRef *self, ChimpRef *stmt)
{
    switch (CHIMP_AST_STMT_TYPE(stmt)) {
        case CHIMP_AST_STMT_EXPR:
            return chimp_symtable_visit_expr (self, CHIMP_AST_STMT(stmt)->expr.expr);
        case CHIMP_AST_STMT_ASSIGN:
            return chimp_symtable_visit_stmt_assign (self, stmt);
        case CHIMP_AST_STMT_IF_:
            return chimp_symtable_visit_stmt_if_ (self, stmt);
        case CHIMP_AST_STMT_WHILE_:
            return chimp_symtable_visit_stmt_while_ (self, stmt);
        case CHIMP_AST_STMT_MATCH:
            return chimp_symtable_visit_stmt_match (self, stmt);
        case CHIMP_AST_STMT_RET:
            return chimp_symtable_visit_stmt_ret (self, stmt);
        case CHIMP_AST_STMT_BREAK_:
            return CHIMP_TRUE;
        default:
            CHIMP_BUG ("unknown AST stmt type: %d", CHIMP_AST_STMT_TYPE(stmt));
            return CHIMP_FALSE;
    }
}

static chimp_bool_t
chimp_symtable_visit_decls (ChimpRef *self, ChimpRef *decls)
{
    size_t i;

    for (i = 0; i < CHIMP_ARRAY_SIZE(decls); i++) {
        if (!chimp_symtable_visit_decl (self, CHIMP_ARRAY_ITEM(decls, i))) {
            /* TODO error message? */
            return CHIMP_FALSE;
        }
    }
    return CHIMP_TRUE;
}

static chimp_bool_t
chimp_symtable_visit_mod (ChimpRef *self, ChimpRef *mod)
{
    ChimpRef *uses = CHIMP_AST_MOD(mod)->root.uses;
    ChimpRef *body = CHIMP_AST_MOD(mod)->root.body;

    if (!chimp_symtable_enter_scope (self, mod, CHIMP_SYM_MODULE)) {
        return CHIMP_FALSE;
    }

    if (!chimp_symtable_visit_decls (self, uses)) {
        return CHIMP_FALSE;
    }

    if (!chimp_symtable_visit_decls (self, body)) {
        return CHIMP_FALSE;
    }

    if (!chimp_symtable_leave_scope (self)) {
        return CHIMP_FALSE;
    }

    return CHIMP_TRUE;
}

ChimpRef *
chimp_symtable_new_from_ast (ChimpRef *filename, ChimpRef *ast)
{
    ChimpRef *ref = chimp_gc_new_object (NULL);
    if (ref == NULL) {
        return NULL;
    }
    CHIMP_SYMTABLE_INIT(ref);
    CHIMP_SYMTABLE(ref)->filename = filename;
    CHIMP_SYMTABLE(ref)->lookup = chimp_hash_new ();
    if (CHIMP_SYMTABLE(ref)->lookup == NULL) {
        return NULL;
    }
    CHIMP_SYMTABLE(ref)->stack = chimp_array_new ();
    if (CHIMP_SYMTABLE(ref)->stack == NULL) {
        return NULL;
    }

    switch (CHIMP_ANY_TYPE(ast)) {
        case CHIMP_VALUE_TYPE_AST_MOD:
            if (!chimp_symtable_visit_mod (ref, ast))
                return NULL;
            break;
        default:
            CHIMP_BUG ("unknown top-level AST node type: %d",
                        CHIMP_ANY_TYPE(ast));
            return NULL;
    }

    return ref;
}

ChimpRef *
chimp_symtable_lookup (ChimpRef *self, ChimpRef *scope)
{
    ChimpRef *sym;
    int rc = chimp_hash_get (CHIMP_SYMTABLE(self)->lookup, scope, &sym);
    if (rc < 0) {
        CHIMP_BUG ("Error looking up symtable entry for scope %p", scope);
        return NULL;
    }
    else if (rc > 0) {
        CHIMP_BUG ("No symtable entry for scope %p", scope);
        return NULL;
    }
    return sym;
}

chimp_bool_t
chimp_symtable_entry_sym_flags (
    ChimpRef *self, ChimpRef *name, int64_t *flags)
{
    ChimpRef *ste = self;
    while (ste != NULL) {
        ChimpRef *symbols = CHIMP_SYMTABLE_ENTRY(ste)->symbols;
        ChimpRef *ref;
        int rc;
        
        rc = chimp_hash_get (symbols, name, &ref);
        if (rc == 0) {
            if (flags != NULL) {
                *flags = CHIMP_INT(ref)->value;
            }
            return CHIMP_TRUE;
        }
        ste = CHIMP_SYMTABLE_ENTRY(ste)->parent;
    }
    return CHIMP_FALSE;
}

chimp_bool_t
chimp_symtable_entry_sym_exists (ChimpRef *self, ChimpRef *name)
{
    return chimp_symtable_entry_sym_flags (self, name, NULL);
}

