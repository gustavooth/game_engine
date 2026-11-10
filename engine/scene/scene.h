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
  EGN_COMPONENT_TYPE_SPRITE = 2
};

// Projection values shared with the V SDK.
enum
{
  EGN_PROJECTION_PERSPECTIVE = 0,
  EGN_PROJECTION_ORTHOGRAPHIC = 1
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
                                  f32 r, f32 g, f32 b, f32 a, b8 enabled);

EGNAPI void *egn_object_add_sprite(void *object, u32 texture,
                                   f32 width, f32 height,
                                   f32 pivot_x, f32 pivot_y,
                                   f32 r, f32 g, f32 b, f32 a, b8 enabled);

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

// Sprite component ---------------------------------------------------------

EGNAPI u32 egn_sprite_get_texture(void *sprite);
EGNAPI void egn_sprite_set_texture(void *sprite, u32 texture);
EGNAPI void egn_sprite_get_size(void *sprite, f32 *width, f32 *height);
EGNAPI void egn_sprite_set_size(void *sprite, f32 width, f32 height);
EGNAPI void egn_sprite_get_pivot(void *sprite, f32 *x, f32 *y);
EGNAPI void egn_sprite_set_pivot(void *sprite, f32 x, f32 y);
EGNAPI void egn_sprite_get_color(void *sprite, f32 *r, f32 *g, f32 *b, f32 *a);
EGNAPI void egn_sprite_set_color(void *sprite, f32 r, f32 g, f32 b, f32 a);
