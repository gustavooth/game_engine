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
	// headless runs the engine as a server: no window, no renderer, nothing
	// is drawn. Scene, physics, events and networking keep working and
	// target_fps becomes the tick rate.
	headless bool
	graphics GraphicsSettings
}

pub enum GraphicsQuality {
	low    = 0
	medium = 1
	high   = 2
	ultra  = 3
}

pub enum GraphicsPreset {
	custom = 0
	low    = 1
	medium = 2
	high   = 3
	ultra  = 4
}

pub enum ToneMapper {
	reinhard = 0
	aces     = 1
	agx      = 2
}

// GraphicsSettings mirrors RendererGraphicsSettings in C. The defaults are the
// high preset; use graphics_preset(.ultra) to start from another preset and
// override individual fields before applying.
pub struct GraphicsSettings {
pub mut:
	// Label only. Every field below is applied explicitly, so presets can be
	// tweaked without changing this field.
	preset                    GraphicsPreset = .high
	hdr_enabled               bool           = true
	tone_mapping_enabled      bool           = true
	tone_mapper               ToneMapper     = .aces
	exposure_compensation     f32
	auto_exposure_enabled     bool           = true
	auto_exposure_quality     GraphicsQuality = .high
	auto_exposure_min_ev      f32            = -6.0
	auto_exposure_max_ev      f32            = 6.0
	auto_exposure_speed       f32            = 1.5
	color_grading_enabled     bool           = true
	contrast                  f32            = 1.0
	saturation                f32            = 1.0
	temperature               f32
	tint                      f32
	lift_r                    f32
	lift_g                    f32
	lift_b                    f32
	gamma_r                   f32            = 1.0
	gamma_g                   f32            = 1.0
	gamma_b                   f32            = 1.0
	gain_r                    f32            = 1.0
	gain_g                    f32            = 1.0
	gain_b                    f32            = 1.0
	vignette                  f32            = 0.15
	lut_enabled               bool
	lut_strength              f32            = 1.0
	lut_texture               u32
	bloom_enabled             bool           = true
	bloom_quality             GraphicsQuality = .high
	bloom_threshold           f32            = 1.1
	bloom_intensity           f32            = 0.055
	bloom_radius              f32            = 0.55
	shadows_enabled           bool           = true
	shadow_quality            GraphicsQuality = .high
	shadow_cascades           u32
	shadow_distance           f32            = 120.0
	shadow_softness           f32            = 1.0
	pbr_enabled               bool           = true
	ao_enabled                bool           = true
	ao_quality                GraphicsQuality = .high
	ao_radius                 f32            = 1.0
	ao_intensity              f32            = 0.7
	gi_enabled                bool           = true
	gi_quality                GraphicsQuality = .high
	gi_intensity              f32            = 0.55
	gi_distance               f32            = 12.0
	atmosphere_enabled        bool           = true
	atmosphere_quality        GraphicsQuality = .high
	atmosphere_rayleigh       f32            = 1.0
	atmosphere_mie            f32            = 0.0035
	atmosphere_turbidity      f32            = 2.4
	atmosphere_mie_anisotropy f32            = 0.76
	atmosphere_sun_intensity  f32            = 20.0
	fog_enabled               bool           = true
	fog_density               f32            = 0.008
	fog_height_falloff        f32            = 0.12
}

fn graphics_settings_to_c(settings GraphicsSettings) C.RendererGraphicsSettings {
	return C.RendererGraphicsSettings{
		preset:                      int(settings.preset)
		hdr_enabled:                 settings.hdr_enabled
		tone_mapping_enabled:        settings.tone_mapping_enabled
		tone_mapper:                 int(settings.tone_mapper)
		exposure_compensation:       settings.exposure_compensation
		auto_exposure_enabled:       settings.auto_exposure_enabled
		auto_exposure_quality:       int(settings.auto_exposure_quality)
		auto_exposure_min_ev:        settings.auto_exposure_min_ev
		auto_exposure_max_ev:        settings.auto_exposure_max_ev
		auto_exposure_speed:         settings.auto_exposure_speed
		color_grading_enabled:       settings.color_grading_enabled
		contrast:                    settings.contrast
		saturation:                  settings.saturation
		temperature:                 settings.temperature
		tint:                        settings.tint
		lift_r:                      settings.lift_r
		lift_g:                      settings.lift_g
		lift_b:                      settings.lift_b
		gamma_r:                     settings.gamma_r
		gamma_g:                     settings.gamma_g
		gamma_b:                     settings.gamma_b
		gain_r:                      settings.gain_r
		gain_g:                      settings.gain_g
		gain_b:                      settings.gain_b
		vignette:                    settings.vignette
		lut_enabled:                 settings.lut_enabled
		lut_strength:                settings.lut_strength
		lut_texture:                 settings.lut_texture
		bloom_enabled:               settings.bloom_enabled
		bloom_quality:               int(settings.bloom_quality)
		bloom_threshold:             settings.bloom_threshold
		bloom_intensity:             settings.bloom_intensity
		bloom_radius:                settings.bloom_radius
		shadows_enabled:             settings.shadows_enabled
		shadow_quality:              int(settings.shadow_quality)
		shadow_cascades:             settings.shadow_cascades
		shadow_distance:             settings.shadow_distance
		shadow_softness:             settings.shadow_softness
		pbr_enabled:                 settings.pbr_enabled
		ao_enabled:                  settings.ao_enabled
		ao_quality:                  int(settings.ao_quality)
		ao_radius:                   settings.ao_radius
		ao_intensity:                settings.ao_intensity
		gi_enabled:                  settings.gi_enabled
		gi_quality:                  int(settings.gi_quality)
		gi_intensity:                settings.gi_intensity
		gi_distance:                 settings.gi_distance
		atmosphere_enabled:          settings.atmosphere_enabled
		atmosphere_quality:          int(settings.atmosphere_quality)
		atmosphere_rayleigh:         settings.atmosphere_rayleigh
		atmosphere_mie:              settings.atmosphere_mie
		atmosphere_turbidity:        settings.atmosphere_turbidity
		atmosphere_mie_anisotropy:   settings.atmosphere_mie_anisotropy
		atmosphere_sun_intensity:    settings.atmosphere_sun_intensity
		fog_enabled:                 settings.fog_enabled
		fog_density:                 settings.fog_density
		fog_height_falloff:          settings.fog_height_falloff
	}
}

fn graphics_settings_from_c(value C.RendererGraphicsSettings) GraphicsSettings {
	return GraphicsSettings{
		preset:                      unsafe { GraphicsPreset(value.preset) }
		hdr_enabled:                 value.hdr_enabled
		tone_mapping_enabled:        value.tone_mapping_enabled
		tone_mapper:                 unsafe { ToneMapper(value.tone_mapper) }
		exposure_compensation:       value.exposure_compensation
		auto_exposure_enabled:       value.auto_exposure_enabled
		auto_exposure_quality:       unsafe { GraphicsQuality(value.auto_exposure_quality) }
		auto_exposure_min_ev:        value.auto_exposure_min_ev
		auto_exposure_max_ev:        value.auto_exposure_max_ev
		auto_exposure_speed:         value.auto_exposure_speed
		color_grading_enabled:       value.color_grading_enabled
		contrast:                    value.contrast
		saturation:                  value.saturation
		temperature:                 value.temperature
		tint:                        value.tint
		lift_r:                      value.lift_r
		lift_g:                      value.lift_g
		lift_b:                      value.lift_b
		gamma_r:                     value.gamma_r
		gamma_g:                     value.gamma_g
		gamma_b:                     value.gamma_b
		gain_r:                      value.gain_r
		gain_g:                      value.gain_g
		gain_b:                      value.gain_b
		vignette:                    value.vignette
		lut_enabled:                 value.lut_enabled
		lut_strength:                value.lut_strength
		lut_texture:                 value.lut_texture
		bloom_enabled:               value.bloom_enabled
		bloom_quality:               unsafe { GraphicsQuality(value.bloom_quality) }
		bloom_threshold:             value.bloom_threshold
		bloom_intensity:             value.bloom_intensity
		bloom_radius:                value.bloom_radius
		shadows_enabled:             value.shadows_enabled
		shadow_quality:              unsafe { GraphicsQuality(value.shadow_quality) }
		shadow_cascades:             value.shadow_cascades
		shadow_distance:             value.shadow_distance
		shadow_softness:             value.shadow_softness
		pbr_enabled:                 value.pbr_enabled
		ao_enabled:                  value.ao_enabled
		ao_quality:                  unsafe { GraphicsQuality(value.ao_quality) }
		ao_radius:                   value.ao_radius
		ao_intensity:                value.ao_intensity
		gi_enabled:                  value.gi_enabled
		gi_quality:                  unsafe { GraphicsQuality(value.gi_quality) }
		gi_intensity:                value.gi_intensity
		gi_distance:                 value.gi_distance
		atmosphere_enabled:          value.atmosphere_enabled
		atmosphere_quality:          unsafe { GraphicsQuality(value.atmosphere_quality) }
		atmosphere_rayleigh:         value.atmosphere_rayleigh
		atmosphere_mie:              value.atmosphere_mie
		atmosphere_turbidity:        value.atmosphere_turbidity
		atmosphere_mie_anisotropy:   value.atmosphere_mie_anisotropy
		atmosphere_sun_intensity:    value.atmosphere_sun_intensity
		fog_enabled:                 value.fog_enabled
		fog_density:                 value.fog_density
		fog_height_falloff:          value.fog_height_falloff
	}
}

// graphics_preset returns a fully populated preset ready to be tweaked and
// passed to set_graphics_settings or Settings.graphics.
pub fn graphics_preset(preset GraphicsPreset) GraphicsSettings {
	return graphics_settings_from_c(C.renderer_graphics_settings_preset(int(preset)))
}

// Applies all graphics options as one atomic batch on the next frame. Returns
// false when any quality/range is invalid.
pub fn set_graphics_settings(settings GraphicsSettings) bool {
	value := graphics_settings_to_c(settings)
	return C.renderer_set_graphics_settings(&value)
}

// graphics_settings returns the most recently requested settings.
pub fn graphics_settings() GraphicsSettings {
	return graphics_settings_from_c(C.renderer_get_graphics_settings())
}

// active_graphics_settings returns what the renderer is currently using. It
// can lag the requested settings by one frame while resources are recreated.
pub fn active_graphics_settings() GraphicsSettings {
	return graphics_settings_from_c(C.renderer_get_active_graphics_settings())
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
		headless:      settings.headless
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
// is_headless reports whether the engine runs without window/renderer.
pub fn (e &Engine) is_headless() bool {
	return e.handle != 0 && C.engine_is_headless(e.handle)
}

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

// canvas_hovered is true while the mouse cursor is over the canvas object:
// the top-most canvas sprite under the cursor belongs to `object` or to one of
// its descendants. It is always false while the pointer is locked, because a
// locked pointer has no cursor position.
pub fn (e &Engine) canvas_hovered(object Object) bool {
	if !object.is_valid() || e.pointer_locked() {
		return false
	}
	x, y := e.mouse_position()
	hit := e.canvas_pick(f32(x), f32(y)) or { return false }
	target := object.id()
	mut current := hit
	for _ in 0 .. 64 {
		if current.id() == target {
			return true
		}
		current = current.parent() or { return false }
	}
	return false
}

// canvas_clicked is true on the frame `button` is pressed with the cursor
// over the canvas object (see canvas_hovered).
pub fn (e &Engine) canvas_clicked(object Object, button MouseButton) bool {
	return e.mouse_pressed(button) && e.canvas_hovered(object)
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
