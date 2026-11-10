#pragma once

#include "vkrenderer_types.h"

/**
 * Renders every enabled camera in the active scene. Each camera clears its
 * viewport (optional), sets its viewport/scissor and draws the Model/Sprite
 * components whose layer matches its layer mask.
 *
 * Must be called inside the main render pass.
 */
void vkrenderer_scene_render(VkContext *ctx, VkRendererCommandBuffer *command_buffer);
