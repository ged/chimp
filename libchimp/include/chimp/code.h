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

#ifndef _CHIMP_CODE_H_INCLUDED_
#define _CHIMP_CODE_H_INCLUDED_

#include <chimp/gc.h>
#include <chimp/any.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _ChimpOpcode {
    CHIMP_OPCODE_JUMPIFFALSE,
    CHIMP_OPCODE_JUMPIFTRUE,
    CHIMP_OPCODE_JUMP,
    CHIMP_OPCODE_PUSHCONST,
    CHIMP_OPCODE_STORENAME,
    CHIMP_OPCODE_PUSHNAME,
    CHIMP_OPCODE_PUSHNIL,
    CHIMP_OPCODE_GETATTR,
    CHIMP_OPCODE_GETITEM,
    CHIMP_OPCODE_CALL,
    CHIMP_OPCODE_MAKEARRAY,
    CHIMP_OPCODE_MAKEHASH,

    CHIMP_OPCODE_CMPEQ,
    CHIMP_OPCODE_CMPNEQ,
    CHIMP_OPCODE_CMPGT,
    CHIMP_OPCODE_CMPGTE,
    CHIMP_OPCODE_CMPLT,
    CHIMP_OPCODE_CMPLTE,

    CHIMP_OPCODE_NOT,
    CHIMP_OPCODE_DUP,

    CHIMP_OPCODE_POP,

    CHIMP_OPCODE_RET,

    CHIMP_OPCODE_ADD,
    CHIMP_OPCODE_SUB,
    CHIMP_OPCODE_MUL,
    CHIMP_OPCODE_DIV,
    
    CHIMP_OPCODE_MAKECLOSURE,

    /* XXX temporary until we get a better way to do it */
    CHIMP_OPCODE_GETCLASS
} ChimpOpcode;

typedef enum _ChimpBinopType {
    CHIMP_BINOP_EQ,
    CHIMP_BINOP_NEQ,
    CHIMP_BINOP_OR,
    CHIMP_BINOP_GT,
    CHIMP_BINOP_GTE,
    CHIMP_BINOP_LT,
    CHIMP_BINOP_LTE,
    CHIMP_BINOP_AND,
    CHIMP_BINOP_ADD,
    CHIMP_BINOP_SUB,
    CHIMP_BINOP_MUL,
    CHIMP_BINOP_DIV
} ChimpBinopType;

typedef struct _ChimpCode {
    ChimpAny base;
    ChimpRef *constants;
    ChimpRef *names;
    uint32_t *bytecode;
    size_t    used;
    size_t    allocated;
    ChimpRef *vars;
    ChimpRef *freevars;
} ChimpCode;

typedef struct _ChimpLabel {
    size_t      *patchlist;
    size_t       patchlist_size;
    size_t       addr;
    chimp_bool_t in_use;
} ChimpLabel;

chimp_bool_t
chimp_code_class_bootstrap (void);

ChimpRef *
chimp_code_new (void);

chimp_bool_t
chimp_code_pushconst (ChimpRef *self, ChimpRef *value);

chimp_bool_t
chimp_code_pushname (ChimpRef *self, ChimpRef *id);

chimp_bool_t
chimp_code_pushnil (ChimpRef *self);

chimp_bool_t
chimp_code_storename (ChimpRef *self, ChimpRef *id);

chimp_bool_t
chimp_code_getclass (ChimpRef *self);

chimp_bool_t
chimp_code_getattr (ChimpRef *self, ChimpRef *id);

chimp_bool_t
chimp_code_getitem (ChimpRef *self);

chimp_bool_t
chimp_code_call (ChimpRef *self, uint8_t nargs);

chimp_bool_t
chimp_code_ret (ChimpRef *self);

chimp_bool_t
chimp_code_not (ChimpRef *self);

chimp_bool_t
chimp_code_dup (ChimpRef *self);

chimp_bool_t
chimp_code_makearray (ChimpRef *self, uint8_t nargs);

chimp_bool_t
chimp_code_makehash (ChimpRef *self, uint8_t nargs);

chimp_bool_t
chimp_code_makeclosure (ChimpRef *self);

chimp_bool_t
chimp_code_jumpiftrue (ChimpRef *self, ChimpLabel *label);

chimp_bool_t
chimp_code_jumpiffalse (ChimpRef *self, ChimpLabel *label);

chimp_bool_t
chimp_code_jump (ChimpRef *self, ChimpLabel *label);

chimp_bool_t
chimp_code_eq (ChimpRef *self);

chimp_bool_t
chimp_code_neq (ChimpRef *self);

chimp_bool_t
chimp_code_gt (ChimpRef *self);

chimp_bool_t
chimp_code_gte (ChimpRef *self);

chimp_bool_t
chimp_code_lt (ChimpRef *self);

chimp_bool_t
chimp_code_lte (ChimpRef *self);

chimp_bool_t
chimp_code_add (ChimpRef *self);

chimp_bool_t
chimp_code_sub (ChimpRef *self);

chimp_bool_t
chimp_code_mul (ChimpRef *self);

chimp_bool_t
chimp_code_div (ChimpRef *self);

chimp_bool_t
chimp_code_pop (ChimpRef *self);

ChimpRef *
chimp_code_dump (ChimpRef *self);

void
chimp_code_use_label (ChimpRef *self, ChimpLabel *label);

void
chimp_label_free (ChimpLabel *self);

#define CHIMP_LABEL_INIT { NULL, 0, 0, CHIMP_FALSE }

#define CHIMP_CODE(ref)  CHIMP_CHECK_CAST(ChimpCode, (ref), CHIMP_VALUE_TYPE_CODE)

#define CHIMP_CODE_INSTR(ref, n) CHIMP_CODE(ref)->bytecode[n]

#define CHIMP_INSTR_OP(ref, n) ((ChimpOpcode)((CHIMP_CODE_INSTR(ref, n) & 0xff000000) >> 24))
#define CHIMP_INSTR_ADDR(ref, n) (CHIMP_CODE_INSTR(ref, n) & 0x00ffffff)

#define CHIMP_INSTR_ARG1(ref, n) ((CHIMP_CODE_INSTR(ref, n) & 0x00ff0000) >> 16)
#define CHIMP_INSTR_ARG2(ref, n) ((CHIMP_CODE_INSTR(ref, n) & 0x0000ff00) >> 8)
#define CHIMP_INSTR_ARG3(ref, n) ((CHIMP_CODE_INSTR(ref, n) & 0x000000ff))

/* constants */

#define CHIMP_INSTR_CONST1(ref, n) \
    CHIMP_ARRAY_ITEM(CHIMP_CODE_CONSTANTS(ref), CHIMP_INSTR_ARG1(ref, n))

#define CHIMP_INSTR_CONST2(ref, n) \
    CHIMP_ARRAY_ITEM(CHIMP_CODE_CONSTANTS(ref), CHIMP_INSTR_ARG2(ref, n))

#define CHIMP_INSTR_CONST3(ref, n) \
    CHIMP_ARRAY_ITEM(CHIMP_CODE_CONSTANTS(ref), CHIMP_INSTR_ARG3(ref, n))

/* names */

#define CHIMP_INSTR_NAME1(ref, n) \
    CHIMP_ARRAY_ITEM(CHIMP_CODE_NAMES(ref), CHIMP_INSTR_ARG1(ref, n))

#define CHIMP_INSTR_NAME2(ref, n) \
    CHIMP_ARRAY_ITEM(CHIMP_CODE_NAMES(ref), CHIMP_INSTR_ARG2(ref, n))

#define CHIMP_INSTR_NAME3(ref, n) \
    CHIMP_ARRAY_ITEM(CHIMP_CODE_NAMES(ref), CHIMP_INSTR_ARG3(ref, n))

#define CHIMP_CODE_SIZE(ref) CHIMP_CODE(ref)->used
#define CHIMP_CODE_CONSTANTS(ref) CHIMP_CODE(ref)->constants
#define CHIMP_CODE_NAMES(ref) CHIMP_CODE(ref)->names

#define CHIMP_DEBUG_INSPECT(obj) CHIMP_STR_DATA(chimp_object_str(obj))

#define CHIMP_DEBUG_CLASS_NAME(klass) \
    CHIMP_STR_DATA(CHIMP_CLASS(klass)->name)

#define CHIMP_DEBUG_SUPERCLASS_NAME(klass) \
    CHIMP_DEBUG_CLASS_NAME(CHIMP_CLASS(klass)->super)

CHIMP_EXTERN_CLASS(code);

#ifdef __cplusplus
};
#endif

#endif

