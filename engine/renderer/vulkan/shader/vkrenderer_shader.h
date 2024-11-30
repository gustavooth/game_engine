#pragma once

#include "renderer/vulkan/vkrenderer_types.h"

b8 vkrenderer_shader_create(VkContext *ctx, VkRendererShader *out_shader);

void vkrenderer_shader_destroy(VkContext *ctx, VkRendererShader *shader);

void vkrendereR_shader_use(VkContext *ctx, VkRendererShader *shader);
