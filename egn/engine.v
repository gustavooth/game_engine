module egn

// High-level V API for the EGN engine.
//
// The engine is a C shared library; this module is the only bridge game code
// should need. It keeps platform details (Wayland, Vulkan, future backends)
// out of the game and exposes a small, typed V API.
//
// Basic usage:
//
//	import egn { Settings, create }
//
//	fn main() {
//	    mut app := create(Settings{ title: 'My game' })!
//	    defer { app.destroy() }
//	    for app.should_run() {
//	        app.frame()
//	    }
//	}

pub struct Settings {
pub mut:
	title  string = 'EGN Game'
	width  int    = 1280
	height int    = 720
	x      int    = 100
	y      int    = 100
}

pub struct Engine {
pub mut:
	// Title is kept here so its backing C string stays alive for as long as
	// the engine instance exists.
	title string
mut:
	handle voidptr
}

// create initializes the engine (platform, window, renderer, inputs, events,
// logger and memory) and returns a handle to it. The engine does not start its
// own loop: the game drives frames with frame().
pub fn create(settings Settings) !Engine {
	mut c_settings := C.EngineSettings{
		window_name:   settings.title.str
		window_width:  settings.width
		window_height: settings.height
		window_pos_x:  settings.x
		window_pos_y:  settings.y
	}

	mut handle := voidptr(0)
	if !C.engine_create_instance(&c_settings, &handle) {
		return error('engine_create_instance failed')
	}

	return Engine{
		title:  settings.title
		handle: handle
	}
}

// frame processes OS/window messages, updates inputs and renders one frame.
// Returns true while the engine should keep running.
pub fn (mut e Engine) frame() bool {
	if e.handle == 0 {
		return false
	}
	return C.engine_frame(e.handle)
}

// should_run tells whether the game loop should continue.
pub fn (e &Engine) should_run() bool {
	if e.handle == 0 {
		return false
	}
	return C.engine_should_run(e.handle)
}

// quit requests shutdown at the end of the current frame.
pub fn (mut e Engine) quit() {
	if e.handle != 0 {
		C.engine_request_quit(e.handle)
	}
}

// destroy shuts every subsystem down and releases the engine instance.
// It is safe to call more than once.
pub fn (mut e Engine) destroy() {
	if e.handle != 0 {
		C.engine_destroy_instance(e.handle)
		e.handle = voidptr(0)
	}
}

// delta_time returns the duration of the last frame, in seconds.
pub fn (e &Engine) delta_time() f64 {
	return C.engine_get_delta_time(e.handle)
}

// elapsed_time returns the time since the engine was created, in seconds.
pub fn (e &Engine) elapsed_time() f64 {
	return C.engine_get_elapsed_time(e.handle)
}

// window_size returns the current window width and height in pixels.
pub fn (e &Engine) window_size() (int, int) {
	mut width := 0
	mut height := 0
	C.engine_get_window_size(e.handle, &width, &height)
	return width, height
}

// scene returns the scene root object (object id 1).
pub fn (e &Engine) scene() Object {
	return Object{
		handle: C.egn_scene_root()
	}
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------

// key_down is true while the key is held down.
pub fn (e &Engine) key_down(key Key) bool {
	return C.inputs_key_down(int(key))
}

// key_up is true while the key is released.
pub fn (e &Engine) key_up(key Key) bool {
	return C.inputs_key_up(int(key))
}

// key_pressed is true only on the frame the key went down.
pub fn (e &Engine) key_pressed(key Key) bool {
	return C.inputs_was_key_down(int(key))
}

// key_released is true only on the frame the key went up.
pub fn (e &Engine) key_released(key Key) bool {
	return C.inputs_was_key_up(int(key))
}

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------

pub fn (e &Engine) mouse_down(button MouseButton) bool {
	return C.inputs_btn_down(int(button))
}

pub fn (e &Engine) mouse_up(button MouseButton) bool {
	return C.inputs_btn_up(int(button))
}

pub fn (e &Engine) mouse_pressed(button MouseButton) bool {
	return C.inputs_was_btn_down(int(button))
}

pub fn (e &Engine) mouse_released(button MouseButton) bool {
	return C.inputs_was_btn_up(int(button))
}

// mouse_position returns the current mouse position in window coordinates.
pub fn (e &Engine) mouse_position() (int, int) {
	mut x := 0
	mut y := 0
	C.inputs_get_mouse_pos(&x, &y)
	return x, y
}

// mouse_previous_position returns the mouse position on the previous frame.
pub fn (e &Engine) mouse_previous_position() (int, int) {
	mut x := 0
	mut y := 0
	C.inputs_get_prev_mouse_pos(&x, &y)
	return x, y
}

// mouse_wheel returns the wheel delta of the current frame.
pub fn (e &Engine) mouse_wheel() i16 {
	return C.inputs_mouse_wheel()
}

// mouse_previous_wheel returns the wheel delta of the previous frame.
pub fn (e &Engine) mouse_previous_wheel() i16 {
	return C.inputs_prev_mouse_wheel()
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

// on_event registers a callback for an event code. The callback must be a
// plain function (not a closure).
pub fn (e &Engine) on_event(code EventCode, callback EventCallback) {
	C.events_register(u16(code), voidptr(0), callback)
}

// off_event unregisters a previously registered callback.
pub fn (e &Engine) off_event(code EventCode, callback EventCallback) bool {
	return C.events_unregister(u16(code), callback)
}

// fire_event sends a custom event to every registered listener.
pub fn (e &Engine) fire_event(code EventCode, data EventContext) bool {
	return C.events_fire2(u16(code), voidptr(0), u64(data.i64_at(0)), u64(data.i64_at(1)))
}
