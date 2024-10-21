#pragma once
#include "defines.h"

typedef struct WaylandState
{
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_surface *surface;

    struct xdg_wm_base *xdg_wm_base;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    u32 width;
    u32 height;
} WaylandState;
