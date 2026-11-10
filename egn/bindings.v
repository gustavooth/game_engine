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
#include "math/noise.h"
#include "renderer/renderer.h"
#include "scene/scene.h"
#include "containers/egnstring.h"
#include "network/network.h"

// The SDK lives in <repo>/egn, the engine headers in <repo>/engine and the
// compiled shared library in <repo>/output/linux.
#flag -I @VMODROOT/../engine
#flag -L @VMODROOT/../output/linux
#flag -lengine
// Make the binary find libengine.so next to itself (output/linux/).
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
	vsync         bool
	target_fps    int
}

fn C.engine_create_instance(settings &C.EngineSettings, out_instance_ptr &voidptr) bool
fn C.engine_frame(instance_ptr voidptr) bool
fn C.engine_should_run(instance_ptr voidptr) bool
fn C.engine_request_quit(instance_ptr voidptr)
fn C.engine_get_delta_time(instance_ptr voidptr) f64
fn C.engine_get_elapsed_time(instance_ptr voidptr) f64
fn C.engine_get_window_size(instance_ptr voidptr, out_width &int, out_height &int) bool
fn C.engine_destroy_instance(instance_ptr voidptr)
fn C.engine_set_vsync(instance_ptr voidptr, vsync bool)
fn C.engine_get_vsync(instance_ptr voidptr) bool
fn C.engine_set_target_fps(instance_ptr voidptr, target_fps i32)
fn C.engine_get_target_fps(instance_ptr voidptr) i32
fn C.engine_set_pointer_lock(instance_ptr voidptr, locked bool)
fn C.engine_is_pointer_locked(instance_ptr voidptr) bool

@[typedef]
struct C.RendererGraphicsSettings {
mut:
	shadows_enabled bool
	shadow_quality  int
}
fn C.renderer_set_graphics_settings(settings &C.RendererGraphicsSettings) bool
fn C.renderer_get_graphics_settings() C.RendererGraphicsSettings

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
fn C.inputs_get_mouse_delta(out_x &f32, out_y &f32)
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
fn C.renderer_text_texture_load(path &char, text &C.EgnString, pixel_height f32, width &u32, height &u32) u32
fn C.renderer_texture_release(texture u32)
fn C.renderer_mesh_cube() u32
fn C.renderer_mesh_sphere() u32
fn C.renderer_mesh_create_static(positions &f32, normals &f32, uvs &f32, vertex_count u32, indices &u32, index_count u32) u32
fn C.renderer_mesh_load_obj(path &char) u32
fn C.renderer_mesh_load_glb(path &char) u32
fn C.renderer_mesh_release(mesh u32)
fn C.renderer_mesh_animation_count(mesh u32) u32
fn C.renderer_mesh_animation_name(mesh u32, index u32) &char
fn C.renderer_mesh_animation_duration(mesh u32, index u32) f32
fn C.renderer_screenshot(path &char) bool

@[typedef]
struct C.EgnString {
mut:
    data &char
    length u64
    capacity u64
}
fn C.egn_string_init(s &C.EgnString)
fn C.egn_string_from_utf8(s &C.EgnString, bytes &char, length u64) bool
fn C.egn_string_destroy(s &C.EgnString)
fn C.egn_string_utf8_valid(bytes &char, length u64) bool

// ---------------------------------------------------------------------------
// Scene / objects
// ---------------------------------------------------------------------------

fn C.egn_scene_root() voidptr
fn C.egn_scene_find_object_by_id(id u32) voidptr

@[typedef]
struct C.EgnPhysicsHit {
  object_id u32
  distance f32
}
fn C.egn_camera_ray_cast(camera voidptr, x f32, y f32, width f32, height f32, distance f32, out &C.EgnPhysicsHit, capacity u32) u32
fn C.egn_physics_ray_cast(x f32, y f32, z f32, dx f32, dy f32, dz f32, distance f32, out &C.EgnPhysicsHit, capacity u32) u32
fn C.egn_physics_overlap_sphere(x f32, y f32, z f32, radius f32, out &C.EgnPhysicsHit, capacity u32) u32
fn C.egn_physics_overlap_circle_2d(x f32, y f32, z f32, radius f32, out &C.EgnPhysicsHit, capacity u32) u32
fn C.egn_physics_overlap_box_2d(x f32, y f32, z f32, width f32, height f32, out &C.EgnPhysicsHit, capacity u32) u32
fn C.egn_physics_overlap_box(x f32, y f32, z f32, sx f32, sy f32, sz f32, out &C.EgnPhysicsHit, capacity u32) u32

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

fn C.egn_object_set_render_domain(object voidptr, domain i32)
fn C.egn_object_get_render_domain(object voidptr) i32
fn C.egn_object_set_canvas_anchor(object voidptr, anchor i32)
fn C.egn_object_get_canvas_anchor(object voidptr) i32
fn C.egn_object_set_layer(object voidptr, layer u32)
fn C.egn_object_get_layer(object voidptr) u32

fn C.egn_object_set_active(object voidptr, active bool)
fn C.egn_object_is_active(object voidptr) bool
fn C.egn_object_is_active_in_hierarchy(object voidptr) bool

fn C.egn_canvas_pick(screen_x f32, screen_y f32, framebuffer_width f32, framebuffer_height f32) u32
fn C.egn_object_canvas_rect(object voidptr, framebuffer_width f32, framebuffer_height f32, out_x &f32, out_y &f32, out_width &f32, out_height &f32) bool

// ---------------------------------------------------------------------------
// Components
// ---------------------------------------------------------------------------

fn C.egn_object_add_camera(object voidptr, projection i32, fov f32, ortho_size f32, near_clip f32, far_clip f32, viewport_x f32, viewport_y f32, viewport_width f32, viewport_height f32, layer_mask u32, clear_r f32, clear_g f32, clear_b f32, clear_a f32, clear_color bool, clear_depth bool, order i32, enabled bool) voidptr
fn C.egn_object_add_character(object voidptr, is_2d bool, radius f32, height f32, gravity f32, enabled bool) voidptr
fn C.egn_character_set_velocity(character voidptr, x f32, z f32)
fn C.egn_character_jump(character voidptr, speed f32)
fn C.egn_character_is_grounded(character voidptr) bool
fn C.egn_object_add_camera_2d(object voidptr, ortho_size f32, mask u32, order i32, enabled bool) voidptr
fn C.egn_object_add_canvas(object voidptr, mask u32, order i32, enabled bool) voidptr
fn C.egn_object_add_model(object voidptr, mesh u32, texture u32, r f32, g f32, b f32, a f32, cast_shadows bool, enabled bool) voidptr
fn C.egn_object_add_sprite(object voidptr, texture u32, width f32, height f32, pivot_x f32, pivot_y f32, r f32, g f32, b f32, a f32, enabled bool) voidptr
fn C.egn_object_add_animation(object voidptr, clip i32, speed f32, loop bool, playing bool, enabled bool) voidptr
fn C.egn_object_add_rigidbody(object voidptr, motion_type i32, is_2d bool, gravity_factor f32, friction f32, restitution f32, linear_damping f32, angular_damping f32, enabled bool) voidptr
fn C.egn_object_add_collider(object voidptr, shape_type i32, size_x f32, size_y f32, size_z f32, radius f32, mesh u32, offset_x f32, offset_y f32, offset_z f32, enabled bool) voidptr
fn C.egn_object_add_particles(object voidptr, max_particles u32, texture u32, spawn_rate f32, lifetime f32, speed f32, speed_variation f32, gravity f32, drag f32, spread f32, radius f32, dir_x f32, dir_y f32, dir_z f32, size_start f32, size_end f32, size_scale f32, fade_in f32, fade_out f32, r0 f32, g0 f32, b0 f32, a0 f32, r1 f32, g1 f32, b1 f32, a1 f32, enabled bool) voidptr
fn C.egn_object_add_light(object voidptr, light_type i32, r f32, g f32, b f32, intensity f32, range f32, cast_shadows bool, enabled bool) voidptr

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
fn C.egn_model_set_pbr(model voidptr, metallic f32, roughness f32) bool
fn C.egn_model_get_pbr(model voidptr, metallic &f32, roughness &f32)
fn C.egn_model_set_mesh(model voidptr, mesh u32)
fn C.egn_model_get_texture(model voidptr) u32
fn C.egn_model_set_texture(model voidptr, texture u32)
fn C.egn_model_get_color(model voidptr, r &f32, g &f32, b &f32, a &f32)
fn C.egn_model_set_color(model voidptr, r f32, g f32, b f32, a f32)
fn C.egn_model_get_cast_shadows(model voidptr) bool
fn C.egn_model_set_cast_shadows(model voidptr, cast_shadows bool)

// Sprite component --------------------------------------------------------

fn C.egn_sprite_get_texture(sprite voidptr) u32
fn C.egn_sprite_set_texture(sprite voidptr, texture u32)
fn C.egn_sprite_get_size(sprite voidptr, width &f32, height &f32)
fn C.egn_sprite_set_size(sprite voidptr, width f32, height f32)
fn C.egn_sprite_get_pivot(sprite voidptr, x &f32, y &f32)
fn C.egn_sprite_set_pivot(sprite voidptr, x f32, y f32)
fn C.egn_sprite_get_color(sprite voidptr, r &f32, g &f32, b &f32, a &f32)
fn C.egn_sprite_set_color(sprite voidptr, r f32, g f32, b f32, a f32)

// Animation component
fn C.egn_animation_get_clip(animation voidptr) i32
fn C.egn_animation_set_clip(animation voidptr, clip i32)
fn C.egn_animation_get_time(animation voidptr) f32
fn C.egn_animation_set_time(animation voidptr, time f32)
fn C.egn_animation_get_speed(animation voidptr) f32
fn C.egn_animation_set_speed(animation voidptr, speed f32)
fn C.egn_animation_get_loop(animation voidptr) bool
fn C.egn_animation_set_loop(animation voidptr, loop bool)
fn C.egn_animation_get_playing(animation voidptr) bool
fn C.egn_animation_set_playing(animation voidptr, playing bool)

// Light component
fn C.egn_light_get_type(light voidptr) i32
fn C.egn_light_set_type(light voidptr, light_type i32)
fn C.egn_light_get_color(light voidptr, r &f32, g &f32, b &f32)
fn C.egn_light_set_color(light voidptr, r f32, g f32, b f32)
fn C.egn_light_get_intensity(light voidptr) f32
fn C.egn_light_set_intensity(light voidptr, intensity f32)
fn C.egn_light_get_range(light voidptr) f32
fn C.egn_light_set_range(light voidptr, range f32)
fn C.egn_light_get_cast_shadows(light voidptr) bool
fn C.egn_light_set_cast_shadows(light voidptr, cast_shadows bool)

// Rigidbody component
fn C.egn_rigidbody_get_motion_type(rigidbody voidptr) i32
fn C.egn_rigidbody_set_motion_type(rigidbody voidptr, motion_type i32)
fn C.egn_rigidbody_is_2d(rigidbody voidptr) bool
fn C.egn_rigidbody_set_2d(rigidbody voidptr, is_2d bool)
fn C.egn_rigidbody_get_gravity_factor(rigidbody voidptr) f32
fn C.egn_rigidbody_set_gravity_factor(rigidbody voidptr, factor f32)
fn C.egn_rigidbody_get_mass(rigidbody voidptr) f32
fn C.egn_rigidbody_set_mass(rigidbody voidptr, mass f32)
fn C.egn_rigidbody_get_friction(rigidbody voidptr) f32
fn C.egn_rigidbody_set_friction(rigidbody voidptr, friction f32)
fn C.egn_rigidbody_get_restitution(rigidbody voidptr) f32
fn C.egn_rigidbody_set_restitution(rigidbody voidptr, restitution f32)
fn C.egn_rigidbody_get_linear_damping(rigidbody voidptr) f32
fn C.egn_rigidbody_set_linear_damping(rigidbody voidptr, damping f32)
fn C.egn_rigidbody_get_angular_damping(rigidbody voidptr) f32
fn C.egn_rigidbody_set_angular_damping(rigidbody voidptr, damping f32)
fn C.egn_rigidbody_apply_impulse(rigidbody voidptr, x f32, y f32, z f32)
fn C.egn_rigidbody_get_linear_velocity(rigidbody voidptr, x &f32, y &f32, z &f32)
fn C.egn_rigidbody_set_linear_velocity(rigidbody voidptr, x f32, y f32, z f32)
fn C.egn_rigidbody_get_angular_velocity(rigidbody voidptr, x &f32, y &f32, z &f32)
fn C.egn_rigidbody_set_angular_velocity(rigidbody voidptr, x f32, y f32, z f32)

// Collider component
fn C.egn_collider_get_shape(collider voidptr) i32
fn C.egn_collider_set_shape(collider voidptr, shape_type i32)
fn C.egn_collider_get_size(collider voidptr, x &f32, y &f32, z &f32)
fn C.egn_collider_set_size(collider voidptr, x f32, y f32, z f32)
fn C.egn_collider_get_radius(collider voidptr) f32
fn C.egn_collider_set_radius(collider voidptr, radius f32)
fn C.egn_collider_get_mesh(collider voidptr) u32
fn C.egn_collider_set_mesh(collider voidptr, mesh u32)
fn C.egn_collider_get_offset(collider voidptr, x &f32, y &f32, z &f32)
fn C.egn_collider_set_offset(collider voidptr, x f32, y f32, z f32)

// Particles component
fn C.egn_particles_get_max(particles voidptr) u32
fn C.egn_particles_set_max(particles voidptr, max_particles u32)
fn C.egn_particles_get_texture(particles voidptr) u32
fn C.egn_particles_set_texture(particles voidptr, texture u32)
fn C.egn_particles_get_spawn_rate(particles voidptr) f32
fn C.egn_particles_set_spawn_rate(particles voidptr, spawn_rate f32)
fn C.egn_particles_get_lifetime(particles voidptr) f32
fn C.egn_particles_set_lifetime(particles voidptr, lifetime f32)
fn C.egn_particles_get_speed(particles voidptr) f32
fn C.egn_particles_set_speed(particles voidptr, speed f32)
fn C.egn_particles_get_gravity(particles voidptr) f32
fn C.egn_particles_set_gravity(particles voidptr, gravity f32)
fn C.egn_particles_get_drag(particles voidptr) f32
fn C.egn_particles_set_drag(particles voidptr, drag f32)
fn C.egn_particles_get_spread(particles voidptr) f32
fn C.egn_particles_set_spread(particles voidptr, spread f32)
fn C.egn_particles_set_direction(particles voidptr, x f32, y f32, z f32)
fn C.egn_particles_get_size(particles voidptr, start &f32, end &f32)
fn C.egn_particles_set_size(particles voidptr, start f32, end f32)
fn C.egn_particles_get_color_start(particles voidptr, r &f32, g &f32, b &f32, a &f32)
fn C.egn_particles_set_color_start(particles voidptr, r f32, g f32, b f32, a f32)
fn C.egn_particles_get_color_end(particles voidptr, r &f32, g &f32, b &f32, a &f32)
fn C.egn_particles_set_color_end(particles voidptr, r f32, g f32, b f32, a f32)
fn C.egn_particles_emit(particles voidptr, count u32)

// Network bindings ---------------------------------------------------------

fn C.egn_net_server_create(port u16, udp bool) voidptr
fn C.egn_net_server_destroy(server voidptr)
fn C.egn_net_server_poll(server voidptr)
fn C.egn_net_server_broadcast(server voidptr, data voidptr, size u32) bool
fn C.egn_net_server_send(server voidptr, client_id u32, data voidptr, size u32) bool
fn C.egn_net_server_poll_event(server voidptr, event voidptr) bool
fn C.egn_net_server_client_count(server voidptr) u32

fn C.egn_net_client_create(host &char, port u16, udp bool) voidptr
fn C.egn_net_client_destroy(client voidptr)
fn C.egn_net_client_poll(client voidptr)
fn C.egn_net_client_send(client voidptr, data voidptr, size u32) bool
fn C.egn_net_client_poll_event(client voidptr, event voidptr) bool
fn C.egn_net_client_is_connected(client voidptr) bool

fn C.egn_net_host_create(port u16, udp bool) voidptr
fn C.egn_net_host_destroy(host voidptr)
fn C.egn_net_host_poll(host voidptr)
fn C.egn_net_host_send(host voidptr, data voidptr, size u32) bool
fn C.egn_net_host_broadcast(host voidptr, data voidptr, size u32) bool
fn C.egn_net_host_poll_server_event(host voidptr, event voidptr) bool
fn C.egn_net_host_poll_client_event(host voidptr, event voidptr) bool
fn C.egn_net_host_client_count(host voidptr) u32
fn C.egn_net_host_client_connected(host voidptr) bool
