#include "vkrenderer_sky.h"
#include "vkrenderer_lighting.h"
#include "vkrenderer_post.h"
#include "renderer/renderer.h"
#include "renderer/renderer_settings.h"
#include "shader/vkrenderer_shader.h"
#include "core/logger.h"
#include "core/memo.h"

typedef struct SkyPushConstants
{
  mat4 inverse_view_projection;
  vec4 camera_position;
  vec4 sun_direction;
  vec4 sun_color;
  vec4 atmosphere;
  vec4 params;
} SkyPushConstants;

typedef char SkyPushConstants_size_check[(sizeof(SkyPushConstants) == 144) ? 1 : -1];

static VkRendererShader sky_shader;

b8 vkrenderer_sky_create(VkContext *ctx)
{
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
}

void vkrenderer_sky_render(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                           mat4 inverse_view_projection)
{
  (void)ctx;
  const VkRendererLighting *lighting = vkrenderer_lighting_current();
  if (!lighting)
    return;

  RendererGraphicsSettings settings = renderer_get_active_graphics_settings();
  if (!settings.atmosphere_enabled)
    return;

  SkyPushConstants push;
  MEMOZERO(&push, sizeof(push));
  push.inverse_view_projection = inverse_view_projection;
  push.camera_position = lighting->camera_position;
  push.sun_direction = lighting->sun_direction;
  push.sun_color = lighting->sun_color;

  f32 steps = 8.0f;
  f32 light_steps = 2.0f;
  switch (settings.atmosphere_quality)
  {
  case RENDERER_QUALITY_LOW:
    steps = 4.0f;
    light_steps = 1.0f;
    break;
  case RENDERER_QUALITY_MEDIUM:
    steps = 10.0f;
    light_steps = 2.0f;
    break;
  case RENDERER_QUALITY_HIGH:
    steps = 16.0f;
    light_steps = 3.0f;
    break;
  case RENDERER_QUALITY_ULTRA:
  default:
    steps = 24.0f;
    light_steps = 4.0f;
    break;
  }

  push.atmosphere = vec4_create(settings.atmosphere_rayleigh,
                                settings.atmosphere_mie,
                                settings.atmosphere_turbidity,
                                settings.atmosphere_mie_anisotropy);
  push.params = vec4_create(settings.atmosphere_sun_intensity, steps, light_steps, 0.0f);

  vkCmdBindPipeline(command_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    sky_shader.pipeline.handle);
  vkCmdPushConstants(command_buffer->handle, sky_shader.pipeline.layout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, sizeof(push), &push);
  vkCmdDraw(command_buffer->handle, 3, 1, 0, 0);
}
