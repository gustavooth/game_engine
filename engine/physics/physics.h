#pragma once

#include "defines.h"

// Internal physics entry points. The scene components are exposed to games
// through the regular scene/component API; this module only drives Jolt.

b8 egn_physics_initialize(void);
void egn_physics_shutdown(void);
void egn_physics_update(f64 delta_time);

// Used by the Rigidbody component accessors.
void egn_physics_apply_impulse(void *rigidbody, f32 x, f32 y, f32 z);
b8 egn_physics_get_linear_velocity(void *rigidbody, f32 *x, f32 *y, f32 *z);
void egn_physics_set_linear_velocity(void *rigidbody, f32 x, f32 y, f32 z);
b8 egn_physics_get_angular_velocity(void *rigidbody, f32 *x, f32 *y, f32 *z);
void egn_physics_set_angular_velocity(void *rigidbody, f32 x, f32 y, f32 z);
