#pragma once

#include "vkrenderer_types.h"

/**
 * Requests a PNG screenshot of the next rendered frame. The file is written
 * after that frame is presented, so the call is asynchronous with respect to
 * the game loop.
 */
b8 vkrenderer_screenshot_request(const char *path);

/**
 * Records the swapchain-to-buffer copy for the current frame. Must be called
 * after the render pass ends and before the command buffer ends.
 */
void vkrenderer_screenshot_record(VkContext *ctx, VkRendererCommandBuffer *command_buffer);

/**
 * Waits for the submitted frame and writes the pending screenshot to disk.
 */
void vkrenderer_screenshot_capture(VkContext *ctx);

void vkrenderer_screenshot_shutdown(VkContext *ctx);
