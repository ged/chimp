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

#include "chimp/module.h"
#include "chimp/array.h"
#include "chimp/object.h"
#include "chimp/str.h"
#include "chimp/compile.h"

ChimpRef *chimp_module_class = NULL;

static ChimpRef *
chimp_module_init (ChimpRef *self, ChimpRef *args)
{
    CHIMP_MODULE(self)->name = CHIMP_ARRAY_ITEM(args, 0);
    if (CHIMP_ARRAY_SIZE(args) == 2) {
        CHIMP_MODULE(self)->locals = CHIMP_ARRAY_ITEM(args, 1);
    }
    else {
        ChimpRef *temp = chimp_hash_new ();
        if (temp == NULL) {
            return NULL;
        }
        CHIMP_MODULE(self)->locals = temp;
    }
    return self;
}

static ChimpRef *
chimp_module_getattr (ChimpRef *self, ChimpRef *name)
{
    ChimpRef *klass;
    /* XXX this check is needed because getattr is called by the class ctor */
    if (CHIMP_MODULE(self)->locals != NULL) {
        ChimpRef *result;
        int rc;
        
        rc = chimp_hash_get (CHIMP_MODULE(self)->locals, name, &result);
        if (rc == 0) {
            return result;
        }
    }
    klass = CHIMP_CLASS(CHIMP_ANY_CLASS(self))->super;
    if (CHIMP_CLASS(klass)->getattr != NULL) {
        return CHIMP_CLASS(klass)->getattr (self, name);
    }
    return NULL;
}

static ChimpRef *
chimp_module_str (ChimpRef *self)
{
    ChimpRef *name = CHIMP_STR_NEW ("<module \"");
    chimp_str_append (name, CHIMP_MODULE(self)->name);
    chimp_str_append_str (name, "\">");
    return name;
}

static void
_chimp_module_mark (ChimpGC *gc, ChimpRef *self)
{
    CHIMP_SUPER(self)->mark (gc, self);

    chimp_gc_mark_ref (gc, CHIMP_MODULE(self)->name);
    chimp_gc_mark_ref (gc, CHIMP_MODULE(self)->locals);
}

chimp_bool_t
chimp_module_class_bootstrap (void)
{
    chimp_module_class = chimp_class_new (CHIMP_STR_NEW ("module"), NULL, sizeof(ChimpModule));
    if (chimp_module_class == NULL) {
        return CHIMP_FALSE;
    }
    chimp_gc_make_root (NULL, chimp_module_class);
    CHIMP_CLASS(chimp_module_class)->str = chimp_module_str;
    CHIMP_CLASS(chimp_module_class)->getattr = chimp_module_getattr;
    CHIMP_CLASS(chimp_module_class)->init = chimp_module_init;
    CHIMP_CLASS(chimp_module_class)->mark = _chimp_module_mark;
    return CHIMP_TRUE;
}

ChimpRef *
chimp_module_new (ChimpRef *name, ChimpRef *locals)
{
    if (locals == NULL) {
        locals = chimp_hash_new ();
        if (locals == NULL) {
            return NULL;
        }
    }
    return chimp_class_new_instance (chimp_module_class, name, locals, NULL);
}

ChimpRef *
chimp_module_new_str (const char *name, ChimpRef *locals)
{
    return chimp_module_new (chimp_str_new (name, strlen(name)), locals);
}

chimp_bool_t
chimp_module_add_local (ChimpRef *self, ChimpRef *name, ChimpRef *value)
{
    return chimp_hash_put (CHIMP_MODULE(self)->locals, name, value);
}

chimp_bool_t
chimp_module_add_method_str (
    ChimpRef *self,
    const char *name,
    ChimpNativeMethodFunc impl
)
{
    ChimpRef *nameref;
    ChimpRef *method;
    
    nameref = chimp_str_new (name, strlen(name));
    if (nameref == NULL) {
        return CHIMP_FALSE;
    }

    method = chimp_method_new_native (self, impl);
    if (method == NULL) {
        return CHIMP_FALSE;
    }

    return chimp_hash_put (CHIMP_MODULE(self)->locals, nameref, method);
}

chimp_bool_t
chimp_module_add_local_str (
    ChimpRef *self,
    const char *name,
    ChimpRef *value
)
{
    ChimpRef *nameref;

    nameref = chimp_str_new (name, strlen (name));
    if (nameref == NULL) {
        return CHIMP_FALSE;
    }

    return chimp_module_add_local (self, nameref, value);
}

