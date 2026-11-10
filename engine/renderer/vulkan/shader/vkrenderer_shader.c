#include "vkrenderer_shader.h"
#include "vkrenderer_shader_utils.h"
#include "core/logger.h"
#include "core/memo.h"

b8 vkrenderer_shader_create(VkContext *ctx, const VkRendererShaderParams *params,
                            VkRendererShader *dest_shader)
{
  if (!ctx || !params || !dest_shader || !params->vert_path || !params->frag_path)
    return false;

  MEMOZERO(dest_shader, sizeof(VkRendererShader));

  const char *stage_paths[VKRENDERER_SHADER_STATGE_TYPE_COUNT] = {
      params->vert_path,
      params->frag_path};

  VkShaderStageFlagBits stage_types[VKRENDERER_SHADER_STATGE_TYPE_COUNT] = {
      VK_SHADER_STAGE_VERTEX_BIT,
      VK_SHADER_STAGE_FRAGMENT_BIT};

  for (u32 i = 0; i < VKRENDERER_SHADER_STATGE_TYPE_COUNT; i++)
  {
    if (!vkrenderer_shader_create_stage(ctx, stage_paths[i], stage_types[i], &dest_shader->stages[i]))
    {
      vkrenderer_shader_destroy(ctx, dest_shader);
      return false;
    }
  }

  VkPushConstantRange push_range = {0};
  push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  push_range.offset = 0;
  push_range.size = params->push_constant_size;

  VkPipelineLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout_info.setLayoutCount = 1;
  layout_info.pSetLayouts = &ctx->descriptor_set_layout;
  if (params->push_constant_size > 0)
  {
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_range;
  }

  if (vkCreatePipelineLayout(ctx->device.handle, &layout_info, ctx->allocator,
                             &dest_shader->pipeline.layout) != VK_SUCCESS)
  {
    EGNERROR("VKRENDERER failed to create pipeline layout");
    vkrenderer_shader_destroy(ctx, dest_shader);
    return false;
  }

  VkPipelineShaderStageCreateInfo stage_infos[VKRENDERER_SHADER_STATGE_TYPE_COUNT];
  for (u32 i = 0; i < VKRENDERER_SHADER_STATGE_TYPE_COUNT; i++)
    stage_infos[i] = dest_shader->stages[i].pipeline_stage_info;

  VkPipelineVertexInputStateCreateInfo vertex_input = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  if (params->binding && params->attributes && params->attribute_count > 0)
  {
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = params->binding;
    vertex_input.vertexAttributeDescriptionCount = params->attribute_count;
    vertex_input.pVertexAttributeDescriptions = params->attributes;
  }

  VkPipelineInputAssemblyStateCreateInfo input_assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  input_assembly.primitiveRestartEnable = VK_FALSE;

  VkPipelineViewportStateCreateInfo viewport_state = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport_state.viewportCount = 1;
  viewport_state.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizer = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = params->cull_mode;
  rasterizer.frontFace = params->front_face;
  rasterizer.depthBiasEnable = VK_FALSE;

  VkPipelineMultisampleStateCreateInfo multisampling = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisampling.sampleShadingEnable = VK_FALSE;

  VkPipelineDepthStencilStateCreateInfo depth_stencil = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  depth_stencil.depthTestEnable = params->depth_test ? VK_TRUE : VK_FALSE;
  depth_stencil.depthWriteEnable = params->depth_write ? VK_TRUE : VK_FALSE;
  depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  depth_stencil.depthBoundsTestEnable = VK_FALSE;
  depth_stencil.stencilTestEnable = VK_FALSE;

  VkPipelineColorBlendAttachmentState color_attachment = {0};
  color_attachment.blendEnable = params->blending ? VK_TRUE : VK_FALSE;
  color_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  color_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  color_attachment.colorBlendOp = VK_BLEND_OP_ADD;
  color_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  color_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  color_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
  color_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                    VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT |
                                    VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendStateCreateInfo color_blending = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  color_blending.logicOpEnable = VK_FALSE;
  color_blending.attachmentCount = 1;
  color_blending.pAttachments = &color_attachment;

  VkDynamicState dynamic_states[2] = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR};

  VkPipelineDynamicStateCreateInfo dynamic_state = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic_state.dynamicStateCount = 2;
  dynamic_state.pDynamicStates = dynamic_states;

  VkGraphicsPipelineCreateInfo pipeline_info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pipeline_info.stageCount = VKRENDERER_SHADER_STATGE_TYPE_COUNT;
  pipeline_info.pStages = stage_infos;
  pipeline_info.pVertexInputState = &vertex_input;
  pipeline_info.pInputAssemblyState = &input_assembly;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &rasterizer;
  pipeline_info.pMultisampleState = &multisampling;
  pipeline_info.pDepthStencilState = &depth_stencil;
  pipeline_info.pColorBlendState = &color_blending;
  pipeline_info.pDynamicState = &dynamic_state;
  pipeline_info.layout = dest_shader->pipeline.layout;
  pipeline_info.renderPass = ctx->main_renderpass.handle;
  pipeline_info.subpass = 0;
  pipeline_info.basePipelineHandle = 0;
  pipeline_info.basePipelineIndex = -1;

  VkResult result = vkCreateGraphicsPipelines(ctx->device.handle, 0, 1,
                                              &pipeline_info, ctx->allocator,
                                              &dest_shader->pipeline.handle);
  if (result != VK_SUCCESS)
  {
    EGNERROR("VKRENDERER failed to create graphics pipeline");
    vkrenderer_shader_destroy(ctx, dest_shader);
    return false;
  }

  return true;
}

void vkrenderer_shader_destroy(VkContext *ctx, VkRendererShader *shader)
{
  if (!ctx || !shader)
    return;

  if (shader->pipeline.handle)
    vkDestroyPipeline(ctx->device.handle, shader->pipeline.handle, ctx->allocator);

  if (shader->pipeline.layout)
    vkDestroyPipelineLayout(ctx->device.handle, shader->pipeline.layout, ctx->allocator);

  for (u32 i = 0; i < VKRENDERER_SHADER_STATGE_TYPE_COUNT; i++)
  {
    if (shader->stages[i].handle)
      vkrenderer_shader_destroy_stage(ctx, &shader->stages[i]);
  }

  MEMOZERO(shader, sizeof(VkRendererShader));
}

void vkrenderer_shader_bind(VkContext *ctx, VkRendererShader *shader,
                            VkCommandBuffer command_buffer)
{
  (void)ctx;
  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    shader->pipeline.handle);
}
