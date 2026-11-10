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

  struct wl_seat *seat;
  struct wl_keyboard *keyboard;
  struct wl_pointer *pointer;

  struct zwp_pointer_constraints_v1 *pointer_constraints;
  struct zwp_relative_pointer_manager_v1 *relative_pointer_manager;
  struct zwp_locked_pointer_v1 *locked_pointer;
  struct zwp_relative_pointer_v1 *relative_pointer;

  u32 last_serial;
  b8 pointer_locked;
  b8 pointer_lock_requested;
  b8 pointer_entered;

  u32 width;
  u32 height;
} WaylandState;
