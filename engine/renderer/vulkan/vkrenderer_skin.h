#pragma once

#include "vkrenderer_types.h"

b8 vkrenderer_skin_create(VkContext *ctx);
void vkrenderer_skin_destroy(VkContext *ctx);

/** Resets the current frame's bone buffer and writes the identity at slot 0. */
void vkrenderer_skin_begin_frame(VkContext *ctx);

/** Allocates @p bone_count consecutive matrices. Returns their base offset. */
u32 vkrenderer_skin_allocate(VkContext *ctx, u32 bone_count);

void vkrenderer_skin_write(VkContext *ctx, u32 offset, const mat4 *bones, u32 count);

VkDescriptorSet vkrenderer_skin_descriptor(VkContext *ctx);
VkDescriptorSetLayout vkrenderer_skin_set_layout(VkContext *ctx);

/** Records the host-write -> vertex-shader-read barrier before the render pass. */
void vkrenderer_skin_barrier(VkContext *ctx, VkRendererCommandBuffer *command_buffer);
