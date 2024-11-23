#include "vkrenderer_renderpass.h"
#include "core/memo.h"
#include "core/logger.h"

b8 vkrenderer_renderpass_create(VkContext *ctx, VkRendererRenderpass *out_renderpass,
                                f32 x, f32 y, f32 h, f32 w,
                                f32 r, f32 g, f32 b, f32 a,
                                f32 depth, u32 stencil)
{
  EGNDEBUG("VKRENDERER Creating renderpass...");

  // Main subpass
  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

  // Attachments TODO: make this configurable.
  enum { ATTACHMENT_DESCRIPTION_COUNT = 2 };
  VkAttachmentDescription attachment_descriptions[ATTACHMENT_DESCRIPTION_COUNT];

  // Color attachment
  VkAttachmentDescription color_attachment;
  color_attachment.format = ctx->swapchain.image_format.format;
  color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;     // Do not expect any particular layout before render pass starts.
  color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // Transitioned to after the render pass
  color_attachment.flags = 0;
  attachment_descriptions[0] = color_attachment;

  VkAttachmentReference color_attachment_reference;
  color_attachment_reference.attachment = 0; // Attachment description array index
  color_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_attachment_reference;

  // Depth attachment, if there is one
  VkAttachmentDescription depth_attachment = {};
  depth_attachment.format = ctx->device.depth_format;
  depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  attachment_descriptions[1] = depth_attachment;

  // Depth attachment reference
  VkAttachmentReference depth_attachment_reference;
  depth_attachment_reference.attachment = 1;
  depth_attachment_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  // TODO: other attachment types (input, resolve, preserve)

  // Depth stencil data.
  subpass.pDepthStencilAttachment = &depth_attachment_reference;
  // Input from a shader
  subpass.inputAttachmentCount = 0;
  subpass.pInputAttachments = 0;
  // Attachments used for multisampling colour attachments
  subpass.pResolveAttachments = 0;
  // Attachments not used in this subpass, but must be preserved for the next.
  subpass.preserveAttachmentCount = 0;
  subpass.pPreserveAttachments = 0;
  // Render pass dependencies. TODO: make this configurable.

  VkSubpassDependency dependency;
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependency.dependencyFlags = 0;
  // Render pass create.
  VkRenderPassCreateInfo render_pass_create_info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  render_pass_create_info.attachmentCount = ATTACHMENT_DESCRIPTION_COUNT;
  render_pass_create_info.pAttachments = attachment_descriptions;
  render_pass_create_info.subpassCount = 1;
  render_pass_create_info.pSubpasses = &subpass;
  render_pass_create_info.dependencyCount = 1;
  render_pass_create_info.pDependencies = &dependency;
  render_pass_create_info.pNext = 0;
  render_pass_create_info.flags = 0;

  VKCHECK(vkCreateRenderPass(ctx->device.handle, &render_pass_create_info, ctx->allocator, &out_renderpass->handle))

  out_renderpass->x = x;
  out_renderpass->y = y;
  out_renderpass->h = h;
  out_renderpass->w = w;
  out_renderpass->r = r;
  out_renderpass->g = g;
  out_renderpass->b = b;
  out_renderpass->a = a;
  out_renderpass->depth = depth;
  out_renderpass->stencil = stencil;

  return true;
}

void vkrenderer_renderpass_destroy(VkContext *ctx, VkRendererRenderpass *renderpass)
{
  if (renderpass && renderpass->handle)
  {
    vkDestroyRenderPass(ctx->device.handle, renderpass->handle, ctx->allocator);
    MEMOZERO(renderpass, sizeof(VkRendererRenderpass));
  }
}

void vkrenderer_renderpass_begin(VkRendererCommandBuffer *commandbuffer,
                                 VkRendererRenderpass *renderpass, VkFramebuffer framebuffer)
{
  VkRenderPassBeginInfo begin_info = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  begin_info.renderPass = renderpass->handle;
  begin_info.framebuffer = framebuffer;
  begin_info.renderArea.offset.x = renderpass->x;
  begin_info.renderArea.offset.y = renderpass->y;
  begin_info.renderArea.extent.width = renderpass->w;
  begin_info.renderArea.extent.height = renderpass->h;
  VkClearValue clear_values[2];
  MEMOZERO(clear_values, sizeof(VkClearValue) * 2);
  clear_values[0].color.float32[0] = renderpass->r;
  clear_values[0].color.float32[1] = renderpass->g;
  clear_values[0].color.float32[2] = renderpass->b;
  clear_values[0].color.float32[3] = renderpass->a;
  clear_values[1].depthStencil.depth = renderpass->depth;
  clear_values[1].depthStencil.stencil = renderpass->stencil;
  begin_info.clearValueCount = 2;
  begin_info.pClearValues = clear_values;

  vkCmdBeginRenderPass(commandbuffer->handle, &begin_info, VK_SUBPASS_CONTENTS_INLINE);

  commandbuffer->state = VKRENDERER_STATE_IN_RENDERPASS;
}

void vkrenderer_renderpass_end(VkRendererCommandBuffer *commandbuffer, VkRendererRenderpass *renderpass)
{
  vkCmdEndRenderPass(commandbuffer->handle);
  commandbuffer->state = VKRENDERER_STATE_RECORDING;
}