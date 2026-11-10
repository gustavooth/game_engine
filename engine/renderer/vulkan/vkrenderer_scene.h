#pragma once

#include "vkrenderer_types.h"

/**
 * Prepares CPU-side scene data for the current frame: evaluates skinned model
 * animations, fills the bone buffer and records the host-to-shader barrier.
 *
 * Must be called after begin_frame() and before the main render pass begins.
 */
b8 vkrenderer_scene_prepare(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                            f64 delta_time);

/**
 * Renders the directional shadow map. Must be called after
 * vkrenderer_scene_prepare() and before the main render pass begins.
 */
void vkrenderer_scene_render_shadows(VkContext *ctx, VkRendererCommandBuffer *command_buffer);

/**
 * Renders every enabled camera in the active scene. Each camera clears its
 * viewport (optional), sets its viewport/scissor and draws the Model/Sprite
 * components whose layer matches its layer mask.
 *
 * Must be called inside the main render pass.
 */
void vkrenderer_scene_render(VkContext *ctx, VkRendererCommandBuffer *command_buffer);

/** Frees renderer-side per-model animation state. */
void vkrenderer_scene_shutdown(VkContext *ctx);
