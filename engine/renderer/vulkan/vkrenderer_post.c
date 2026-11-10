#include "vkrenderer_post.h"
#include "vkrenderer_image.h"
#include "vkrenderer_command_buffer.h"
#include "vkrenderer_renderpass.h"
#include "renderer/renderer.h"
#include "shader/vkrenderer_shader.h"
#include "core/logger.h"
#include "core/memo.h"

#include <math.h>
#include <string.h>

typedef struct PostPushConstants
{
  vec4 exposure_tonemap; // exposure, tone mapper, tone mapping enabled, HDR
  vec4 grading;          // contrast, saturation, temperature, tint
  vec4 lift;             // lift rgb, vignette
  vec4 gamma;            // gamma rgb, lut strength
  vec4 gain;             // gain rgb, lut enabled
} PostPushConstants;

typedef char PostPushConstants_size_check[(sizeof(PostPushConstants) == 80) ? 1 : -1];

typedef struct VkRendererPost
{
  VkRenderPass scene_renderpass;
  VkFramebuffer scene_framebuffer;

  VkRendererImage scene_color;
  VkRendererImage scene_normal;
  VkRendererImage scene_depth;

  VkSampler color_sampler;
  VkDescriptorSetLayout set_layout;
  VkDescriptorPool pool;
  VkDescriptorSet set;
  VkRendererShader shader;

  u32 width;
  u32 height;
  b8 ready;
} VkRendererPost;

static VkRendererPost post;

static b8 create_scene_renderpass(VkContext *ctx)
{
  VkAttachmentDescription attachments[3];
  MEMOZERO(attachments, sizeof(attachments));

  attachments[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
  attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  attachments[1] = attachments[0];

  attachments[2].format = ctx->device.depth_format;
  attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkAttachmentReference color_refs[2];
  color_refs[0].attachment = 0;
  color_refs[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color_refs[1].attachment = 1;
  color_refs[1].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depth_ref;
  depth_ref.attachment = 2;
  depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass;
  MEMOZERO(&subpass, sizeof(subpass));
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 2;
  subpass.pColorAttachments = color_refs;
  subpass.pDepthStencilAttachment = &depth_ref;

  VkSubpassDependency dependencies[2];
  MEMOZERO(dependencies, sizeof(dependencies));

  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependencies[0].srcAccessMask = 0;
  dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  dependencies[1].srcSubpass = 0;
  dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  VkRenderPassCreateInfo info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  info.attachmentCount = 3;
  info.pAttachments = attachments;
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = 2;
  info.pDependencies = dependencies;

  if (vkCreateRenderPass(ctx->device.handle, &info, ctx->allocator,
                         &post.scene_renderpass) != VK_SUCCESS)
  {
    EGNERROR("VKRENDERER failed to create the HDR scene render pass");
    return false;
  }

  return true;
}

static void destroy_targets(VkContext *ctx)
{
  if (post.scene_framebuffer)
  {
    vkDestroyFramebuffer(ctx->device.handle, post.scene_framebuffer, ctx->allocator);
    post.scene_framebuffer = 0;
  }

  vkrenderer_destroy_image(ctx, &post.scene_color);
  vkrenderer_destroy_image(ctx, &post.scene_normal);
  vkrenderer_destroy_image(ctx, &post.scene_depth);
  post.width = 0;
  post.height = 0;
  post.ready = false;
}

static b8 create_targets(VkContext *ctx)
{
  u32 width = ctx->framebuffer_width;
  u32 height = ctx->framebuffer_height;
  if (width == 0 || height == 0)
    return false;

  vkrenderer_image_create(ctx, VK_IMAGE_TYPE_2D, width, height,
                          VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true,
                          VK_IMAGE_ASPECT_COLOR_BIT, &post.scene_color);
  vkrenderer_image_create(ctx, VK_IMAGE_TYPE_2D, width, height,
                          VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true,
                          VK_IMAGE_ASPECT_COLOR_BIT, &post.scene_normal);
  vkrenderer_image_create(ctx, VK_IMAGE_TYPE_2D, width, height,
                          ctx->device.depth_format, VK_IMAGE_TILING_OPTIMAL,
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true,
                          VK_IMAGE_ASPECT_DEPTH_BIT, &post.scene_depth);

  if (!post.scene_color.handle || !post.scene_color.view ||
      !post.scene_normal.handle || !post.scene_normal.view ||
      !post.scene_depth.handle || !post.scene_depth.view)
  {
    EGNERROR("VKRENDERER failed to create HDR scene attachments");
    destroy_targets(ctx);
    return false;
  }

  VkImageView views[3] = {
      post.scene_color.view,
      post.scene_normal.view,
      post.scene_depth.view,
  };

  VkFramebufferCreateInfo framebuffer_info = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  framebuffer_info.renderPass = post.scene_renderpass;
  framebuffer_info.attachmentCount = 3;
  framebuffer_info.pAttachments = views;
  framebuffer_info.width = width;
  framebuffer_info.height = height;
  framebuffer_info.layers = 1;

  if (vkCreateFramebuffer(ctx->device.handle, &framebuffer_info, ctx->allocator,
                          &post.scene_framebuffer) != VK_SUCCESS)
  {
    EGNERROR("VKRENDERER failed to create the HDR scene framebuffer");
    destroy_targets(ctx);
    return false;
  }

  post.width = width;
  post.height = height;
  post.ready = true;

  if (post.set)
  {
    VkDescriptorImageInfo image_info = {0};
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_info.imageView = post.scene_color.view;
    image_info.sampler = post.color_sampler;

    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = post.set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image_info;
    vkUpdateDescriptorSets(ctx->device.handle, 1, &write, 0, 0);
  }

  return true;
}

static b8 create_descriptors(VkContext *ctx)
{
  VkDescriptorSetLayoutBinding binding = {0};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layout_info.bindingCount = 1;
  layout_info.pBindings = &binding;
  if (vkCreateDescriptorSetLayout(ctx->device.handle, &layout_info, ctx->allocator,
                                  &post.set_layout) != VK_SUCCESS)
    return false;

  VkDescriptorPoolSize pool_size = {0};
  pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  pool_size.descriptorCount = 1;

  VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool_info.maxSets = 1;
  pool_info.poolSizeCount = 1;
  pool_info.pPoolSizes = &pool_size;
  if (vkCreateDescriptorPool(ctx->device.handle, &pool_info, ctx->allocator,
                             &post.pool) != VK_SUCCESS)
    return false;

  VkDescriptorSetAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocate_info.descriptorPool = post.pool;
  allocate_info.descriptorSetCount = 1;
  allocate_info.pSetLayouts = &post.set_layout;
  if (vkAllocateDescriptorSets(ctx->device.handle, &allocate_info, &post.set) != VK_SUCCESS)
    return false;

  VkSamplerCreateInfo sampler_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sampler_info.magFilter = VK_FILTER_LINEAR;
  sampler_info.minFilter = VK_FILTER_LINEAR;
  sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.maxLod = 0.0f;
  if (vkCreateSampler(ctx->device.handle, &sampler_info, ctx->allocator,
                      &post.color_sampler) != VK_SUCCESS)
    return false;

  return true;
}

b8 vkrenderer_post_create(VkContext *ctx)
{
  MEMOZERO(&post, sizeof(post));

  if (!create_scene_renderpass(ctx))
    return false;
  if (!create_descriptors(ctx))
    return false;
  if (!create_targets(ctx))
    return false;

  VkRendererShaderParams params = {0};
  params.vert_path = "shaders/post.vert.spv";
  params.frag_path = "shaders/post.frag.spv";
  params.set_layouts = &post.set_layout;
  params.set_layout_count = 1;
  params.push_constant_size = sizeof(PostPushConstants);
  params.depth_test = false;
  params.depth_write = false;
  params.blending = false;
  params.cull_mode = VK_CULL_MODE_NONE;
  params.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  params.render_pass = ctx->main_renderpass.handle;
  params.color_attachment_count = 1;

  if (!vkrenderer_shader_create(ctx, &params, &post.shader))
  {
    EGNERROR("VKRENDERER failed to create the post-processing pipeline");
    return false;
  }

  return true;
}

void vkrenderer_post_resize(VkContext *ctx)
{
  if (!ctx || !post.scene_renderpass)
    return;

  destroy_targets(ctx);
  create_targets(ctx);
}

void vkrenderer_post_apply_settings(VkContext *ctx)
{
  (void)ctx;
  // The HDR target format is deliberately fixed so runtime quality changes do
  // not invalidate every scene pipeline. The settings control the post stack.
}

VkRenderPass vkrenderer_post_scene_renderpass(VkContext *ctx)
{
  (void)ctx;
  return post.scene_renderpass;
}

void vkrenderer_post_begin_scene(VkContext *ctx, VkRendererCommandBuffer *command_buffer)
{
  if (!post.ready)
    return;

  VkClearValue clears[3];
  MEMOZERO(clears, sizeof(clears));
  clears[0].color.float32[0] = 0.0f;
  clears[0].color.float32[1] = 0.0f;
  clears[0].color.float32[2] = 0.0f;
  clears[0].color.float32[3] = 1.0f;
  clears[2].depthStencil.depth = 1.0f;

  VkRenderPassBeginInfo info = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  info.renderPass = post.scene_renderpass;
  info.framebuffer = post.scene_framebuffer;
  info.renderArea.offset.x = 0;
  info.renderArea.offset.y = 0;
  info.renderArea.extent.width = post.width;
  info.renderArea.extent.height = post.height;
  info.clearValueCount = 3;
  info.pClearValues = clears;

  vkCmdBeginRenderPass(command_buffer->handle, &info, VK_SUBPASS_CONTENTS_INLINE);
  command_buffer->state = VKRENDERER_STATE_IN_RENDERPASS;

  VkViewport viewport;
  viewport.x = 0.0f;
  viewport.y = (f32)post.height;
  viewport.width = (f32)post.width;
  viewport.height = -(f32)post.height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor;
  scissor.offset.x = 0;
  scissor.offset.y = 0;
  scissor.extent.width = post.width;
  scissor.extent.height = post.height;

  vkCmdSetViewport(command_buffer->handle, 0, 1, &viewport);
  vkCmdSetScissor(command_buffer->handle, 0, 1, &scissor);
}

void vkrenderer_post_end_scene(VkContext *ctx, VkRendererCommandBuffer *command_buffer)
{
  (void)ctx;
  vkCmdEndRenderPass(command_buffer->handle);
  command_buffer->state = VKRENDERER_STATE_RECORDING;
}

void vkrenderer_post_process(VkContext *ctx, VkRendererCommandBuffer *command_buffer)
{
  if (!post.ready || !post.set)
    return;

  RendererGraphicsSettings settings = renderer_get_active_graphics_settings();

  vkrenderer_renderpass_begin(command_buffer, &ctx->main_renderpass,
                              ctx->framebuffers[ctx->image_index].handle);

  VkViewport viewport;
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = (f32)ctx->framebuffer_width;
  viewport.height = (f32)ctx->framebuffer_height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor;
  scissor.offset.x = 0;
  scissor.offset.y = 0;
  scissor.extent.width = ctx->framebuffer_width;
  scissor.extent.height = ctx->framebuffer_height;

  vkCmdSetViewport(command_buffer->handle, 0, 1, &viewport);
  vkCmdSetScissor(command_buffer->handle, 0, 1, &scissor);

  vkCmdBindPipeline(command_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    post.shader.pipeline.handle);
  vkCmdBindDescriptorSets(command_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          post.shader.pipeline.layout, 0, 1, &post.set, 0, 0);

  PostPushConstants push;
  MEMOZERO(&push, sizeof(push));

  f32 contrast = 1.0f;
  f32 saturation = 1.0f;
  f32 temperature = 0.0f;
  f32 tint = 0.0f;
  f32 lift_r = 0.0f;
  f32 lift_g = 0.0f;
  f32 lift_b = 0.0f;
  f32 gamma_r = 1.0f;
  f32 gamma_g = 1.0f;
  f32 gamma_b = 1.0f;
  f32 gain_r = 1.0f;
  f32 gain_g = 1.0f;
  f32 gain_b = 1.0f;
  f32 vignette = 0.0f;

  if (settings.color_grading_enabled)
  {
    contrast = settings.contrast;
    saturation = settings.saturation;
    temperature = settings.temperature;
    tint = settings.tint;
    lift_r = settings.lift_r;
    lift_g = settings.lift_g;
    lift_b = settings.lift_b;
    gamma_r = settings.gamma_r;
    gamma_g = settings.gamma_g;
    gamma_b = settings.gamma_b;
    gain_r = settings.gain_r;
    gain_g = settings.gain_g;
    gain_b = settings.gain_b;
    vignette = settings.vignette;
  }

  push.exposure_tonemap = vec4_create(exp2f(settings.exposure_compensation),
                                      (f32)settings.tone_mapper,
                                      settings.tone_mapping_enabled ? 1.0f : 0.0f,
                                      settings.hdr_enabled ? 1.0f : 0.0f);
  push.grading = vec4_create(contrast, saturation, temperature, tint);
  push.lift = vec4_create(lift_r, lift_g, lift_b, vignette);
  push.gamma = vec4_create(gamma_r, gamma_g, gamma_b, settings.lut_strength);
  push.gain = vec4_create(gain_r, gain_g, gain_b,
                          settings.lut_enabled ? 1.0f : 0.0f);

  vkCmdPushConstants(command_buffer->handle, post.shader.pipeline.layout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, sizeof(push), &push);

  vkCmdDraw(command_buffer->handle, 3, 1, 0, 0);
}

void vkrenderer_post_destroy(VkContext *ctx)
{
  if (!ctx)
    return;

  vkrenderer_shader_destroy(ctx, &post.shader);
  vkrenderer_destroy_image(ctx, &post.scene_color);
  vkrenderer_destroy_image(ctx, &post.scene_normal);
  vkrenderer_destroy_image(ctx, &post.scene_depth);

  if (post.scene_framebuffer)
  {
    vkDestroyFramebuffer(ctx->device.handle, post.scene_framebuffer, ctx->allocator);
    post.scene_framebuffer = 0;
  }
  if (post.pool)
  {
    vkDestroyDescriptorPool(ctx->device.handle, post.pool, ctx->allocator);
    post.pool = 0;
  }
  if (post.set_layout)
  {
    vkDestroyDescriptorSetLayout(ctx->device.handle, post.set_layout, ctx->allocator);
    post.set_layout = 0;
  }
  if (post.color_sampler)
  {
    vkDestroySampler(ctx->device.handle, post.color_sampler, ctx->allocator);
    post.color_sampler = 0;
  }
  if (post.scene_renderpass)
  {
    vkDestroyRenderPass(ctx->device.handle, post.scene_renderpass, ctx->allocator);
    post.scene_renderpass = 0;
  }

  MEMOZERO(&post, sizeof(post));
}
