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
	title      string = 'EGN Game'
	width      int    = 1280
	height     int    = 720
	x          int    = 100
	y          int    = 100
	vsync      bool   = true
	target_fps int
	graphics   GraphicsSettings
}

pub enum GraphicsQuality {
	low
	medium
	high
}

pub struct GraphicsSettings {
pub mut:
	shadows_enabled bool = true
	shadow_quality GraphicsQuality = .medium
}

// Applies all graphics options together. Returns false for an invalid quality.
pub fn set_graphics_settings(settings GraphicsSettings) bool {
	value := C.RendererGraphicsSettings{
		shadows_enabled: settings.shadows_enabled
		shadow_quality: int(settings.shadow_quality)
	}
	return C.renderer_set_graphics_settings(&value)
}

pub fn graphics_settings() GraphicsSettings {
	value := C.renderer_get_graphics_settings()
	return GraphicsSettings{
		shadows_enabled: value.shadows_enabled
		shadow_quality: unsafe { GraphicsQuality(value.shadow_quality) }
	}
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
	if !set_graphics_settings(settings.graphics) {
		return error('invalid graphics settings')
	}
	mut c_settings := C.EngineSettings{
		window_name:   settings.title.str
		window_width:  settings.width
		window_height: settings.height
		window_pos_x:  settings.x
		window_pos_y:  settings.y
		vsync:         settings.vsync
		target_fps:    settings.target_fps
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

// set_vsync enables/disables vertical sync. Changing it recreates the
// swapchain on the next frame.
pub fn (e &Engine) set_vsync(vsync bool) {
	C.engine_set_vsync(e.handle, vsync)
}

// vsync reports whether vertical sync is enabled.
pub fn (e &Engine) vsync() bool {
	return C.engine_get_vsync(e.handle)
}

// set_target_fps limits the game loop to the given FPS (0 disables the cap).
pub fn (e &Engine) set_target_fps(target_fps int) {
	C.engine_set_target_fps(e.handle, i32(target_fps))
}

// target_fps returns the current frame-rate cap (0 = no cap).
pub fn (e &Engine) target_fps() int {
	return int(C.engine_get_target_fps(e.handle))
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

// object_by_id resolves an active scene object ID, or none after destruction.
pub fn (e &Engine) object_by_id(id u32) ?Object {
	h := C.egn_scene_find_object_by_id(id)
	if h == 0 {
		return none
	}
	return Object{ handle: h }
}

// canvas_pick returns the top-most canvas object under the given screen point
// (pixels from the window's top-left corner), or none when nothing was hit.
// Canvas objects can be created with Object.set_render_domain(.canvas) and
// Object.set_canvas_anchor(...).
pub fn (e &Engine) canvas_pick(x f32, y f32) ?Object {
	width, height := e.window_size()
	id := C.egn_canvas_pick(x, y, f32(width), f32(height))
	if id == 0 {
		return none
	}
	return e.object_by_id(id)
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
	return C.inputs_key_down(int(key)) && !C.inputs_was_key_down(int(key))
}

// key_released is true only on the frame the key went up.
pub fn (e &Engine) key_released(key Key) bool {
	return C.inputs_key_up(int(key)) && !C.inputs_was_key_up(int(key))
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
	return C.inputs_btn_down(int(button)) && !C.inputs_was_btn_down(int(button))
}

pub fn (e &Engine) mouse_released(button MouseButton) bool {
	return C.inputs_btn_up(int(button)) && !C.inputs_was_btn_up(int(button))
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

// mouse_delta returns the relative motion accumulated in the last frame.
// Use it for FPS camera look while the pointer is locked.
pub fn (e &Engine) mouse_delta() (f32, f32) {
	mut dx := f32(0)
	mut dy := f32(0)
	C.inputs_get_mouse_delta(&dx, &dy)
	return dx, dy
}

// set_pointer_lock locks/releases the mouse cursor. While locked the cursor
// is hidden and mouse_delta reports unrestricted relative motion.
pub fn (e &Engine) set_pointer_lock(locked bool) {
	C.engine_set_pointer_lock(e.handle, locked)
}

// pointer_locked reports whether the pointer lock is currently active.
pub fn (e &Engine) pointer_locked() bool {
	return C.engine_is_pointer_locked(e.handle)
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
