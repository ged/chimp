#include "chimp/hash.h"
#include "chimp/object.h"

#define CHIMP_HASH_INIT(ref) \
    CHIMP_ANY(ref)->type = CHIMP_VALUE_TYPE_HASH; \
    CHIMP_ANY(ref)->klass = chimp_hash_class;

ChimpRef *chimp_hash_class = NULL;

static ChimpRef *
chimp_hash_str (ChimpGC *gc, ChimpRef *self)
{
    size_t size = CHIMP_HASH_SIZE(self);
    /* '{' + '}' + (': ' x size) + (', ' x (size-1)) + '\0' */
    size_t total_len = 2 + (size * 2) + (size > 0 ? ((size-1) * 2) : 0) + 1;
    ChimpRef *ref;
    ChimpRef *strs;
    char *data;
    size_t i, k;


    strs = chimp_array_new (gc);
    if (strs == NULL) {
        return NULL;
    }

    for (i = 0; i < size; i++) {
        size_t j;
        ChimpRef *item[2];
        item[0] = CHIMP_HASH(self)->keys[i];
        item[1] = CHIMP_HASH(self)->values[i];
        for (j = 0; j < 2; j++) {
            ref = item[j];
            /* XXX what we really want is something like Python's repr() */
            if (CHIMP_ANY_TYPE(ref) == CHIMP_VALUE_TYPE_STR) {
                /* for surrounding quotes */
                total_len += 2;
            }
            ref = chimp_object_str (gc, ref);
            if (ref == NULL) {
                return NULL;
            }
            chimp_array_push (strs, ref);
            total_len += CHIMP_STR_SIZE(ref);
        }
    }

    data = CHIMP_MALLOC(char, total_len);
    if (data == NULL) {
        return NULL;
    }
    k = 0;
    data[k++] = '{';

    for (i = 0; i < size; i++) {
        size_t j;
        ChimpRef *item[2];
        item[0] = CHIMP_HASH(self)->keys[i];
        item[1] = CHIMP_HASH(self)->values[i];
        for (j = 0; j < 2; j++) {
            ref = CHIMP_ARRAY_ITEM(strs, (i * 2) + j);
            /* XXX what we really want is something like Python's repr() */
            if (CHIMP_ANY_TYPE(item[j]) == CHIMP_VALUE_TYPE_STR) {
                data[k++] = '"';
            }
            memcpy (data + k, CHIMP_STR_DATA(ref), CHIMP_STR_SIZE(ref));
            k += CHIMP_STR_SIZE(ref);
            if (CHIMP_ANY_TYPE(item[j]) == CHIMP_VALUE_TYPE_STR) {
                data[k++] = '"';
            }
            if (j == 0) {
                data[k++] = ':';
                data[k++] = ' ';
            }
        }
        if (i < (size-1)) {
            data[k++] = ',';
            data[k++] = ' ';
        }
    }

    data[k++] = '}';
    data[k] = '\0';

    return chimp_str_new_take (gc, data, total_len-1);
}

static ChimpRef *
_chimp_hash_put (ChimpRef *self, ChimpRef *args)
{
    return CHIMP_BOOL_REF(chimp_hash_put (self, CHIMP_ARRAY_ITEM(args, 0), CHIMP_ARRAY_ITEM(args, 1)));
}

static ChimpRef *
_chimp_hash_get (ChimpRef *self, ChimpRef *args)
{
    return chimp_hash_get (self, CHIMP_ARRAY_ITEM(args, 0));
}

chimp_bool_t
chimp_hash_class_bootstrap (ChimpGC *gc)
{
    chimp_hash_class =
        chimp_class_new (gc, CHIMP_STR_NEW(gc, "hash"), chimp_object_class);
    if (chimp_hash_class == NULL) {
        return CHIMP_FALSE;
    }
    CHIMP_CLASS(chimp_hash_class)->str = chimp_hash_str;
    chimp_gc_make_root (gc, chimp_hash_class);
    chimp_class_add_native_method (gc, chimp_hash_class, "put", _chimp_hash_put);
    chimp_class_add_native_method (gc, chimp_hash_class, "get", _chimp_hash_get);
    return CHIMP_TRUE;
}

ChimpRef *
chimp_hash_new (ChimpGC *gc)
{
    ChimpRef *ref = chimp_gc_new_object (gc);
    if (ref == NULL) {
        return NULL;
    }
    CHIMP_HASH_INIT(ref);
    return ref;
}

chimp_bool_t
chimp_hash_put (ChimpRef *self, ChimpRef *key, ChimpRef *value)
{
    ChimpRef **keys;
    ChimpRef **values;
    size_t i;

    for (i = 0; i < CHIMP_HASH_SIZE(self); i++) {
        ChimpCmpResult r = chimp_object_cmp (CHIMP_HASH(self)->keys[i], key);
        if (r == CHIMP_CMP_ERROR) {
            return CHIMP_FALSE;
        }
        else if (r == CHIMP_CMP_EQ) {
            CHIMP_HASH(self)->values[i] = value;
            return CHIMP_TRUE;
        }
    }

    keys = CHIMP_REALLOC(ChimpRef *, CHIMP_HASH(self)->keys, sizeof(*keys) * (CHIMP_HASH_SIZE(self)+1));
    if (keys == NULL) {
        return CHIMP_FALSE;
    }
    CHIMP_HASH(self)->keys = keys;

    values = CHIMP_REALLOC(ChimpRef *, CHIMP_HASH(self)->values, sizeof(*values) * (CHIMP_HASH_SIZE(self)+1));
    if (values == NULL) {
        return CHIMP_FALSE;
    }
    CHIMP_HASH(self)->values = values;

    keys[CHIMP_HASH_SIZE(self)] = key;
    values[CHIMP_HASH_SIZE(self)] = value;
    CHIMP_HASH(self)->size++;

    return CHIMP_TRUE;
}

ChimpRef *
chimp_hash_get (ChimpRef *self, ChimpRef *key)
{
    size_t i;
    for (i = 0; i < CHIMP_HASH_SIZE(self); i++) {
        ChimpCmpResult r = chimp_object_cmp (CHIMP_HASH(self)->keys[i], key);
        if (r == CHIMP_CMP_ERROR) return NULL;
        else if (r == CHIMP_CMP_EQ) {
            return CHIMP_HASH(self)->values[i];
        }
    }
    return chimp_nil;
}

ChimpRef *
chimp_hash_keys (ChimpRef *self)
{
    chimp_bug (__FILE__, __LINE__, "not implemented");
    return NULL;
}

ChimpRef *
chimp_hash_values (ChimpRef *self)
{
    chimp_bug (__FILE__, __LINE__, "not implemented");
    return NULL;
}
