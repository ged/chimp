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

#ifndef _CHIMP_OBJECT_H_INCLUDED_
#define _CHIMP_OBJECT_H_INCLUDED_

#include <chimp/core.h>
#include <chimp/gc.h>
#include <chimp/class.h>
#include <chimp/str.h>
#include <chimp/int.h>
#include <chimp/array.h>
#include <chimp/frame.h>
#include <chimp/code.h>
#include <chimp/method.h>
#include <chimp/hash.h>
#include <chimp/ast.h>
#include <chimp/module.h>
#include <chimp/symtable.h>
#include <chimp/msg.h>
#include <chimp/task.h>
#include <chimp/var.h>
#include <chimp/error.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _ChimpObject {
    ChimpAny     base;
} ChimpObject;

ChimpCmpResult
chimp_object_cmp (ChimpRef *a, ChimpRef *b);

ChimpRef *
chimp_object_str (ChimpRef *self);

ChimpRef *
chimp_object_add (ChimpRef *left, ChimpRef *right);

ChimpRef *
chimp_object_sub (ChimpRef *left, ChimpRef *right);

ChimpRef *
chimp_object_mul (ChimpRef *left, ChimpRef *right);

ChimpRef *
chimp_object_div (ChimpRef *left, ChimpRef *right);

ChimpRef *
chimp_object_call (ChimpRef *target, ChimpRef *args);

ChimpRef *
chimp_object_call_method (ChimpRef *target, const char *name, ChimpRef *args);

ChimpRef *
chimp_object_getattr (ChimpRef *self, ChimpRef *name);

ChimpRef *
chimp_object_getitem (ChimpRef *self, ChimpRef *key);

ChimpRef *
chimp_object_getattr_str (ChimpRef *self, const char *name);

chimp_bool_t
chimp_object_instance_of (ChimpRef *object, ChimpRef *klass);

#define CHIMP_OBJECT(ref) CHIMP_CHECK_CAST(ChimpObject, (ref), chimp_object_class)

CHIMP_EXTERN_CLASS(object);
CHIMP_EXTERN_CLASS(bool);
CHIMP_EXTERN_CLASS(nil);

extern struct _ChimpRef *chimp_nil;
extern struct _ChimpRef *chimp_true;
extern struct _ChimpRef *chimp_false;
extern struct _ChimpRef *chimp_builtins;

#define CHIMP_BOOL_REF(b) ((b) ? chimp_true : chimp_false)

#define CHIMP_OBJECT_STR(ref) CHIMP_STR_DATA(chimp_object_str (NULL, (ref)))

#ifdef __cplusplus
};
#endif

#endif

