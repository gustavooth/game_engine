#include "vkrenderer_shader_utils.h"
#include "platform/filemanager.h"
#include "core/memo.h"
#include "core/logger.h"

b8 vkrenderer_shader_create_stage(VkContext *ctx, const char *path,
                                   VkShaderStageFlagBits stage_flag, VkRendererShaderStage *dest_shader_stage)
{
  u8* buffer;
  u64 buf_len;
  if(!filemanager_read_all(path, &buffer, &buf_len)) {
    EGNERROR("vkrenderer_shader_create_stage: failed to read file %s", path);
    return false;
  }

  MEMOSET(dest_shader_stage, 0, sizeof(VkRendererShaderStage));

  dest_shader_stage->module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  dest_shader_stage->module_info.codeSize = buf_len;
  dest_shader_stage->module_info.pCode = (u32*)buffer;

  VKCHECK(vkCreateShaderModule(ctx->device.handle, &dest_shader_stage->module_info, ctx->allocator, &dest_shader_stage->handle));

  dest_shader_stage->pipeline_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  dest_shader_stage->pipeline_stage_info.stage = stage_flag;  
  dest_shader_stage->pipeline_stage_info.module = dest_shader_stage->handle;
  dest_shader_stage->pipeline_stage_info.pName = "main";

  if (buffer)
    MEMOFREE(buffer);

  return true;
}

void vkrenderer_shader_destroy_stage(VkContext *ctx, VkRendererShaderStage *shader_stage)
{
  vkDestroyShaderModule(ctx->device.handle, shader_stage->handle, ctx->allocator);
  MEMOSET(shader_stage, 0, sizeof(VkRendererShaderStage));
}
