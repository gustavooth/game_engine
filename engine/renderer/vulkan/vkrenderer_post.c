#include "vkrenderer_post.h"
#include "vkrenderer_scene.h"
#include "vkrenderer_lut.h"
#include "vkrenderer_image.h"
#include "vkrenderer_buffer.h"
#include "vkrenderer_command_buffer.h"
#include "vkrenderer_renderpass.h"
#include "renderer/renderer.h"
#include "shader/vkrenderer_shader.h"
#include "shader/vkrenderer_shader_utils.h"
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
  vec4 effects;          // ao enabled, gi enabled, gi intensity, ao intensity
  vec4 bloom;            // enabled, intensity, threshold, radius
  vec4 exposure_mode;    // auto exposure enabled, manual multiplier, unused
} PostPushConstants;

typedef char PostPushConstants_size_check[(sizeof(PostPushConstants) == 128) ? 1 : -1];

// Pushed as constants (not a shared UBO) so a frame still in flight never
// sees the matrices of the next one.
typedef struct EffectsParams
{
  mat4 inverse_view_projection;
  mat4 view_projection;
  vec4 camera_position;
  vec4 params;  // ao radius, ao intensity, gi intensity, gi distance
  vec4 params2; // sample count, projection scale y, aspect, flags
} EffectsParams;

typedef char EffectsParams_size_check[(sizeof(EffectsParams) == 176) ? 1 : -1];

#define BLOOM_MAX_MIPS 7

typedef struct BloomPushConstants
{
  vec4 params;  // source texel size xy, mode, upsample radius
  vec4 params2; // threshold, knee, firefly clamp, unused
} BloomPushConstants;

typedef char BloomPushConstants_size_check[(sizeof(BloomPushConstants) == 32) ? 1 : -1];

typedef struct ExposurePushConstants
{
  vec4 params;  // dt, adaptation speed, min EV, max EV
  vec4 params2; // grid size, unused, auto enabled, unused
} ExposurePushConstants;

typedef char ExposurePushConstants_size_check[(sizeof(ExposurePushConstants) == 32) ? 1 : -1];

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

  // Combined SSAO + SSGI target at half resolution.
  VkRenderPass effects_renderpass;
  VkFramebuffer effects_framebuffer;
  VkRendererImage effects_image;
  VkSampler effects_sampler;
  VkSampler effects_depth_sampler;
  VkDescriptorSetLayout effects_set_layout;
  VkDescriptorPool effects_pool;
  VkDescriptorSet effects_set;
  VkRendererShader effects_shader;

  // Bloom mip chain: mip 0 is half resolution, each next one half of that.
  // The downsample pass overwrites a mip, the upsample pass adds onto it.
  VkRenderPass bloom_renderpass;    // downsample: previous content discarded
  VkRenderPass bloom_up_renderpass; // upsample: previous content kept
  VkFramebuffer bloom_framebuffers[BLOOM_MAX_MIPS];
  VkRendererImage bloom_images[BLOOM_MAX_MIPS];
  VkDescriptorSetLayout bloom_set_layout;
  VkDescriptorPool bloom_pool;
  VkDescriptorSet bloom_scene_set;            // samples the HDR scene
  VkDescriptorSet bloom_sets[BLOOM_MAX_MIPS]; // samples each mip
  VkSampler bloom_sampler;
  VkRendererShader bloom_shader;    // downsample
  VkRendererShader bloom_up_shader; // additive upsample
  u32 bloom_mip_count;

  // Histogram-based auto exposure.
  VkDescriptorSetLayout exposure_set_layout;
  VkDescriptorPool exposure_pool;
  VkDescriptorSet exposure_set;
  VkBuffer exposure_buffer;
  VkDeviceMemory exposure_memory;
  void *exposure_mapped;
  VkPipelineLayout exposure_pipeline_layout;
  VkPipeline exposure_pipeline;

  u32 default_lut;
  u32 current_lut;

  u32 width;
  u32 height;
  u32 effects_width;
  u32 effects_height;
  b8 ready;
} VkRendererPost;

static VkRendererPost post;

static void clear_effects_image(VkContext *ctx, VkRendererImage *image);

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

static b8 create_effects_renderpass(VkContext *ctx)
{
  VkAttachmentDescription attachment = {0};
  attachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
  attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // every pixel is written
  attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkAttachmentReference reference = {0};
  reference.attachment = 0;
  reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {0};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &reference;

  VkSubpassDependency dependencies[2];
  MEMOZERO(dependencies, sizeof(dependencies));
  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[1].srcSubpass = 0;
  dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  VkRenderPassCreateInfo info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  info.attachmentCount = 1;
  info.pAttachments = &attachment;
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = 2;
  info.pDependencies = dependencies;

  if (vkCreateRenderPass(ctx->device.handle, &info, ctx->allocator,
                         &post.effects_renderpass) != VK_SUCCESS)
  {
    EGNERROR("VKRENDERER failed to create the AO/GI render pass");
    return false;
  }
  return true;
}

static b8 create_bloom_renderpass(VkContext *ctx, b8 keep_contents, VkRenderPass *out_renderpass)
{
  VkAttachmentDescription attachment = {0};
  attachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
  attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp = keep_contents ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout = keep_contents ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                           : VK_IMAGE_LAYOUT_UNDEFINED;
  attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkAttachmentReference reference = {0};
  reference.attachment = 0;
  reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {0};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &reference;

  VkSubpassDependency dependencies[2];
  MEMOZERO(dependencies, sizeof(dependencies));
  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
                                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[1].srcSubpass = 0;
  dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  VkRenderPassCreateInfo info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  info.attachmentCount = 1;
  info.pAttachments = &attachment;
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = 2;
  info.pDependencies = dependencies;
  return vkCreateRenderPass(ctx->device.handle, &info, ctx->allocator,
                            out_renderpass) == VK_SUCCESS;
}

static void destroy_bloom_targets(VkContext *ctx)
{
  for (u32 i = 0; i < BLOOM_MAX_MIPS; i++)
  {
    if (post.bloom_framebuffers[i])
    {
      vkDestroyFramebuffer(ctx->device.handle, post.bloom_framebuffers[i], ctx->allocator);
      post.bloom_framebuffers[i] = 0;
    }
    vkrenderer_destroy_image(ctx, &post.bloom_images[i]);
  }
  post.bloom_mip_count = 0;
}

static void write_sampled_image(VkContext *ctx, VkDescriptorSet set, u32 binding,
                                VkImageView view, VkSampler sampler)
{
  VkDescriptorImageInfo info = {0};
  info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  info.imageView = view;
  info.sampler = sampler;
  VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = set;
  write.dstBinding = binding;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = &info;
  vkUpdateDescriptorSets(ctx->device.handle, 1, &write, 0, 0);
}

static b8 create_bloom_targets(VkContext *ctx)
{
  u32 width = ctx->framebuffer_width / 2;
  u32 height = ctx->framebuffer_height / 2;

  u32 mip_count = 0;
  for (u32 i = 0; i < BLOOM_MAX_MIPS; i++)
  {
    if (width == 0)
      width = 1;
    if (height == 0)
      height = 1;
    // Stop once a mip gets too small to add anything useful.
    if (i > 0 && (width < 4 || height < 4))
      break;

    vkrenderer_image_create(ctx, VK_IMAGE_TYPE_2D, width, height,
                            VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                VK_IMAGE_USAGE_SAMPLED_BIT |
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true,
                            VK_IMAGE_ASPECT_COLOR_BIT, &post.bloom_images[i]);
    if (!post.bloom_images[i].handle || !post.bloom_images[i].view)
      return false;

    VkFramebufferCreateInfo framebuffer_info = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer_info.renderPass = post.bloom_renderpass;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments = &post.bloom_images[i].view;
    framebuffer_info.width = width;
    framebuffer_info.height = height;
    framebuffer_info.layers = 1;
    if (vkCreateFramebuffer(ctx->device.handle, &framebuffer_info, ctx->allocator,
                            &post.bloom_framebuffers[i]) != VK_SUCCESS)
      return false;
    clear_effects_image(ctx, &post.bloom_images[i]);

    if (post.bloom_sets[i])
      write_sampled_image(ctx, post.bloom_sets[i], 0, post.bloom_images[i].view,
                          post.bloom_sampler);

    mip_count++;
    width /= 2;
    height /= 2;
  }
  post.bloom_mip_count = mip_count;

  if (post.bloom_scene_set && post.scene_color.view)
    write_sampled_image(ctx, post.bloom_scene_set, 0, post.scene_color.view,
                        post.bloom_sampler);

  if (post.set)
    write_sampled_image(ctx, post.set, 2, post.bloom_images[0].view, post.color_sampler);

  return true;
}

static void destroy_effects_targets(VkContext *ctx)
{
  if (post.effects_framebuffer)
  {
    vkDestroyFramebuffer(ctx->device.handle, post.effects_framebuffer, ctx->allocator);
    post.effects_framebuffer = 0;
  }
  vkrenderer_destroy_image(ctx, &post.effects_image);
  post.effects_width = 0;
  post.effects_height = 0;
}

static void clear_effects_image(VkContext *ctx, VkRendererImage *image)
{
  VkRendererCommandBuffer command_buffer = {0};
  vkrenderer_command_buffer_allocate_and_begin_single_use(ctx, ctx->device.graphics_command_pool,
                                                         &command_buffer);

  VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image->handle;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  vkCmdPipelineBarrier(command_buffer.handle, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &barrier);

  VkClearColorValue clear;
  MEMOZERO(&clear, sizeof(clear));
  clear.float32[3] = 1.0f;
  vkCmdClearColorImage(command_buffer.handle, image->handle,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1,
                       &(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});

  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(command_buffer.handle, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, 0, 0, 0, 1, &barrier);

  vkrenderer_command_buffer_end_single_use(ctx, ctx->device.graphics_command_pool,
                                           &command_buffer, ctx->device.graphics);
}

static b8 create_effects_targets(VkContext *ctx)
{
  u32 width = ctx->framebuffer_width / 2;
  u32 height = ctx->framebuffer_height / 2;
  if (width == 0)
    width = 1;
  if (height == 0)
    height = 1;

  vkrenderer_image_create(ctx, VK_IMAGE_TYPE_2D, width, height,
                          VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true,
                          VK_IMAGE_ASPECT_COLOR_BIT, &post.effects_image);
  if (!post.effects_image.handle || !post.effects_image.view)
    return false;

  VkFramebufferCreateInfo framebuffer_info = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  framebuffer_info.renderPass = post.effects_renderpass;
  framebuffer_info.attachmentCount = 1;
  framebuffer_info.pAttachments = &post.effects_image.view;
  framebuffer_info.width = width;
  framebuffer_info.height = height;
  framebuffer_info.layers = 1;
  if (vkCreateFramebuffer(ctx->device.handle, &framebuffer_info, ctx->allocator,
                          &post.effects_framebuffer) != VK_SUCCESS)
    return false;

  post.effects_width = width;
  post.effects_height = height;
  clear_effects_image(ctx, &post.effects_image);

  if (post.effects_set && post.scene_color.view && post.scene_normal.view &&
      post.scene_depth.view)
  {
    write_sampled_image(ctx, post.effects_set, 0, post.scene_color.view, post.color_sampler);
    // Normals are octahedral encoded and must not be filtered.
    write_sampled_image(ctx, post.effects_set, 1, post.scene_normal.view,
                        post.effects_depth_sampler);
    write_sampled_image(ctx, post.effects_set, 2, post.scene_depth.view,
                        post.effects_depth_sampler);
  }

  if (post.set)
    write_sampled_image(ctx, post.set, 1, post.effects_image.view, post.color_sampler);

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

    VkWriteDescriptorSet writes[2];
    MEMOZERO(writes, sizeof(writes));
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = post.set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &image_info;

    VkDescriptorBufferInfo exposure_info = {0};
    exposure_info.buffer = post.exposure_buffer;
    exposure_info.range = 16;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = post.set;
    writes[1].dstBinding = 3;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &exposure_info;
    vkUpdateDescriptorSets(ctx->device.handle, 2, writes, 0, 0);

    write_sampled_image(ctx, post.set, 5, post.scene_normal.view, post.effects_depth_sampler);
    write_sampled_image(ctx, post.set, 6, post.scene_depth.view, post.effects_depth_sampler);
  }

  if (post.exposure_set)
  {
    VkDescriptorBufferInfo buffer_info = {0};
    buffer_info.buffer = post.exposure_buffer;
    buffer_info.range = 16;
    VkDescriptorImageInfo scene_info = {0};
    scene_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    scene_info.imageView = post.scene_color.view;
    scene_info.sampler = post.color_sampler;

    VkWriteDescriptorSet writes[2];
    MEMOZERO(writes, sizeof(writes));
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = post.exposure_set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &buffer_info;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = post.exposure_set;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &scene_info;
    vkUpdateDescriptorSets(ctx->device.handle, 2, writes, 0, 0);
  }

  return true;
}

static b8 create_descriptors(VkContext *ctx)
{
  VkDescriptorSetLayoutBinding bindings[7];
  MEMOZERO(bindings, sizeof(bindings));
  for (u32 i = 5; i < 7; i++)
  {
    // Scene normal/albedo and depth, for the depth-aware AO/GI upsample.
    bindings[i].binding = i;
    bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[i].descriptorCount = 1;
    bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  for (u32 i = 0; i < 3; i++)
  {
    bindings[i].binding = i;
    bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[i].descriptorCount = 1;
    bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  bindings[3].binding = 3;
  bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  bindings[3].descriptorCount = 1;
  bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  bindings[4].binding = 4;
  bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bindings[4].descriptorCount = 1;
  bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layout_info.bindingCount = 7;
  layout_info.pBindings = bindings;
  if (vkCreateDescriptorSetLayout(ctx->device.handle, &layout_info, ctx->allocator,
                                  &post.set_layout) != VK_SUCCESS)
    return false;

  VkDescriptorPoolSize pool_sizes[2] = {0};
  pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  pool_sizes[0].descriptorCount = 6;
  pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  pool_sizes[1].descriptorCount = 1;

  VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool_info.maxSets = 1;
  pool_info.poolSizeCount = 2;
  pool_info.pPoolSizes = pool_sizes;
  if (vkCreateDescriptorPool(ctx->device.handle, &pool_info, ctx->allocator,
                             &post.pool) != VK_SUCCESS)
    return false;

  VkDescriptorSetAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocate_info.descriptorPool = post.pool;
  allocate_info.descriptorSetCount = 1;
  allocate_info.pSetLayouts = &post.set_layout;
  if (vkAllocateDescriptorSets(ctx->device.handle, &allocate_info, &post.set) != VK_SUCCESS)
    return false;

  post.default_lut = vkrenderer_lut_create_identity(ctx);
  if (!post.default_lut)
    return false;
  post.current_lut = post.default_lut;

  VkImageView default_lut_view = 0;
  VkSampler default_lut_sampler = 0;
  if (!vkrenderer_lut_get(ctx, post.default_lut, &default_lut_view, &default_lut_sampler))
    return false;
  VkDescriptorImageInfo lut_info = {0};
  lut_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  lut_info.imageView = default_lut_view;
  lut_info.sampler = default_lut_sampler;
  VkWriteDescriptorSet lut_write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  lut_write.dstSet = post.set;
  lut_write.dstBinding = 4;
  lut_write.descriptorCount = 1;
  lut_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  lut_write.pImageInfo = &lut_info;
  vkUpdateDescriptorSets(ctx->device.handle, 1, &lut_write, 0, 0);

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

  sampler_info.magFilter = VK_FILTER_NEAREST;
  sampler_info.minFilter = VK_FILTER_NEAREST;
  if (vkCreateSampler(ctx->device.handle, &sampler_info, ctx->allocator,
                      &post.effects_depth_sampler) != VK_SUCCESS)
    return false;

  sampler_info.magFilter = VK_FILTER_LINEAR;
  sampler_info.minFilter = VK_FILTER_LINEAR;
  if (vkCreateSampler(ctx->device.handle, &sampler_info, ctx->allocator,
                      &post.effects_sampler) != VK_SUCCESS)
    return false;

  VkDescriptorSetLayoutBinding effects_bindings[3];
  MEMOZERO(effects_bindings, sizeof(effects_bindings));
  for (u32 i = 0; i < 3; i++)
  {
    effects_bindings[i].binding = i;
    effects_bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    effects_bindings[i].descriptorCount = 1;
    effects_bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  }

  VkDescriptorSetLayoutCreateInfo effects_layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  effects_layout_info.bindingCount = 3;
  effects_layout_info.pBindings = effects_bindings;
  if (vkCreateDescriptorSetLayout(ctx->device.handle, &effects_layout_info, ctx->allocator,
                                  &post.effects_set_layout) != VK_SUCCESS)
    return false;

  VkDescriptorPoolSize effects_pool_size = {0};
  effects_pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  effects_pool_size.descriptorCount = 3;

  VkDescriptorPoolCreateInfo effects_pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  effects_pool_info.maxSets = 1;
  effects_pool_info.poolSizeCount = 1;
  effects_pool_info.pPoolSizes = &effects_pool_size;
  if (vkCreateDescriptorPool(ctx->device.handle, &effects_pool_info, ctx->allocator,
                             &post.effects_pool) != VK_SUCCESS)
    return false;

  VkDescriptorSetAllocateInfo effects_allocate = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  effects_allocate.descriptorPool = post.effects_pool;
  effects_allocate.descriptorSetCount = 1;
  effects_allocate.pSetLayouts = &post.effects_set_layout;
  if (vkAllocateDescriptorSets(ctx->device.handle, &effects_allocate,
                               &post.effects_set) != VK_SUCCESS)
    return false;

  VkDescriptorSetLayoutBinding bloom_binding = {0};
  bloom_binding.binding = 0;
  bloom_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bloom_binding.descriptorCount = 1;
  bloom_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutCreateInfo bloom_layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  bloom_layout_info.bindingCount = 1;
  bloom_layout_info.pBindings = &bloom_binding;
  if (vkCreateDescriptorSetLayout(ctx->device.handle, &bloom_layout_info, ctx->allocator,
                                  &post.bloom_set_layout) != VK_SUCCESS)
    return false;

  VkDescriptorPoolSize bloom_pool_size = {0};
  bloom_pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bloom_pool_size.descriptorCount = BLOOM_MAX_MIPS + 1;
  VkDescriptorPoolCreateInfo bloom_pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  bloom_pool_info.maxSets = BLOOM_MAX_MIPS + 1;
  bloom_pool_info.poolSizeCount = 1;
  bloom_pool_info.pPoolSizes = &bloom_pool_size;
  if (vkCreateDescriptorPool(ctx->device.handle, &bloom_pool_info, ctx->allocator,
                             &post.bloom_pool) != VK_SUCCESS)
    return false;

  VkDescriptorSetLayout bloom_layouts[BLOOM_MAX_MIPS + 1];
  VkDescriptorSet bloom_allocated[BLOOM_MAX_MIPS + 1];
  for (u32 i = 0; i < BLOOM_MAX_MIPS + 1; i++)
    bloom_layouts[i] = post.bloom_set_layout;
  VkDescriptorSetAllocateInfo bloom_allocate = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  bloom_allocate.descriptorPool = post.bloom_pool;
  bloom_allocate.descriptorSetCount = BLOOM_MAX_MIPS + 1;
  bloom_allocate.pSetLayouts = bloom_layouts;
  if (vkAllocateDescriptorSets(ctx->device.handle, &bloom_allocate,
                               bloom_allocated) != VK_SUCCESS)
    return false;
  post.bloom_scene_set = bloom_allocated[0];
  for (u32 i = 0; i < BLOOM_MAX_MIPS; i++)
    post.bloom_sets[i] = bloom_allocated[i + 1];

  sampler_info.magFilter = VK_FILTER_LINEAR;
  sampler_info.minFilter = VK_FILTER_LINEAR;
  if (vkCreateSampler(ctx->device.handle, &sampler_info, ctx->allocator,
                      &post.bloom_sampler) != VK_SUCCESS)
    return false;

  VkDescriptorSetLayoutBinding exposure_bindings[2] = {0};
  exposure_bindings[0].binding = 0;
  exposure_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  exposure_bindings[0].descriptorCount = 1;
  exposure_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  exposure_bindings[1].binding = 1;
  exposure_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  exposure_bindings[1].descriptorCount = 1;
  exposure_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  VkDescriptorSetLayoutCreateInfo exposure_layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  exposure_layout_info.bindingCount = 2;
  exposure_layout_info.pBindings = exposure_bindings;
  if (vkCreateDescriptorSetLayout(ctx->device.handle, &exposure_layout_info, ctx->allocator,
                                  &post.exposure_set_layout) != VK_SUCCESS)
    return false;

  VkDescriptorPoolSize exposure_pool_sizes[2] = {0};
  exposure_pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  exposure_pool_sizes[0].descriptorCount = 1;
  exposure_pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  exposure_pool_sizes[1].descriptorCount = 1;
  VkDescriptorPoolCreateInfo exposure_pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  exposure_pool_info.maxSets = 1;
  exposure_pool_info.poolSizeCount = 2;
  exposure_pool_info.pPoolSizes = exposure_pool_sizes;
  if (vkCreateDescriptorPool(ctx->device.handle, &exposure_pool_info, ctx->allocator,
                             &post.exposure_pool) != VK_SUCCESS)
    return false;

  VkDescriptorSetAllocateInfo exposure_allocate = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  exposure_allocate.descriptorPool = post.exposure_pool;
  exposure_allocate.descriptorSetCount = 1;
  exposure_allocate.pSetLayouts = &post.exposure_set_layout;
  if (vkAllocateDescriptorSets(ctx->device.handle, &exposure_allocate,
                               &post.exposure_set) != VK_SUCCESS)
    return false;

  if (!vkrenderer_buffer_create(ctx, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                &post.exposure_buffer, &post.exposure_memory))
    return false;
  if (vkMapMemory(ctx->device.handle, post.exposure_memory, 0, VK_WHOLE_SIZE, 0,
                  &post.exposure_mapped) != VK_SUCCESS)
    return false;
  f32 initial[4] = {1.0f, 0.18f, 1.0f, 0.0f};
  MEMOCPY(post.exposure_mapped, initial, sizeof(initial));

  return true;
}

b8 vkrenderer_post_create(VkContext *ctx)
{
  MEMOZERO(&post, sizeof(post));

  if (!create_scene_renderpass(ctx))
    return false;
  if (!create_effects_renderpass(ctx))
    return false;
  if (!create_bloom_renderpass(ctx, false, &post.bloom_renderpass) ||
      !create_bloom_renderpass(ctx, true, &post.bloom_up_renderpass))
    return false;
  if (!create_descriptors(ctx))
    return false;
  if (!create_targets(ctx))
    return false;
  if (!create_effects_targets(ctx))
    return false;
  if (!create_bloom_targets(ctx))
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

  VkRendererShaderParams effects_params = {0};
  effects_params.vert_path = "shaders/effects.vert.spv";
  effects_params.frag_path = "shaders/effects.frag.spv";
  effects_params.set_layouts = &post.effects_set_layout;
  effects_params.set_layout_count = 1;
  effects_params.push_constant_size = sizeof(EffectsParams);
  effects_params.depth_test = false;
  effects_params.depth_write = false;
  effects_params.blending = false;
  effects_params.cull_mode = VK_CULL_MODE_NONE;
  effects_params.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  effects_params.render_pass = post.effects_renderpass;
  effects_params.color_attachment_count = 1;
  if (!vkrenderer_shader_create(ctx, &effects_params, &post.effects_shader))
  {
    EGNERROR("VKRENDERER failed to create the AO/GI pipeline");
    return false;
  }

  VkRendererShaderParams bloom_params = {0};
  bloom_params.vert_path = "shaders/bloom.vert.spv";
  bloom_params.frag_path = "shaders/bloom.frag.spv";
  bloom_params.set_layouts = &post.bloom_set_layout;
  bloom_params.set_layout_count = 1;
  bloom_params.push_constant_size = sizeof(BloomPushConstants);
  bloom_params.depth_test = false;
  bloom_params.depth_write = false;
  bloom_params.blending = false;
  bloom_params.cull_mode = VK_CULL_MODE_NONE;
  bloom_params.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  bloom_params.render_pass = post.bloom_renderpass;
  bloom_params.color_attachment_count = 1;
  if (!vkrenderer_shader_create(ctx, &bloom_params, &post.bloom_shader))
  {
    EGNERROR("VKRENDERER failed to create the bloom pipeline");
    return false;
  }
  bloom_params.blending = true;
  bloom_params.additive = true;
  bloom_params.render_pass = post.bloom_up_renderpass;
  if (!vkrenderer_shader_create(ctx, &bloom_params, &post.bloom_up_shader))
  {
    EGNERROR("VKRENDERER failed to create the bloom upsample pipeline");
    return false;
  }

  VkPushConstantRange exposure_range = {0};
  exposure_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  exposure_range.size = sizeof(ExposurePushConstants);
  VkPipelineLayoutCreateInfo exposure_layout_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  exposure_layout_info.setLayoutCount = 1;
  exposure_layout_info.pSetLayouts = &post.exposure_set_layout;
  exposure_layout_info.pushConstantRangeCount = 1;
  exposure_layout_info.pPushConstantRanges = &exposure_range;
  if (vkCreatePipelineLayout(ctx->device.handle, &exposure_layout_info, ctx->allocator,
                             &post.exposure_pipeline_layout) != VK_SUCCESS)
    return false;

  VkRendererShaderStage exposure_stage = {0};
  if (!vkrenderer_shader_create_stage(ctx, "shaders/exposure.comp.spv",
                                      VK_SHADER_STAGE_COMPUTE_BIT, &exposure_stage))
    return false;
  VkComputePipelineCreateInfo exposure_pipeline_info = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  exposure_pipeline_info.stage = exposure_stage.pipeline_stage_info;
  exposure_pipeline_info.layout = post.exposure_pipeline_layout;
  VkResult exposure_result = vkCreateComputePipelines(ctx->device.handle, 0, 1,
                                                      &exposure_pipeline_info, ctx->allocator,
                                                      &post.exposure_pipeline);
  vkrenderer_shader_destroy_stage(ctx, &exposure_stage);
  if (exposure_result != VK_SUCCESS)
  {
    EGNERROR("VKRENDERER failed to create the auto exposure pipeline");
    return false;
  }

  return true;
}

void vkrenderer_post_resize(VkContext *ctx)
{
  if (!ctx || !post.scene_renderpass)
    return;

  destroy_targets(ctx);
  destroy_effects_targets(ctx);
  destroy_bloom_targets(ctx);
  create_targets(ctx);
  create_effects_targets(ctx);
  create_bloom_targets(ctx);
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

static void render_exposure(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                            f64 delta_time)
{
  RendererGraphicsSettings settings = renderer_get_active_graphics_settings();
  if (!settings.auto_exposure_enabled || !post.exposure_pipeline ||
      !post.exposure_set || !post.exposure_mapped)
    return;

  u32 grid = 128;
  switch (settings.auto_exposure_quality)
  {
  case RENDERER_QUALITY_LOW:
    grid = 64;
    break;
  case RENDERER_QUALITY_MEDIUM:
    grid = 96;
    break;
  case RENDERER_QUALITY_HIGH:
    grid = 128;
    break;
  case RENDERER_QUALITY_ULTRA:
  default:
    grid = 192;
    break;
  }

  ExposurePushConstants push;
  MEMOZERO(&push, sizeof(push));
  push.params = vec4_create((f32)delta_time, settings.auto_exposure_speed,
                            settings.auto_exposure_min_ev,
                            settings.auto_exposure_max_ev);
  push.params2 = vec4_create((f32)grid, 0.0f, 1.0f, 0.0f);

  vkCmdBindPipeline(command_buffer->handle, VK_PIPELINE_BIND_POINT_COMPUTE,
                    post.exposure_pipeline);
  vkCmdBindDescriptorSets(command_buffer->handle, VK_PIPELINE_BIND_POINT_COMPUTE,
                          post.exposure_pipeline_layout, 0, 1,
                          &post.exposure_set, 0, 0);
  vkCmdPushConstants(command_buffer->handle, post.exposure_pipeline_layout,
                     VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
  vkCmdDispatch(command_buffer->handle, 1, 1, 1);

  VkBufferMemoryBarrier barrier = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer = post.exposure_buffer;
  barrier.offset = 0;
  barrier.size = 16;
  vkCmdPipelineBarrier(command_buffer->handle,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       0, 0, 0, 1, &barrier, 0, 0);
}

static void bloom_pass(VkRendererCommandBuffer *command_buffer, u32 target,
                       VkDescriptorSet source, u32 source_width, u32 source_height,
                       u32 mode, f32 radius, f32 threshold)
{
  b8 upsample = mode == 2;
  VkRendererShader *shader = upsample ? &post.bloom_up_shader : &post.bloom_shader;
  u32 width = post.bloom_images[target].width;
  u32 height = post.bloom_images[target].height;

  VkRenderPassBeginInfo info = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  info.renderPass = upsample ? post.bloom_up_renderpass : post.bloom_renderpass;
  info.framebuffer = post.bloom_framebuffers[target];
  info.renderArea.extent.width = width;
  info.renderArea.extent.height = height;
  vkCmdBeginRenderPass(command_buffer->handle, &info, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport = {0.0f, 0.0f, (f32)width, (f32)height, 0.0f, 1.0f};
  VkRect2D scissor = {{0, 0}, {width, height}};
  vkCmdSetViewport(command_buffer->handle, 0, 1, &viewport);
  vkCmdSetScissor(command_buffer->handle, 0, 1, &scissor);

  BloomPushConstants push;
  MEMOZERO(&push, sizeof(push));
  push.params = vec4_create(1.0f / (f32)source_width, 1.0f / (f32)source_height,
                            (f32)mode, radius);
  push.params2 = vec4_create(threshold, threshold * 0.5f, threshold * 48.0f + 16.0f, 0.0f);

  vkCmdBindPipeline(command_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    shader->pipeline.handle);
  vkCmdBindDescriptorSets(command_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          shader->pipeline.layout, 0, 1, &source, 0, 0);
  vkCmdPushConstants(command_buffer->handle, shader->pipeline.layout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, sizeof(push), &push);
  vkCmdDraw(command_buffer->handle, 3, 1, 0, 0);
  vkCmdEndRenderPass(command_buffer->handle);
  command_buffer->state = VKRENDERER_STATE_RECORDING;
}

static u32 bloom_mips(const RendererGraphicsSettings *settings)
{
  u32 mips = 6;
  if (settings->bloom_quality == RENDERER_QUALITY_LOW)
    mips = 4;
  else if (settings->bloom_quality == RENDERER_QUALITY_MEDIUM)
    mips = 5;
  else if (settings->bloom_quality == RENDERER_QUALITY_ULTRA)
    mips = BLOOM_MAX_MIPS;
  return mips < post.bloom_mip_count ? mips : post.bloom_mip_count;
}

static void render_bloom(VkContext *ctx, VkRendererCommandBuffer *command_buffer)
{
  (void)ctx;
  RendererGraphicsSettings settings = renderer_get_active_graphics_settings();
  if (!settings.bloom_enabled || !post.bloom_shader.pipeline.handle ||
      !post.bloom_up_shader.pipeline.handle || post.bloom_mip_count == 0 ||
      !post.bloom_scene_set)
    return;

  u32 mips = bloom_mips(&settings);

  // The threshold is authored for an exposed image (1.0 = display white), so
  // convert it to scene units with the exposure of the previous frame.
  f32 exposure = exp2f(settings.exposure_compensation);
  if (settings.auto_exposure_enabled && post.exposure_mapped)
  {
    f32 adapted = ((const f32 *)post.exposure_mapped)[0];
    if (adapted > 0.0001f && adapted < 100000.0f)
      exposure *= adapted;
  }
  f32 threshold = settings.bloom_threshold / exposure;

  // Downsample chain: scene -> mip 0 (thresholded) -> mip 1 -> ...
  bloom_pass(command_buffer, 0, post.bloom_scene_set, post.width, post.height,
             0, 0.0f, threshold);
  for (u32 mip = 1; mip < mips; mip++)
    bloom_pass(command_buffer, mip, post.bloom_sets[mip - 1],
               post.bloom_images[mip - 1].width, post.bloom_images[mip - 1].height,
               1, 0.0f, 0.0f);

  // Upsample chain: each mip is blurred and added onto the next larger one,
  // so mip 0 ends up holding the sum of every blur radius.
  f32 radius = 0.6f + settings.bloom_radius;
  for (u32 mip = mips - 1; mip > 0; mip--)
    bloom_pass(command_buffer, mip - 1, post.bloom_sets[mip],
               post.bloom_images[mip].width, post.bloom_images[mip].height,
               2, radius, 0.0f);
}

static void update_post_lut(VkContext *ctx, const RendererGraphicsSettings *settings)
{
  u32 requested = settings->lut_enabled ? settings->lut_texture : post.default_lut;
  if (requested == 0)
    requested = post.default_lut;
  if (requested == post.current_lut)
    return;

  VkImageView view = 0;
  VkSampler sampler = 0;
  if (!vkrenderer_lut_get(ctx, requested, &view, &sampler))
  {
    requested = post.default_lut;
    if (!vkrenderer_lut_get(ctx, requested, &view, &sampler))
      return;
  }

  VkDescriptorImageInfo info = {0};
  info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  info.imageView = view;
  info.sampler = sampler;
  VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = post.set;
  write.dstBinding = 4;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = &info;
  vkUpdateDescriptorSets(ctx->device.handle, 1, &write, 0, 0);
  post.current_lut = requested;
}

static u32 effects_samples(RendererQuality quality)
{
  switch (quality)
  {
  case RENDERER_QUALITY_LOW:
    return 8;
  case RENDERER_QUALITY_MEDIUM:
    return 12;
  case RENDERER_QUALITY_ULTRA:
    return 24;
  case RENDERER_QUALITY_HIGH:
  default:
    return 16;
  }
}

static void render_effects(VkContext *ctx, VkRendererCommandBuffer *command_buffer)
{
  RendererGraphicsSettings settings = renderer_get_active_graphics_settings();
  if (!settings.ao_enabled && !settings.gi_enabled)
    return;
  if (!post.effects_set || !post.effects_framebuffer ||
      !post.effects_shader.pipeline.handle)
    return;

  mat4 view_projection;
  vec3 camera_position;
  f32 projection_scale = 1.0f;
  f32 aspect = 1.0f;
  if (!vkrenderer_scene_primary_camera(ctx, &view_projection, &camera_position) ||
      !vkrenderer_scene_primary_projection(&projection_scale, &aspect))
    return;

  EffectsParams params;
  MEMOZERO(&params, sizeof(params));
  params.inverse_view_projection = mat4_inverse(view_projection);
  params.view_projection = view_projection;
  params.camera_position = vec4_create(camera_position.x, camera_position.y,
                                       camera_position.z, 1.0f);
  params.params = vec4_create(settings.ao_radius, settings.ao_intensity,
                              settings.gi_intensity, settings.gi_distance);
  RendererQuality quality = settings.ao_enabled ? settings.ao_quality : settings.gi_quality;
  if (settings.gi_enabled && settings.gi_quality > quality)
    quality = settings.gi_quality;
  u32 effect_flags = (settings.ao_enabled ? 1u : 0u) |
                     (settings.gi_enabled ? 2u : 0u);
  params.params2 = vec4_create((f32)effects_samples(quality), projection_scale,
                               aspect, (f32)effect_flags);

  VkRenderPassBeginInfo info = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  info.renderPass = post.effects_renderpass;
  info.framebuffer = post.effects_framebuffer;
  info.renderArea.extent.width = post.effects_width;
  info.renderArea.extent.height = post.effects_height;
  vkCmdBeginRenderPass(command_buffer->handle, &info, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport = {0.0f, 0.0f, (f32)post.effects_width, (f32)post.effects_height,
                         0.0f, 1.0f};
  VkRect2D scissor = {{0, 0}, {post.effects_width, post.effects_height}};
  vkCmdSetViewport(command_buffer->handle, 0, 1, &viewport);
  vkCmdSetScissor(command_buffer->handle, 0, 1, &scissor);

  vkCmdBindPipeline(command_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    post.effects_shader.pipeline.handle);
  vkCmdBindDescriptorSets(command_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          post.effects_shader.pipeline.layout, 0, 1,
                          &post.effects_set, 0, 0);
  vkCmdPushConstants(command_buffer->handle, post.effects_shader.pipeline.layout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, sizeof(params), &params);
  vkCmdDraw(command_buffer->handle, 3, 1, 0, 0);
  vkCmdEndRenderPass(command_buffer->handle);
  command_buffer->state = VKRENDERER_STATE_RECORDING;
}

void vkrenderer_post_process(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                             f64 delta_time)
{
  if (!post.ready || !post.set)
    return;

  RendererGraphicsSettings settings = renderer_get_active_graphics_settings();
  update_post_lut(ctx, &settings);
  render_exposure(ctx, command_buffer, delta_time);
  render_effects(ctx, command_buffer);
  render_bloom(ctx, command_buffer);

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
  push.effects = vec4_create(settings.ao_enabled ? 1.0f : 0.0f,
                             settings.gi_enabled ? 1.0f : 0.0f,
                             settings.gi_intensity, settings.ao_intensity);
  // The chain sums one blur per mip; normalise so the intensity setting keeps
  // its meaning whatever the quality level.
  u32 active_bloom_mips = bloom_mips(&settings);
  f32 bloom_scale = active_bloom_mips > 0 ? 4.0f / (f32)active_bloom_mips : 0.0f;
  push.bloom = vec4_create(settings.bloom_enabled && active_bloom_mips > 0 ? 1.0f : 0.0f,
                           settings.bloom_intensity * bloom_scale, settings.bloom_threshold,
                           settings.bloom_radius);
  push.exposure_mode = vec4_create(settings.auto_exposure_enabled ? 1.0f : 0.0f,
                                   exp2f(settings.exposure_compensation), 0.0f, 0.0f);

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
  vkrenderer_shader_destroy(ctx, &post.effects_shader);
  vkrenderer_shader_destroy(ctx, &post.bloom_shader);
  vkrenderer_shader_destroy(ctx, &post.bloom_up_shader);
  vkrenderer_destroy_image(ctx, &post.scene_color);
  vkrenderer_destroy_image(ctx, &post.scene_normal);
  vkrenderer_destroy_image(ctx, &post.scene_depth);
  destroy_effects_targets(ctx);
  destroy_bloom_targets(ctx);

  if (post.bloom_pool)
  {
    vkDestroyDescriptorPool(ctx->device.handle, post.bloom_pool, ctx->allocator);
    post.bloom_pool = 0;
  }
  if (post.bloom_set_layout)
  {
    vkDestroyDescriptorSetLayout(ctx->device.handle, post.bloom_set_layout, ctx->allocator);
    post.bloom_set_layout = 0;
  }
  if (post.bloom_sampler)
  {
    vkDestroySampler(ctx->device.handle, post.bloom_sampler, ctx->allocator);
    post.bloom_sampler = 0;
  }
  if (post.bloom_renderpass)
  {
    vkDestroyRenderPass(ctx->device.handle, post.bloom_renderpass, ctx->allocator);
    post.bloom_renderpass = 0;
  }
  if (post.bloom_up_renderpass)
  {
    vkDestroyRenderPass(ctx->device.handle, post.bloom_up_renderpass, ctx->allocator);
    post.bloom_up_renderpass = 0;
  }

  if (post.exposure_pipeline)
  {
    vkDestroyPipeline(ctx->device.handle, post.exposure_pipeline, ctx->allocator);
    post.exposure_pipeline = 0;
  }
  if (post.exposure_pipeline_layout)
  {
    vkDestroyPipelineLayout(ctx->device.handle, post.exposure_pipeline_layout, ctx->allocator);
    post.exposure_pipeline_layout = 0;
  }
  if (post.exposure_mapped)
  {
    vkUnmapMemory(ctx->device.handle, post.exposure_memory);
    post.exposure_mapped = 0;
  }
  vkrenderer_buffer_destroy(ctx, &post.exposure_buffer, &post.exposure_memory);
  if (post.exposure_pool)
  {
    vkDestroyDescriptorPool(ctx->device.handle, post.exposure_pool, ctx->allocator);
    post.exposure_pool = 0;
  }
  if (post.exposure_set_layout)
  {
    vkDestroyDescriptorSetLayout(ctx->device.handle, post.exposure_set_layout, ctx->allocator);
    post.exposure_set_layout = 0;
  }

  vkrenderer_lut_destroy_all(ctx);

  if (post.effects_pool)
  {
    vkDestroyDescriptorPool(ctx->device.handle, post.effects_pool, ctx->allocator);
    post.effects_pool = 0;
  }
  if (post.effects_set_layout)
  {
    vkDestroyDescriptorSetLayout(ctx->device.handle, post.effects_set_layout, ctx->allocator);
    post.effects_set_layout = 0;
  }
  if (post.effects_sampler)
  {
    vkDestroySampler(ctx->device.handle, post.effects_sampler, ctx->allocator);
    post.effects_sampler = 0;
  }
  if (post.effects_depth_sampler)
  {
    vkDestroySampler(ctx->device.handle, post.effects_depth_sampler, ctx->allocator);
    post.effects_depth_sampler = 0;
  }
  if (post.effects_renderpass)
  {
    vkDestroyRenderPass(ctx->device.handle, post.effects_renderpass, ctx->allocator);
    post.effects_renderpass = 0;
  }

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
