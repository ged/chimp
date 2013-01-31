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

#ifndef _CHIMP_MODULE_H_INCLUDED_
#define _CHIMP_MODULE_H_INCLUDED_

#include <chimp/any.h>
#include <chimp/method.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _ChimpModule {
    ChimpAny base;
    ChimpRef *name;
    ChimpRef *locals;
} ChimpModule;

chimp_bool_t
chimp_module_class_bootstrap (void);

ChimpRef *
chimp_module_load (ChimpRef *name, ChimpRef *path);

ChimpRef *
chimp_module_new (ChimpRef *name, ChimpRef *locals);

ChimpRef *
chimp_module_new_str (const char *name, ChimpRef *locals);

chimp_bool_t
chimp_module_add_method_str (
    ChimpRef *self,
    const char *name,
    ChimpNativeMethodFunc impl
);

chimp_bool_t
chimp_module_add_local (
    ChimpRef *self,
    ChimpRef *name,
    ChimpRef *value
);

chimp_bool_t
chimp_module_add_local_str (
    ChimpRef *self,
    const char *name,
    ChimpRef *value
);

chimp_bool_t
chimp_module_add_builtin (ChimpRef *module);

#define CHIMP_MODULE(ref) CHIMP_CHECK_CAST(ChimpModule, (ref), chimp_module_class)
#define CHIMP_MODULE_NAME(ref) (CHIMP_MODULE(ref)->name)
#define CHIMP_MODULE_LOCALS(ref) (CHIMP_MODULE(ref)->locals)

#define CHIMP_MODULE_INT_CONSTANT(mod, name, value) \
    do { \
        ChimpRef *temp = chimp_int_new (value); \
        if (temp == NULL) { \
            return NULL; \
        } \
        if (!chimp_module_add_local_str ((mod), (name), temp)) { \
            return NULL; \
        } \
    } while (0)

CHIMP_EXTERN_CLASS(module);

#ifdef __cplusplus
};
#endif

#endif

