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

#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "chimp/any.h"
#include "chimp/object.h"
#include "chimp/array.h"
#include "chimp/str.h"

#define CHIMP_MODULE_ADD_CLASS(mod, name, klass) \
    do { \
        if ((klass) == NULL) { \
            return NULL; \
        } \
        \
        if (!chimp_module_add_local_str ((mod), (name), (klass))) { \
            return NULL; \
        } \
    } while (0)

typedef struct _ChimpNetSocket {
    ChimpAny base;
    int fd;
} ChimpNetSocket;

static ChimpRef *chimp_net_socket_class = NULL;

#define CHIMP_NET_SOCKET(ref) \
    CHIMP_CHECK_CAST(ChimpNetSocket, (ref), chimp_net_socket_class)

static ChimpRef *
_chimp_socket_init (ChimpRef *self, ChimpRef *args)
{
    int64_t fd;
    int64_t family, type, protocol = 0;
    
    if (CHIMP_ARRAY_SIZE(args) == 3) {
        if (!chimp_method_parse_args (args, "III", &family, &type, &protocol)) {
            return NULL;
        }
        CHIMP_NET_SOCKET(self)->fd =
            socket ((int)family, (int)type, (int)protocol);
        if (CHIMP_NET_SOCKET(self)->fd < 0) {
            CHIMP_BUG ("socket() failed");
            return NULL;
        }
        return self;
    }
    else if (CHIMP_ARRAY_SIZE(args) == 1) {
        if (!chimp_method_parse_args (args, "I", &fd)) {
            return NULL;
        }
        CHIMP_NET_SOCKET(self)->fd = fd;
        return self;
    }
    else {
        return NULL;
    }
}

static ChimpRef *
_chimp_socket_bind (ChimpRef *self, ChimpRef *args)
{
    struct sockaddr_in addr;
    int64_t port;
    int fd;

    if (!chimp_method_parse_args (args, "I", &port)) {
        return NULL;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons ((int)port);
    memset (addr.sin_zero, 0, sizeof(addr.sin_zero));

    fd = CHIMP_NET_SOCKET(self)->fd;

    if (bind (fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        CHIMP_BUG ("bind() failed");
        return NULL;
    }

    return chimp_nil;
}

static ChimpRef *
_chimp_socket_listen (ChimpRef *self, ChimpRef *args)
{
    int64_t backlog = SOMAXCONN;
    int fd = CHIMP_NET_SOCKET(self)->fd;

    if (!chimp_method_parse_args (args, "|I", &backlog)) {
        return NULL;
    }

    if (listen (fd, backlog) < 0) {
        CHIMP_BUG ("listen() failed");
        return NULL;
    }
    return chimp_nil;
}

static ChimpRef *
_chimp_socket_setsockopt (ChimpRef *self, ChimpRef *args)
{
    int32_t level, optname, optval;

    if (!chimp_method_parse_args (args, "iii", &level, &optname, &optval)) {
        return NULL;
    }

    if (setsockopt (CHIMP_NET_SOCKET(self)->fd, level, optname, &optval, sizeof(optval)) != 0) {
        return chimp_false;
    }

    return chimp_true;
}

static ChimpRef *
_chimp_socket_accept (ChimpRef *self, ChimpRef *args)
{
    int fd = CHIMP_NET_SOCKET(self)->fd;
    struct sockaddr_in addr;
    uint32_t addrlen = sizeof(addr);
    int cfd;
    ChimpRef *fd_obj;

    cfd = accept (fd, (struct sockaddr *)&addr, &addrlen);
    if (cfd < 0) {
        CHIMP_BUG ("accept() failed");
        return NULL;
    }

    fd_obj = chimp_int_new (cfd);
    if (fd_obj == NULL) {
        return NULL;
    }
    return chimp_class_new_instance (
            chimp_net_socket_class, fd_obj, NULL);
}

static ChimpRef *
_chimp_socket_close (ChimpRef *self, ChimpRef *args)
{
    if (CHIMP_NET_SOCKET(self)->fd > 0) {
        close (CHIMP_NET_SOCKET(self)->fd);
        CHIMP_NET_SOCKET(self)->fd = -1;
    }
    return chimp_nil;
}

static ChimpRef *
_chimp_socket_shutdown (ChimpRef *self, ChimpRef *args)
{
    int32_t how = SHUT_RDWR;

    if (!chimp_method_parse_args (args, "i", &how)) {
        return NULL;
    }

    if (shutdown (CHIMP_NET_SOCKET (self)->fd, how) != 0) {
        return chimp_false;
    }
    return chimp_true;
}

static ChimpRef *
_chimp_socket_send (ChimpRef *self, ChimpRef *args)
{
    ChimpRef *data;
    int rc;

    if (!chimp_method_parse_args (args, "o", &data)) {
        return NULL;
    }

    rc = send (CHIMP_NET_SOCKET (self)->fd,
                CHIMP_STR_DATA(data), CHIMP_STR_SIZE(data), 0);

    return chimp_int_new (rc);
}

static ChimpRef *
_chimp_socket_recv (ChimpRef *self, ChimpRef *args)
{
    int32_t size;
    char *buf;
    int n;

    if (!chimp_method_parse_args (args, "i", &size)) {
        return NULL;
    }

    buf = malloc (size + 1);
    if (buf == NULL) {
        CHIMP_BUG ("Failed to allocate recv buffer");
        return NULL;
    }

    n = recv (CHIMP_NET_SOCKET (self)->fd, buf, size, 0);
    /* XXX can't distinguish between an error and an EOF atm */
    if (n <= 0) {
        return CHIMP_STR_NEW ("");
    }

    if (n < size) {
        char *temp = realloc (buf, n + 1);
        if (temp == NULL) {
            CHIMP_BUG ("Failed to shrink recv buffer");
            return NULL;
        }
        buf = temp;
    }
    buf[n] = '\0';

    return chimp_str_new_take (buf, n);
}

static ChimpRef *
_chimp_socket_getattr (ChimpRef *self, ChimpRef *attr)
{
    if (strcmp ("fd", CHIMP_STR_DATA(attr)) == 0) {
        return chimp_int_new (CHIMP_NET_SOCKET (self)->fd);
    }
    else {
        ChimpRef *super = CHIMP_CLASS_SUPER(CHIMP_ANY_CLASS(self));
        return CHIMP_CLASS(super)->getattr (self, attr);
    }
}

static void
_chimp_socket_dtor (ChimpRef *self)
{
#if 0
    _chimp_socket_close (self, NULL);
#endif
}

static chimp_bool_t
_chimp_socket_class_bootstrap (void)
{
    if (chimp_net_socket_class == NULL) {
        ChimpRef *net_socket_class;
        chimp_net_socket_class = chimp_class_new (
            CHIMP_STR_NEW("net.socket"), NULL, sizeof(ChimpNetSocket));
        if (chimp_net_socket_class == NULL) {
            return CHIMP_FALSE;
        }

        /* protect net.socket from the GC */
        net_socket_class = chimp_net_socket_class;

        CHIMP_CLASS(net_socket_class)->init = _chimp_socket_init;
        CHIMP_CLASS(net_socket_class)->dtor = _chimp_socket_dtor;
        CHIMP_CLASS(net_socket_class)->getattr = _chimp_socket_getattr;

        if (!chimp_class_add_native_method (
                net_socket_class, "close", _chimp_socket_close)) {
            return CHIMP_FALSE;
        }

        if (!chimp_class_add_native_method (
                net_socket_class, "bind", _chimp_socket_bind)) {
            return CHIMP_FALSE;
        }

        if (!chimp_class_add_native_method (
                net_socket_class, "setsockopt", _chimp_socket_setsockopt)) {
            return CHIMP_FALSE;
        }

        if (!chimp_class_add_native_method (
                net_socket_class, "listen", _chimp_socket_listen)) {
            return CHIMP_FALSE;
        }

        if (!chimp_class_add_native_method (
                net_socket_class, "accept", _chimp_socket_accept)) {
            return CHIMP_FALSE;
        }

        if (!chimp_class_add_native_method (
                net_socket_class, "recv", _chimp_socket_recv)) {
            return CHIMP_FALSE;
        }

        if (!chimp_class_add_native_method (
                net_socket_class, "send", _chimp_socket_send)) {
            return CHIMP_FALSE;
        }

        if (!chimp_class_add_native_method (
                net_socket_class, "shutdown", _chimp_socket_shutdown)) {
            return CHIMP_FALSE;
        }
    }
    return CHIMP_TRUE;
}

ChimpRef *
chimp_init_net_module (void)
{
    ChimpRef *net;
    ChimpRef *net_socket;

    net = chimp_module_new_str ("net", NULL);
    if (net == NULL) {
        return NULL;
    }

    CHIMP_MODULE_INT_CONSTANT(net, "AF_INET", AF_INET);
    CHIMP_MODULE_INT_CONSTANT(net, "SOCK_DGRAM", SOCK_DGRAM);
    CHIMP_MODULE_INT_CONSTANT(net, "SOCK_STREAM", SOCK_STREAM);
    CHIMP_MODULE_INT_CONSTANT(net, "SOL_SOCKET", SOL_SOCKET);
    CHIMP_MODULE_INT_CONSTANT(net, "SO_REUSEADDR", SO_REUSEADDR);
    CHIMP_MODULE_INT_CONSTANT(net, "SHUT_RD", SHUT_RD);
    CHIMP_MODULE_INT_CONSTANT(net, "SHUT_WR", SHUT_WR);
    /* XXX adding this seems to trigger a GC bug */
    CHIMP_MODULE_INT_CONSTANT(net, "SHUT_RDWR", SHUT_RDWR);

    if (!_chimp_socket_class_bootstrap ()) {
        return NULL;
    }

    /* protect net.socket from the garbage collector */
    net_socket = chimp_net_socket_class;

    CHIMP_MODULE_ADD_CLASS(net, "socket", net_socket);

    return net;
}

