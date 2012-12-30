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

#include "chimp/core.h"
#include "chimp/gc.h"
#include "chimp/object.h"
#include "chimp/lwhash.h"
#include "chimp/class.h"
#include "chimp/str.h"
#include "chimp/int.h"
#include "chimp/array.h"
#include "chimp/method.h"
#include "chimp/task.h"
#include "chimp/hash.h"
#include "chimp/frame.h"
#include "chimp/ast.h"
#include "chimp/code.h"
#include "chimp/module.h"
#include "chimp/modules.h"
#include "chimp/symtable.h"

#define CHIMP_BOOTSTRAP_CLASS_L1(gc, c, n, sup) \
    do { \
        CHIMP_ANY(c)->type = CHIMP_VALUE_TYPE_CLASS; \
        CHIMP_ANY(c)->klass = chimp_class_class; \
        CHIMP_CLASS(c)->super = (sup); \
        CHIMP_CLASS(c)->name = chimp_gc_new_object ((gc)); \
        CHIMP_ANY(CHIMP_CLASS(c)->name)->type = CHIMP_VALUE_TYPE_STR; \
        CHIMP_ANY(CHIMP_CLASS(c)->name)->klass = chimp_str_class; \
        CHIMP_STR(CHIMP_CLASS(c)->name)->data = fake_strndup ((n), (sizeof(n)-1)); \
        if (CHIMP_STR(CHIMP_CLASS(c)->name)->data == NULL) { \
            chimp_task_main_delete (); \
            main_task = NULL; \
            return CHIMP_FALSE; \
        } \
        CHIMP_STR(CHIMP_CLASS(c)->name)->size = sizeof(n)-1; \
        chimp_gc_make_root ((gc), (c)); \
    } while (0)

#define CHIMP_BOOTSTRAP_CLASS_L2(gc, c) \
    do { \
    } while (0)

#define CHIMP_BUILTIN_METHOD(method, method_name) \
    do { \
      ChimpRef* temp = chimp_method_new_native (NULL, method); \
      if (temp == NULL) { \
          return CHIMP_FALSE; \
      } \
      chimp_hash_put_str (chimp_builtins, method_name, temp); \
    } while (0)

ChimpRef *chimp_object_class = NULL;
ChimpRef *chimp_class_class = NULL;
ChimpRef *chimp_str_class = NULL;
ChimpRef *chimp_nil_class = NULL;
ChimpRef *chimp_bool_class = NULL;

ChimpRef *chimp_nil = NULL;
ChimpRef *chimp_true = NULL;
ChimpRef *chimp_false = NULL;
ChimpRef *chimp_builtins = NULL;

/* XXX clean me up -- strndup is a GNU extension */
static char *
fake_strndup (const char *s, size_t len)
{
    char *buf = malloc (len + 1);
    if (buf == NULL) {
        return NULL;
    }
    memcpy (buf, s, len);
    buf[len] = '\0';
    return buf;
}

static ChimpCmpResult
chimp_str_cmp (ChimpRef *a, ChimpRef *b)
{
    ChimpStr *as;
    ChimpStr *bs;
    ChimpValueType at, bt;
    int r;

    if (a == b) {
        return CHIMP_CMP_EQ;
    }

    at = CHIMP_REF_TYPE(a);
    bt = CHIMP_REF_TYPE(b);

    if (at != CHIMP_VALUE_TYPE_STR) {
        /* TODO this is almost certainly a bug */
        return CHIMP_CMP_ERROR;
    }

    /* TODO should probably be a subtype check? */
    if (at != bt) {
        return CHIMP_CMP_NOT_IMPL;
    }

    as = CHIMP_STR(a);
    bs = CHIMP_STR(b);

    if (as->size != bs->size) {
        r = strcmp (as->data, bs->data);
        if (r < 0) {
            return CHIMP_CMP_LT;
        }
        else if (r > 0) {
            return CHIMP_CMP_GT;
        }
        else {
            return CHIMP_CMP_EQ;
        }
    }

    r = memcmp (CHIMP_STR(a)->data, CHIMP_STR(b)->data, as->size);
    if (r < 0) {
        return CHIMP_CMP_LT;
    }
    else if (r > 0) {
        return CHIMP_CMP_GT;
    }
    else {
        return CHIMP_CMP_EQ;
    }
}

static ChimpRef *
_chimp_object_str (ChimpRef *self)
{
    char buf[32];
    ChimpRef *name = CHIMP_CLASS_NAME(CHIMP_ANY_CLASS(self));
    ChimpRef *str;
    
    snprintf (buf, sizeof(buf), " @ %p>", self);
    str = chimp_str_new_concat ("<", CHIMP_STR_DATA(name), buf, NULL);
    if (str == NULL) {
        return NULL;
    }
    return str;
}

static ChimpRef *
chimp_bool_str (ChimpRef *self)
{
    if (self == chimp_true) {
        return CHIMP_STR_NEW("true");
    }
    else if (self == chimp_false) {
        return CHIMP_STR_NEW("false");
    }
    else {
        return NULL;
    }
}

static ChimpRef *
chimp_str_str (ChimpRef *self)
{
    return self;
}

static ChimpRef *
_chimp_object_getattr (ChimpRef *self, ChimpRef *name)
{
    /* TODO check CHIMP_ANY(self)->attributes ? */
    ChimpRef *class = CHIMP_ANY_CLASS(self);
    while (class != NULL) {
        ChimpLWHash *methods = CHIMP_CLASS(class)->methods;
        ChimpRef *method;
        if (chimp_lwhash_get (methods, name, &method)) {
            if (method == NULL) {
                class = CHIMP_CLASS(class)->super;
                continue;
            }
            /* XXX binding on every access is probably dumb/slow */
            return chimp_method_new_bound (method, self);
        }
        else break;
    }
    return NULL;
}

static ChimpRef *
chimp_class_getattr (ChimpRef *self, ChimpRef *name)
{
    if (strcmp (CHIMP_STR_DATA(name), "name") == 0) {
        return CHIMP_CLASS(self)->name;
    }
    else if (strcmp (CHIMP_STR_DATA(name), "super") == 0) {
        if (CHIMP_CLASS(self)->super == NULL) {
            return chimp_nil;
        }
        else {
            return CHIMP_CLASS(self)->super;
        }
    }
    else {
        while (self != NULL) {
            ChimpLWHash *methods = CHIMP_CLASS(self)->methods;
            ChimpRef *method;
            if (chimp_lwhash_get (methods, name, &method)) {
                if (method == NULL) {
                    self = CHIMP_CLASS(self)->super;
                    continue;
                }
                return method;
            }
            else break;
        }
        return NULL;
    }
}

static ChimpRef *
chimp_nil_str (ChimpRef *self)
{
    return CHIMP_CLASS_NAME(CHIMP_ANY_CLASS(self));
}

static ChimpRef *
_chimp_task_recv (ChimpRef *self, ChimpRef *args)
{
    return chimp_task_recv (chimp_task_get_self (CHIMP_CURRENT_TASK));
}

static ChimpRef *
_chimp_task_self (ChimpRef *self, ChimpRef *args)
{
    return chimp_task_get_self (CHIMP_CURRENT_TASK);
}

static ChimpRef *
_chimp_task_spawn (ChimpRef *self, ChimpRef *args)
{
    ChimpRef *method = CHIMP_ARRAY_ITEM(args, 0);
    if (CHIMP_METHOD(method)->flags & CHIMP_METHOD_FLAG_FREEVARS) {
        CHIMP_BUG("spawn() cannot be called on a closure that uses freevars");
        return NULL;
    }
    /* TODO ensure method is not an instance method ... */
    return chimp_task_new (method);
}

static ChimpTaskInternal *main_task = NULL;

static chimp_bool_t
chimp_core_init_builtins (void)
{
    chimp_builtins = chimp_hash_new ();
    if (chimp_builtins == NULL) {
        return CHIMP_FALSE;
    }
    chimp_gc_make_root (NULL, chimp_builtins);

    /* TODO error checking */

    chimp_hash_put_str (chimp_builtins, "hash",   chimp_hash_class);
    chimp_hash_put_str (chimp_builtins, "array",  chimp_array_class);
    chimp_hash_put_str (chimp_builtins, "int",    chimp_int_class);
    chimp_hash_put_str (chimp_builtins, "str",    chimp_str_class);
    chimp_hash_put_str (chimp_builtins, "bool",   chimp_bool_class);
    chimp_hash_put_str (chimp_builtins, "module", chimp_module_class);
    chimp_hash_put_str (chimp_builtins, "object", chimp_object_class);
    chimp_hash_put_str (chimp_builtins, "class",  chimp_class_class);
    chimp_hash_put_str (chimp_builtins, "method", chimp_method_class);

    CHIMP_BUILTIN_METHOD(_chimp_task_recv, "recv");
    CHIMP_BUILTIN_METHOD(_chimp_task_self, "self");
    CHIMP_BUILTIN_METHOD(_chimp_task_spawn, "spawn");

    return CHIMP_TRUE;
}

chimp_bool_t
chimp_core_startup (void *stack_start)
{
    main_task = chimp_task_new_main (stack_start);
    if (main_task == NULL) {
        return CHIMP_FALSE;
    }

    chimp_object_class = chimp_gc_new_object (NULL);
    chimp_class_class  = chimp_gc_new_object (NULL);
    chimp_str_class    = chimp_gc_new_object (NULL);

    CHIMP_BOOTSTRAP_CLASS_L1(NULL, chimp_object_class, "object", NULL);
    CHIMP_CLASS(chimp_object_class)->str = _chimp_object_str;
    CHIMP_CLASS(chimp_object_class)->getattr = _chimp_object_getattr;
    CHIMP_BOOTSTRAP_CLASS_L1(NULL, chimp_class_class, "class", chimp_object_class);
    CHIMP_CLASS(chimp_class_class)->getattr = chimp_class_getattr;
    CHIMP_BOOTSTRAP_CLASS_L1(NULL, chimp_str_class, "str", chimp_object_class);
    CHIMP_CLASS(chimp_str_class)->cmp = chimp_str_cmp;
    CHIMP_CLASS(chimp_str_class)->str = chimp_str_str;

    CHIMP_BOOTSTRAP_CLASS_L2(NULL, chimp_object_class);
    CHIMP_BOOTSTRAP_CLASS_L2(NULL, chimp_class_class);
    CHIMP_BOOTSTRAP_CLASS_L2(NULL, chimp_str_class);

    if (!chimp_method_class_bootstrap ()) goto error;

    if (!_chimp_bootstrap_L3 ()) {
        return CHIMP_FALSE;
    }

    if (!chimp_int_class_bootstrap ()) goto error;
    if (!chimp_array_class_bootstrap ()) goto error;
    if (!chimp_hash_class_bootstrap ()) goto error;

    chimp_nil_class = chimp_class_new (CHIMP_STR_NEW("nil"), chimp_object_class);
    if (chimp_nil_class == NULL) goto error;
    chimp_gc_make_root (NULL, chimp_nil_class);
    CHIMP_CLASS(chimp_nil_class)->str = chimp_nil_str;
    chimp_nil = chimp_class_new_instance (chimp_nil_class);
    if (chimp_nil == NULL) goto error;
    chimp_gc_make_root (NULL, chimp_nil);

    chimp_bool_class = chimp_class_new (CHIMP_STR_NEW("bool"), chimp_object_class);
    if (chimp_bool_class == NULL) goto error;
    chimp_gc_make_root (NULL, chimp_bool_class);
    CHIMP_CLASS(chimp_bool_class)->str = chimp_bool_str;

    chimp_true = chimp_class_new_instance (chimp_bool_class, NULL);
    if (chimp_true == NULL) goto error;
    chimp_gc_make_root (NULL, chimp_true);
    chimp_false = chimp_class_new_instance (chimp_bool_class, NULL);
    if (chimp_false == NULL) goto error;
    chimp_gc_make_root (NULL, chimp_false);

    if (!chimp_frame_class_bootstrap ()) goto error;
    if (!chimp_code_class_bootstrap ()) goto error;
    if (!chimp_module_class_bootstrap ()) goto error;
    if (!chimp_symtable_class_bootstrap ()) goto error;
    if (!chimp_symtable_entry_class_bootstrap ()) goto error;
    if (!chimp_task_class_bootstrap ()) goto error;
    if (!chimp_var_class_bootstrap ()) goto error;

    /*
    if (!chimp_task_push_frame (main_task)) goto error;
    */
    if (!chimp_ast_class_bootstrap ()) goto error;

    chimp_task_add_module (NULL, chimp_init_io_module ());
    chimp_task_add_module (NULL, chimp_init_assert_module ());
    chimp_task_add_module (NULL, chimp_init_os_module ());
    chimp_task_add_module (NULL, chimp_init_gc_module ());

    if (!chimp_core_init_builtins ()) goto error;

    if (!chimp_task_main_ready ()) goto error;

    return CHIMP_TRUE;

error:

    chimp_task_main_delete ();
    main_task = NULL;
    return CHIMP_FALSE;
}

void
chimp_core_shutdown (void)
{
    if (main_task != NULL) {
        chimp_task_main_delete ();
        main_task = NULL;
        chimp_object_class = NULL;
        chimp_class_class = NULL;
        chimp_str_class = NULL;
    }
}

void
chimp_bug (const char *filename, int lineno, const char *format, ...)
{
    va_list args;

    fprintf (stderr, "bug: %s:%d: ", filename, lineno);

    va_start (args, format);
    vfprintf (stderr, format, args);
    va_end (args);

    fprintf (stderr, ". aborting ...\n");

    abort ();
}

chimp_bool_t
chimp_is_builtin (ChimpRef *name)
{
    return chimp_get_builtin (name, NULL);
}

chimp_bool_t
chimp_get_builtin (struct _ChimpRef *name, ChimpRef **value)
{
    int rc;
    
    rc = chimp_hash_get (chimp_builtins, name, value);
    if (rc < 0) {
        CHIMP_BUG ("could not determine if %s is a builtin",
                    CHIMP_STR_DATA(name));
        return CHIMP_FALSE;
    }
    else if (rc > 0) {
        return CHIMP_FALSE;
    }
    else {
        return CHIMP_TRUE;
    }
}

