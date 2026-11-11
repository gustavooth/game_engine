#pragma once

#include "renderer/vulkan/vkrenderer_types.h"

typedef struct VkRendererShaderParams
{
  const char *vert_path;
  const char *frag_path;

  const VkVertexInputBindingDescription *binding;
  const VkVertexInputAttributeDescription *attributes;
  u32 attribute_count;

  const VkDescriptorSetLayout *set_layouts;
  u32 set_layout_count;

  u32 push_constant_size;

  b8 depth_test;
  b8 depth_write;
  b8 blending;
  b8 additive; // with blending: dst + src instead of alpha blending
  VkCullModeFlags cull_mode;
  VkFrontFace front_face;

  // Rasterizer depth bias (shadow maps). Both zero = disabled.
  f32 depth_bias_constant;
  f32 depth_bias_slope;
  // Clamp fragments in front of the near plane instead of clipping them, so
  // shadow casters behind the light volume still cast (needs the device
  // feature; ignored otherwise).
  b8 depth_clamp;

  VkRenderPass render_pass;     // 0 = main render pass
  u32 color_attachment_count;   // 0 for depth-only shadow pipelines
} VkRendererShaderParams;

/**
 * Loads the SPIR-V stages and creates a complete graphics pipeline using the
 * renderer's main render pass and shared descriptor set layout.
 */
b8 vkrenderer_shader_create(VkContext *ctx, const VkRendererShaderParams *params,
                            VkRendererShader *out_shader);

void vkrenderer_shader_destroy(VkContext *ctx, VkRendererShader *shader);

void vkrenderer_shader_bind(VkContext *ctx, VkRendererShader *shader,
                            VkCommandBuffer command_buffer);
