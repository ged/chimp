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

#include "SDL.h"

#include "chimp/any.h"
#include "chimp/object.h"
#include "chimp/array.h"
#include "chimp/str.h"

typedef struct _ChimpSdlSurface {
    ChimpAny    base;
    SDL_Surface *impl;
    int         destroy;
} ChimpSdlSurface;

typedef struct _ChimpSdlRect {
    ChimpAny base;
    union {
        SDL_Rect impl;
        struct {
            Sint16 x, y;
            Uint16 w, h;
        };
    };
} ChimpSdlRect;

typedef struct _ChimpSdlKeyEvent {
    ChimpAny base;
    SDL_Event impl;
    chimp_bool_t down;
} ChimpSdlKeyEvent;

static ChimpRef *chimp_sdl_surface_class = NULL;
static ChimpRef *chimp_sdl_rect_class = NULL;
static ChimpRef *chimp_sdl_key_event_class = NULL;

#define CHIMP_SDL_SURFACE(ref) \
    CHIMP_CHECK_CAST(ChimpSdlSurface, (ref), chimp_sdl_surface_class)

#define CHIMP_SDL_RECT(ref) \
    CHIMP_CHECK_CAST(ChimpSdlRect, (ref), chimp_sdl_rect_class)

#define CHIMP_SDL_KEY_EVENT(ref) \
    CHIMP_CHECK_CAST(ChimpSdlKeyEvent, (ref), chimp_sdl_key_event_class)

static ChimpRef *
_chimp_sdl_set_video_mode (ChimpRef *self, ChimpRef *args)
{
    SDL_Surface *impl;
    ChimpRef *surface;
    Uint32 width, height, depth, flags;
    
    depth = 0;
    flags = SDL_DOUBLEBUF;
    if (!chimp_method_parse_args (
            args, "ii|ii", &width, &height, &depth, &flags)) {
        return NULL;
    }
    impl = SDL_SetVideoMode (width, height, depth, flags);
    if (impl == NULL) {
        CHIMP_BUG ("failed to set video mode to %ux%u", width, height);
        return NULL;
    }
    surface = chimp_class_new_instance (chimp_sdl_surface_class, NULL);
    if (surface == NULL) {
        return NULL;
    }
    CHIMP_SDL_SURFACE(surface)->impl = impl;
    CHIMP_SDL_SURFACE(surface)->destroy = 0;
    return surface;
}

static ChimpRef *
_chimp_sdl_flip (ChimpRef *self, ChimpRef *args)
{
    ChimpRef *screen = chimp_nil;
    SDL_Surface *impl;

    if (!chimp_method_parse_args (args, "|o", &screen)) {
        return NULL;
    }

    if (screen == chimp_nil) {
        impl = SDL_GetVideoSurface ();
    }
    else {
        impl = CHIMP_SDL_SURFACE(screen)->impl;
    }

    return chimp_int_new (SDL_Flip (impl));
}

static ChimpRef *
_chimp_sdl_poll_event (ChimpRef *self, ChimpRef *args)
{
    SDL_Event ev;

try_again:
    if (SDL_PollEvent (&ev)) {
        switch (ev.type) {
            case SDL_KEYUP:
            case SDL_KEYDOWN:
                {
                    ChimpRef *e;
                    e = chimp_class_new_instance (
                            chimp_sdl_key_event_class, NULL);
                    if (e == NULL) {
                        return NULL;
                    }
                    memcpy (&CHIMP_SDL_KEY_EVENT(e)->impl, &ev, sizeof(ev));
                    return e;
                }
            default:
                /* XXX total hack, just until we support all the event types */
                goto try_again;
        };
    }

    return chimp_nil;
}

static ChimpRef *
_chimp_sdl_key_event_init (ChimpRef *self, ChimpRef *args)
{
    return self;
}

static ChimpRef *
_chimp_sdl_key_event_getattr (ChimpRef *self, ChimpRef *attr)
{
    if (strcmp (CHIMP_STR_DATA(attr), "key") == 0) {
        return chimp_int_new (CHIMP_SDL_KEY_EVENT(self)->impl.key.keysym.sym);
    }
    else if (strcmp (CHIMP_STR_DATA(attr), "down") == 0) {
        return CHIMP_SDL_KEY_EVENT(self)->down ? chimp_true : chimp_false;
    }
    else if (strcmp (CHIMP_STR_DATA(attr), "type") == 0) {
        int rv = CHIMP_SDL_KEY_EVENT(self)->down ? SDL_KEYDOWN : SDL_KEYUP;
        return chimp_int_new (rv);
    }
    else {
        return CHIMP_SUPER(self)->getattr (self, attr);
    }
}

static ChimpRef *
_chimp_init_sdl_key_event_class (void)
{
    ChimpRef *c = chimp_class_new(
        CHIMP_STR_NEW("sdl.key_event"), NULL, sizeof(ChimpSdlKeyEvent));
    if (c == NULL) {
        return NULL;
    }
    CHIMP_CLASS(c)->init = _chimp_sdl_key_event_init;
    CHIMP_CLASS(c)->getattr = _chimp_sdl_key_event_getattr;
    return c;
}

static ChimpRef *
_chimp_sdl_rect_init (ChimpRef *self, ChimpRef *args)
{
    int x = 0, y = 0, w = 0, h = 0;

    if (!chimp_method_parse_args (args, "|iiii", &x, &y, &w, &h)) {
        return NULL;
    }

    CHIMP_SDL_RECT(self)->x = x;
    CHIMP_SDL_RECT(self)->y = y;
    CHIMP_SDL_RECT(self)->w = w;
    CHIMP_SDL_RECT(self)->h = h;

    return self;
}

static ChimpRef *
_chimp_sdl_rect_getattr (ChimpRef *self, ChimpRef *attr)
{
    const char *name = CHIMP_STR_DATA(attr);

    if (strcmp (name, "x") == 0) {
        return chimp_int_new (CHIMP_SDL_RECT(self)->x);
    }
    else if (strcmp (name, "y") == 0) {
        return chimp_int_new (CHIMP_SDL_RECT(self)->y);
    }
    else if (strcmp (name, "width") == 0 || strcmp (name, "w") == 0) {
        return chimp_int_new (CHIMP_SDL_RECT(self)->w);
    }
    else if (strcmp (name, "height") == 0 || strcmp (name, "h") == 0) {
        return chimp_int_new (CHIMP_SDL_RECT(self)->h);
    }
    else {
        return CHIMP_SUPER(self)->getattr (self, attr);
    }

}

static ChimpRef *
_chimp_init_sdl_rect_class (void)
{
    ChimpRef *c = chimp_class_new(
        CHIMP_STR_NEW("sdl.rect"), NULL, sizeof(ChimpSdlRect));
    if (c == NULL) {
        return NULL;
    }
    CHIMP_CLASS(c)->init = _chimp_sdl_rect_init;
    CHIMP_CLASS(c)->getattr = _chimp_sdl_rect_getattr;
    return c;
}

static ChimpRef *
_chimp_sdl_surface_init (ChimpRef *self, ChimpRef *args)
{
    return self;
}

static void
_chimp_sdl_surface_dtor (ChimpRef *self)
{
    if (CHIMP_SDL_SURFACE(self)->destroy) {
        SDL_FreeSurface(CHIMP_SDL_SURFACE(self)->impl);
    }
}

static ChimpRef *
_chimp_sdl_surface_fill (ChimpRef *self, ChimpRef *args)
{
    ChimpRef *rect;
    ChimpRef *color = chimp_nil;
    SDL_Rect *prect;

    if (!chimp_method_parse_args (args, "o|o", &rect, &color)) {
        return NULL;
    }

    if (color == chimp_nil) {
        color = rect;
        rect = chimp_nil;
    }

    if (rect == chimp_nil) {
        prect = NULL;
    }
    else {
        prect = &CHIMP_SDL_RECT(rect)->impl;
    }

    SDL_FillRect (
        CHIMP_SDL_SURFACE(self)->impl, prect, (Uint32) CHIMP_INT(color)->value);

    return chimp_nil;
}

static ChimpRef *
_chimp_sdl_surface_color (ChimpRef *self, ChimpRef *args)
{
    int r, g, b, a = -1;
    int rc;

    if (!chimp_method_parse_args (args, "iii|i", &r, &g, &b, &a)) {
        return NULL;
    }

    if (a == -1) {
        rc = SDL_MapRGB(
            CHIMP_SDL_SURFACE(self)->impl->format,
            (Uint8) r, (Uint8) g, (Uint8) b);
    }
    else {
        rc = SDL_MapRGBA(
            CHIMP_SDL_SURFACE(self)->impl->format,
            (Uint8) r, (Uint8) g, (Uint8) b, (Uint8) a);
    }

    return chimp_int_new (rc);
}

static ChimpRef *
_chimp_init_sdl_surface_class (void)
{
    ChimpRef *c = chimp_class_new(
        CHIMP_STR_NEW("sdl.surface"), NULL, sizeof(ChimpSdlSurface));
    if (c == NULL) {
        return NULL;
    }
    CHIMP_CLASS(c)->init = _chimp_sdl_surface_init;
    CHIMP_CLASS(c)->dtor = _chimp_sdl_surface_dtor;
    if (!chimp_class_add_native_method (c, "fill", _chimp_sdl_surface_fill)) {
        return NULL;
    }
    if (!chimp_class_add_native_method (c, "color", _chimp_sdl_surface_color)) {
        return NULL;
    }
    return c;
}

ChimpRef *
chimp_init_sdl_module (void)
{
    ChimpRef *sdl;
    ChimpRef *sdl_surface;
    ChimpRef *sdl_rect;
    ChimpRef *sdl_key_event;

    if (SDL_Init (SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        return NULL;
    }
    atexit(SDL_Quit);

    sdl = chimp_module_new_str ("sdl", NULL);
    if (sdl == NULL) {
        return NULL;
    }

    if (!chimp_module_add_method_str (
            sdl, "set_video_mode", _chimp_sdl_set_video_mode)) {
        return NULL;
    }

    if (!chimp_module_add_method_str (sdl, "flip", _chimp_sdl_flip)) {
        return NULL;
    }

    if (!chimp_module_add_method_str (
            sdl, "poll_event", _chimp_sdl_poll_event)) {
        return NULL;
    }

    sdl_surface = _chimp_init_sdl_surface_class ();
    if (sdl_surface == NULL) {
        return NULL;
    }
    chimp_sdl_surface_class = sdl_surface;
    if (!chimp_module_add_local_str (sdl, "surface", sdl_surface)) {
        return NULL;
    }

    sdl_rect = _chimp_init_sdl_rect_class ();
    if (sdl_rect == NULL) {
        return NULL;
    }
    chimp_sdl_rect_class = sdl_rect;
    if (!chimp_module_add_local_str (sdl, "rect", sdl_rect)) {
        return NULL;
    }

    sdl_key_event = _chimp_init_sdl_key_event_class ();
    if (sdl_key_event == NULL) {
        return NULL;
    }
    chimp_sdl_key_event_class = sdl_key_event;
    if (!chimp_module_add_local_str (sdl, "key_event", sdl_key_event)) {
        return NULL;
    }

    CHIMP_MODULE_INT_CONSTANT(sdl, "DOUBLEBUF", SDL_DOUBLEBUF);
    CHIMP_MODULE_INT_CONSTANT(sdl, "ASYNCBLIT", SDL_ASYNCBLIT);
    CHIMP_MODULE_INT_CONSTANT(sdl, "HWSURFACE", SDL_HWSURFACE);
    CHIMP_MODULE_INT_CONSTANT(sdl, "SWSURFACE", SDL_SWSURFACE);
    CHIMP_MODULE_INT_CONSTANT(sdl, "HWPALETTE", SDL_HWPALETTE);
    CHIMP_MODULE_INT_CONSTANT(sdl, "OPENGL", SDL_OPENGL);
    CHIMP_MODULE_INT_CONSTANT(sdl, "FULLSCREEN", SDL_FULLSCREEN);
    CHIMP_MODULE_INT_CONSTANT(sdl, "RESIZABLE", SDL_RESIZABLE);
    CHIMP_MODULE_INT_CONSTANT(sdl, "NOFRAME", SDL_NOFRAME);

    CHIMP_MODULE_INT_CONSTANT(sdl, "KEYUP", SDL_KEYUP);
    CHIMP_MODULE_INT_CONSTANT(sdl, "KEYDOWN", SDL_KEYDOWN);
    CHIMP_MODULE_INT_CONSTANT(sdl, "QUIT", SDL_QUIT);

    CHIMP_MODULE_INT_CONSTANT(sdl, "K_ESCAPE", SDLK_ESCAPE);

    return sdl;
}


