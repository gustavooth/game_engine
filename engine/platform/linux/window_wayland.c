#include "defines.h"

#ifdef EGNPLATFORM_WAYLAND
#include "window_wayland.h"
#include "platform/platform.h"
#include "platform/window.h"
#include "core/logger.h"
#include "core/events.h"
#include "core/inputs.h"
#include "core/memo.h"

#include <wayland-client.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include "xdg-shell-client-protocol.h"
#include "protocols/pointer-constraints-unstable-v1-client-protocol.h"
#include "protocols/relative-pointer-unstable-v1-client-protocol.h"

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

// ---------------------------------------------------------------------------
// Keyboard: translate Linux evdev codes to the engine's key values.
// ---------------------------------------------------------------------------

static i32 evdev_to_key(u32 code)
{
  if (code >= 2 && code <= 10)
    return '1' + (i32)(code - 2);
  if (code >= 59 && code <= 68)
    return KEY_F1 + (i32)(code - 59);
  if (code >= 71 && code <= 83)
    return KEY_NUMPAD0 + (i32)(code - 71);

  switch (code)
  {
  // Letter keys in evdev order. Do not use arithmetic on ASCII here: the
  // keyboard ORDER is QWERTY, not alphabetical.
  case 16: return KEY_Q;
  case 17: return KEY_W;
  case 18: return KEY_E;
  case 19: return KEY_R;
  case 20: return KEY_T;
  case 21: return KEY_Y;
  case 22: return KEY_U;
  case 23: return KEY_I;
  case 24: return KEY_O;
  case 25: return KEY_P;
  case 30: return KEY_A;
  case 31: return KEY_S;
  case 32: return KEY_D;
  case 33: return KEY_F;
  case 34: return KEY_G;
  case 35: return KEY_H;
  case 36: return KEY_J;
  case 37: return KEY_K;
  case 38: return KEY_L;
  case 44: return KEY_Z;
  case 45: return KEY_X;
  case 46: return KEY_C;
  case 47: return KEY_V;
  case 48: return KEY_B;
  case 49: return KEY_N;
  case 50: return KEY_M;
  case 1: return KEY_ESCAPE;
  case 11: return '0';
  case 12: return KEY_MINUS;
  case 13: return KEY_PLUS;
  case 14: return KEY_BACKSPACE;
  case 15: return KEY_TAB;
  case 28: return KEY_ENTER;
  case 29: return KEY_CONTROL;
  case 39: return KEY_SEMICOLON;
  case 41: return KEY_MINUS; // grave approximation
  case 42: return KEY_SHIFT;
  case 51: return KEY_COMMA;
  case 52: return KEY_PERIOD;
  case 53: return KEY_SLASH;
  case 54: return KEY_SHIFT;
  case 56: return KEY_LALT;
  case 57: return KEY_SPACE;
  case 58: return KEY_CAPITAL;
  case 69: return KEY_NUMLOCK;
  case 70: return KEY_SCROLL;
  case 87: return KEY_F11;
  case 88: return KEY_F12;
  case 96: return KEY_ENTER;
  case 97: return KEY_CONTROL;
  case 100: return KEY_RALT;
  case 102: return KEY_HOME;
  case 103: return KEY_UP;
  case 104: return KEY_PRIOR;
  case 105: return KEY_LEFT;
  case 106: return KEY_RIGHT;
  case 107: return KEY_END;
  case 108: return KEY_DOWN;
  case 109: return KEY_NEXT;
  case 110: return KEY_INSERT;
  case 111: return KEY_DELETE;
  default: return -1;
  }
}

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard, u32 format,
                            int32_t fd, u32 size)
{
  (void)data;
  (void)keyboard;
  (void)format;
  (void)size;
  // The engine has its own key table; the keymap fd is not needed.
  close(fd);
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard, u32 serial,
                           struct wl_surface *surface, struct wl_array *keys)
{
  (void)keyboard;
  (void)surface;
  (void)keys;
  WaylandState *state = (WaylandState *)data;
  state->last_serial = serial;
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard, u32 serial,
                           struct wl_surface *surface)
{
  (void)data;
  (void)keyboard;
  (void)serial;
  (void)surface;
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard, u32 serial,
                         u32 time, u32 key, u32 state)
{
  (void)keyboard;
  (void)time;
  WaylandState *wayland = (WaylandState *)data;
  wayland->last_serial = serial;

  i32 mapped = evdev_to_key(key);
  if (mapped >= 0)
    inputs_process_key((keys)mapped, state == WL_KEYBOARD_KEY_STATE_PRESSED);
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard, u32 serial,
                               u32 depressed, u32 latched, u32 locked, u32 group)
{
  (void)data;
  (void)keyboard;
  (void)serial;
  (void)depressed;
  (void)latched;
  (void)locked;
  (void)group;
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
                                 int32_t rate, int32_t delay)
{
  (void)data;
  (void)keyboard;
  (void)rate;
  (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    keyboard_keymap,
    keyboard_enter,
    keyboard_leave,
    keyboard_key,
    keyboard_modifiers,
    keyboard_repeat_info,
};

// ---------------------------------------------------------------------------
// Pointer and pointer lock
// ---------------------------------------------------------------------------

static const struct zwp_locked_pointer_v1_listener locked_pointer_listener;

static void pointer_enter(void *data, struct wl_pointer *pointer, u32 serial,
                          struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy)
{
  (void)pointer;
  (void)surface;
  WaylandState *state = (WaylandState *)data;
  state->last_serial = serial;
  state->pointer_entered = true;

  if (state->pointer_lock_requested && !state->locked_pointer && state->pointer_constraints)
  {
    state->locked_pointer = zwp_pointer_constraints_v1_lock_pointer(
        state->pointer_constraints, state->surface, state->pointer, NULL,
        ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    if (state->locked_pointer)
      zwp_locked_pointer_v1_add_listener(state->locked_pointer, &locked_pointer_listener, state);
  }
  if (state->pointer_lock_requested)
    wl_pointer_set_cursor(pointer, serial, NULL, 0, 0);

  inputs_process_mouse_move((i16)wl_fixed_to_int(sx), (i16)wl_fixed_to_int(sy));
}

static void pointer_leave(void *data, struct wl_pointer *pointer, u32 serial,
                          struct wl_surface *surface)
{
  (void)pointer;
  (void)serial;
  (void)surface;
  WaylandState *state = (WaylandState *)data;
  state->pointer_entered = false;
}

static void pointer_motion(void *data, struct wl_pointer *pointer, u32 time,
                           wl_fixed_t sx, wl_fixed_t sy)
{
  (void)pointer;
  (void)time;
  (void)data;
  inputs_process_mouse_move((i16)wl_fixed_to_int(sx), (i16)wl_fixed_to_int(sy));
}

static void pointer_button(void *data, struct wl_pointer *pointer, u32 serial,
                           u32 time, u32 button, u32 state)
{
  (void)pointer;
  (void)time;
  WaylandState *wayland = (WaylandState *)data;
  wayland->last_serial = serial;

  Buttons mapped = BUTTON_MAX_BUTTONS;
  if (button == 0x110)
    mapped = BTN_LEFT;
  else if (button == 0x111)
    mapped = BTN_RIGHT;
  else if (button == 0x112)
    mapped = BTN_MIDDLE;

  if (mapped != BUTTON_MAX_BUTTONS)
    inputs_process_button(mapped, state == WL_POINTER_BUTTON_STATE_PRESSED);
}

static void pointer_axis(void *data, struct wl_pointer *pointer, u32 time,
                         u32 axis, wl_fixed_t value)
{
  (void)data;
  (void)pointer;
  (void)time;
  if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
    return;

  i32 steps = wl_fixed_to_int(value) / 120;
  if (steps == 0)
    steps = value > 0 ? 1 : (value < 0 ? -1 : 0);
  if (steps != 0)
    inputs_process_mouse_wheel((i8)steps);
}

static void pointer_frame(void *data, struct wl_pointer *pointer)
{
  (void)data;
  (void)pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer, u32 source)
{
  (void)data;
  (void)pointer;
  (void)source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer, u32 time, u32 axis)
{
  (void)data;
  (void)pointer;
  (void)time;
  (void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer, u32 axis, i32 discrete)
{
  (void)data;
  (void)pointer;
  (void)axis;
  (void)discrete;
}

static const struct wl_pointer_listener pointer_listener = {
    pointer_enter,
    pointer_leave,
    pointer_motion,
    pointer_button,
    pointer_axis,
    pointer_frame,
    pointer_axis_source,
    pointer_axis_stop,
    pointer_axis_discrete,
};

static void locked_pointer_locked(void *data, struct zwp_locked_pointer_v1 *locked_pointer)
{
  (void)locked_pointer;
  WaylandState *state = (WaylandState *)data;
  state->pointer_locked = true;
  state->pointer_lock_requested = true;
}

static void locked_pointer_unlocked(void *data, struct zwp_locked_pointer_v1 *locked_pointer)
{
  (void)locked_pointer;
  WaylandState *state = (WaylandState *)data;
  state->pointer_locked = false;
}

static const struct zwp_locked_pointer_v1_listener locked_pointer_listener = {
    locked_pointer_locked,
    locked_pointer_unlocked,
};

static void relative_pointer_motion(void *data, struct zwp_relative_pointer_v1 *relative_pointer,
                                    u32 utime_hi, u32 utime_lo,
                                    wl_fixed_t dx, wl_fixed_t dy,
                                    wl_fixed_t dx_unaccel, wl_fixed_t dy_unaccel)
{
  (void)data;
  (void)relative_pointer;
  (void)utime_hi;
  (void)utime_lo;
  (void)dx_unaccel;
  (void)dy_unaccel;

  inputs_process_mouse_delta((f32)wl_fixed_to_double(dx), (f32)wl_fixed_to_double(dy));
}

static const struct zwp_relative_pointer_v1_listener relative_pointer_listener = {
    relative_pointer_motion,
};

static void create_relative_pointer(WaylandState *state)
{
  if (state->relative_pointer || !state->relative_pointer_manager || !state->pointer)
    return;

  state->relative_pointer = zwp_relative_pointer_manager_v1_get_relative_pointer(
      state->relative_pointer_manager, state->pointer);
  if (state->relative_pointer)
    zwp_relative_pointer_v1_add_listener(state->relative_pointer, &relative_pointer_listener, state);
}

static void seat_capabilities(void *data, struct wl_seat *seat, u32 capabilities)
{
  WaylandState *state = (WaylandState *)data;

  if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD)
  {
    if (!state->keyboard)
    {
      state->keyboard = wl_seat_get_keyboard(seat);
      wl_keyboard_add_listener(state->keyboard, &keyboard_listener, state);
    }
  }
  else if (state->keyboard)
  {
    wl_keyboard_release(state->keyboard);
    state->keyboard = NULL;
  }

  if (capabilities & WL_SEAT_CAPABILITY_POINTER)
  {
    if (!state->pointer)
    {
      state->pointer = wl_seat_get_pointer(seat);
      wl_pointer_add_listener(state->pointer, &pointer_listener, state);
      create_relative_pointer(state);
    }
  }
  else if (state->pointer)
  {
    wl_pointer_release(state->pointer);
    state->pointer = NULL;
  }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
  (void)data;
  (void)seat;
  (void)name;
}

static const struct wl_seat_listener seat_listener = {
    seat_capabilities,
    seat_name,
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

  if (state->locked_pointer)
  {
    zwp_locked_pointer_v1_destroy(state->locked_pointer);
    state->locked_pointer = NULL;
  }
  if (state->relative_pointer)
    zwp_relative_pointer_v1_destroy(state->relative_pointer);
  if (state->keyboard)
    wl_keyboard_release(state->keyboard);
  if (state->pointer)
    wl_pointer_release(state->pointer);

  wl_surface_destroy(state->surface);
  wl_display_disconnect(state->display);
  MEMOFREE(state);
  window->internal_state = NULL;
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

b8 window_set_pointer_lock(Window *window, b8 locked)
{
  if (!window || !window->internal_state)
    return false;

  WaylandState *state = (WaylandState *)window->internal_state;
  state->pointer_lock_requested = locked;

  if (locked)
  {
    if (!state->pointer_constraints || !state->pointer || !state->surface)
      return false;

    if (!state->locked_pointer)
    {
      state->locked_pointer = zwp_pointer_constraints_v1_lock_pointer(
          state->pointer_constraints, state->surface, state->pointer, NULL,
          ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
      if (state->locked_pointer)
        zwp_locked_pointer_v1_add_listener(state->locked_pointer, &locked_pointer_listener, state);
    }

    if (state->pointer && state->last_serial != 0)
      wl_pointer_set_cursor(state->pointer, state->last_serial, NULL, 0, 0);

    wl_display_flush(state->display);
    return state->locked_pointer != NULL;
  }

  if (state->locked_pointer)
  {
    zwp_locked_pointer_v1_destroy(state->locked_pointer);
    state->locked_pointer = NULL;
  }
  state->pointer_locked = false;
  wl_display_flush(state->display);
  return true;
}

b8 window_is_pointer_locked(Window *window)
{
  if (!window || !window->internal_state)
    return false;
  WaylandState *state = (WaylandState *)window->internal_state;
  return state->pointer_locked;
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
  else if (!strcmp(interface, wl_seat_interface.name))
  {
    u32 bind_version = version < 7 ? version : 7;
    state->seat = wl_registry_bind(registry, id, &wl_seat_interface, bind_version);
    wl_seat_add_listener(state->seat, &seat_listener, state);
  }
  else if (!strcmp(interface, zwp_pointer_constraints_v1_interface.name))
  {
    u32 bind_version = version < 1 ? version : 1;
    state->pointer_constraints = wl_registry_bind(
        registry, id, &zwp_pointer_constraints_v1_interface, bind_version);
  }
  else if (!strcmp(interface, zwp_relative_pointer_manager_v1_interface.name))
  {
    u32 bind_version = version < 1 ? version : 1;
    state->relative_pointer_manager = wl_registry_bind(
        registry, id, &zwp_relative_pointer_manager_v1_interface, bind_version);
    create_relative_pointer(state);
  }
}

static void global_registry_remover(void *data, struct wl_registry *registry, u32 id)
{
  (void)data;
  (void)registry;
  (void)id;
}

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *shell, u32 serial)
{
  (void)data;
  xdg_wm_base_pong(shell, serial);
}

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, u32 serial)
{
  (void)data;
  xdg_surface_ack_configure(xdg_surface, serial);
}

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, i32 width,
                                   i32 height, struct wl_array *states)
{
  (void)xdg_toplevel;
  (void)states;

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
  (void)xdg_toplevel;
  EventContext ctx = {0};
  events_fire(EVENT_CODE_APPLICATION_QUIT, data, ctx);
}

void configure_bounds(void *data, struct xdg_toplevel *xdg_toplevel, i32 width, i32 height)
{
  (void)data;
  (void)xdg_toplevel;
  (void)width;
  (void)height;
}

void wm_capabilities(void *data, struct xdg_toplevel *xdg_toplevel, struct wl_array *capabilities)
{
  (void)data;
  (void)xdg_toplevel;
  (void)capabilities;
}

#endif
