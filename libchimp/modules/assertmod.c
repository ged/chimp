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

#include "chimp/any.h"
#include "chimp/object.h"
#include "chimp/array.h"
#include "chimp/str.h"
#include "chimp/vm.h"

static ChimpRef *
_chimp_assert_equal (ChimpRef *self, ChimpRef *args)
{
    ChimpCmpResult r;
    ChimpRef *left = CHIMP_ARRAY_ITEM(args, 0);
    ChimpRef *right = CHIMP_ARRAY_ITEM(args, 1);

    r = chimp_object_cmp (left, right);

    if (r == CHIMP_CMP_ERROR) {
        return NULL;
    }
    if (r != CHIMP_CMP_EQ) {
        chimp_vm_panic (
            NULL,
            chimp_str_new_format (
                NULL,
                "expected %s to be not equal to %s",
                CHIMP_STR_DATA(left),
                CHIMP_STR_DATA(right))
        );
        return NULL;
    }
    else {
        return chimp_nil;
    }
}

static ChimpRef *
_chimp_assert_not_equal (ChimpRef *self, ChimpRef *args)
{
    ChimpCmpResult r;
    ChimpRef *left = CHIMP_ARRAY_ITEM(args, 0);
    ChimpRef *right = CHIMP_ARRAY_ITEM(args, 1);

    r = chimp_object_cmp (left, right);

    if (r == CHIMP_CMP_ERROR) {
        return NULL;
    }
    if (r == CHIMP_CMP_EQ) {
        chimp_vm_panic (
            NULL,
            chimp_str_new_format (
                NULL,
                "expected %s to be not equal to %s",
                CHIMP_STR_DATA(left),
                CHIMP_STR_DATA(right))
        );
        return NULL;
    }
    else {
        return chimp_nil;
    }
}

ChimpRef *
chimp_init_assert_module (void)
{
    ChimpRef *mod;

    mod = chimp_module_new_str ("assert", NULL);
    if (mod == NULL) {
        return NULL;
    }

    if (!chimp_module_add_method_str (mod, "eq", _chimp_assert_equal)) {
        return NULL;
    }

    if (!chimp_module_add_method_str (mod, "neq", _chimp_assert_not_equal)) {
        return NULL;
    }

    return mod;
}

