#include "vkrenderer_command_buffer.h"

void vkrenderer_command_buffer_allocate(VkContext *ctx, VkCommandPool pool, b8 is_primary,
                                      VkRendererCommandBuffer *out_command_buffer)
{
  VkCommandBufferAllocateInfo info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  info.commandPool = pool;
  info.commandBufferCount = 1;

  VKCHECK(vkAllocateCommandBuffers(ctx->device.handle, &info, &out_command_buffer->handle));

  out_command_buffer->state = VKRENDERER_STATE_READY;
}

void vkrenderer_command_buffer_free(VkContext *ctx, VkCommandPool pool,
                                    VkRendererCommandBuffer *command_buffer)
{
  vkFreeCommandBuffers(ctx->device.handle, pool, 1, &command_buffer->handle);

  command_buffer->handle = 0;
  command_buffer->state = VKRENDERER_STATE_NOT_ALLOCATED;
}

void vkrenderer_command_buffer_begin(VkRendererCommandBuffer *command_buffer,
                                     b8 is_single_use, b8 is_renderpass_cotinue, b8 is_simultaneous_use)
{
  VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

  if (is_single_use)
    begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  if (is_renderpass_cotinue)
    begin_info.flags |= VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;

  if (is_simultaneous_use)
    begin_info.flags |= VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
  
  VKCHECK(vkBeginCommandBuffer(command_buffer->handle, &begin_info));

  command_buffer->state = VKRENDERER_STATE_RECORDING;
}

void vkrenderer_command_buffer_end(VkRendererCommandBuffer *command_buffer)
{
  vkEndCommandBuffer(command_buffer->handle);

  command_buffer->state = VKRENDERER_STATE_RECORDING_ENDED;
}

void vkrenderer_command_buffer_update_submitted(VkRendererCommandBuffer *command_buffer)
{
  command_buffer->state = VKRENDERER_STATE_SUBMITTED;
}

void vkrenderer_command_buffer_reset(VkRendererCommandBuffer *command_buffer)
{
  command_buffer->state = VKRENDERER_STATE_READY;
}

/**
 * Allocates and begins recording to out_command_buffer.
 */
void vkrenderer_command_buffer_allocate_and_begin_single_use(VkContext *ctx,
                                                             VkCommandPool pool,
                                                             VkRendererCommandBuffer *out_command_buffer)
{
  vkrenderer_command_buffer_allocate(ctx, pool, true, out_command_buffer);
  vkrenderer_command_buffer_begin(out_command_buffer, true, false, false);
}

/**
 * Ends recording, submits to and waits for queue operation and frees the provided command buffer.
 */
void vkrenderer_command_buffer_end_single_use(VkContext *ctx,
                                              VkCommandPool pool,
                                              VkRendererCommandBuffer *command_buffer,
                                              VkQueue queue)
{
  vkrenderer_command_buffer_end(command_buffer);
  
  VkSubmitInfo info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
  info.commandBufferCount = 1;
  info.pCommandBuffers = &command_buffer->handle;

  VKCHECK(vkQueueSubmit(queue, 1, &info, NULL));

  VKCHECK(vkQueueWaitIdle(queue));

  vkrenderer_command_buffer_free(ctx, pool, command_buffer);
}