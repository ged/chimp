#include <sys/stat.h>

#include "chimp/module_mgr.h"
#include "chimp/object.h"
#include "chimp/compile.h"
#include "chimp/array.h"
#include "chimp/str.h"
#include "chimp/task.h"
#include "chimp/modules.h"

static ChimpRef *module_mgr_task = NULL;
static ChimpRef *func = NULL;
static ChimpRef *cache = NULL;
static ChimpRef *builtins = NULL;

#define IS_MODULE_MGR_TASK() \
    (chimp_task_current() == CHIMP_TASK(module_mgr_task)->priv)

static chimp_bool_t
_chimp_module_mgr_add_builtin (ChimpRef *module)
{
    if (!chimp_hash_put (
            builtins, CHIMP_MODULE_NAME(module), module)) {
        return CHIMP_FALSE;
    }
    return CHIMP_TRUE;
}

static ChimpRef *
_chimp_module_mgr_compile (
    ChimpRef *name, ChimpRef *filename, chimp_bool_t check)
{
    ChimpRef *mod;
    
    if (check) {
        if (chimp_hash_get (cache, name, NULL) == 0) {
            CHIMP_BUG ("attempt to bind the same module twice: %s",
                    CHIMP_STR_DATA(name));
            return NULL;
        }
    }

    mod = chimp_compile_file (name, CHIMP_STR_DATA(filename));
    if (mod == NULL) {
        return NULL;
    }
    if (!chimp_hash_put (cache, name, mod)) {
        return NULL;
    }
    return mod;
}

static ChimpRef *
_chimp_module_mgr_load (ChimpRef *name, ChimpRef *path)
{
    size_t i;
    int rc;
    ChimpRef *mod;

    /* TODO circular references ... ? */

    rc = chimp_hash_get (cache, name, &mod);
    if (rc < 0) {
        CHIMP_BUG ("error looking up module %s in cache",
                CHIMP_STR_DATA(name));
        return NULL;
    }
    else if (rc == 0) {
        return mod;
    }

    if (path != NULL) {
        for (i = 0; i < CHIMP_ARRAY_SIZE(path); i++) {
            ChimpRef *element = CHIMP_ARRAY_ITEM(path, i);
            ChimpRef *filename = chimp_str_new_format (
                "%s/%s.chimp", CHIMP_STR_DATA(element), CHIMP_STR_DATA(name));
            struct stat st;

            if (stat (CHIMP_STR_DATA(filename), &st) != 0) {
                continue;
            }

            if (S_ISREG(st.st_mode)) {
                return _chimp_module_mgr_compile (name, filename, CHIMP_FALSE);
            }
        }
    }

    rc = chimp_hash_get (builtins, name, &mod);

    if (rc == 0) {
        if (!chimp_hash_put (cache, name, mod)) {
            return NULL;
        }
        return mod;
    }
    else if (rc == 1) {
        CHIMP_BUG ("module not found: %s", CHIMP_STR_DATA(name));
        return NULL;
    }
    else {
        CHIMP_BUG ("bad key in builtin_modules?");
        return NULL;
    }
}

static ChimpRef *
_chimp_module_mgr_func (ChimpRef *self, ChimpRef *args)
{
    cache = chimp_hash_new ();
    if (cache == NULL) {
        return NULL;
    }
    chimp_gc_make_root (NULL, cache);
    builtins = chimp_hash_new ();
    if (builtins == NULL) {
        return NULL;
    }
    chimp_gc_make_root (NULL, builtins);

    if (!_chimp_module_mgr_add_builtin (chimp_init_io_module ())) {
        return CHIMP_FALSE;
    }

    if (!_chimp_module_mgr_add_builtin (chimp_init_assert_module ())) {
        return CHIMP_FALSE;
    }

    if (!_chimp_module_mgr_add_builtin (chimp_init_unit_module ())) {
        return CHIMP_FALSE;
    }

    if (!_chimp_module_mgr_add_builtin (chimp_init_os_module ())) {
        return CHIMP_FALSE;
    }

    if (!_chimp_module_mgr_add_builtin (chimp_init_gc_module ())) {
        return CHIMP_FALSE;
    }

    if (!_chimp_module_mgr_add_builtin (chimp_init_net_module ())) {
        return CHIMP_FALSE;
    }

    if (!_chimp_module_mgr_add_builtin (chimp_init_http_module ())) {
        return CHIMP_FALSE;
    }

#ifdef CHIMP_WITH_SDL
    if (!_chimp_module_mgr_add_builtin (chimp_init_sdl_module ())) {
        return CHIMP_FALSE;
    }
#endif

    if (!chimp_task_send (
            CHIMP_ARRAY_ITEM(args, 0), CHIMP_STR_NEW ("ready"))) {
        return NULL;
    }

    for (;;) {
        ChimpRef *msg = chimp_task_recv (NULL);
        if (msg == NULL) {
            return NULL;
        }

        if (CHIMP_ANY_CLASS(msg) == chimp_array_class) {
            ChimpRef *action = CHIMP_ARRAY_ITEM(msg, 0);

            if (CHIMP_ANY_CLASS(action) != chimp_str_class) {
                CHIMP_BUG ("module managed received unexpected action: %s",
                    CHIMP_STR_DATA(chimp_object_str (action)));
                return NULL;
            }

            if (strcmp (CHIMP_STR_DATA(action), "load") == 0) {
                ChimpRef *sender = CHIMP_ARRAY_ITEM(msg, 1);
                ChimpRef *name = CHIMP_ARRAY_ITEM(msg, 2);
                ChimpRef *mod;
                mod = _chimp_module_mgr_load (name, chimp_module_path);
                if (mod == NULL) {
                    return NULL;
                }
                if (!chimp_task_send (sender, mod)) {
                    return NULL;
                }
            }
            else if (strcmp (CHIMP_STR_DATA(action), "compile") == 0) {
                ChimpRef *sender = CHIMP_ARRAY_ITEM(msg, 1);
                ChimpRef *name = CHIMP_ARRAY_ITEM(msg, 2);
                ChimpRef *filename = CHIMP_ARRAY_ITEM(msg, 3);
                ChimpRef *mod =
                    _chimp_module_mgr_compile (name, filename, CHIMP_TRUE);
                if (mod == NULL) {
                    return NULL;
                }
                if (!chimp_task_send (sender, mod)) {
                    return NULL;
                }
            }
            else if (strcmp (CHIMP_STR_DATA(action), "exit") == 0) {
                break;
            }
            else {
                CHIMP_BUG ("module manager received unknown command: %s",
                    CHIMP_STR_DATA(chimp_object_str (action)));
                return NULL;
            }
        }
        else {
            CHIMP_BUG ("module manager received unexpected message: %s",
                CHIMP_STR_DATA(chimp_object_str (msg)));
            return NULL;
        }
    }

    return chimp_nil;
}

chimp_bool_t
chimp_module_mgr_init (void)
{
    ChimpRef *self = chimp_task_get_self (chimp_task_current ());
    func = chimp_method_new_native (NULL, _chimp_module_mgr_func);
    if (func == NULL) {
        return CHIMP_FALSE;
    }
    chimp_gc_make_root (NULL, func);
    module_mgr_task = chimp_task_new (func);
    if (module_mgr_task == NULL) {
        return CHIMP_FALSE;
    }
    chimp_gc_make_root (NULL, module_mgr_task);

    /* send args */
    if (!chimp_task_send (
            module_mgr_task, chimp_array_new_var (self, NULL))) {
        return CHIMP_FALSE;
    }
    /* XXX wait to be notified the module mgr task is ready */
    if (!chimp_task_recv (NULL)) {
        return CHIMP_FALSE;
    }

    return CHIMP_TRUE;
}

void
chimp_module_mgr_shutdown (void)
{
    if (!chimp_task_send (
            module_mgr_task,
            chimp_array_new_var (CHIMP_STR_NEW ("exit"), NULL))) {
        CHIMP_BUG ("failed to shutdown module manager task");
    }
    chimp_task_join (CHIMP_TASK(module_mgr_task)->priv);

    module_mgr_task = NULL;
    func = NULL;
}

static ChimpRef *
_chimp_module_mgr_send_cmd (
    const char *action, size_t actionlen, ...)
{
    va_list args;
    size_t num_args = 0;
    ChimpRef *cmd;
    ChimpRef *arg;
    ChimpRef *self = chimp_task_get_self (chimp_task_current ());
    
    va_start (args, actionlen);
    while ((arg = va_arg (args, ChimpRef *)) != NULL) {
        num_args++;
    }
    va_end (args);

    cmd = chimp_array_new_with_capacity (2 + num_args);
    if (cmd == NULL) {
        return NULL;
    }

    if (!chimp_array_push (cmd, chimp_str_new (action, actionlen))) {
        return NULL;
    }
    if (!chimp_array_push (cmd, self)) {
        return NULL;
    }

    va_start (args, actionlen);
    while ((arg = va_arg (args, ChimpRef *)) != NULL) {
        if (!chimp_array_push (cmd, arg)) {
            return NULL;
        }
    }
    va_end (args);

    if (!chimp_task_send (module_mgr_task, cmd)) {
        CHIMP_BUG ("failed to send %s request to module manager", action);
        return NULL;
    }

    return chimp_task_recv (NULL);
}

ChimpRef *
chimp_module_mgr_load (ChimpRef *name)
{
    if (IS_MODULE_MGR_TASK ()) {
        return _chimp_module_mgr_load (name, chimp_module_path);
    }
    else {
        return _chimp_module_mgr_send_cmd ("load", sizeof("load")-1, name, NULL);
    }
}

ChimpRef *
chimp_module_mgr_compile (ChimpRef *name, ChimpRef *filename)
{
    if (IS_MODULE_MGR_TASK ()) {
        return _chimp_module_mgr_compile (name, filename, CHIMP_TRUE);
    }
    else {
        return _chimp_module_mgr_send_cmd (
                "compile", sizeof("compile")-1, name, filename, NULL);
    }
}

