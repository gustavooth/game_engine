#pragma once

#include "vkrenderer_types.h"

#define VKRENDERER_MAX_POINT_LIGHTS 8
#define VKRENDERER_MAX_SHADOW_CASCADES 4

typedef struct VkRendererPointLight
{
  vec4 position; // xyz = world position, w = range
  vec4 color;    // rgb = colour, w = intensity
} VkRendererPointLight;

typedef struct VkRendererLighting
{
  vec4 ambient;         // rgb = fallback ambient colour, w = intensity
  vec4 sun_direction;   // xyz = direction toward the sun, w = intensity
  vec4 sun_color;       // rgb = colour reaching the ground, w = shadow strength
  vec4 camera_position; // xyz = primary camera, w = unused
  mat4 sun_view_projection[VKRENDERER_MAX_SHADOW_CASCADES];
  vec4 cascade_splits; // xyz/w = far distance of each cascade
  vec4 shadow_params;  // x = texel size, y = enabled, z = cascade count, w = softness
  vec4 point_count;    // x = count, yzw = reserved
  vec4 fog_params;     // x = density, y = height falloff, z = enabled, w = sun in-scatter
  vec4 fog_color;      // rgb = fog colour, w = unused
  vec4 time;           // x = elapsed seconds
  vec4 cascade_texel;  // world-space size of one shadow texel per cascade
  vec4 shadow_fade;    // x = shadow distance, y = fade length, z = cascade blend fraction
  vec4 sky_zenith;     // rgb = sky radiance straight up
  vec4 sky_horizon;    // rgb = sky radiance at the horizon
  vec4 sky_ground;     // rgb = radiance bounced by the ground
  VkRendererPointLight point_lights[VKRENDERER_MAX_POINT_LIGHTS];
} VkRendererLighting;

b8 vkrenderer_lighting_create(VkContext *ctx);
void vkrenderer_lighting_destroy(VkContext *ctx);

// Recreates shadow images when the shadow resolution changes at runtime.
void vkrenderer_lighting_apply_settings(VkContext *ctx);

// Binds the sky-view LUT sampled by the PBR reflections (binding 5).
void vkrenderer_lighting_set_sky_lut(VkContext *ctx, VkImageView view, VkSampler sampler);

void vkrenderer_lighting_write(VkContext *ctx, const VkRendererLighting *lighting);
const VkRendererLighting *vkrenderer_lighting_current(void);

VkDescriptorSet vkrenderer_lighting_descriptor(VkContext *ctx);
VkDescriptorSetLayout vkrenderer_lighting_set_layout(VkContext *ctx);

VkRenderPass vkrenderer_lighting_shadow_renderpass(VkContext *ctx);
VkFramebuffer vkrenderer_lighting_shadow_framebuffer(VkContext *ctx, u32 cascade);
u32 vkrenderer_lighting_shadow_size(VkContext *ctx);
