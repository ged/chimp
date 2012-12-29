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

#ifndef _CHIMP_SYMTABLE_H_INCLUDED_
#define _CHIMP_SYMTABLE_H_INCLUDED_

#include <chimp/any.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CHIMP_SYM_DECL    = 0x0001, /* symbol explicitly introduced via a decl */
    CHIMP_SYM_BUILTIN = 0x0002, /* builtin */
    CHIMP_SYM_FREE    = 0x0004, /* free var */
    CHIMP_SYM_MODULE  = 0x1000, /* symbol declared at the module level */
    CHIMP_SYM_CLASS   = 0x2000, /* symbol declared at the class level */
    CHIMP_SYM_FUNC    = 0x8000  /* symbol declared inside a function */
};

#define CHIMP_SYM_TYPE_MASK 0xf000

typedef struct _ChimpSymtable {
    ChimpAny base;
    ChimpRef *filename;
    ChimpRef *lookup;
    ChimpRef *stack;
    ChimpRef *ste;
} ChimpSymtable;

typedef struct _ChimpSymtableEntry {
    ChimpAny  base;
    int       flags;
    ChimpRef *symtable;
    ChimpRef *scope;
    ChimpRef *symbols;
    ChimpRef *varnames;
    ChimpRef *parent;
    ChimpRef *children;
} ChimpSymtableEntry;

chimp_bool_t
chimp_symtable_class_bootstrap (void);

chimp_bool_t
chimp_symtable_entry_class_bootstrap (void);

ChimpRef *
chimp_symtable_new_from_ast (ChimpRef *filename, ChimpRef *ast);

ChimpRef *
chimp_symtable_lookup (ChimpRef *self, ChimpRef *scope);

chimp_bool_t
chimp_symtable_entry_sym_flags (
    ChimpRef *self, ChimpRef *name, int64_t *flags);

chimp_bool_t
chimp_symtable_entry_sym_exists (ChimpRef *self, ChimpRef *name);

#define CHIMP_SYMTABLE(ref) \
    CHIMP_CHECK_CAST(ChimpSymtable, (ref), CHIMP_VALUE_TYPE_SYMTABLE)

#define CHIMP_SYMTABLE_ENTRY(ref) \
    CHIMP_CHECK_CAST( \
        ChimpSymtableEntry, (ref), CHIMP_VALUE_TYPE_SYMTABLE_ENTRY)

#define CHIMP_SYMTABLE_ENTRY_CHILDREN(ref) CHIMP_SYMTABLE_ENTRY(ref)->children

CHIMP_EXTERN_CLASS(symtable);
CHIMP_EXTERN_CLASS(symtable_entry);

#ifdef __cplusplus
};
#endif

#endif

