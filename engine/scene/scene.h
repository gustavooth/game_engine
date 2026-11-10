#pragma once

#include "defines.h"

// ---------------------------------------------------------------------------
// Scene lifecycle (called by the engine core)
// ---------------------------------------------------------------------------

b8 egn_scene_initialize(void);
void egn_scene_shutdown(void);

// ---------------------------------------------------------------------------
// Scene
// ---------------------------------------------------------------------------

/** @brief Returns the scene root object (object id 1). */
EGNAPI void *egn_scene_root(void);

// ---------------------------------------------------------------------------
// Objects
// ---------------------------------------------------------------------------

EGNAPI b8 egn_object_is_valid(void *object);
EGNAPI u32 egn_object_get_id(void *object);
EGNAPI void egn_object_set_name(void *object, const char *name);
EGNAPI const char *egn_object_get_name(void *object);

EGNAPI void *egn_object_create(void *parent, const char *name);
EGNAPI void egn_object_destroy(void *object);
EGNAPI void *egn_object_get_parent(void *object);
EGNAPI void egn_object_set_parent(void *object, void *parent);
EGNAPI u32 egn_object_child_count(void *object);
EGNAPI void *egn_object_child_at(void *object, u32 index);

EGNAPI void egn_object_set_position(void *object, f32 x, f32 y, f32 z);
EGNAPI void egn_object_get_position(void *object, f32 *x, f32 *y, f32 *z);

EGNAPI void egn_object_set_rotation_quat(void *object, f32 x, f32 y, f32 z, f32 w);
EGNAPI void egn_object_get_rotation_quat(void *object, f32 *x, f32 *y, f32 *z, f32 *w);

/** @brief Sets rotation from Euler angles in radians (X * Y * Z order). */
EGNAPI void egn_object_set_rotation_euler(void *object, f32 x, f32 y, f32 z);

/** @brief Applies an additional local rotation from Euler angles in radians. */
EGNAPI void egn_object_rotate_euler(void *object, f32 x, f32 y, f32 z);

/** @brief Orients the object so its local -Z axis points at the world target. */
EGNAPI void egn_object_look_at(void *object, f32 target_x, f32 target_y, f32 target_z);

EGNAPI void egn_object_set_scale(void *object, f32 x, f32 y, f32 z);
EGNAPI void egn_object_get_scale(void *object, f32 *x, f32 *y, f32 *z);

EGNAPI void egn_object_set_layer(void *object, u32 layer);
EGNAPI u32 egn_object_get_layer(void *object);

EGNAPI void egn_object_set_active(void *object, b8 active);
EGNAPI b8 egn_object_is_active(void *object);
EGNAPI b8 egn_object_is_active_in_hierarchy(void *object);

// ---------------------------------------------------------------------------
// Components
// ---------------------------------------------------------------------------

// Component type values shared with the V SDK.
enum
{
  EGN_COMPONENT_TYPE_CAMERA = 0,
  EGN_COMPONENT_TYPE_MODEL = 1,
  EGN_COMPONENT_TYPE_SPRITE = 2,
  EGN_COMPONENT_TYPE_ANIMATION = 3,
  EGN_COMPONENT_TYPE_LIGHT = 4,
  EGN_COMPONENT_TYPE_RIGIDBODY = 5,
  EGN_COMPONENT_TYPE_COLLIDER = 6,
  EGN_COMPONENT_TYPE_PARTICLES = 7
};

// Projection values shared with the V SDK.
enum
{
  EGN_PROJECTION_PERSPECTIVE = 0,
  EGN_PROJECTION_ORTHOGRAPHIC = 1
};

// Light values shared with the V SDK.
enum
{
  EGN_LIGHT_DIRECTIONAL = 0,
  EGN_LIGHT_POINT = 1
};

// Physics values shared with the V SDK.
enum
{
  EGN_PHYSICS_MOTION_STATIC = 0,
  EGN_PHYSICS_MOTION_KINEMATIC = 1,
  EGN_PHYSICS_MOTION_DYNAMIC = 2
};

enum
{
  EGN_COLLIDER_BOX = 0,
  EGN_COLLIDER_SPHERE = 1,
  EGN_COLLIDER_MESH = 2
};

EGNAPI void *egn_object_add_camera(void *object, i32 projection,
                                   f32 fov, f32 ortho_size,
                                   f32 near_clip, f32 far_clip,
                                   f32 viewport_x, f32 viewport_y,
                                   f32 viewport_width, f32 viewport_height,
                                   u32 layer_mask,
                                   f32 clear_r, f32 clear_g, f32 clear_b, f32 clear_a,
                                   b8 clear_color, b8 clear_depth,
                                   i32 order, b8 enabled);

EGNAPI void *egn_object_add_model(void *object, u32 mesh, u32 texture,
                                  f32 r, f32 g, f32 b, f32 a,
                                  b8 cast_shadows, b8 enabled);

EGNAPI void *egn_object_add_sprite(void *object, u32 texture,
                                   f32 width, f32 height,
                                   f32 pivot_x, f32 pivot_y,
                                   f32 r, f32 g, f32 b, f32 a, b8 enabled);

EGNAPI void *egn_object_add_animation(void *object, i32 clip, f32 speed,
                                      b8 loop, b8 playing, b8 enabled);

EGNAPI void *egn_object_add_light(void *object, i32 light_type,
                                  f32 r, f32 g, f32 b, f32 intensity,
                                  f32 range, b8 cast_shadows, b8 enabled);

EGNAPI void *egn_object_add_rigidbody(void *object, i32 motion_type, b8 is_2d,
                                      f32 gravity_factor, f32 friction,
                                      f32 restitution, f32 linear_damping,
                                      f32 angular_damping, b8 enabled);

EGNAPI void *egn_object_add_collider(void *object, i32 shape_type,
                                     f32 size_x, f32 size_y, f32 size_z,
                                     f32 radius, u32 mesh,
                                     f32 offset_x, f32 offset_y, f32 offset_z,
                                     b8 enabled);

EGNAPI void *egn_object_add_particles(void *object, u32 max_particles, u32 texture,
                                      f32 spawn_rate, f32 lifetime, f32 speed,
                                      f32 speed_variation, f32 gravity, f32 drag,
                                      f32 spread, f32 radius,
                                      f32 dir_x, f32 dir_y, f32 dir_z,
                                      f32 size_start, f32 size_end, f32 size_scale,
                                      f32 fade_in, f32 fade_out,
                                      f32 r0, f32 g0, f32 b0, f32 a0,
                                      f32 r1, f32 g1, f32 b1, f32 a1,
                                      b8 enabled);

EGNAPI void *egn_object_get_component(void *object, i32 type);
EGNAPI void *egn_object_get_component_at(void *object, i32 type, u32 index);
EGNAPI u32 egn_object_component_count(void *object, i32 type);

EGNAPI void egn_component_destroy(void *component);
EGNAPI b8 egn_component_is_valid(void *component);
EGNAPI i32 egn_component_get_type(void *component);
EGNAPI void egn_component_set_enabled(void *component, b8 enabled);
EGNAPI b8 egn_component_is_enabled(void *component);

// Camera component ---------------------------------------------------------

EGNAPI i32 egn_camera_get_projection(void *camera);
EGNAPI void egn_camera_set_projection(void *camera, i32 projection);
EGNAPI f32 egn_camera_get_fov(void *camera);
EGNAPI void egn_camera_set_fov(void *camera, f32 fov);
EGNAPI f32 egn_camera_get_ortho_size(void *camera);
EGNAPI void egn_camera_set_ortho_size(void *camera, f32 ortho_size);
EGNAPI f32 egn_camera_get_near(void *camera);
EGNAPI f32 egn_camera_get_far(void *camera);
EGNAPI void egn_camera_set_clip_planes(void *camera, f32 near_clip, f32 far_clip);
EGNAPI void egn_camera_get_viewport(void *camera, f32 *x, f32 *y, f32 *w, f32 *h);
EGNAPI void egn_camera_set_viewport(void *camera, f32 x, f32 y, f32 w, f32 h);
EGNAPI u32 egn_camera_get_layer_mask(void *camera);
EGNAPI void egn_camera_set_layer_mask(void *camera, u32 layer_mask);
EGNAPI void egn_camera_get_clear_color(void *camera, f32 *r, f32 *g, f32 *b, f32 *a);
EGNAPI void egn_camera_set_clear_color(void *camera, f32 r, f32 g, f32 b, f32 a);
EGNAPI b8 egn_camera_get_clear_color_enabled(void *camera);
EGNAPI void egn_camera_set_clear_color_enabled(void *camera, b8 enabled);
EGNAPI b8 egn_camera_get_clear_depth_enabled(void *camera);
EGNAPI void egn_camera_set_clear_depth_enabled(void *camera, b8 enabled);
EGNAPI i32 egn_camera_get_order(void *camera);
EGNAPI void egn_camera_set_order(void *camera, i32 order);

// Model component ----------------------------------------------------------

EGNAPI u32 egn_model_get_mesh(void *model);
EGNAPI void egn_model_set_mesh(void *model, u32 mesh);
EGNAPI u32 egn_model_get_texture(void *model);
EGNAPI void egn_model_set_texture(void *model, u32 texture);
EGNAPI void egn_model_get_color(void *model, f32 *r, f32 *g, f32 *b, f32 *a);
EGNAPI void egn_model_set_color(void *model, f32 r, f32 g, f32 b, f32 a);
EGNAPI b8 egn_model_get_cast_shadows(void *model);
EGNAPI void egn_model_set_cast_shadows(void *model, b8 cast_shadows);

// Sprite component ---------------------------------------------------------

EGNAPI u32 egn_sprite_get_texture(void *sprite);
EGNAPI void egn_sprite_set_texture(void *sprite, u32 texture);
EGNAPI void egn_sprite_get_size(void *sprite, f32 *width, f32 *height);
EGNAPI void egn_sprite_set_size(void *sprite, f32 width, f32 height);
EGNAPI void egn_sprite_get_pivot(void *sprite, f32 *x, f32 *y);
EGNAPI void egn_sprite_set_pivot(void *sprite, f32 x, f32 y);
EGNAPI void egn_sprite_get_color(void *sprite, f32 *r, f32 *g, f32 *b, f32 *a);
EGNAPI void egn_sprite_set_color(void *sprite, f32 r, f32 g, f32 b, f32 a);

// Animation component ------------------------------------------------------

EGNAPI i32 egn_animation_get_clip(void *animation);
EGNAPI void egn_animation_set_clip(void *animation, i32 clip);
EGNAPI f32 egn_animation_get_time(void *animation);
EGNAPI void egn_animation_set_time(void *animation, f32 time);
EGNAPI f32 egn_animation_get_speed(void *animation);
EGNAPI void egn_animation_set_speed(void *animation, f32 speed);
EGNAPI b8 egn_animation_get_loop(void *animation);
EGNAPI void egn_animation_set_loop(void *animation, b8 loop);
EGNAPI b8 egn_animation_get_playing(void *animation);
EGNAPI void egn_animation_set_playing(void *animation, b8 playing);

// Light component ----------------------------------------------------------

EGNAPI i32 egn_light_get_type(void *light);
EGNAPI void egn_light_set_type(void *light, i32 light_type);
EGNAPI void egn_light_get_color(void *light, f32 *r, f32 *g, f32 *b);
EGNAPI void egn_light_set_color(void *light, f32 r, f32 g, f32 b);
EGNAPI f32 egn_light_get_intensity(void *light);
EGNAPI void egn_light_set_intensity(void *light, f32 intensity);
EGNAPI f32 egn_light_get_range(void *light);
EGNAPI void egn_light_set_range(void *light, f32 range);
EGNAPI b8 egn_light_get_cast_shadows(void *light);
EGNAPI void egn_light_set_cast_shadows(void *light, b8 cast_shadows);

// Rigidbody component ------------------------------------------------------

EGNAPI i32 egn_rigidbody_get_motion_type(void *rigidbody);
EGNAPI void egn_rigidbody_set_motion_type(void *rigidbody, i32 motion_type);
EGNAPI b8 egn_rigidbody_is_2d(void *rigidbody);
EGNAPI void egn_rigidbody_set_2d(void *rigidbody, b8 is_2d);
EGNAPI f32 egn_rigidbody_get_gravity_factor(void *rigidbody);
EGNAPI void egn_rigidbody_set_gravity_factor(void *rigidbody, f32 factor);
EGNAPI f32 egn_rigidbody_get_mass(void *rigidbody);
EGNAPI void egn_rigidbody_set_mass(void *rigidbody, f32 mass);
EGNAPI f32 egn_rigidbody_get_friction(void *rigidbody);
EGNAPI void egn_rigidbody_set_friction(void *rigidbody, f32 friction);
EGNAPI f32 egn_rigidbody_get_restitution(void *rigidbody);
EGNAPI void egn_rigidbody_set_restitution(void *rigidbody, f32 restitution);
EGNAPI f32 egn_rigidbody_get_linear_damping(void *rigidbody);
EGNAPI void egn_rigidbody_set_linear_damping(void *rigidbody, f32 damping);
EGNAPI f32 egn_rigidbody_get_angular_damping(void *rigidbody);
EGNAPI void egn_rigidbody_set_angular_damping(void *rigidbody, f32 damping);
EGNAPI void egn_rigidbody_apply_impulse(void *rigidbody, f32 x, f32 y, f32 z);
EGNAPI void egn_rigidbody_get_linear_velocity(void *rigidbody, f32 *x, f32 *y, f32 *z);
EGNAPI void egn_rigidbody_set_linear_velocity(void *rigidbody, f32 x, f32 y, f32 z);
EGNAPI void egn_rigidbody_get_angular_velocity(void *rigidbody, f32 *x, f32 *y, f32 *z);
EGNAPI void egn_rigidbody_set_angular_velocity(void *rigidbody, f32 x, f32 y, f32 z);

// Collider component -------------------------------------------------------

EGNAPI i32 egn_collider_get_shape(void *collider);
EGNAPI void egn_collider_set_shape(void *collider, i32 shape_type);
EGNAPI void egn_collider_get_size(void *collider, f32 *x, f32 *y, f32 *z);
EGNAPI void egn_collider_set_size(void *collider, f32 x, f32 y, f32 z);
EGNAPI f32 egn_collider_get_radius(void *collider);
EGNAPI void egn_collider_set_radius(void *collider, f32 radius);
EGNAPI u32 egn_collider_get_mesh(void *collider);
EGNAPI void egn_collider_set_mesh(void *collider, u32 mesh);
EGNAPI void egn_collider_get_offset(void *collider, f32 *x, f32 *y, f32 *z);
EGNAPI void egn_collider_set_offset(void *collider, f32 x, f32 y, f32 z);

// Particle emitter component ------------------------------------------------

EGNAPI u32 egn_particles_get_max(void *particles);
EGNAPI void egn_particles_set_max(void *particles, u32 max_particles);
EGNAPI u32 egn_particles_get_texture(void *particles);
EGNAPI void egn_particles_set_texture(void *particles, u32 texture);
EGNAPI f32 egn_particles_get_spawn_rate(void *particles);
EGNAPI void egn_particles_set_spawn_rate(void *particles, f32 spawn_rate);
EGNAPI f32 egn_particles_get_lifetime(void *particles);
EGNAPI void egn_particles_set_lifetime(void *particles, f32 lifetime);
EGNAPI f32 egn_particles_get_speed(void *particles);
EGNAPI void egn_particles_set_speed(void *particles, f32 speed);
EGNAPI f32 egn_particles_get_gravity(void *particles);
EGNAPI void egn_particles_set_gravity(void *particles, f32 gravity);
EGNAPI f32 egn_particles_get_drag(void *particles);
EGNAPI void egn_particles_set_drag(void *particles, f32 drag);
EGNAPI f32 egn_particles_get_spread(void *particles);
EGNAPI void egn_particles_set_spread(void *particles, f32 spread);
EGNAPI void egn_particles_set_direction(void *particles, f32 x, f32 y, f32 z);
EGNAPI void egn_particles_get_size(void *particles, f32 *start, f32 *end);
EGNAPI void egn_particles_set_size(void *particles, f32 start, f32 end);
EGNAPI void egn_particles_get_color_start(void *particles, f32 *r, f32 *g, f32 *b, f32 *a);
EGNAPI void egn_particles_set_color_start(void *particles, f32 r, f32 g, f32 b, f32 a);
EGNAPI void egn_particles_get_color_end(void *particles, f32 *r, f32 *g, f32 *b, f32 *a);
EGNAPI void egn_particles_set_color_end(void *particles, f32 r, f32 g, f32 b, f32 a);
EGNAPI void egn_particles_emit(void *particles, u32 count);

// Advances animation playback. Called once per frame by the engine core.
void egn_scene_update(f64 delta_time);
