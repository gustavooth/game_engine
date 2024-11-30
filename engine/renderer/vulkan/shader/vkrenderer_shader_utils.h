#pragma once

#include "renderer/vulkan/vkrenderer_types.h"

b8 vkrenderer_shader_create_stage(VkContext *ctx, const char *path,
                                   VkShaderStageFlagBits stage_flag, VkRendererShaderStage *dest_shader_stage);

void vkrenderer_shader_destroy_stage(VkContext *ctx, VkRendererShaderStage *shader_stage);
