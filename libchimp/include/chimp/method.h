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

#ifndef _CHIMP_METHOD_H_INCLUDED_
#define _CHIMP_METHOD_H_INCLUDED_

#include <chimp/gc.h>
#include <chimp/any.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _ChimpMethodType {
    CHIMP_METHOD_TYPE_NATIVE,
    CHIMP_METHOD_TYPE_BYTECODE,
    CHIMP_METHOD_TYPE_CLOSURE
} ChimpMethodType;

enum {
    CHIMP_METHOD_FLAG_FREEVARS = 0x0001 /* a closure with freevars */
};

typedef ChimpRef *(*ChimpNativeMethodFunc)(ChimpRef *, ChimpRef *);

typedef struct _ChimpMethod {
    ChimpAny         base;
    ChimpRef        *self; /* NULL for unbound/function */
    ChimpMethodType  type;
    int              flags;
    ChimpRef        *module;
    union {
        struct {
            ChimpNativeMethodFunc func;
        } native;
        struct {
            ChimpRef *code;
        } bytecode;
        struct {
            ChimpRef *code;
            ChimpRef *bindings;
        } closure;
    };
} ChimpMethod;

chimp_bool_t
chimp_method_class_bootstrap (void);

ChimpRef *
chimp_method_new_native (ChimpRef *module, ChimpNativeMethodFunc func);

ChimpRef *
chimp_method_new_bytecode (ChimpRef *module, ChimpRef *code);

ChimpRef *
chimp_method_new_closure (ChimpRef *method, ChimpRef *bindings);

ChimpRef *
chimp_method_new_bound (ChimpRef *unbound, ChimpRef *self);

#define CHIMP_METHOD(ref) CHIMP_CHECK_CAST(ChimpMethod, (ref), CHIMP_VALUE_TYPE_METHOD)
#define CHIMP_METHOD_SELF(ref) (CHIMP_METHOD(ref)->self)
#define CHIMP_METHOD_TYPE(ref) (CHIMP_METHOD(ref)->type)

#define CHIMP_NATIVE_METHOD(ref) (&(CHIMP_METHOD(ref)->native))
#define CHIMP_BYTECODE_METHOD(ref) (&(CHIMP_METHOD(ref)->bytecode))
#define CHIMP_CLOSURE_METHOD(ref) (&(CHIMP_METHOD(ref)->closure))

CHIMP_EXTERN_CLASS(method);

#ifdef __cplusplus
};
#endif

#endif

