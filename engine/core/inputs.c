#include "inputs.h"
#include "logger.h"
#include "events.h"
#include "memo.h"

typedef struct KeyboardState
{
  b8 keys[256];
} KeyboardState;

typedef struct MouseState
{
  i16 x;
  i16 y;
  f32 delta_x;
  f32 delta_y;
  i8 wheel;
  u8 buttons[BUTTON_MAX_BUTTONS];
} MouseState;

typedef struct InputState
{
  KeyboardState current_keyboard;
  KeyboardState previous_keyboard;
  MouseState current_mouse;
  MouseState previous_mouse;
} InputState;

static InputState* state_ptr;

b8 inputs_initialize(u64* out_memory_requirement, void* state)
{
  if (!state)
  {
    *out_memory_requirement = sizeof(InputState);
    return false;
  }

  state_ptr = (InputState *)state;

  EGNINFO("Core Inputs initialized!");
  return true;
}

void inputs_shutdown()
{
  state_ptr = 0;
}

void inputs_update(f64 delta_time)
{
  (void)delta_time;

  MEMOCPY(&state_ptr->previous_keyboard, &state_ptr->current_keyboard, sizeof(KeyboardState));
  MEMOCPY(&state_ptr->previous_mouse, &state_ptr->current_mouse, sizeof(MouseState));

  state_ptr->current_mouse.delta_x = 0.0f;
  state_ptr->current_mouse.delta_y = 0.0f;
  // The wheel is a per-frame delta, like the pointer motion. It used to keep
  // its last value forever, so the game saw a permanent scroll.
  state_ptr->current_mouse.wheel = 0;
}

void inputs_process_key(keys key, b8 pressed)
{
  if (!state_ptr || (u32)key >= 256)
    return;
  pressed = pressed ? true : false;
  // The check prevents the event from being triggered multiple times in the same frame.
  if (state_ptr->current_keyboard.keys[key] != pressed)
  {
    // Update internal state_ptr->
    state_ptr->current_keyboard.keys[key] = pressed;

    // Fire the event.
    EventContext context;
    context.data.u16[0] = key;
    events_fire(pressed ? EVENT_CODE_KEY_PRESSED : EVENT_CODE_KEY_RELEASED, 0, context);
  }
}

void inputs_process_button(Buttons button, b8 pressed)
{
  if (!state_ptr || (u32)button >= BUTTON_MAX_BUTTONS)
    return;
  pressed = pressed ? true : false;
  // The check prevents the event from being triggered multiple times in the same frame.
  if (state_ptr->current_mouse.buttons[button] != pressed)
  {
    // Update internal state_ptr->
    state_ptr->current_mouse.buttons[button] = pressed;

    // Fire the event.
    EventContext context;
    context.data.u16[0] = button;
    events_fire(pressed ? EVENT_CODE_BUTTON_PRESSED : EVENT_CODE_BUTTON_RELEASED, 0, context);
  }
}

void inputs_process_mouse_move(i16 x, i16 y)
{
  // The check prevents the event from being triggered multiple times in the same frame.
  if (state_ptr->current_mouse.x != x || state_ptr->current_mouse.y != y)
  {
    // Update internal state_ptr->
    state_ptr->current_mouse.x = x;
    state_ptr->current_mouse.y = y;

    // Fire the event.
    EventContext context;
    context.data.u16[0] = x;
    context.data.u16[1] = y;
    events_fire(EVENT_CODE_MOUSE_MOVED, 0, context);
  }
}

void inputs_process_mouse_delta(f32 dx, f32 dy)
{
  state_ptr->current_mouse.delta_x += dx;
  state_ptr->current_mouse.delta_y += dy;
}

void inputs_process_mouse_wheel(i8 z_delta)
{
  if (!state_ptr || z_delta == 0)
    return;

  // Accumulate within the frame and always fire: two notches in the same
  // direction are two events, not one.
  i32 total = (i32)state_ptr->current_mouse.wheel + z_delta;
  state_ptr->current_mouse.wheel = (i8)EGNCLAMP(total, -127, 127);

  EventContext context;
  context.data.u8[0] = (u8)z_delta;
  events_fire(EVENT_CODE_MOUSE_WHEEL, 0, context);
}

void inputs_release_all(void)
{
  if (!state_ptr)
    return;
  for (u32 key = 0; key < 256; key++)
  {
    if (state_ptr->current_keyboard.keys[key])
      inputs_process_key((keys)key, false);
  }
  for (u32 button = 0; button < BUTTON_MAX_BUTTONS; button++)
  {
    if (state_ptr->current_mouse.buttons[button])
      inputs_process_button((Buttons)button, false);
  }
}

b8 inputs_key_down(keys key)
{
  if (!state_ptr || (u32)key >= 256)
    return false;
  return state_ptr->current_keyboard.keys[key] == true;
}

b8 inputs_key_up(keys key)
{
  if (!state_ptr || (u32)key >= 256)
    return true;
  return state_ptr->current_keyboard.keys[key] == false;
}

b8 inputs_was_key_down(keys key)
{
  if (!state_ptr || (u32)key >= 256)
    return false;
  return state_ptr->previous_keyboard.keys[key] == true;
}

b8 inputs_was_key_up(keys key)
{
  if (!state_ptr || (u32)key >= 256)
    return true;
  return state_ptr->previous_keyboard.keys[key] == false;
}

b8 inputs_btn_down(Buttons button)
{
  if (!state_ptr || (u32)button >= BUTTON_MAX_BUTTONS)
    return false;
  return state_ptr->current_mouse.buttons[button] == true;
}

b8 inputs_btn_up(Buttons button)
{
  if (!state_ptr || (u32)button >= BUTTON_MAX_BUTTONS)
    return true;
  return state_ptr->current_mouse.buttons[button] == false;
}

b8 inputs_was_btn_down(Buttons button)
{
  if (!state_ptr || (u32)button >= BUTTON_MAX_BUTTONS)
    return false;
  return state_ptr->previous_mouse.buttons[button] == true;
}

b8 inputs_was_btn_up(Buttons button)
{
  if (!state_ptr || (u32)button >= BUTTON_MAX_BUTTONS)
    return true;
  return state_ptr->previous_mouse.buttons[button] == false;
}

i16 inputs_mouse_wheel()
{
  return state_ptr->current_mouse.wheel;
}

i16 inputs_prev_mouse_wheel()
{
  return state_ptr->previous_mouse.wheel;
}

void inputs_get_mouse_pos(i32 *x, i32 *y)
{
  *x = state_ptr->current_mouse.x;
  *y = state_ptr->current_mouse.y;
}

void inputs_get_prev_mouse_pos(i32 *x, i32 *y)
{
  *x = state_ptr->previous_mouse.x;
  *y = state_ptr->previous_mouse.y;
}

void inputs_get_mouse_delta(f32 *dx, f32 *dy)
{
  *dx = state_ptr->current_mouse.delta_x;
  *dy = state_ptr->current_mouse.delta_y;
}
