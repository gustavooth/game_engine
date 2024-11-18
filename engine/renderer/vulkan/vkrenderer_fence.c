#include "vkrenderer_fence.h"
#include "core/memo.h"
#include "core/logger.h"

void vkrenderer_fence_create(VkContext *ctx, b8 create_signaled, VkRendererFence *out_fence)
{
  VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  if (create_signaled)
  {
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    out_fence->is_signaled = true;
  }
  else
  {
    out_fence->is_signaled = false;
  }

  VKCHECK(vkCreateFence(ctx->device.handle, &fence_info, ctx->allocator, &out_fence->handle));
}

void vkrenderer_fence_destroy(VkContext *ctx, VkRendererFence *fence)
{
  if (fence->handle)
    vkDestroyFence(ctx->device.handle, fence->handle, ctx->allocator);

  MEMOZERO(fence, sizeof(VkRendererFence));
}

b8 vkrenderer_fence_wait(VkContext *ctx, VkRendererFence *fence, u64 timeout_ns)
{
  if (!fence->is_signaled)
  {
    VkResult result = vkWaitForFences(ctx->device.handle, 1, &fence->handle, VK_TRUE, timeout_ns);

    switch (result)
    {
    case VK_SUCCESS:
      fence->is_signaled = true;
      return true;
    case VK_TIMEOUT:
      EGNWARN("VKRENDERER vkrenderer_fence_wait - Timed out");
      break;
    case VK_ERROR_DEVICE_LOST:
      EGNERROR("VKRENDERER vkrenderer_fence_wait - VK_ERROR_DEVICE_LOST.");
      break;
    case VK_ERROR_OUT_OF_HOST_MEMORY:
      EGNERROR("VKRENDERER vkrenderer_fence_wait - VK_ERROR_OUT_OF_HOST_MEMORY.");
      break;
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
      EGNERROR("VKRENDERER vkrenderer_fence_wait - VK_ERROR_OUT_OF_DEVICE_MEMORY.");
      break;
    default:
      EGNERROR("VKRENDERER vkrenderer_fence_wait - An unknown error has occurred.");
      break;
    }
  }
  else
  {
    return true;
  }

  return false;
}

void vkrenderer_fence_reset(VkContext *ctx, VkRendererFence *fence)
{
  if (fence->is_signaled) {
    vkResetFences(ctx->device.handle, 1, &fence->handle);
    fence->is_signaled = false;
  }
}