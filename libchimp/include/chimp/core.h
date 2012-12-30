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

#ifndef _CHIMP_CORE_H_INCLUDED_
#define _CHIMP_CORE_H_INCLUDED_

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <memory.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int chimp_bool_t;

struct _ChimpRef;

#define CHIMP_TRUE ((chimp_bool_t) 1)
#define CHIMP_FALSE ((chimp_bool_t) 0)

chimp_bool_t
chimp_core_startup (void *stack_start);

void
chimp_core_shutdown (void);

void
chimp_bug (const char *filename, int lineno, const char *format, ...);

chimp_bool_t
chimp_is_builtin (struct _ChimpRef *name);

chimp_bool_t
chimp_get_builtin (struct _ChimpRef *name, struct _ChimpRef **value);

#define CHIMP_BUG(fmt, ...) \
    chimp_bug (__FILE__, __LINE__, (fmt), ## __VA_ARGS__)

#define CHIMP_MALLOC(type, size) ((type *) malloc (size))
#define CHIMP_REALLOC(type, ptr, size) ((type *) realloc ((ptr), (size)))
#define CHIMP_FREE(ptr) \
    do { \
        if ((ptr) != NULL) { free (ptr); } \
    } while (0)

#define CHIMP_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            chimp_bug (__FILE__, __LINE__, "assertion failed: \"%s\"", #cond); \
        } \
    } while (0)

#ifdef __cplusplus
};
#endif

#endif

