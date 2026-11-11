#include "vkrenderer_lighting.h"
#include "vkrenderer_buffer.h"
#include "vkrenderer_image.h"
#include "vkrenderer_command_buffer.h"
#include "renderer/renderer.h"
#include "renderer/renderer_settings.h"
#include "core/logger.h"
#include "core/memo.h"

static VkRendererLighting current_lighting;

typedef char VkRendererLighting_size_check[(sizeof(VkRendererLighting) == 752) ? 1 : -1];

static u32 lighting_frame_index(VkContext *ctx)
{
  return ctx->current_frame % VKRENDERER_BONE_FRAMES;
}

static void transition_shadow_image(VkContext *ctx, VkRendererImage *image)
{
  VkRendererCommandBuffer command_buffer = {0};
  vkrenderer_command_buffer_allocate_and_begin_single_use(ctx, ctx->device.graphics_command_pool,
                                                         &command_buffer);

  VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image->handle;
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
}

static void destroy_shadow_images(VkContext *ctx)
{
  for (u32 cascade = 0; cascade < VKRENDERER_MAX_SHADOW_CASCADES; cascade++)
  {
    if (ctx->shadow_framebuffers[cascade])
    {
      vkDestroyFramebuffer(ctx->device.handle, ctx->shadow_framebuffers[cascade], ctx->allocator);
      ctx->shadow_framebuffers[cascade] = 0;
    }
    vkrenderer_destroy_image(ctx, &ctx->shadow_images[cascade]);
  }
}

static b8 create_shadow_images(VkContext *ctx, u32 size)
{
  for (u32 cascade = 0; cascade < VKRENDERER_MAX_SHADOW_CASCADES; cascade++)
  {
    vkrenderer_image_create(ctx, VK_IMAGE_TYPE_2D, size, size, VK_FORMAT_D32_SFLOAT,
                            VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            true, VK_IMAGE_ASPECT_DEPTH_BIT,
                            &ctx->shadow_images[cascade]);
    if (!ctx->shadow_images[cascade].handle || !ctx->shadow_images[cascade].view)
      return false;

    VkFramebufferCreateInfo framebuffer_info = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer_info.renderPass = ctx->shadow_renderpass;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments = &ctx->shadow_images[cascade].view;
    framebuffer_info.width = size;
    framebuffer_info.height = size;
    framebuffer_info.layers = 1;
    VKCHECK(vkCreateFramebuffer(ctx->device.handle, &framebuffer_info, ctx->allocator,
                                &ctx->shadow_framebuffers[cascade]))

    // The shadow map is sampled by the main pass even when no caster is
    // active, so move it to the shader-read layout once at creation.
    transition_shadow_image(ctx, &ctx->shadow_images[cascade]);
  }
  return true;
}

static b8 create_shadow_resources(VkContext *ctx)
{
  RendererGraphicsSettings settings = renderer_get_active_graphics_settings();
  u32 size = renderer_graphics_settings_shadow_size(&settings);

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

  // [0] waits for the previous frame's sampling before clearing the map,
  // [1] makes the scene pass sampling wait for this pass' depth writes.
  VkSubpassDependency dependencies[2];
  MEMOZERO(dependencies, sizeof(dependencies));
  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
  dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dependencies[1].srcSubpass = 0;
  dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[1].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  VkRenderPassCreateInfo renderpass_info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  renderpass_info.attachmentCount = 1;
  renderpass_info.pAttachments = &depth_attachment;
  renderpass_info.subpassCount = 1;
  renderpass_info.pSubpasses = &subpass;
  renderpass_info.dependencyCount = 2;
  renderpass_info.pDependencies = dependencies;
  VKCHECK(vkCreateRenderPass(ctx->device.handle, &renderpass_info, ctx->allocator,
                             &ctx->shadow_renderpass))

  ctx->shadow_size = size;
  if (!create_shadow_images(ctx, size))
    return false;

  ctx->shadow_cascade_count = renderer_graphics_settings_cascades(&settings);
  return true;
}

static void write_lighting_descriptors(VkContext *ctx)
{
  for (u32 frame = 0; frame < VKRENDERER_BONE_FRAMES; frame++)
  {
    VkDescriptorBufferInfo buffer_descriptor = {0};
    buffer_descriptor.buffer = ctx->lighting_buffers[frame];
    buffer_descriptor.offset = 0;
    buffer_descriptor.range = sizeof(VkRendererLighting);

    VkDescriptorImageInfo image_descriptors[VKRENDERER_MAX_SHADOW_CASCADES];
    MEMOZERO(image_descriptors, sizeof(image_descriptors));
    for (u32 cascade = 0; cascade < VKRENDERER_MAX_SHADOW_CASCADES; cascade++)
    {
      image_descriptors[cascade].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      image_descriptors[cascade].imageView = ctx->shadow_images[cascade].view;
      image_descriptors[cascade].sampler = ctx->shadow_sampler;
    }

    VkWriteDescriptorSet writes[1 + VKRENDERER_MAX_SHADOW_CASCADES];
    MEMOZERO(writes, sizeof(writes));

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = ctx->lighting_sets[frame];
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &buffer_descriptor;

    for (u32 cascade = 0; cascade < VKRENDERER_MAX_SHADOW_CASCADES; cascade++)
    {
      writes[1 + cascade].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[1 + cascade].dstSet = ctx->lighting_sets[frame];
      writes[1 + cascade].dstBinding = 1 + cascade;
      writes[1 + cascade].descriptorCount = 1;
      writes[1 + cascade].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      writes[1 + cascade].pImageInfo = &image_descriptors[cascade];
    }

    vkUpdateDescriptorSets(ctx->device.handle, 1 + VKRENDERER_MAX_SHADOW_CASCADES,
                           writes, 0, 0);
  }
}

b8 vkrenderer_lighting_create(VkContext *ctx)
{
  MEMOZERO(&current_lighting, sizeof(current_lighting));

  if (!create_shadow_resources(ctx))
    return false;

  VkDescriptorSetLayoutBinding bindings[2 + VKRENDERER_MAX_SHADOW_CASCADES];
  MEMOZERO(bindings, sizeof(bindings));

  // Sky-view LUT, written by vkrenderer_lighting_set_sky_lut().
  bindings[1 + VKRENDERER_MAX_SHADOW_CASCADES].binding = 1 + VKRENDERER_MAX_SHADOW_CASCADES;
  bindings[1 + VKRENDERER_MAX_SHADOW_CASCADES].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bindings[1 + VKRENDERER_MAX_SHADOW_CASCADES].descriptorCount = 1;
  bindings[1 + VKRENDERER_MAX_SHADOW_CASCADES].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  bindings[0].binding = 0;
  bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  bindings[0].descriptorCount = 1;
  bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

  for (u32 cascade = 0; cascade < VKRENDERER_MAX_SHADOW_CASCADES; cascade++)
  {
    bindings[1 + cascade].binding = 1 + cascade;
    bindings[1 + cascade].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1 + cascade].descriptorCount = 1;
    bindings[1 + cascade].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  }

  VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layout_info.bindingCount = 2 + VKRENDERER_MAX_SHADOW_CASCADES;
  layout_info.pBindings = bindings;
  VKCHECK(vkCreateDescriptorSetLayout(ctx->device.handle, &layout_info, ctx->allocator,
                                      &ctx->lighting_set_layout))

  VkDescriptorPoolSize pool_sizes[2] = {0};
  pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  pool_sizes[0].descriptorCount = VKRENDERER_BONE_FRAMES;
  pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  pool_sizes[1].descriptorCount = VKRENDERER_BONE_FRAMES * (VKRENDERER_MAX_SHADOW_CASCADES + 1);

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
  }

  write_lighting_descriptors(ctx);
  return true;
}

void vkrenderer_lighting_set_sky_lut(VkContext *ctx, VkImageView view, VkSampler sampler)
{
  VkDescriptorImageInfo image_info = {0};
  image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  image_info.imageView = view;
  image_info.sampler = sampler;

  for (u32 frame = 0; frame < VKRENDERER_BONE_FRAMES; frame++)
  {
    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = ctx->lighting_sets[frame];
    write.dstBinding = 1 + VKRENDERER_MAX_SHADOW_CASCADES;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image_info;
    vkUpdateDescriptorSets(ctx->device.handle, 1, &write, 0, 0);
  }
}

void vkrenderer_lighting_apply_settings(VkContext *ctx)
{
  if (!ctx || !ctx->shadow_renderpass)
    return;

  RendererGraphicsSettings settings = renderer_get_active_graphics_settings();
  u32 size = renderer_graphics_settings_shadow_size(&settings);
  ctx->shadow_cascade_count = renderer_graphics_settings_cascades(&settings);

  if (size == ctx->shadow_size)
    return;

  // Recreate images/framebuffers at the new resolution while keeping the
  // sampler, render pass, UBOs and descriptor sets alive.
  destroy_shadow_images(ctx);
  ctx->shadow_size = size;
  if (!create_shadow_images(ctx, size))
  {
    EGNERROR("VKRENDERER failed to recreate shadow resources");
    return;
  }
  write_lighting_descriptors(ctx);
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

  destroy_shadow_images(ctx);

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

  ctx->shadow_size = 0;
}

void vkrenderer_lighting_write(VkContext *ctx, const VkRendererLighting *lighting)
{
  if (!ctx || !lighting)
    return;
  u32 frame = lighting_frame_index(ctx);
  if (ctx->lighting_mapped[frame])
    MEMOCPY(ctx->lighting_mapped[frame], lighting, sizeof(VkRendererLighting));
  current_lighting = *lighting;
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

VkFramebuffer vkrenderer_lighting_shadow_framebuffer(VkContext *ctx, u32 cascade)
{
  if (cascade >= VKRENDERER_MAX_SHADOW_CASCADES)
    cascade = VKRENDERER_MAX_SHADOW_CASCADES - 1;
  return ctx->shadow_framebuffers[cascade];
}

u32 vkrenderer_lighting_shadow_size(VkContext *ctx)
{
  return ctx->shadow_size;
}
