#pragma once

#include "vkrenderer_types.h"

// Offscreen HDR scene render pass plus tone mapping / color grading output.
// The scene pass uses MRT: HDR colour, world-space normals and depth. AO/GI
// passes consume the normal/depth attachments in later stages.

b8 vkrenderer_post_create(VkContext *ctx);
void vkrenderer_post_destroy(VkContext *ctx);

// Recreates the offscreen attachments when the swapchain size changes.
void vkrenderer_post_resize(VkContext *ctx);

// Called by the atomic settings batch after the active settings changed.
void vkrenderer_post_apply_settings(VkContext *ctx);

VkRenderPass vkrenderer_post_scene_renderpass(VkContext *ctx);

// Scene pass (colour + normal + depth). Ends before post_process().
void vkrenderer_post_begin_scene(VkContext *ctx, VkRendererCommandBuffer *command_buffer);
void vkrenderer_post_end_scene(VkContext *ctx, VkRendererCommandBuffer *command_buffer);

// Tone mapping + color grading into the current swapchain image. Must be
// called outside the scene render pass.
void vkrenderer_post_process(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                             f64 delta_time);
