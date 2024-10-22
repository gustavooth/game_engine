#include "defines.h"

#ifdef EGNPLATFORM_WAYLAND
#include "window_wayland.h"
#include "platform/platform.h"
#include "platform/window.h"
#include "core/logger.h"
#include "core/events.h"

#include <wayland-client.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "xdg-shell-client-protocol.h"

static void global_registry_handler(void *data, struct wl_registry *registry, u32 id,
                                    const char *interface, u32 version);
static void global_registry_remover(void *data, struct wl_registry *registry, u32 id);
static const struct wl_registry_listener registry_listener = {
    global_registry_handler,
    global_registry_remover};

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *shell, u32 serial);
static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, u32 serial);
static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, i32 width,
                                   i32 height, struct wl_array *states);
void xgd_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel);
void configure_bounds(void *data, struct xdg_toplevel *xdg_toplevel, i32 width, i32 height);
void wm_capabilities(void *data, struct xdg_toplevel *xdg_toplevel, struct wl_array *capabilities);
static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xgd_toplevel_close,
    .configure_bounds = configure_bounds,
    .wm_capabilities = wm_capabilities,
};

b8 window_create(const char *window_name, u32 pos_x, u32 pos_y, u32 width, u32 height, Window *out_window)
{
  WaylandState *state = malloc(sizeof(WaylandState));
  memset(state, 0, sizeof(WaylandState));
  out_window->internal_state = state;

  state->display = wl_display_connect(NULL);
  if (!state->display)
  {
    EGNERROR("Failed to connnect to Wayland display");
    return false;
  }

  state->registry = wl_display_get_registry(state->display);
  wl_registry_add_listener(state->registry, &registry_listener, state);

  // warn server to send menssage to client
  wl_display_roundtrip(state->display);

  state->surface = wl_compositor_create_surface(state->compositor);

  state->xdg_surface = xdg_wm_base_get_xdg_surface(state->xdg_wm_base, state->surface);
  state->xdg_toplevel = xdg_surface_get_toplevel(state->xdg_surface);

  xdg_surface_add_listener(state->xdg_surface, &xdg_surface_listener, state);
  xdg_toplevel_add_listener(state->xdg_toplevel, &xdg_toplevel_listener, state);

  xdg_toplevel_set_title(state->xdg_toplevel, window_name);

  state->width = width;
  state->height = height;

  wl_surface_commit(state->surface);

  EGNINFO("Wayland state initialized!");

  return true;
}

void window_destroy(Window *window)
{
  WaylandState *state = (WaylandState *)window->internal_state;

  wl_surface_destroy(state->surface);
  wl_display_disconnect(state->display);
}

b8 window_show(Window *window)
{
  WaylandState *state = (WaylandState *)window->internal_state;
  xdg_toplevel_set_maximized(state->xdg_toplevel);
  return true;
}
b8 window_hide(Window *window)
{
  WaylandState *state = (WaylandState *)window->internal_state;
  xdg_toplevel_set_minimized(state->xdg_toplevel);
  return true;
}
b8 window_process_messages(Window *window)
{
  WaylandState *state = (WaylandState *)window->internal_state;
  wl_display_dispatch(state->display);
  return true;
}

static void global_registry_handler(void *data, struct wl_registry *registry, u32 id,
                                    const char *interface, u32 version)
{

  WaylandState *state = (WaylandState *)data;
  if (!strcmp(interface, wl_compositor_interface.name))
  {
    state->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, version);
  }
  else if (!strcmp(interface, xdg_wm_base_interface.name))
  {
    state->xdg_wm_base = wl_registry_bind(registry, id, &xdg_wm_base_interface, version);
    xdg_wm_base_add_listener(state->xdg_wm_base, &xdg_wm_base_listener, data);
  }
}

static void global_registry_remover(void *data, struct wl_registry *registry, u32 id) {}

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *shell, u32 serial)
{
  xdg_wm_base_pong(shell, serial);
}

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, u32 serial) {}

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, i32 width,
                                   i32 height, struct wl_array *states)
{

  WaylandState *state = (WaylandState *)data;

  if (!width || !height)
    return;
  if (width == state->width && height == state->height)
    return;

  EventContext ctx = {0};
  ctx.data.u16[0] = width;
  ctx.data.u16[1] = height;
  event_fire(EVENT_CODE_RESIZED, state, ctx);
}

void xgd_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
  EventContext ctx = {0};
  event_fire(EVENT_CODE_APPLICATION_QUIT, data, ctx);
}

void configure_bounds(void *data, struct xdg_toplevel *xdg_toplevel, i32 width, i32 height) {}
void wm_capabilities(void *data, struct xdg_toplevel *xdg_toplevel, struct wl_array *capabilities) {}

#endif