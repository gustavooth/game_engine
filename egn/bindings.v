module egn

// Low-level C bindings for the EGN engine.
//
// Everything in this file maps 1:1 to the engine's public C ABI. Game code
// should normally use the higher-level wrappers in the other SDK files and
// never call these declarations directly.

#include "core/engine.h"
#include "core/logger.h"
#include "core/events.h"
#include "core/inputs.h"
#include "renderer/renderer.h"
#include "scene/scene.h"

// The SDK lives in <repo>/egn, the engine headers in <repo>/engine and the
// compiled shared library in <repo>/bin.
#flag -I @VMODROOT/../engine
#flag -L @VMODROOT/../bin
#flag -lengine
// Make the binary find libengine.so next to itself (bin/).
#flag linux -Wl,-rpath,'$ORIGIN'

// ---------------------------------------------------------------------------
// Engine settings
// ---------------------------------------------------------------------------

// C.EngineSettings mirrors `EngineSettings` from engine/core/engine.h.
// Keep the field order and types in sync with the C header.
@[typedef]
pub struct C.EngineSettings {
pub mut:
	window_name   &char
	window_width  int
	window_height int
	window_pos_x  int
	window_pos_y  int
}

fn C.engine_create_instance(settings &C.EngineSettings, out_instance_ptr &voidptr) bool
fn C.engine_frame(instance_ptr voidptr) bool
fn C.engine_should_run(instance_ptr voidptr) bool
fn C.engine_request_quit(instance_ptr voidptr)
fn C.engine_get_delta_time(instance_ptr voidptr) f64
fn C.engine_get_elapsed_time(instance_ptr voidptr) f64
fn C.engine_get_window_size(instance_ptr voidptr, out_width &int, out_height &int) bool
fn C.engine_destroy_instance(instance_ptr voidptr)

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

fn C.log_output(level int, message &char, ...voidptr)

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

fn C.inputs_key_down(key int) bool
fn C.inputs_key_up(key int) bool
fn C.inputs_was_key_down(key int) bool
fn C.inputs_was_key_up(key int) bool

fn C.inputs_btn_down(button int) bool
fn C.inputs_btn_up(button int) bool
fn C.inputs_was_btn_down(button int) bool
fn C.inputs_was_btn_up(button int) bool

fn C.inputs_get_mouse_pos(out_x &int, out_y &int)
fn C.inputs_get_prev_mouse_pos(out_x &int, out_y &int)
fn C.inputs_mouse_wheel() i16
fn C.inputs_prev_mouse_wheel() i16

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

fn C.events_register(code u16, listener voidptr, on_event EventCallback)
fn C.events_unregister(code u16, on_event EventCallback) bool
fn C.events_fire2(code u16, sender voidptr, data0 u64, data1 u64) bool

// ---------------------------------------------------------------------------
// Renderer resources
// ---------------------------------------------------------------------------

fn C.renderer_texture_load(path &char) u32
fn C.renderer_texture_release(texture u32)
fn C.renderer_mesh_cube() u32
fn C.renderer_mesh_sphere() u32
fn C.renderer_screenshot(path &char) bool

// ---------------------------------------------------------------------------
// Scene / objects
// ---------------------------------------------------------------------------

fn C.egn_scene_root() voidptr

fn C.egn_object_is_valid(object voidptr) bool
fn C.egn_object_get_id(object voidptr) u32
fn C.egn_object_set_name(object voidptr, name &char)
fn C.egn_object_get_name(object voidptr) &char

fn C.egn_object_create(parent voidptr, name &char) voidptr
fn C.egn_object_destroy(object voidptr)
fn C.egn_object_get_parent(object voidptr) voidptr
fn C.egn_object_set_parent(object voidptr, parent voidptr)
fn C.egn_object_child_count(object voidptr) u32
fn C.egn_object_child_at(object voidptr, index u32) voidptr

fn C.egn_object_set_position(object voidptr, x f32, y f32, z f32)
fn C.egn_object_get_position(object voidptr, x &f32, y &f32, z &f32)

fn C.egn_object_set_rotation_quat(object voidptr, x f32, y f32, z f32, w f32)
fn C.egn_object_get_rotation_quat(object voidptr, x &f32, y &f32, z &f32, w &f32)
fn C.egn_object_set_rotation_euler(object voidptr, x f32, y f32, z f32)
fn C.egn_object_rotate_euler(object voidptr, x f32, y f32, z f32)
fn C.egn_object_look_at(object voidptr, target_x f32, target_y f32, target_z f32)

fn C.egn_object_set_scale(object voidptr, x f32, y f32, z f32)
fn C.egn_object_get_scale(object voidptr, x &f32, y &f32, z &f32)

fn C.egn_object_set_layer(object voidptr, layer u32)
fn C.egn_object_get_layer(object voidptr) u32

fn C.egn_object_set_active(object voidptr, active bool)
fn C.egn_object_is_active(object voidptr) bool
fn C.egn_object_is_active_in_hierarchy(object voidptr) bool

// ---------------------------------------------------------------------------
// Components
// ---------------------------------------------------------------------------

fn C.egn_object_add_camera(object voidptr, projection i32, fov f32, ortho_size f32, near_clip f32, far_clip f32, viewport_x f32, viewport_y f32, viewport_width f32, viewport_height f32, layer_mask u32, clear_r f32, clear_g f32, clear_b f32, clear_a f32, clear_color bool, clear_depth bool, order i32, enabled bool) voidptr
fn C.egn_object_add_model(object voidptr, mesh u32, texture u32, r f32, g f32, b f32, a f32, enabled bool) voidptr
fn C.egn_object_add_sprite(object voidptr, texture u32, width f32, height f32, pivot_x f32, pivot_y f32, r f32, g f32, b f32, a f32, enabled bool) voidptr

fn C.egn_object_get_component(object voidptr, component_type i32) voidptr
fn C.egn_object_get_component_at(object voidptr, component_type i32, index u32) voidptr
fn C.egn_object_component_count(object voidptr, component_type i32) u32

fn C.egn_component_destroy(component voidptr)
fn C.egn_component_is_valid(component voidptr) bool
fn C.egn_component_get_type(component voidptr) i32
fn C.egn_component_set_enabled(component voidptr, enabled bool)
fn C.egn_component_is_enabled(component voidptr) bool

// Camera component ---------------------------------------------------------

fn C.egn_camera_get_projection(camera voidptr) i32
fn C.egn_camera_set_projection(camera voidptr, projection i32)
fn C.egn_camera_get_fov(camera voidptr) f32
fn C.egn_camera_set_fov(camera voidptr, fov f32)
fn C.egn_camera_get_ortho_size(camera voidptr) f32
fn C.egn_camera_set_ortho_size(camera voidptr, ortho_size f32)
fn C.egn_camera_get_near(camera voidptr) f32
fn C.egn_camera_get_far(camera voidptr) f32
fn C.egn_camera_set_clip_planes(camera voidptr, near_clip f32, far_clip f32)
fn C.egn_camera_get_viewport(camera voidptr, x &f32, y &f32, w &f32, h &f32)
fn C.egn_camera_set_viewport(camera voidptr, x f32, y f32, w f32, h f32)
fn C.egn_camera_get_layer_mask(camera voidptr) u32
fn C.egn_camera_set_layer_mask(camera voidptr, layer_mask u32)
fn C.egn_camera_get_clear_color(camera voidptr, r &f32, g &f32, b &f32, a &f32)
fn C.egn_camera_set_clear_color(camera voidptr, r f32, g f32, b f32, a f32)
fn C.egn_camera_get_clear_color_enabled(camera voidptr) bool
fn C.egn_camera_set_clear_color_enabled(camera voidptr, enabled bool)
fn C.egn_camera_get_clear_depth_enabled(camera voidptr) bool
fn C.egn_camera_set_clear_depth_enabled(camera voidptr, enabled bool)
fn C.egn_camera_get_order(camera voidptr) i32
fn C.egn_camera_set_order(camera voidptr, order i32)

// Model component ---------------------------------------------------------

fn C.egn_model_get_mesh(model voidptr) u32
fn C.egn_model_set_mesh(model voidptr, mesh u32)
fn C.egn_model_get_texture(model voidptr) u32
fn C.egn_model_set_texture(model voidptr, texture u32)
fn C.egn_model_get_color(model voidptr, r &f32, g &f32, b &f32, a &f32)
fn C.egn_model_set_color(model voidptr, r f32, g f32, b f32, a f32)

// Sprite component --------------------------------------------------------

fn C.egn_sprite_get_texture(sprite voidptr) u32
fn C.egn_sprite_set_texture(sprite voidptr, texture u32)
fn C.egn_sprite_get_size(sprite voidptr, width &f32, height &f32)
fn C.egn_sprite_set_size(sprite voidptr, width f32, height f32)
fn C.egn_sprite_get_pivot(sprite voidptr, x &f32, y &f32)
fn C.egn_sprite_set_pivot(sprite voidptr, x f32, y f32)
fn C.egn_sprite_get_color(sprite voidptr, r &f32, g &f32, b &f32, a &f32)
fn C.egn_sprite_set_color(sprite voidptr, r f32, g f32, b f32, a f32)
