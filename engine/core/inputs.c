#include "inputs.h"
#include "logger.h"
#include "events.h"
#include "memo.h"

typedef struct keyboard_state
{
    b8 keys[256];
} keyboard_state;

typedef struct mouse_state
{
    i16 x;
    i16 y;
    i8 wheel;
    u8 buttons[BUTTON_MAX_BUTTONS];
} mouse_state;

typedef struct input_state
{
    keyboard_state current_keyboard;
    keyboard_state previous_keyboard;
    mouse_state current_mouse;
    mouse_state previous_mouse;
} input_state;

static input_state state;

b8 input_initialize()
{
    EGNINFO("Inputs system initialized!");
    return true;
}

void input_shutdown()
{
}

void input_update(f64 delta_time)
{
    MEMOCPY(&state.previous_keyboard, &state.current_keyboard, sizeof(keyboard_state));
    MEMOCPY(&state.previous_mouse, &state.current_mouse, sizeof(mouse_state));
}

void input_process_key(keys key, b8 pressed)
{
    // The check prevents the event from being triggered multiple times in the same frame.
    if (state.current_keyboard.keys[key] != pressed)
    {
        // Update internal state.
        state.current_keyboard.keys[key] = pressed;

        // Fire the event.
        EventContext context;
        context.data.u16[0] = key;
        event_fire(pressed ? EVENT_CODE_KEY_PRESSED : EVENT_CODE_KEY_RELEASED, 0, context);
    }
}

void input_process_button(Buttons button, b8 pressed)
{
    // The check prevents the event from being triggered multiple times in the same frame.
    if (state.current_mouse.buttons[button] != pressed)
    {
        // Update internal state.
        state.previous_mouse.buttons[button] = pressed;

        // Fire the event.
        EventContext context;
        context.data.u16[0] = button;
        event_fire(pressed ? EVENT_CODE_BUTTON_PRESSED : EVENT_CODE_BUTTON_RELEASED, 0, context);
    }
}

void input_process_mouse_move(i16 x, i16 y)
{
    // The check prevents the event from being triggered multiple times in the same frame.
    if (state.current_mouse.x != x || state.current_mouse.y != y)
    {
        // Update internal state.
        state.current_mouse.x = x;
        state.current_mouse.y = y;

        // Fire the event.
        EventContext context;
        context.data.u16[0] = x;
        context.data.u16[1] = y;
        event_fire(EVENT_CODE_MOUSE_MOVED, 0, context);
    }
}

void input_process_mouse_wheel(i8 z_delta)
{
    // The check prevents the event from being triggered multiple times in the same frame.
    if (state.current_mouse.wheel != z_delta)
    {
        // Update internal state.
        state.current_mouse.wheel = z_delta;

        // Fire the event.
        EventContext context;
        context.data.u8[0] = z_delta;
        event_fire(EVENT_CODE_MOUSE_WHEEL, 0, context);
    }
}

b8 input_key_down(keys key)
{
    return state.current_keyboard.keys[key] == true;
}

b8 input_key_up(keys key)
{
    return state.current_keyboard.keys[key] == false;
}

b8 input_was_key_down(keys key)
{
    return state.previous_keyboard.keys[key] == true;
}

b8 input_was_key_up(keys key)
{
    return state.previous_keyboard.keys[key] == false;
}

b8 input_btn_down(Buttons button)
{
    return state.current_mouse.buttons[button] == true;
}

b8 input_btn_up(Buttons button)
{
    return state.current_mouse.buttons[button] == false;
}

b8 input_was_button_down(Buttons button)
{
    return state.previous_mouse.buttons[button] == true;
}

b8 input_was_button_up(Buttons button)
{
    return state.previous_mouse.buttons[button] == false;
}

i16 input_mouse_wheel()
{
    return state.current_mouse.wheel;
}

i16 input_prev_mouse_wheel()
{
    return state.previous_mouse.wheel;
}

void input_get_mouse_position(i32 *x, i32 *y)
{
    *x = state.current_mouse.x;
    *y = state.current_mouse.y;
}

void input_get_previous_mouse_position(i32 *x, i32 *y)
{
    *x = state.previous_mouse.x;
    *y = state.previous_mouse.y;
}
