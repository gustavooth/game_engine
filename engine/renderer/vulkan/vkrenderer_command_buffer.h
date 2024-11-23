#pragma once

#include "vkrenderer_types.h"

void vkrenderer_command_buffer_allocate(VkContext *ctx, VkCommandPool pool, b8 is_primary,
                                      VkRendererCommandBuffer *out_command_buffer);

void vkrenderer_command_buffer_free(VkContext *ctx, VkCommandPool pool,
                                    VkRendererCommandBuffer *command_buffer);

void vkrenderer_command_buffer_begin(VkRendererCommandBuffer *command_buffer,
                                     b8 is_single_use, b8 is_renderpass_cotinue, b8 is_simultaneous_use);

void vkrenderer_command_buffer_end(VkRendererCommandBuffer *command_buffer);

void vkrenderer_command_buffer_update_submitted(VkRendererCommandBuffer *command_buffer);

void vkrenderer_command_buffer_reset(VkRendererCommandBuffer *command_buffer);

/**
 * Allocates and begins recording to out_command_buffer.
 */
void vkrenderer_command_buffer_allocate_and_begin_single_use(VkContext *context,
                                                             VkCommandPool pool,
                                                             VkRendererCommandBuffer *out_command_buffer);

/**
 * Ends recording, submits to and waits for queue operation and frees the provided command buffer.
 */
void vkrenderer_command_buffer_end_single_use(VkContext *context,
                                              VkCommandPool pool,
                                              VkRendererCommandBuffer *command_buffer,
                                              VkQueue queue);
