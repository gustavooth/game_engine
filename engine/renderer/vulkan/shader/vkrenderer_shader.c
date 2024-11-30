#include "vkrenderer_shader.h"
#include "vkrenderer_shader_utils.h"

b8 vkrenderer_shader_create(VkContext *ctx, VkRendererShader *dest_shader)
{
  const char* stage_paths[VKRENDERER_SHADER_STATGE_TYPE_COUNT] = {
    "shaders/object.vert.spv", 
    "shaders/object.frag.spv"
  };

  VkShaderStageFlagBits stage_types[VKRENDERER_SHADER_STATGE_TYPE_COUNT] = {
    VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT
  };

  for (u32 i = 0; i < VKRENDERER_SHADER_STATGE_TYPE_COUNT; i++)
  {
    if(!vkrenderer_shader_create_stage(ctx, stage_paths[i], stage_types[i], &dest_shader->stages[i]))
      return false;
  }
  
  return true;
}

void vkrenderer_shader_destroy(VkContext *ctx, VkRendererShader *shader)
{
}

void vkrendereR_shader_use(VkContext *ctx, VkRendererShader *shader)
{
}