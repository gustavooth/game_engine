#pragma once

#include "vkrenderer_types.h"

typedef struct VkRendererParticleParams
{
  vec3 position;
  vec3 direction;
  f32 spawn_rate;
  f32 spread;
  f32 lifetime;
  f32 speed;
  f32 speed_variation;
  f32 gravity;
  f32 drag;
  f32 radius;
  vec4 color_start;
  vec4 color_end;
  f32 size_start;
  f32 size_end;
  f32 size_scale;
  f32 fade_in;
  f32 fade_out;
} VkRendererParticleParams;

b8 vkrenderer_particles_create(VkContext *ctx);
void vkrenderer_particles_destroy(VkContext *ctx);

// Returns a 1-based particle system handle, or 0 on failure.
u32 vkrenderer_particle_system_create(VkContext *ctx, u32 max_particles, u32 texture);
void vkrenderer_particle_system_destroy(VkContext *ctx, u32 handle);

void vkrenderer_particles_dispatch(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                                   u32 handle, const VkRendererParticleParams *params,
                                   f32 delta_time);

void vkrenderer_particle_system_render(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                                       u32 handle, const VkRendererParticleParams *params,
                                       mat4 view_projection, vec3 camera_right, vec3 camera_up);
