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
#include "chimp/compile.h"

// XXX -- Hacky, but for now can't compile in multiple tasks at once.
// Get around it by just locking on our runtime compiler.
static pthread_mutex_t compiler_lock;

ChimpRef *
_chimp_compiler_compile (ChimpRef *self, ChimpRef *args)
{
    pthread_mutex_lock (&compiler_lock);

    ChimpRef *filename = CHIMP_ARRAY_ITEM(args, 0);
    ChimpRef *module = CHIMP_COMPILE_MODULE_FROM_FILE (NULL, CHIMP_STR_DATA(filename));
    if (module == NULL) {
        fprintf (stderr, "error: failed to compile %s\n", CHIMP_STR_DATA(filename));
        module = chimp_nil;
    }

    pthread_mutex_unlock (&compiler_lock);
    return module;
}

ChimpRef *
chimp_init_compiler_module (void)
{
    if (pthread_mutex_init (&compiler_lock, NULL) != 0) {
      return NULL;
    }

    ChimpRef *mod;

    mod = chimp_module_new_str ("compiler", NULL);
    if (mod == NULL) {
        return NULL;
    }

    if (!chimp_module_add_method_str (mod, "compile", _chimp_compiler_compile)) {
        return NULL;
    }

    return mod;
}

