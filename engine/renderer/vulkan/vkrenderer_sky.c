#include "vkrenderer_sky.h"
#include "vkrenderer_lighting.h"
#include "vkrenderer_post.h"
#include "vkrenderer_image.h"
#include "vkrenderer_descriptor.h"
#include "renderer/renderer.h"
#include "renderer/renderer_settings.h"
#include "shader/vkrenderer_shader.h"
#include "core/logger.h"
#include "core/memo.h"

#include <string.h>

typedef struct SkyPushConstants
{
  mat4 inverse_view_projection;
  vec4 sun_direction; // xyz = toward the sun
  vec4 sun_disc;      // rgb = radiance of the sun disc seen from the ground
  vec4 horizon;       // rgb = horizon radiance (matches the fog colour)
  vec4 ground;        // rgb = radiance below the horizon
} SkyPushConstants;

typedef char SkyPushConstants_size_check[(sizeof(SkyPushConstants) == 128) ? 1 : -1];

typedef struct SkyLutPushConstants
{
  vec4 sun_direction; // xyz = toward the sun, w = observer altitude (km)
  vec4 sun_radiance;  // rgb = sun radiance outside the atmosphere
  vec4 atmosphere;    // rayleigh, mie, turbidity, mie anisotropy
  vec4 params;        // sun intensity, steps, light steps, unused
} SkyLutPushConstants;

typedef char SkyLutPushConstants_size_check[(sizeof(SkyLutPushConstants) == 64) ? 1 : -1];

static VkRendererShader sky_shader;
static VkRendererShader sky_lut_shader;
static VkRendererImage lut_image;
static VkRenderPass lut_renderpass;
static VkFramebuffer lut_framebuffer;
static VkSampler lut_sampler;
static VkDescriptorSet lut_set;
static vec3 sun_radiance;

// The LUT only depends on these inputs; it is re-rendered when they change.
static SkyLutPushConstants lut_cached;
static b8 lut_valid;

static b8 create_lut_resources(VkContext *ctx)
{
  VkAttachmentDescription attachment = {0};
  attachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
  attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
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
  dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
  dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[1].srcSubpass = 0;
  dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  VkRenderPassCreateInfo renderpass_info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  renderpass_info.attachmentCount = 1;
  renderpass_info.pAttachments = &attachment;
  renderpass_info.subpassCount = 1;
  renderpass_info.pSubpasses = &subpass;
  renderpass_info.dependencyCount = 2;
  renderpass_info.pDependencies = dependencies;
  if (vkCreateRenderPass(ctx->device.handle, &renderpass_info, ctx->allocator,
                         &lut_renderpass) != VK_SUCCESS)
    return false;

  vkrenderer_image_create(ctx, VK_IMAGE_TYPE_2D, VKRENDERER_SKY_LUT_WIDTH,
                          VKRENDERER_SKY_LUT_HEIGHT, VK_FORMAT_R16G16B16A16_SFLOAT,
                          VK_IMAGE_TILING_OPTIMAL,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true,
                          VK_IMAGE_ASPECT_COLOR_BIT, &lut_image);
  if (!lut_image.handle || !lut_image.view)
    return false;

  VkFramebufferCreateInfo framebuffer_info = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  framebuffer_info.renderPass = lut_renderpass;
  framebuffer_info.attachmentCount = 1;
  framebuffer_info.pAttachments = &lut_image.view;
  framebuffer_info.width = VKRENDERER_SKY_LUT_WIDTH;
  framebuffer_info.height = VKRENDERER_SKY_LUT_HEIGHT;
  framebuffer_info.layers = 1;
  if (vkCreateFramebuffer(ctx->device.handle, &framebuffer_info, ctx->allocator,
                          &lut_framebuffer) != VK_SUCCESS)
    return false;

  // Azimuth wraps around, elevation clamps at the horizon and the zenith.
  VkSamplerCreateInfo sampler_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sampler_info.magFilter = VK_FILTER_LINEAR;
  sampler_info.minFilter = VK_FILTER_LINEAR;
  sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.maxLod = 0.0f;
  if (vkCreateSampler(ctx->device.handle, &sampler_info, ctx->allocator,
                      &lut_sampler) != VK_SUCCESS)
    return false;

  if (!vkrenderer_descriptor_allocate(ctx, &lut_set))
    return false;
  vkrenderer_descriptor_update(ctx, lut_set, lut_image.view, lut_sampler);
  vkrenderer_lighting_set_sky_lut(ctx, lut_image.view, lut_sampler);
  return true;
}

b8 vkrenderer_sky_create(VkContext *ctx)
{
  lut_valid = false;
  sun_radiance = vec3_zero();

  if (!create_lut_resources(ctx))
  {
    EGNERROR("VKRENDERER failed to create the sky LUT");
    return false;
  }

  VkRendererShaderParams lut_params = {0};
  lut_params.vert_path = "shaders/sky.vert.spv";
  lut_params.frag_path = "shaders/sky_lut.frag.spv";
  lut_params.push_constant_size = sizeof(SkyLutPushConstants);
  lut_params.cull_mode = VK_CULL_MODE_NONE;
  lut_params.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  lut_params.render_pass = lut_renderpass;
  lut_params.color_attachment_count = 1;
  if (!vkrenderer_shader_create(ctx, &lut_params, &sky_lut_shader))
  {
    EGNERROR("VKRENDERER failed to create the sky LUT pipeline");
    return false;
  }

  VkRendererShaderParams params = {0};
  params.vert_path = "shaders/sky.vert.spv";
  params.frag_path = "shaders/sky.frag.spv";
  params.push_constant_size = sizeof(SkyPushConstants);
  params.depth_test = false;
  params.depth_write = false;
  params.blending = false;
  params.cull_mode = VK_CULL_MODE_NONE;
  params.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  params.render_pass = vkrenderer_post_scene_renderpass(ctx);
  params.color_attachment_count = 2;

  if (!vkrenderer_shader_create(ctx, &params, &sky_shader))
  {
    EGNERROR("VKRENDERER failed to create the atmosphere pipeline");
    return false;
  }

  return true;
}

void vkrenderer_sky_destroy(VkContext *ctx)
{
  vkrenderer_shader_destroy(ctx, &sky_shader);
  vkrenderer_shader_destroy(ctx, &sky_lut_shader);

  if (lut_set)
  {
    vkrenderer_descriptor_free(ctx, lut_set);
    lut_set = 0;
  }
  if (lut_sampler)
  {
    vkDestroySampler(ctx->device.handle, lut_sampler, ctx->allocator);
    lut_sampler = 0;
  }
  if (lut_framebuffer)
  {
    vkDestroyFramebuffer(ctx->device.handle, lut_framebuffer, ctx->allocator);
    lut_framebuffer = 0;
  }
  vkrenderer_destroy_image(ctx, &lut_image);
  if (lut_renderpass)
  {
    vkDestroyRenderPass(ctx->device.handle, lut_renderpass, ctx->allocator);
    lut_renderpass = 0;
  }
  lut_valid = false;
}

void vkrenderer_sky_set_sun(vec3 radiance)
{
  sun_radiance = radiance;
}

void vkrenderer_sky_update(VkContext *ctx, VkRendererCommandBuffer *command_buffer)
{
  (void)ctx;
  const VkRendererLighting *lighting = vkrenderer_lighting_current();
  RendererGraphicsSettings settings = renderer_get_active_graphics_settings();
  if (!lighting || !settings.atmosphere_enabled || !lut_framebuffer)
    return;

  f32 steps = 16.0f;
  f32 light_steps = 4.0f;
  switch (settings.atmosphere_quality)
  {
  case RENDERER_QUALITY_LOW:
    steps = 8.0f;
    light_steps = 2.0f;
    break;
  case RENDERER_QUALITY_MEDIUM:
    steps = 12.0f;
    light_steps = 3.0f;
    break;
  case RENDERER_QUALITY_HIGH:
    steps = 16.0f;
    light_steps = 4.0f;
    break;
  case RENDERER_QUALITY_ULTRA:
  default:
    steps = 24.0f;
    light_steps = 6.0f;
    break;
  }

  SkyLutPushConstants push;
  MEMOZERO(&push, sizeof(push));
  // Quantise the altitude so walking around does not invalidate the LUT.
  f32 altitude_km = (f32)(i32)(lighting->camera_position.y * 0.02f) * 0.05f;
  push.sun_direction = vec4_create(lighting->sun_direction.x, lighting->sun_direction.y,
                                   lighting->sun_direction.z, altitude_km > 0.0f ? altitude_km : 0.0f);
  push.sun_radiance = vec4_create(sun_radiance.x, sun_radiance.y, sun_radiance.z, 0.0f);
  push.atmosphere = vec4_create(settings.atmosphere_rayleigh, settings.atmosphere_mie,
                                settings.atmosphere_turbidity,
                                settings.atmosphere_mie_anisotropy);
  push.params = vec4_create(settings.atmosphere_sun_intensity, steps, light_steps, 0.0f);

  if (lut_valid && memcmp(&push, &lut_cached, sizeof(push)) == 0)
    return;
  lut_cached = push;
  lut_valid = true;

  VkRenderPassBeginInfo info = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  info.renderPass = lut_renderpass;
  info.framebuffer = lut_framebuffer;
  info.renderArea.extent.width = VKRENDERER_SKY_LUT_WIDTH;
  info.renderArea.extent.height = VKRENDERER_SKY_LUT_HEIGHT;
  vkCmdBeginRenderPass(command_buffer->handle, &info, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport = {0.0f, 0.0f, (f32)VKRENDERER_SKY_LUT_WIDTH,
                         (f32)VKRENDERER_SKY_LUT_HEIGHT, 0.0f, 1.0f};
  VkRect2D scissor = {{0, 0}, {VKRENDERER_SKY_LUT_WIDTH, VKRENDERER_SKY_LUT_HEIGHT}};
  vkCmdSetViewport(command_buffer->handle, 0, 1, &viewport);
  vkCmdSetScissor(command_buffer->handle, 0, 1, &scissor);
  vkCmdBindPipeline(command_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    sky_lut_shader.pipeline.handle);
  vkCmdPushConstants(command_buffer->handle, sky_lut_shader.pipeline.layout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, sizeof(push), &push);
  vkCmdDraw(command_buffer->handle, 3, 1, 0, 0);
  vkCmdEndRenderPass(command_buffer->handle);
}

void vkrenderer_sky_render(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                           mat4 inverse_view_projection)
{
  (void)ctx;
  const VkRendererLighting *lighting = vkrenderer_lighting_current();
  if (!lighting || !lut_set)
    return;

  SkyPushConstants push;
  MEMOZERO(&push, sizeof(push));
  push.inverse_view_projection = inverse_view_projection;
  push.sun_direction = lighting->sun_direction;
  // The disc is the sun seen through the atmosphere: the ground-level sun
  // colour, far brighter than any surface so bloom and exposure react to it.
  f32 disc = lighting->sun_direction.w * 80.0f;
  push.sun_disc = vec4_create(lighting->sun_color.x * disc, lighting->sun_color.y * disc,
                              lighting->sun_color.z * disc, 0.0f);
  push.horizon = lighting->sky_horizon;
  push.ground = lighting->sky_ground;

  vkCmdBindPipeline(command_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    sky_shader.pipeline.handle);
  vkCmdBindDescriptorSets(command_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          sky_shader.pipeline.layout, 0, 1, &lut_set, 0, 0);
  vkCmdPushConstants(command_buffer->handle, sky_shader.pipeline.layout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, sizeof(push), &push);
  vkCmdDraw(command_buffer->handle, 3, 1, 0, 0);
}
