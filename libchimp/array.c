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

#include "chimp/array.h"
#include "chimp/object.h"
#include "chimp/core.h"
#include "chimp/class.h"
#include "chimp/str.h"

ChimpRef *chimp_array_class = NULL;

static ChimpRef *
_chimp_array_push (ChimpRef *self, ChimpRef *args)
{
    ChimpRef *arg;
    if (!chimp_method_parse_args (args, "o", &arg)) {
        return NULL;
    }
    /* TODO error if args len == 0 */
    if (!chimp_array_push (self, arg)) {
        /* XXX error? exception? abort? */
        return NULL;
    }
    return chimp_nil;
}

static ChimpRef *
_chimp_array_pop (ChimpRef *self, ChimpRef *args)
{
    if (!chimp_method_no_args (args)) {
        return NULL;
    }
    return chimp_array_pop (self);
}

static ChimpRef *
_chimp_array_map (ChimpRef *self, ChimpRef *args)
{
    size_t i;
    ChimpRef *fn;
    ChimpRef *result = chimp_array_new_with_capacity (CHIMP_ARRAY_SIZE(self));
    if (!chimp_method_parse_args (args, "o", &fn)) {
        return NULL;
    }
    for (i = 0; i < CHIMP_ARRAY_SIZE(self); i++) {
        ChimpRef *fn_args;
        ChimpRef *mapped;
        ChimpRef *value = CHIMP_ARRAY_ITEM(self, i);

        fn_args = chimp_array_new_var (value, NULL);
        if (fn_args == NULL) {
            return NULL;
        }
        mapped = chimp_object_call (fn, fn_args);
        if (mapped == NULL) {
            return NULL;
        }
        if (!chimp_array_push (result, mapped)) {
            return NULL;
        }
    }
    return result;
}

static ChimpRef *
_chimp_array_each (ChimpRef *self, ChimpRef *args)
{
    size_t i;
    ChimpRef *fn;
    if (!chimp_method_parse_args (args, "o", &fn)) {
        return NULL;
    }
    for (i = 0; i < CHIMP_ARRAY_SIZE(self); i++) {
        ChimpRef *fn_args;
        ChimpRef *value;
        
        value = CHIMP_ARRAY_ITEM(self, i);
        fn_args = chimp_array_new_var (value, NULL);
        if (fn_args == NULL) {
            return NULL;
        }
        if (chimp_object_call (fn, fn_args) == NULL) {
            return NULL;
        }
    }
    return chimp_nil;
}

static ChimpRef *
_chimp_array_contains (ChimpRef *self, ChimpRef *args)
{
    size_t i;
    ChimpRef *right;
    if (!chimp_method_parse_args (args, "o", &right)) {
        return NULL;
    }
    for (i = 0; i < CHIMP_ARRAY_SIZE(self); i++) {
        ChimpCmpResult r;
        ChimpRef *left = CHIMP_ARRAY_ITEM(self, i);
        
        r = chimp_object_cmp (left, right);
        if (r == CHIMP_CMP_ERROR) {
            return NULL;
        }
        else if (r == CHIMP_CMP_EQ) {
            return chimp_true;
        }
    }
    return chimp_false;
}

static ChimpRef *
_chimp_array_filter (ChimpRef *self, ChimpRef *args)
{
    size_t i;
    ChimpRef *result;
    ChimpRef *fn;

    if (!chimp_method_parse_args (args, "o", &fn)) {
        return NULL;
    }

    result = chimp_array_new ();
    for (i = 0; i < CHIMP_ARRAY_SIZE(self); i++) {
        ChimpRef *value = CHIMP_ARRAY_ITEM(self, i);
        ChimpRef *fn_args;
        ChimpRef *r;
        
        fn_args = chimp_array_new_var (value, NULL);
        if (fn_args == NULL) {
            return NULL;
        }
        r = chimp_object_call (fn, fn_args);
        if (r == NULL) {
            return NULL;
        }
        if (r == chimp_true) {
            if (!chimp_array_push (result, value)) {
                return NULL;
            }
        }
    }
    return result;
}

static ChimpRef *
chimp_array_str (ChimpRef *self)
{
    size_t size = CHIMP_ARRAY_SIZE(self);
    /* '[' + ']' + (', ' x (size-1)) + '\0' */
    size_t total_len = 2 + (size > 0 ? ((size-1) * 2) : 0) + 1;
    ChimpRef *ref;
    ChimpRef *item_strs;
    char *data;
    size_t i, j;

    item_strs = chimp_array_new ();
    if (item_strs == NULL) {
        return NULL;
    }

    for (i = 0; i < size; i++) {
        ref = CHIMP_ARRAY_ITEM(self, i);
        /* XXX what we really want is something like Python's repr() */
        /* TODO instanceof */
        if (CHIMP_ANY_CLASS(ref) == chimp_str_class) {
            /* for surrounding quotes */
            total_len += 2;
        }
        ref = chimp_object_str (ref);
        if (ref == NULL) {
            return NULL;
        }
        chimp_array_push (item_strs, ref);
        total_len += CHIMP_STR_SIZE(ref);
    }

    data = CHIMP_MALLOC(char, total_len);
    if (data == NULL) {
        return NULL;
    }
    j = 0;
    data[j++] = '[';

    for (i = 0; i < size; i++) {
        ref = CHIMP_ARRAY_ITEM(item_strs, i);
        /* XXX what we really want is something like Python's repr() */
        if (CHIMP_ANY_CLASS(CHIMP_ARRAY_ITEM(self, i)) == chimp_str_class) {
            data[j++] = '"';
        }
        memcpy (data + j, CHIMP_STR_DATA(ref), CHIMP_STR_SIZE(ref));
        j += CHIMP_STR_SIZE(ref);
        if (CHIMP_ANY_CLASS(CHIMP_ARRAY_ITEM(self, i)) == chimp_str_class) {
            data[j++] = '"';
        }
        if (i < (size-1)) {
            memcpy (data + j, ", ", 2);
            j += 2;
        }
    }

    data[j++] = ']';
    data[j] = '\0';

    return chimp_str_new_take (data, total_len-1);
}

static ChimpRef *
_chimp_array_size (ChimpRef *self, ChimpRef *args)
{
    if (!chimp_method_no_args (args)) {
        return NULL;
    }

    return chimp_int_new (CHIMP_ARRAY_SIZE(self));
}

static ChimpRef *
_chimp_array_join (ChimpRef *self, ChimpRef *args)
{
    size_t i;
    size_t len;
    size_t seplen;
    const char *sep;
    ChimpRef *strs;
    char *result;
    char *ptr;

    if (!chimp_method_parse_args (args, "s", &sep)) {
        return NULL;
    }
    seplen = strlen (sep);

    strs = chimp_array_new_with_capacity (CHIMP_ARRAY_SIZE(self));
    if (strs == NULL) {
        return NULL;
    }

    len = strlen (sep) * (CHIMP_ARRAY_SIZE(self) - 1);
    for (i = 0; i < CHIMP_ARRAY_SIZE(self); i++) {
        ChimpRef *strval = chimp_object_str (CHIMP_ARRAY_ITEM(self, i));
        if (strval == NULL) {
            return NULL;
        }
        if (!chimp_array_push (strs, strval)) {
            return NULL;
        }
        len += CHIMP_STR_SIZE(strval);
    }

    result = malloc (len + 1);
    if (result == NULL) {
        return NULL;
    }
    ptr = result;

    for (i = 0; i < CHIMP_ARRAY_SIZE(self); i++) {
        ChimpRef *s = CHIMP_ARRAY_ITEM(strs, i);
        memcpy (ptr, CHIMP_STR_DATA(s), CHIMP_STR_SIZE(s));
        ptr += CHIMP_STR_SIZE(s);
        if (i < CHIMP_ARRAY_SIZE(self)-1) {
            memcpy (ptr, sep, seplen);
            ptr += seplen;
        }
    }
    result[len] = '\0';

    return chimp_str_new_take (result, len);
}

static ChimpRef *
_chimp_array_getitem (ChimpRef *self, ChimpRef *key)
{
    if (CHIMP_ANY_CLASS(key) != chimp_int_class) {
        CHIMP_BUG ("bad argument type for array.__getattr__");
        return NULL;
    }

    return CHIMP_ARRAY_ITEM(self, (size_t)CHIMP_INT(key)->value);
}

static void
_chimp_array_dtor (ChimpRef *self)
{
    CHIMP_FREE (CHIMP_ARRAY(self)->items);
}

static void
_chimp_array_mark (ChimpGC *gc, ChimpRef *self)
{
    CHIMP_SUPER (self)->mark (gc, self);

    size_t i;
    for (i = 0; i < CHIMP_ARRAY(self)->size; i++) {
        chimp_gc_mark_ref (gc, CHIMP_ARRAY(self)->items[i]);
    }
}

static ChimpCmpResult
_chimp_array_cmp(ChimpRef *left, ChimpRef *right)
{
    size_t lsize = CHIMP_ARRAY_SIZE(left);
    size_t rsize = CHIMP_ARRAY_SIZE(right);

    if (lsize != rsize)
    {
        return CHIMP_CMP_NOT_IMPL;
    }
    else
    {
        int i;
        for (i = 0; i < lsize; i++) {
            ChimpRef *lval = CHIMP_ARRAY_ITEM(left, i);
            ChimpRef *rval = CHIMP_ARRAY_ITEM(right, i);

            ChimpCmpResult r = chimp_object_cmp (lval, rval);
            if (r != CHIMP_CMP_EQ)
            {
                return r;
            }
        }
        return CHIMP_CMP_EQ;
    }
}

static ChimpRef *
_chimp_array_add (ChimpRef *self, ChimpRef *other)
{
    if (CHIMP_ANY_CLASS(self) == CHIMP_ANY_CLASS(other)) {
        size_t i;
        size_t required_capacity =
            CHIMP_ARRAY_SIZE(self) + CHIMP_ARRAY_SIZE(other);
        ChimpRef *new = chimp_array_new_with_capacity (required_capacity);
        if (new == NULL) {
            CHIMP_BUG ("could not allocate array");
            return NULL;
        }
        for (i = 0; i < CHIMP_ARRAY_SIZE(self); i++) {
            CHIMP_ARRAY(new)->items[i] = CHIMP_ARRAY_ITEM(self, i);
        }
        for (; i < required_capacity; i++) {
            CHIMP_ARRAY(new)->items[i] =
                CHIMP_ARRAY_ITEM(other, i - CHIMP_ARRAY_SIZE(self));
        }
        CHIMP_ARRAY(new)->size = required_capacity;
        return new;
    }
    else {
        CHIMP_BUG ("cannot add array and %s type",
                CHIMP_CLASS_NAME (CHIMP_ANY_CLASS(other)));
        return NULL;
    }
}

static void
_chimp_array_remove_at_internal (ChimpRef *self, size_t index)
{
    if (CHIMP_ARRAY_SIZE(self) > index) {
        memmove (
            CHIMP_ARRAY(self)->items + index,
            CHIMP_ARRAY(self)->items + index + 1,
            sizeof(ChimpRef*) * (CHIMP_ARRAY_SIZE(self) - index - 1)
        );
    }
    CHIMP_ARRAY(self)->size--;
}

static ChimpRef *
_chimp_array_remove (ChimpRef *self, ChimpRef *args)
{
    ChimpRef *other;
    size_t i;
    if (!chimp_method_parse_args (args, "o", &other)) {
        return NULL;
    }
    for (i = 0; i < CHIMP_ARRAY_SIZE(self); i++) {
        int rc = chimp_object_cmp (CHIMP_ARRAY_ITEM(self, i), other);
        if (rc == CHIMP_CMP_EQ) {
            _chimp_array_remove_at_internal (self, i);
            return chimp_true;
        }
        else if (rc == CHIMP_CMP_ERROR) {
            return NULL;
        }
    }
    return chimp_false;
}

static ChimpRef *
_chimp_array_remove_at (ChimpRef *self, ChimpRef *args)
{
    ChimpRef *removed;
    int64_t pos;
    if (!chimp_method_parse_args (args, "I", &pos)) {
        return NULL;
    }
    if (pos < 0) {
        pos = CHIMP_ARRAY_SIZE(self) + pos;
    }
    if (pos < 0 || pos >= CHIMP_ARRAY_SIZE(self)) {
        CHIMP_BUG ("invalid array index: %zu", (intmax_t) pos);
        return NULL;
    }
    removed = CHIMP_ARRAY_ITEM(self, (size_t) pos);
    _chimp_array_remove_at_internal (self, (size_t) pos);
    return removed;
}


static ChimpRef *
_chimp_array_slice(ChimpRef *self, ChimpRef *args)
{
    int64_t start;
    int64_t end;
    if (!chimp_method_parse_args (args, "II", &start, &end)) {
        CHIMP_BUG("invalid parameters, expecting two numeric values");
        return NULL;
    }
    // XXX - Better parameter validation?
    // XXX - Negative positioning for slicing

    ChimpRef *result = chimp_array_new_with_capacity (end - start);
    int64_t i;
    for (i = start; i < end; i++) {
      ChimpRef *item = CHIMP_ARRAY_ITEM(self, (size_t) i);
      chimp_array_push(result, item);
    }
    return result;
}

chimp_bool_t
chimp_array_class_bootstrap (void)
{
    chimp_array_class = chimp_class_new (
            CHIMP_STR_NEW("array"), chimp_object_class, sizeof(ChimpArray));
    if (chimp_array_class == NULL) {
        return CHIMP_FALSE;
    }
    CHIMP_CLASS(chimp_array_class)->str = chimp_array_str;
    CHIMP_CLASS(chimp_array_class)->dtor = _chimp_array_dtor;
    CHIMP_CLASS(chimp_array_class)->getitem = _chimp_array_getitem;
    CHIMP_CLASS(chimp_array_class)->mark = _chimp_array_mark;
    CHIMP_CLASS(chimp_array_class)->cmp = _chimp_array_cmp;
    CHIMP_CLASS(chimp_array_class)->add = _chimp_array_add;
    chimp_gc_make_root (NULL, chimp_array_class);
    chimp_class_add_native_method (chimp_array_class, "push", _chimp_array_push);
    chimp_class_add_native_method (chimp_array_class, "pop", _chimp_array_pop);
    chimp_class_add_native_method (chimp_array_class, "map", _chimp_array_map);
    chimp_class_add_native_method (chimp_array_class, "filter", _chimp_array_filter);
    chimp_class_add_native_method (chimp_array_class, "each", _chimp_array_each);
    chimp_class_add_native_method (chimp_array_class, "contains", _chimp_array_contains);
    chimp_class_add_native_method (chimp_array_class, "size", _chimp_array_size);
    chimp_class_add_native_method (chimp_array_class, "join", _chimp_array_join);
    chimp_class_add_native_method (chimp_array_class, "remove", _chimp_array_remove);
    chimp_class_add_native_method (chimp_array_class, "remove_at", _chimp_array_remove_at);
    chimp_class_add_native_method (chimp_array_class, "slice", _chimp_array_slice);
    return CHIMP_TRUE;
}

ChimpRef *
chimp_array_new (void)
{
    return chimp_array_new_with_capacity (10);
}

ChimpRef *
chimp_array_new_with_capacity (size_t capacity)
{
    ChimpRef *ref = chimp_gc_new_object (NULL);
    if (ref == NULL) {
        return NULL;
    }
    CHIMP_ANY(ref)->klass = chimp_array_class;
    CHIMP_ARRAY(ref)->items = CHIMP_MALLOC(ChimpRef *, capacity * sizeof(ChimpRef *));
    if (CHIMP_ARRAY(ref)->items == NULL) {
        return NULL;
    }
    CHIMP_ARRAY(ref)->size = 0;
    CHIMP_ARRAY(ref)->capacity = capacity;
    return ref;
}

ChimpRef *
chimp_array_new_var (ChimpRef *a, ...)
{
    va_list args;
    ChimpRef *arg;
    ChimpRef *ref = chimp_array_new ();
    if (ref == NULL) {
        return NULL;
    }

    va_start (args, a);
    if (!chimp_array_push (ref, a)) {
        return NULL;
    }
    while ((arg = va_arg (args, ChimpRef *)) != NULL) {
        chimp_array_push (ref, arg);
    }
    va_end (args);

    return ref;
}

#if 0
static chimp_bool_t
chimp_array_ensure_capacity (ChimpRef *self, size_t capacity)
{
    if (CHIMP_ARRAY(self)->capacity < capacity) {
        ChimpRef **items;
        items = CHIMP_REALLOC(
            ChimpRef *, arr->items, sizeof(*arr->items) * new_capacity);
        if (items == NULL) {
            return CHIMP_FALSE;
        }
        arr->items = items;
        arr->capacity = capacity;
    }
    return CHIMP_TRUE;
}
#endif

static chimp_bool_t
chimp_array_grow (ChimpRef *self)
{
    ChimpRef **items;
    ChimpArray *arr = CHIMP_ARRAY(self);
    if (arr->size >= arr->capacity) {
        size_t new_capacity =
            (arr->capacity == 0 ? 10 : (size_t)(arr->capacity * 1.8));
        items = CHIMP_REALLOC(
            ChimpRef *, arr->items, sizeof(*arr->items) * new_capacity);
        if (items == NULL) {
            return CHIMP_FALSE;
        }
        arr->items = items;
        arr->capacity = new_capacity;
    }
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_array_unshift (ChimpRef *self, ChimpRef *value)
{
    size_t i;
    ChimpArray *arr = CHIMP_ARRAY(self);
    if (!chimp_array_grow (self)) {
        return CHIMP_FALSE;
    }
    if (arr->size > 0) {
        for (i = arr->size; i > 0; i--) {
            arr->items[i] = arr->items[i-1];
        }
    }
    arr->items[0] = value;
    arr->size++;
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_array_push (ChimpRef *self, ChimpRef *value)
{
    ChimpArray *arr = CHIMP_ARRAY(self);
    if (!chimp_array_grow (self)) {
        return CHIMP_FALSE;
    }
    arr->items[arr->size++] = value;
    return CHIMP_TRUE;
}

ChimpRef *
chimp_array_pop (ChimpRef *self)
{
    ChimpArray *arr = CHIMP_ARRAY(self);
    if (arr->size > 0) {
        return arr->items[--arr->size];
    }
    else {
        return chimp_nil;
    }
}

ChimpRef *
chimp_array_get (ChimpRef *self, int32_t pos)
{
    ChimpArray *arr = CHIMP_ARRAY(self);
    if (pos >= 0) {
        if (pos < arr->size) {
            return arr->items[pos];
        }
        else {
            return chimp_nil;
        }
    }
    else {
        if ((size_t)(-pos) <= arr->size) {
            return arr->items[arr->size - (size_t)(-pos)];
        }
        else {
            return chimp_nil;
        }
    }
}

int32_t
chimp_array_find (ChimpRef *self, ChimpRef *value)
{
    size_t i;
    for (i = 0; i < CHIMP_ARRAY_SIZE(self); i++) {
        ChimpCmpResult r = chimp_object_cmp (CHIMP_ARRAY_ITEM(self, i), value);
        if (r == CHIMP_CMP_ERROR) {
            return -1;
        }
        else if (r == CHIMP_CMP_EQ) {
            return (int32_t)i;
        }
    }
    return -1;
}

ChimpRef *
chimp_array_first (ChimpRef *self)
{
    if (CHIMP_ARRAY_SIZE(self) > 0) {
        return CHIMP_ARRAY_ITEM(self, 0);
    }
    else {
        return chimp_nil;
    }
}

ChimpRef *
chimp_array_last (ChimpRef *self)
{
    if (CHIMP_ARRAY_SIZE(self) > 0) {
        return CHIMP_ARRAY_ITEM(self, CHIMP_ARRAY_SIZE(self)-1);
    }
    else {
        return chimp_nil;
    }
}

