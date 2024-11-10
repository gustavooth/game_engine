#include "vkrenderer_framebuffer.h"
#include "core/memo.h"

void vkrenderer_framebuffer_create(VkContext *ctx, VkRendererRenderpass *renderpass,
                                   u32 width, u32 height, u32 attachment_count, VkImageView *attachments,
                                   VkRendererFramebuffer *out_framebuffer)
{
  out_framebuffer->attachments = MEMOALLOC(sizeof(VkImageView) * attachment_count, MEMO_RENDERER);
  out_framebuffer->attachment_count = attachment_count;

  for (u32 i = 0; i < attachment_count; i++)
    out_framebuffer->attachments[i] = attachments[i];

  out_framebuffer->renderpass = renderpass;

  VkFramebufferCreateInfo framebuffer_info = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  framebuffer_info.renderPass = renderpass->handle;
  framebuffer_info.attachmentCount = out_framebuffer->attachment_count;
  framebuffer_info.pAttachments = out_framebuffer->attachments;
  framebuffer_info.width = width;
  framebuffer_info.height = height;
  framebuffer_info.layers = 1;

  VKCHECK(vkCreateFramebuffer(ctx->device.handle, &framebuffer_info, ctx->allocator, &out_framebuffer->handle));
}

void vkrenderer_framebuffer_destroy(VkContext *ctx, VkRendererFramebuffer *framebuffer)
{
  vkDestroyFramebuffer(ctx->device.handle, framebuffer->handle, ctx->allocator);
  MEMOFREE(framebuffer->attachments);

  MEMOZERO(framebuffer, sizeof(VkRendererFramebuffer));
}
