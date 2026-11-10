#pragma once

#include "vkrenderer_types.h"

b8 vkrenderer_sky_create(VkContext *ctx);
void vkrenderer_sky_destroy(VkContext *ctx);

// Draws the procedural atmosphere over the current camera viewport. Must be
// called inside the HDR scene render pass, after the camera viewport/scissor
// was set.
void vkrenderer_sky_render(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                           mat4 inverse_view_projection);
