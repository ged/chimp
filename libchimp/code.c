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

#include "chimp/code.h"
#include "chimp/str.h"
#include "chimp/class.h"
#include "chimp/array.h"
#include "chimp/int.h"

ChimpRef *chimp_code_class = NULL;

void
chimp_code_use_label (ChimpRef *self, ChimpLabel *label)
{
    if (!label->in_use) {
        size_t i;
        label->addr = CHIMP_CODE(self)->used;
        label->in_use = CHIMP_TRUE;
        for (i = 0; i < label->patchlist_size; i++) {
            CHIMP_CODE(self)->bytecode[label->patchlist[i]] |=
                                            (label->addr & 0xffffff);
        }
        chimp_label_free (label);
    }
    else {
        CHIMP_BUG ("Label is already in use\n");
    }
}

void
chimp_label_free (ChimpLabel *label)
{
    if (label->patchlist != NULL) {
        free (label->patchlist);
        label->patchlist = NULL;
    }
}

static chimp_bool_t
chimp_code_get_jump_addr (ChimpRef *self, ChimpLabel *label, size_t *dest_addr)
{
    if (label->in_use) {
        *dest_addr = label->addr;
    }
    else {
        const size_t size =
            sizeof(*label->patchlist) * (label->patchlist_size + 1);
        size_t *patchlist = realloc (label->patchlist, size);
        if (patchlist == NULL) {
            return CHIMP_FALSE;
        }
        label->patchlist = patchlist;
        label->patchlist[label->patchlist_size++] = CHIMP_CODE(self)->used;
        *dest_addr = 0;
    }
    return CHIMP_TRUE;
}

static void
_chimp_code_dtor (ChimpRef *self)
{
    CHIMP_FREE (CHIMP_CODE(self)->bytecode);
}

chimp_bool_t
chimp_code_class_bootstrap (void)
{
    chimp_code_class =
        chimp_class_new (CHIMP_STR_NEW("code"), NULL);
    if (chimp_code_class == NULL) {
        return CHIMP_FALSE;
    }
    CHIMP_CLASS(chimp_code_class)->dtor = _chimp_code_dtor;
    chimp_gc_make_root (NULL, chimp_code_class);
    return CHIMP_TRUE;
}

ChimpRef *
chimp_code_new (void)
{
    ChimpRef *ref = chimp_gc_new_object (NULL);
    ChimpRef *temp;
    if (ref == NULL) {
        return NULL;
    }
    CHIMP_ANY(ref)->type = CHIMP_VALUE_TYPE_CODE;
    CHIMP_ANY(ref)->klass = chimp_code_class;
    temp = chimp_array_new ();
    if (temp == NULL) {
        CHIMP_BUG ("wtf");
        return NULL;
    }
    CHIMP_CODE(ref)->constants = temp;
    temp = chimp_array_new ();
    if (temp == NULL) {
        CHIMP_BUG ("wtf");
        return NULL;
    }
    CHIMP_CODE(ref)->names = temp;
    CHIMP_CODE(ref)->allocated = 256;
    CHIMP_CODE(ref)->bytecode = CHIMP_MALLOC(uint32_t, sizeof(uint32_t) * CHIMP_CODE(ref)->allocated);
    if (CHIMP_CODE(ref)->bytecode == NULL) {
        return NULL;
    }
    temp = chimp_array_new ();
    if (temp == NULL) {
        CHIMP_BUG ("could not allocate vars array");
        return NULL;
    }
    CHIMP_CODE(ref)->vars = temp;
    temp = chimp_array_new ();
    if (temp == NULL) {
        CHIMP_BUG ("could not allocate freevars array");
        return NULL;
    }
    CHIMP_CODE(ref)->freevars = temp;
    return ref;
}

#define CHIMP_CURR_INSTR(co) CHIMP_CODE(co)->bytecode[CHIMP_CODE(co)->used]
#define CHIMP_NEXT_INSTR(co) CHIMP_CODE(co)->bytecode[CHIMP_CODE(co)->used++]

#define CHIMP_JUMP_INSTR(co, op, label) \
    do { \
        size_t jump_addr; \
        if (!chimp_code_get_jump_addr ((co), (label), &jump_addr)) { \
            chimp_label_free (label); \
            return CHIMP_FALSE; \
        } \
        CHIMP_NEXT_INSTR(co) = \
            CHIMP_MAKE_INSTR0(op) | (jump_addr & 0xffffff); \
    } while (0)

#define CHIMP_MAKE_INSTR0(op) \
    (((CHIMP_OPCODE_ ## op) & 0xff) << 24)

#define CHIMP_MAKE_INSTR1(op, arg1) \
    ((((CHIMP_OPCODE_ ## op) & 0xff) << 24) | (((arg1) & 0xff) << 16))

#define CHIMP_MAKE_INSTR2(op, arg1, arg2) \
    ((((CHIMP_OPCODE_ ## op) & 0xff) << 24) | (((arg1) & 0xff) << 16) | (((arg2) & 0xff) << 8))

#define CHIMP_MAKE_INSTR3(op, arg1, arg2, arg3) \
    ((((CHIMP_OPCODE_ ## op) & 0xff) << 24) | (((arg1) & 0xff) << 16) | (((arg2) & 0xff) << 8) | ((arg3) & 0xff))

static chimp_bool_t
chimp_code_grow (ChimpRef *self)
{
    uint32_t *bytecode;
    if (CHIMP_CODE(self)->used < CHIMP_CODE(self)->allocated) {
        return CHIMP_TRUE;
    }
    bytecode = CHIMP_REALLOC(uint32_t, CHIMP_CODE(self)->bytecode, sizeof(uint32_t) * (CHIMP_CODE(self)->allocated + 32));
    if (bytecode == NULL) {
        return CHIMP_FALSE;
    }
    CHIMP_CODE(self)->bytecode = bytecode;
    CHIMP_CODE(self)->allocated += 32;
    return CHIMP_TRUE;
}

static int32_t
chimp_code_add_const (ChimpRef *self, ChimpRef *value)
{
    int32_t n = chimp_array_find (CHIMP_CODE(self)->constants, value);
    if (n >= 0) {
        return n;
    }
    if (!chimp_array_push (CHIMP_CODE(self)->constants, value)) {
        return -1;
    }
    return CHIMP_ARRAY_SIZE(CHIMP_CODE(self)->constants) - 1;
}

static int32_t
chimp_code_add_name (ChimpRef *self, ChimpRef *id)
{
    int32_t n = chimp_array_find (CHIMP_CODE(self)->names, id);
    if (n >= 0) {
        return n;
    }
    if (!chimp_array_push (CHIMP_CODE(self)->names, id)) {
        return -1;
    }
    return CHIMP_ARRAY_SIZE(CHIMP_CODE(self)->names) - 1;
}

chimp_bool_t
chimp_code_pushconst (ChimpRef *self, ChimpRef *value)
{
    int32_t arg;
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }
    arg = chimp_code_add_const (self, value);
    if (arg < 0) {
        return CHIMP_FALSE;
    }
    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR1(PUSHCONST, arg);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_pushname (ChimpRef *self, ChimpRef *id)
{
    int32_t arg;
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }
    arg = chimp_code_add_name (self, id);
    if (arg < 0) {
        return CHIMP_FALSE;
    }
    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR1(PUSHNAME, arg);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_pushnil (ChimpRef *self)
{
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }
    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR0(PUSHNIL);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_storename (ChimpRef *self, ChimpRef *id)
{
    int32_t arg;
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }

    arg = chimp_code_add_name (self, id);
    if (arg < 0) {
        return CHIMP_FALSE;
    }

    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR1(STORENAME, arg);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_getclass (ChimpRef *self)
{
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }

    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR0(GETCLASS);

    return CHIMP_TRUE;
}


chimp_bool_t
chimp_code_getattr (ChimpRef *self, ChimpRef *id)
{
    int32_t arg;
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }

    arg = chimp_code_add_name (self, id);
    if (arg < 0) {
        return CHIMP_FALSE;
    }

    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR1(GETATTR, arg);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_getitem (ChimpRef *self)
{
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }

    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR0(GETITEM);

    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_call (ChimpRef *self, uint8_t nargs)
{
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }
    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR1(CALL, (int32_t)nargs);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_ret (ChimpRef *self)
{
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }
    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR0(RET);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_not (ChimpRef *self)
{
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }
    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR0(NOT);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_dup (ChimpRef *self)
{
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }
    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR0(DUP);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_makearray (ChimpRef *self, uint8_t nargs)
{
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }
    /* XXX this cast should happen automatically in the make_instr macro */
    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR1(MAKEARRAY, (int32_t)nargs);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_makehash (ChimpRef *self, uint8_t nargs)
{
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }
    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR1(MAKEHASH, (int32_t)nargs);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_makeclosure (ChimpRef *self)
{
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }
    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR0(MAKECLOSURE);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_jumpiftrue (ChimpRef *self, ChimpLabel *label)
{
    if (label == NULL) {
        CHIMP_BUG ("jump labels can't be null, fool.");
        return CHIMP_FALSE;
    }

    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }

    CHIMP_JUMP_INSTR(self, JUMPIFTRUE, label);

    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_jumpiffalse (ChimpRef *self, ChimpLabel *label)
{
    if (label == NULL) {
        CHIMP_BUG ("jump labels can't be null, fool.");
        return CHIMP_FALSE;
    }

    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }

    CHIMP_JUMP_INSTR(self, JUMPIFFALSE, label);

    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_jump (ChimpRef *self, ChimpLabel *label)
{
    if (label == NULL) {
        CHIMP_BUG ("jump labels can't be null, fool.");
        return CHIMP_FALSE;
    }

    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }

    CHIMP_JUMP_INSTR(self, JUMP, label);

    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_eq (ChimpRef *self)
{
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }

    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR0(CMPEQ);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_neq (ChimpRef *self)
{
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }

    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR0(CMPNEQ);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_gt (ChimpRef *self)
{
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }

    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR0(CMPGT);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_gte (ChimpRef *self)
{
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }

    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR0(CMPGTE);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_lt (ChimpRef *self)
{
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }

    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR0(CMPLT);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_lte (ChimpRef *self)
{
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }

    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR0(CMPLTE);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_add (ChimpRef *self)
{
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }

    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR0(ADD);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_sub (ChimpRef *self)
{
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }

    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR0(SUB);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_mul (ChimpRef *self)
{
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }

    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR0(MUL);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_div (ChimpRef *self)
{
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }

    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR0(DIV);
    return CHIMP_TRUE;
}

chimp_bool_t
chimp_code_pop (ChimpRef *self)
{
    if (!chimp_code_grow (self)) {
        return CHIMP_FALSE;
    }

    CHIMP_NEXT_INSTR(self) = CHIMP_MAKE_INSTR0(POP);
    return CHIMP_TRUE;
}

static const char *
chimp_code_opcode_str (ChimpOpcode op)
{
    switch (op) {
        case CHIMP_OPCODE_JUMPIFFALSE:
            return "JUMP_IF_FALSE";
        case CHIMP_OPCODE_JUMPIFTRUE:
             return "JUMP_IF_TRUE";
        case CHIMP_OPCODE_JUMP:
             return "JUMP";
        case CHIMP_OPCODE_PUSHCONST:
             return "PUSHCONST";
        case CHIMP_OPCODE_STORENAME:
             return "STORENAME";
        case CHIMP_OPCODE_PUSHNAME:
             return "PUSHNAME";
        case CHIMP_OPCODE_PUSHNIL:
             return "PUSHNIL";
        case CHIMP_OPCODE_GETATTR:
             return "GETATTR";
        case CHIMP_OPCODE_GETITEM:
             return "GETITEM";
        case CHIMP_OPCODE_CALL:
             return "CALL";
        case CHIMP_OPCODE_MAKEARRAY:
             return "MAKEARRAY";
        case CHIMP_OPCODE_MAKEHASH:
             return "MAKEHASH";
        case CHIMP_OPCODE_CMPEQ:
             return "CMP_EQ";
        case CHIMP_OPCODE_CMPNEQ:
             return "CMP_NEQ";
        case CHIMP_OPCODE_CMPGT:
             return "CMP_GT";
        case CHIMP_OPCODE_CMPGTE:
             return "CMP_GTE";
        case CHIMP_OPCODE_CMPLT:
             return "CMP_LT";
        case CHIMP_OPCODE_CMPLTE:
             return "CMP_LTE";
        case CHIMP_OPCODE_POP:
             return "POP";
        case CHIMP_OPCODE_RET:
             return "RET";
        case CHIMP_OPCODE_ADD:
             return "ADD";
        case CHIMP_OPCODE_SUB:
             return "SUB";
        case CHIMP_OPCODE_MUL:
             return "MUL";
        case CHIMP_OPCODE_DIV:
             return "DIV";
        case CHIMP_OPCODE_NOT:
             return "NOT";
        case CHIMP_OPCODE_DUP:
             return "DUP";
        case CHIMP_OPCODE_GETCLASS:
             return "GETCLASS";
        case CHIMP_OPCODE_MAKECLOSURE:
             return "MAKECLOSURE";
        default:
             return "???OPCODE???";
    };
}

ChimpRef *
chimp_code_dump (ChimpRef *self)
{
    /* XXX super duper inefficient & sloppy. */
    /*     mostly for diagnostic purposes atm. */
    size_t i;
    ChimpRef *str = CHIMP_STR_NEW ("");
    for (i = 0; i < CHIMP_CODE(self)->used; i++) {
        int32_t instr = CHIMP_CODE_INSTR(self, i);
        ChimpOpcode op = ((ChimpOpcode)((instr & 0xff000000) >> 24));
        const char *op_str = chimp_code_opcode_str (op);
        if (!chimp_str_append (str, chimp_int_new (i))) {
            return NULL;
        }
        if (!chimp_str_append_str (str, " ")) {
            return NULL;
        }
        if (!chimp_str_append_str (str, op_str)) {
            return NULL;
        }
        if (op == CHIMP_OPCODE_PUSHNAME || op == CHIMP_OPCODE_STORENAME || op == CHIMP_OPCODE_GETATTR) {
            if (!chimp_str_append_str (str, " ")) {
                return NULL;
            }
            if (!chimp_str_append (str, CHIMP_INSTR_NAME1(self, i))) {
                return NULL;
            }
        }
        else if (op == CHIMP_OPCODE_PUSHCONST) {
            if (!chimp_str_append_str (str, " ")) {
                return NULL;
            }
            if (!chimp_str_append (str, CHIMP_INSTR_CONST1(self, i))) {
                return NULL;
            }
        }
        else if (op == CHIMP_OPCODE_MAKEARRAY || op == CHIMP_OPCODE_MAKEHASH || op == CHIMP_OPCODE_CALL) {
            if (!chimp_str_append_str (str, " ")) {
                return NULL;
            }
            if (!chimp_str_append (str, chimp_int_new (CHIMP_INSTR_ARG1(self, i)))) {
                return NULL;
            }
        }
        else if (op == CHIMP_OPCODE_JUMP || op == CHIMP_OPCODE_JUMPIFTRUE || op == CHIMP_OPCODE_JUMPIFFALSE) {
            if (!chimp_str_append_str (str, " ")) {
                return NULL;
            }
            if (!chimp_str_append (str, chimp_int_new ((instr & 0xffffff)))) {
                return NULL;
            }
        }
        if (!chimp_str_append_str (str, "\n")) {
            return NULL;
        }
    }
    return str;
}

