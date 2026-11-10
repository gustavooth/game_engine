#pragma once

#include "vkrenderer_types.h"

#define VKRENDERER_MAX_POINT_LIGHTS 8
#define VKRENDERER_SHADOW_SIZE 2048

typedef struct VkRendererPointLight
{
  vec4 position; // xyz = world position, w = range
  vec4 color;    // rgb = colour, w = intensity
} VkRendererPointLight;

typedef struct VkRendererLighting
{
  vec4 ambient;       // rgb = colour, w = intensity
  vec4 sun_direction; // xyz = direction toward the sun, w = intensity
  vec4 sun_color;     // rgb = colour, w = shadow strength
  mat4 sun_view_projection;
  vec4 point_count; // x = count, y = shadow enabled, z/w = shadow texel size
  VkRendererPointLight point_lights[VKRENDERER_MAX_POINT_LIGHTS];
} VkRendererLighting;

b8 vkrenderer_lighting_create(VkContext *ctx);
void vkrenderer_lighting_destroy(VkContext *ctx);

void vkrenderer_lighting_write(VkContext *ctx, const VkRendererLighting *lighting);
const VkRendererLighting *vkrenderer_lighting_current(void);

VkDescriptorSet vkrenderer_lighting_descriptor(VkContext *ctx);
VkDescriptorSetLayout vkrenderer_lighting_set_layout(VkContext *ctx);

VkRenderPass vkrenderer_lighting_shadow_renderpass(VkContext *ctx);
VkFramebuffer vkrenderer_lighting_shadow_framebuffer(VkContext *ctx);
u32 vkrenderer_lighting_shadow_size(VkContext *ctx);
