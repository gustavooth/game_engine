#include "vkrenderer_lighting.h"
#include "vkrenderer_buffer.h"
#include "vkrenderer_image.h"
#include "vkrenderer_command_buffer.h"
#include "core/logger.h"
#include "core/memo.h"

static VkRendererLighting current_lighting;

typedef char VkRendererLighting_size_check[(sizeof(VkRendererLighting) == 384) ? 1 : -1];

static u32 lighting_frame_index(VkContext *ctx)
{
  return ctx->current_frame % VKRENDERER_BONE_FRAMES;
}

static b8 create_shadow_resources(VkContext *ctx)
{
  vkrenderer_image_create(ctx, VK_IMAGE_TYPE_2D, VKRENDERER_SHADOW_SIZE,
                          VKRENDERER_SHADOW_SIZE, VK_FORMAT_D32_SFLOAT,
                          VK_IMAGE_TILING_OPTIMAL,
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          true, VK_IMAGE_ASPECT_DEPTH_BIT,
                          &ctx->shadow_image);
  if (!ctx->shadow_image.handle || !ctx->shadow_image.view)
    return false;

  VkSamplerCreateInfo sampler_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sampler_info.magFilter = VK_FILTER_LINEAR;
  sampler_info.minFilter = VK_FILTER_LINEAR;
  sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  sampler_info.compareEnable = VK_TRUE;
  sampler_info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  sampler_info.minLod = 0.0f;
  sampler_info.maxLod = 0.0f;
  VKCHECK(vkCreateSampler(ctx->device.handle, &sampler_info, ctx->allocator,
                          &ctx->shadow_sampler))

  VkAttachmentDescription depth_attachment = {0};
  depth_attachment.format = VK_FORMAT_D32_SFLOAT;
  depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depth_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkAttachmentReference depth_reference = {0};
  depth_reference.attachment = 0;
  depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {0};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.pDepthStencilAttachment = &depth_reference;

  VkSubpassDependency dependency = {0};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
  dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo renderpass_info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  renderpass_info.attachmentCount = 1;
  renderpass_info.pAttachments = &depth_attachment;
  renderpass_info.subpassCount = 1;
  renderpass_info.pSubpasses = &subpass;
  renderpass_info.dependencyCount = 1;
  renderpass_info.pDependencies = &dependency;
  VKCHECK(vkCreateRenderPass(ctx->device.handle, &renderpass_info, ctx->allocator,
                             &ctx->shadow_renderpass))

  VkFramebufferCreateInfo framebuffer_info = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  framebuffer_info.renderPass = ctx->shadow_renderpass;
  framebuffer_info.attachmentCount = 1;
  framebuffer_info.pAttachments = &ctx->shadow_image.view;
  framebuffer_info.width = VKRENDERER_SHADOW_SIZE;
  framebuffer_info.height = VKRENDERER_SHADOW_SIZE;
  framebuffer_info.layers = 1;
  VKCHECK(vkCreateFramebuffer(ctx->device.handle, &framebuffer_info, ctx->allocator,
                              &ctx->shadow_framebuffer))

  // The shadow map is sampled by the lighting pass even when no shadow caster
  // is active, so move it to the shader-read layout once at creation.
  VkRendererCommandBuffer command_buffer = {0};
  vkrenderer_command_buffer_allocate_and_begin_single_use(ctx, ctx->device.graphics_command_pool,
                                                         &command_buffer);

  VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = ctx->shadow_image.handle;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(command_buffer.handle,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       0, 0, 0, 0, 0, 1, &barrier);

  vkrenderer_command_buffer_end_single_use(ctx, ctx->device.graphics_command_pool,
                                           &command_buffer, ctx->device.graphics);

  return true;
}

b8 vkrenderer_lighting_create(VkContext *ctx)
{
  MEMOZERO(&current_lighting, sizeof(current_lighting));

  if (!create_shadow_resources(ctx))
    return false;

  VkDescriptorSetLayoutBinding bindings[2] = {0};
  bindings[0].binding = 0;
  bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  bindings[0].descriptorCount = 1;
  bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

  bindings[1].binding = 1;
  bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bindings[1].descriptorCount = 1;
  bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layout_info.bindingCount = 2;
  layout_info.pBindings = bindings;
  VKCHECK(vkCreateDescriptorSetLayout(ctx->device.handle, &layout_info, ctx->allocator,
                                      &ctx->lighting_set_layout))

  VkDescriptorPoolSize pool_sizes[2] = {0};
  pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  pool_sizes[0].descriptorCount = VKRENDERER_BONE_FRAMES;
  pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  pool_sizes[1].descriptorCount = VKRENDERER_BONE_FRAMES;

  VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool_info.maxSets = VKRENDERER_BONE_FRAMES;
  pool_info.poolSizeCount = 2;
  pool_info.pPoolSizes = pool_sizes;
  VKCHECK(vkCreateDescriptorPool(ctx->device.handle, &pool_info, ctx->allocator,
                                 &ctx->lighting_descriptor_pool))

  u64 buffer_size = sizeof(VkRendererLighting);

  for (u32 frame = 0; frame < VKRENDERER_BONE_FRAMES; frame++)
  {
    if (!vkrenderer_buffer_create(ctx, buffer_size,
                                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  &ctx->lighting_buffers[frame],
                                  &ctx->lighting_memories[frame]))
      return false;

    if (vkMapMemory(ctx->device.handle, ctx->lighting_memories[frame], 0,
                    VK_WHOLE_SIZE, 0, &ctx->lighting_mapped[frame]) != VK_SUCCESS)
    {
      EGNERROR("VKRENDERER failed to map lighting buffer");
      return false;
    }

    VkDescriptorSetAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate_info.descriptorPool = ctx->lighting_descriptor_pool;
    allocate_info.descriptorSetCount = 1;
    allocate_info.pSetLayouts = &ctx->lighting_set_layout;
    VKCHECK(vkAllocateDescriptorSets(ctx->device.handle, &allocate_info,
                                     &ctx->lighting_sets[frame]))

    VkDescriptorBufferInfo buffer_descriptor = {0};
    buffer_descriptor.buffer = ctx->lighting_buffers[frame];
    buffer_descriptor.offset = 0;
    buffer_descriptor.range = sizeof(VkRendererLighting);

    VkDescriptorImageInfo image_descriptor = {0};
    image_descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_descriptor.imageView = ctx->shadow_image.view;
    image_descriptor.sampler = ctx->shadow_sampler;

    VkWriteDescriptorSet writes[2] = {0};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = ctx->lighting_sets[frame];
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &buffer_descriptor;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = ctx->lighting_sets[frame];
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &image_descriptor;

    vkUpdateDescriptorSets(ctx->device.handle, 2, writes, 0, 0);
  }

  return true;
}

void vkrenderer_lighting_destroy(VkContext *ctx)
{
  for (u32 frame = 0; frame < VKRENDERER_BONE_FRAMES; frame++)
  {
    if (ctx->lighting_mapped[frame])
    {
      vkUnmapMemory(ctx->device.handle, ctx->lighting_memories[frame]);
      ctx->lighting_mapped[frame] = 0;
    }
    vkrenderer_buffer_destroy(ctx, &ctx->lighting_buffers[frame],
                              &ctx->lighting_memories[frame]);
    ctx->lighting_sets[frame] = 0;
  }

  if (ctx->lighting_descriptor_pool)
  {
    vkDestroyDescriptorPool(ctx->device.handle, ctx->lighting_descriptor_pool, ctx->allocator);
    ctx->lighting_descriptor_pool = 0;
  }

  if (ctx->lighting_set_layout)
  {
    vkDestroyDescriptorSetLayout(ctx->device.handle, ctx->lighting_set_layout, ctx->allocator);
    ctx->lighting_set_layout = 0;
  }

  if (ctx->shadow_framebuffer)
  {
    vkDestroyFramebuffer(ctx->device.handle, ctx->shadow_framebuffer, ctx->allocator);
    ctx->shadow_framebuffer = 0;
  }

  if (ctx->shadow_renderpass)
  {
    vkDestroyRenderPass(ctx->device.handle, ctx->shadow_renderpass, ctx->allocator);
    ctx->shadow_renderpass = 0;
  }

  if (ctx->shadow_sampler)
  {
    vkDestroySampler(ctx->device.handle, ctx->shadow_sampler, ctx->allocator);
    ctx->shadow_sampler = 0;
  }

  vkrenderer_destroy_image(ctx, &ctx->shadow_image);
}

void vkrenderer_lighting_write(VkContext *ctx, const VkRendererLighting *lighting)
{
  if (!ctx || !lighting)
    return;

  current_lighting = *lighting;
  u32 frame = lighting_frame_index(ctx);
  MEMOCPY(ctx->lighting_mapped[frame], lighting, sizeof(VkRendererLighting));
}

const VkRendererLighting *vkrenderer_lighting_current(void)
{
  return &current_lighting;
}

VkDescriptorSet vkrenderer_lighting_descriptor(VkContext *ctx)
{
  return ctx->lighting_sets[lighting_frame_index(ctx)];
}

VkDescriptorSetLayout vkrenderer_lighting_set_layout(VkContext *ctx)
{
  return ctx->lighting_set_layout;
}

VkRenderPass vkrenderer_lighting_shadow_renderpass(VkContext *ctx)
{
  return ctx->shadow_renderpass;
}

VkFramebuffer vkrenderer_lighting_shadow_framebuffer(VkContext *ctx)
{
  return ctx->shadow_framebuffer;
}

u32 vkrenderer_lighting_shadow_size(VkContext *ctx)
{
  (void)ctx;
  return VKRENDERER_SHADOW_SIZE;
}
