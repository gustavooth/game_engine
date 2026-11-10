#include "defines.h"

#ifdef EGNPLATFORM_WAYLAND
#include "window_wayland.h"
#include "platform/platform.h"
#include "platform/window.h"
#include "core/logger.h"
#include "core/events.h"
#include "core/memo.h"

#include <wayland-client.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
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
  // Wayland does not let clients choose the absolute position of a toplevel.
  // pos_x/pos_y are accepted for API portability but are intentionally ignored.
  (void)pos_x;
  (void)pos_y;

  WaylandState *state = MEMOALLOC(sizeof(WaylandState), MEMO_WINDOW);
  MEMOSET(state, 0, sizeof(WaylandState));
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

  EGNINFO("Core Platform Wayland initialized!");

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
  // The surface is mapped when the first swapchain image is presented.
  // Do not maximize here, otherwise the size requested in EngineSettings
  // (window_width/window_height) would be ignored.
  (void)window;
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
  struct wl_display *display = state->display;

  // Pump pending Wayland events without blocking the render loop.
  while (wl_display_prepare_read(display) != 0)
  {
    if (wl_display_dispatch_pending(display) == -1)
      return false;
  }

  if (wl_display_flush(display) == -1 && errno != EAGAIN)
  {
    wl_display_cancel_read(display);
    return false;
  }

  struct pollfd pfd = {wl_display_get_fd(display), POLLIN, 0};
  if (poll(&pfd, 1, 0) > 0)
  {
    if (wl_display_read_events(display) == -1)
      return false;
  }
  else
  {
    wl_display_cancel_read(display);
  }

  if (wl_display_dispatch_pending(display) == -1)
    return false;

  return true;
}

b8 window_get_size(Window *window, u32 *out_width, u32 *out_height)
{
  if (!window || !window->internal_state)
    return false;

  WaylandState *state = (WaylandState *)window->internal_state;
  if (out_width)
    *out_width = state->width;
  if (out_height)
    *out_height = state->height;

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

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, u32 serial)
{
  xdg_surface_ack_configure(xdg_surface, serial);
}

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, i32 width,
                                   i32 height, struct wl_array *states)
{

  WaylandState *state = (WaylandState *)data;

  if (!width || !height)
    return;
  if (width == (i32)state->width && height == (i32)state->height)
    return;

  state->width = width;
  state->height = height;

  EventContext ctx = {0};
  ctx.data.u16[0] = width;
  ctx.data.u16[1] = height;
  events_fire(EVENT_CODE_RESIZED, state, ctx);
}

void xgd_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
  EventContext ctx = {0};
  events_fire(EVENT_CODE_APPLICATION_QUIT, data, ctx);
}

void configure_bounds(void *data, struct xdg_toplevel *xdg_toplevel, i32 width, i32 height) {}
void wm_capabilities(void *data, struct xdg_toplevel *xdg_toplevel, struct wl_array *capabilities) {}

#endif