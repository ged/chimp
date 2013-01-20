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

#include <ctype.h>

// XXX - Windows compatibility?
#include <unistd.h>

#include "chimp/core.h"
#include "chimp/gc.h"
#include "chimp/object.h"
#include "chimp/lwhash.h"
#include "chimp/class.h"
#include "chimp/str.h"
#include "chimp/int.h"
#include "chimp/float.h"
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
        chimp_gc_make_root ((gc), (c)); \
        CHIMP_ANY(c)->klass = chimp_class_class; \
        CHIMP_CLASS(c)->super = (sup); \
        CHIMP_CLASS(c)->name = chimp_gc_new_object ((gc)); \
        CHIMP_ANY(CHIMP_CLASS(c)->name)->klass = chimp_str_class; \
        CHIMP_STR(CHIMP_CLASS(c)->name)->data = fake_strndup ((n), (sizeof(n)-1)); \
        if (CHIMP_STR(CHIMP_CLASS(c)->name)->data == NULL) { \
            chimp_task_main_delete (); \
            main_task = NULL; \
            return CHIMP_FALSE; \
        } \
        CHIMP_STR(CHIMP_CLASS(c)->name)->size = sizeof(n)-1; \
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
ChimpRef *chimp_module_path = NULL;

static ChimpRef *
_chimp_module_make_path (const char *path);

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
    int r;

    if (a == b) {
        return CHIMP_CMP_EQ;
    }

    if (CHIMP_ANY_CLASS(a) != chimp_str_class) {
        return CHIMP_CMP_LT;
    }

    /* TODO should probably be a subtype check? */
    if (CHIMP_ANY_CLASS(a) != CHIMP_ANY_CLASS(b)) {
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

static void
_chimp_object_mark (ChimpGC *gc, ChimpRef *self)
{
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
    ChimpRef *result = chimp_task_recv (chimp_task_get_self (CHIMP_CURRENT_TASK));
    return result;
}

static ChimpRef *
_chimp_task_self (ChimpRef *self, ChimpRef *args)
{
    return chimp_task_get_self (CHIMP_CURRENT_TASK);
}

static ChimpRef *
_chimp_array_range(ChimpRef* a, ChimpRef *args)
{
    int64_t start = 0;
    int64_t stop;
    int64_t step = 1;
    int64_t i;
    ChimpRef *result;

    switch (CHIMP_ARRAY(args)->size) {
        case 0:
            CHIMP_BUG("range(): not enough args");
            return NULL;
        case 1:
            if (!chimp_method_parse_args (args, "I", &stop)) {
                return NULL;
            }
            break;
        case 2:
            if (!chimp_method_parse_args (args, "II", &start, &stop)) {
                return NULL;
            }
            break;
        case 3:
            if (!chimp_method_parse_args (args, "III", &start, &stop, &step)) {
                return NULL;
            }
            if (0 == step) {
                CHIMP_BUG("range(): step argument can't be zero.");
                return NULL;
            }
            break;
        default:
            CHIMP_BUG("range(): too many args.");
            return NULL;
    }

    result = chimp_array_new();
    for (i = start; SIGN(step)*(stop - i) > 0; i += step) {
        if (!chimp_array_push(result, chimp_int_new(i))) {
            return NULL;
        }
    }
    return result;
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
    chimp_hash_put_str (chimp_builtins, "float",  chimp_float_class);
    chimp_hash_put_str (chimp_builtins, "str",    chimp_str_class);
    chimp_hash_put_str (chimp_builtins, "bool",   chimp_bool_class);
    chimp_hash_put_str (chimp_builtins, "module", chimp_module_class);
    chimp_hash_put_str (chimp_builtins, "object", chimp_object_class);
    chimp_hash_put_str (chimp_builtins, "class",  chimp_class_class);
    chimp_hash_put_str (chimp_builtins, "method", chimp_method_class);
    chimp_hash_put_str (chimp_builtins, "error",  chimp_error_class);

    CHIMP_BUILTIN_METHOD(_chimp_task_recv, "recv");
    CHIMP_BUILTIN_METHOD(_chimp_task_self, "self");
    CHIMP_BUILTIN_METHOD(_chimp_array_range, "range");

    return CHIMP_TRUE;
}

static void
_chimp_class_mark (ChimpGC *gc, ChimpRef *self)
{
    CHIMP_SUPER (self)->mark (gc, self);

    chimp_gc_mark_ref (gc, CHIMP_CLASS(self)->super);
    chimp_gc_mark_ref (gc, CHIMP_CLASS(self)->name);
    chimp_gc_mark_lwhash (gc, CHIMP_CLASS(self)->methods);
}

static ChimpRef *
_chimp_str_size (ChimpRef *self, ChimpRef *args)
{
    return chimp_int_new (CHIMP_STR_SIZE(self));
}

static ChimpRef *
_chimp_str_index (ChimpRef *self, ChimpRef *args)
{
    const char *needle;
    const char *res;
    if (!chimp_method_parse_args (args, "s", &needle)) {
        return NULL;
    }
    res = strstr (CHIMP_STR_DATA(self), needle);
    if (res == NULL) {
        return chimp_int_new (-1);
    }
    else {
        return chimp_int_new ((size_t)(res - CHIMP_STR_DATA(self)));
    }
}

static ChimpRef *
_chimp_str_trim (ChimpRef *self, ChimpRef *args)
{
    size_t i, j;

    for (i = 0; i < CHIMP_STR_SIZE(self); i++) {
        if (!isspace (CHIMP_STR(self)->data[i])) {
            break;
        }
    }

    if (CHIMP_STR_SIZE(self) > 0) {
        for (j = CHIMP_STR_SIZE(self)-1; j > i; j--) {
            if (!isspace (CHIMP_STR(self)->data[j])) {
                break;
            }
        }
    }
    else {
        j = 0;
    }

    return chimp_str_new (CHIMP_STR_DATA(self) + i, j + 1 - i);
}

static ChimpRef *
_chimp_str_substr (ChimpRef *self, ChimpRef *args)
{
    int64_t i, j;
    ChimpRef *jobj = NULL;

    if (!chimp_method_parse_args (args, "I|o", &i, &jobj)) {
        return NULL;
    }

    if (i < 0) {
        i += CHIMP_STR_SIZE(self);
    }

    if (jobj == NULL) {
        j = CHIMP_STR_SIZE(self);
    }
    else {
        j = CHIMP_INT(jobj)->value;
        if (j < 0) {
            j += CHIMP_STR_SIZE(self);
        }
    }

    return chimp_str_new (CHIMP_STR_DATA(self) + i, j - i);
}

static ChimpRef *
_chimp_str_split (ChimpRef *self, ChimpRef *args)
{
    const char *begin;
    const char *end;
    const char *sep;
    size_t seplen;
    ChimpRef *arr;
    const size_t len = CHIMP_STR_SIZE(self);
    size_t n = 0;

    if (!chimp_method_parse_args (args, "s", &sep)) {
        return NULL;
    }

    /* XXX could pull this directly from the str object */
    seplen = strlen (sep);

    arr = chimp_array_new ();
    if (arr == NULL) {
        return NULL;
    }

    begin = CHIMP_STR_DATA(self);
    while (n < len) {
        ChimpRef *ref;
        end = strstr (begin + n, sep);
        if (end == NULL) {
            end = CHIMP_STR_DATA(self) + CHIMP_STR_SIZE(self);
        }
        ref = chimp_str_new (begin + n, (size_t)(end - begin - n));
        if (ref == NULL) {
            return NULL;
        }
        if (!chimp_array_push (arr, ref)) {
            return NULL;
        }
        n += CHIMP_STR_SIZE(ref) + seplen;
    }
    return arr;
}

static ChimpRef *
_chimp_str_upper (ChimpRef *self, ChimpRef *args)
{
    size_t i;
    char *data;
    size_t len = CHIMP_STR_SIZE(self);
    ChimpRef *str = chimp_str_new (CHIMP_STR_DATA(self), len);

    data = CHIMP_STR_DATA(str);

    for (i = 0; i < len; i++) {
        if (data[i] >= 'a' && data[i] <= 'z') {
            data[i] -= (char) 0x20;
        }
    }

    return str;
}

static ChimpRef *
_chimp_str_lower (ChimpRef *self, ChimpRef *args)
{
    size_t i;
    char *data;
    size_t len = CHIMP_STR_SIZE(self);
    ChimpRef *str = chimp_str_new (CHIMP_STR_DATA(self), len);

    data = CHIMP_STR_DATA(str);

    for (i = 0; i < len; i++) {
        if (data[i] >= 'A' && data[i] <= 'Z') {
            data[i] += (char) 0x20;
        }
    }

    return str;
}

static ChimpRef *
_chimp_str_add (ChimpRef *self, ChimpRef *other)
{
    ChimpRef *other_str = chimp_object_str (other);
    if (other_str == NULL) {
        CHIMP_BUG ("could not convert to str");
        return NULL;
    }
    return chimp_str_new_concat (
        CHIMP_STR_DATA(self), CHIMP_STR_DATA(other_str), NULL);
}

chimp_bool_t
chimp_core_startup (const char *path, void *stack_start)
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
    CHIMP_CLASS(chimp_object_class)->mark = _chimp_object_mark;
    CHIMP_CLASS(chimp_object_class)->getattr = _chimp_object_getattr;
    CHIMP_BOOTSTRAP_CLASS_L1(NULL, chimp_class_class, "class", chimp_object_class);
    CHIMP_CLASS(chimp_class_class)->getattr = chimp_class_getattr;
    CHIMP_CLASS(chimp_class_class)->mark = _chimp_class_mark;
    CHIMP_BOOTSTRAP_CLASS_L1(NULL, chimp_str_class, "str", chimp_object_class);
    CHIMP_CLASS(chimp_str_class)->cmp = chimp_str_cmp;
    CHIMP_CLASS(chimp_str_class)->str = chimp_str_str;
    CHIMP_CLASS(chimp_str_class)->mark = _chimp_object_mark;
    CHIMP_CLASS(chimp_str_class)->add  = _chimp_str_add;

    CHIMP_BOOTSTRAP_CLASS_L2(NULL, chimp_object_class);
    CHIMP_BOOTSTRAP_CLASS_L2(NULL, chimp_class_class);
    CHIMP_BOOTSTRAP_CLASS_L2(NULL, chimp_str_class);

    if (!chimp_error_class_bootstrap ()) goto error;

    if (!chimp_method_class_bootstrap ()) goto error;

    if (!_chimp_bootstrap_L3 ()) {
        return CHIMP_FALSE;
    }

    if (!chimp_int_class_bootstrap ()) goto error;
    if (!chimp_float_class_bootstrap ()) goto error;
    if (!chimp_array_class_bootstrap ()) goto error;
    if (!chimp_hash_class_bootstrap ()) goto error;

    chimp_nil_class = chimp_class_new (
        CHIMP_STR_NEW("nil"), chimp_object_class, sizeof(ChimpAny));
    if (chimp_nil_class == NULL) goto error;
    chimp_gc_make_root (NULL, chimp_nil_class);
    CHIMP_CLASS(chimp_nil_class)->str = chimp_nil_str;
    chimp_nil = chimp_class_new_instance (chimp_nil_class, NULL);
    if (chimp_nil == NULL) goto error;
    chimp_gc_make_root (NULL, chimp_nil);

    chimp_bool_class = chimp_class_new (
        CHIMP_STR_NEW("bool"), chimp_object_class, sizeof(ChimpAny));
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

    if (!chimp_module_add_builtin (chimp_init_io_module ()))
        goto error;

    if (!chimp_module_add_builtin (chimp_init_assert_module ()))
        goto error;

    if (!chimp_module_add_builtin (chimp_init_unit_module ()))
        goto error;

    if (!chimp_module_add_builtin (chimp_init_os_module ()))
        goto error;

    if (!chimp_module_add_builtin (chimp_init_gc_module ()))
        goto error;

    if (!chimp_module_add_builtin (chimp_init_net_module ()))
        goto error;

    if (!chimp_module_add_builtin (chimp_init_uv_module ()))
        goto error;

    if (!chimp_module_add_builtin (chimp_init_compiler_module ()))
        goto error;

    if (!chimp_core_init_builtins ()) goto error;

    /* XXX got to be a better place for this ... */
    if (!chimp_class_add_native_method (chimp_str_class, "size", _chimp_str_size)) {
        return CHIMP_FALSE;
    }

    if (!chimp_class_add_native_method (chimp_str_class, "trim", _chimp_str_trim)) {
        return CHIMP_FALSE;
    }

    if (!chimp_class_add_native_method (chimp_str_class, "index", _chimp_str_index)) {
        return CHIMP_FALSE;
    }

    if (!chimp_class_add_native_method (chimp_str_class, "substr", _chimp_str_substr)) {
        return CHIMP_FALSE;
    }

    if (!chimp_class_add_native_method (chimp_str_class, "split", _chimp_str_split)) {
        return CHIMP_FALSE;
    }

    if (!chimp_class_add_native_method (chimp_str_class, "upper", _chimp_str_upper)) {
        return CHIMP_FALSE;
    }

    if (!chimp_class_add_native_method (chimp_str_class, "lower", _chimp_str_lower)) {
        return CHIMP_FALSE;
    }

    chimp_module_path = _chimp_module_make_path (path);
    if (chimp_module_path == NULL)
        goto error;
    if (!chimp_gc_make_root (NULL, chimp_module_path))
        goto error;

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
    int rc;
    
    rc = chimp_hash_get (chimp_builtins, name, NULL);
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

static ChimpRef *
_chimp_module_make_path (const char *path)
{
    size_t i, j;
    size_t len;
    ChimpRef *result = chimp_array_new ();
    
    // We only add the current working directory if no paths were passed via the environment
    if (path == NULL) {
        char current_dir[FILENAME_MAX];
        if (getcwd(current_dir, sizeof(current_dir))) {
          ChimpRef *entry = chimp_str_new(current_dir, sizeof(current_dir));
          chimp_array_push(result, entry);
        }
        return result;
    }

    len = strlen (path);

    /* XXX this works, but this is totally crap */
    for (i = j = 0; i < len; j++) {
        if (path[j] == ':' || path[j] == '\0') {
            char *s;
            size_t l;
            ChimpRef *entry;

            l = j - i;
            if (l == 0) continue;

            s = strndup (path + i, l);
            if (s == NULL) {
                return NULL;
            }

            entry = chimp_str_new_take (s, l);
            if (entry == NULL) {
                free (s);
                return NULL;
            }
            if (!chimp_array_push (result, entry)) {
                return NULL;
            }
            i = j + 1;
        }
    }

    return result;
}


