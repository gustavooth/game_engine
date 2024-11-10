#include "vkrenderer_types.h"

void vkrenderer_framebuffer_create(VkContext *ctx, VkRendererRenderpass *renderpass,
                                   u32 width, u32 height, u32 attachments_count, VkImageView *attachments,
                                   VkRendererFramebuffer *out_framebuffer);

void vkrenderer_framebuffer_destroy(VkContext *ctx, VkRendererFramebuffer *framebuffer);