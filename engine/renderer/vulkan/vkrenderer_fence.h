#include "vkrenderer_types.h"

void vkrenderer_fence_create(VkContext *ctx, b8 create_signaled, VkRendererFence *out_fence);

void vkrenderer_fence_destroy(VkContext *ctx, VkRendererFence *fence);

b8 vkrenderer_fence_wait(VkContext *ctx, VkRendererFence *fence, u64 timeout_ns);

void vkrenderer_fence_reset(VkContext *ctx, VkRendererFence *fence);
