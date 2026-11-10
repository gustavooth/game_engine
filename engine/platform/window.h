#pragma once

#include "defines.h"

typedef struct Window
{
  void *internal_state;
} Window;

b8 window_create(const char *name, u32 x, u32 y, u32 width, u32 height, Window *out_window);
void window_destroy(Window *win);

b8 window_show(Window *win);
b8 window_hide(Window *win);

b8 window_get_size(Window *win, u32 *out_width, u32 *out_height);

/**
 * Requests/releases a real pointer lock (Wayland pointer constraints).
 * While locked the cursor is hidden and relative motion is reported through
 * inputs_get_mouse_delta().
 */
b8 window_set_pointer_lock(Window *win, b8 locked);
b8 window_is_pointer_locked(Window *win);

b8 window_process_messages(Window *win);
