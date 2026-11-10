#pragma once

#include "vkrenderer_types.h"

b8 vkrenderer_renderpass_create(VkContext *ctx, VkRendererRenderpass *out_renderpass,
                                f32 x, f32 y, f32 w, f32 h,
                                f32 r, f32 g, f32 b, f32 a,
                                f32 depth, u32 stencil);

void vkrenderer_renderpass_destroy(VkContext *ctx, VkRendererRenderpass *renderpass);

void vkrenderer_renderpass_begin(VkRendererCommandBuffer *commandbuffer,
                                 VkRendererRenderpass *renderpass, VkFramebuffer framebuffer);

void vkrenderer_renderpass_end(VkRendererCommandBuffer *commandbuffer, VkRendererRenderpass *renderpass);
