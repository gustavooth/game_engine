#pragma once

#include "vkrenderer_types.h"

#define VKRENDERER_SKY_LUT_WIDTH 256
#define VKRENDERER_SKY_LUT_HEIGHT 128

b8 vkrenderer_sky_create(VkContext *ctx);
void vkrenderer_sky_destroy(VkContext *ctx);

// Radiance of the sun outside the atmosphere (colour * intensity). Set by the
// scene every frame before vkrenderer_sky_update().
void vkrenderer_sky_set_sun(vec3 radiance);

// Renders the atmosphere into the sky-view LUT (upper hemisphere, lat-long).
// The LUT is what the sky pass and the PBR reflections sample, so the costly
// scattering integral runs on 32k texels instead of on every screen pixel.
// Must be called outside of a render pass, before the scene pass.
void vkrenderer_sky_update(VkContext *ctx, VkRendererCommandBuffer *command_buffer);

// Draws the sky over the current camera viewport. Must be called inside the
// HDR scene render pass, after the camera viewport/scissor was set.
void vkrenderer_sky_render(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                           mat4 inverse_view_projection);
