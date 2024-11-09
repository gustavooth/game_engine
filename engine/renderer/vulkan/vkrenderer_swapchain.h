#pragma once
#include "defines.h"

#include "vkrenderer_types.h"

b8 vkrenderer_swapchain_create(VkContext *ctx, u32 width, u32 height, VkRendererSwapchain *out_swapchain);
void vkrenderer_swapchain_recreate(VkContext *ctx, u32 width, u32 height, VkRendererSwapchain *swapchain);
void vkrenderer_swapchain_destroy(VkContext *ctx, VkRendererSwapchain *swapchain);

b8 vkrenderer_swapchain_acquire_next_image_index(VkContext *ctx, VkRendererSwapchain *swapchain, u64 timeout_ns, VkSemaphore image_available,
                                                 VkFence fence, u32 *out_image_index);

b8 vkrenderer_swapchain_present(VkContext *ctx, VkRendererSwapchain *swapchain, VkQueue graphics_queue,
                                VkQueue present_queue, VkSemaphore renderer_complete_semaphore, u32 present_image_index);
