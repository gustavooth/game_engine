#include "vkrenderer_swapchain.h"
#include "core/logger.h"
#include "core/memo.h"
#include "renderer/renderer.h"
#include "vkrenderer_image.h"

b8 create(VkContext *ctx, u32 width, u32 height, VkRendererSwapchain *out_swapchain);
void destroy(VkContext *ctx, VkRendererSwapchain *swapchain);

b8 vkrenderer_swapchain_create(VkContext *ctx, u32 width, u32 height, VkRendererSwapchain *out_swapchain)
{
  return create(ctx, width, height, out_swapchain);
}

void vkrenderer_swapchain_recreate(VkContext *ctx, u32 width, u32 height, VkRendererSwapchain *swapchain)
{
  destroy(ctx, swapchain);
  create(ctx, width, height, swapchain);
}

void vkrenderer_swapchain_destroy(VkContext *ctx, VkRendererSwapchain *swapchain)
{
  destroy(ctx, swapchain);
}

b8 vkrenderer_swapchain_acquire_next_image_index(VkContext *ctx, VkRendererSwapchain *swapchain, u64 timeout_ns, VkSemaphore image_available,
                                                 VkFence fence, u32 *out_image_index)
{
  VkResult result = vkAcquireNextImageKHR(ctx->device.handle, swapchain->handle, timeout_ns, image_available, fence, out_image_index);

  if (result == VK_ERROR_OUT_OF_DATE_KHR)
  {
    renderer_resize(ctx->framebuffer_width, ctx->framebuffer_height);
    return false;
  }
  else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
  {
    EGNERROR("VKRENDERER failed to acquire next swapchain image index!");
    return false;
  }

  return true;
}

b8 vkrenderer_swapchain_present(VkContext *ctx, VkRendererSwapchain *swapchain, VkQueue graphics_queue,
                                VkQueue present_queue, VkSemaphore renderer_complete_semaphore, u32 present_image_index)
{
  VkPresentInfoKHR present_info = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = &renderer_complete_semaphore;
  present_info.swapchainCount = 1;
  present_info.pSwapchains = &swapchain->handle;
  present_info.pImageIndices = &present_image_index;

  VkResult result = vkQueuePresentKHR(present_queue, &present_info);

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
  {
    renderer_resize(ctx->framebuffer_width, ctx->framebuffer_height);
    return false;
  }
  else if (result != VK_SUCCESS)
  {
    EGNERROR("VKRENDERER failed to present swapchain image!");
    return false;
  }

  return true;
}

b8 create(VkContext *ctx, u32 width, u32 height, VkRendererSwapchain *out_swapchain)
{
  EGNDEBUG("VKRENDERER Creating swapchain...");

  VkRendererSwapchainSupport support = ctx->device.swapchain_support;

  b8 found = false;
  for (u32 i = 0; i < support.format_count; i++)
  {
    if (support.formats[i].format == VK_FORMAT_B8G8R8A8_UNORM && support.formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
    {
      out_swapchain->image_format = support.formats[i];
      found = true;
      break;
    }
  }

  if (!found)
  {
    EGNWARN("VKRENDERER preferred image format not found!");
    out_swapchain->image_format = support.formats[0];
  }

  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
  if (!ctx->vsync)
  {
    present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    for (u32 i = 0; i < support.present_mode_count; i++)
    {
      if (support.present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
      {
        present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
        break;
      }
    }
  }

  VkExtent2D min = support.capabilities.minImageExtent;
  VkExtent2D max = support.capabilities.maxImageExtent;
  width = EGNCLAMP(width, min.width, max.width);
  height = EGNCLAMP(height, min.height, max.height);

  VkExtent2D swapchain_extent = {width, height};

  u32 image_count = support.capabilities.minImageCount + 1;
  if (support.capabilities.maxImageCount > 0 && image_count > support.capabilities.maxImageCount)
    image_count = support.capabilities.maxImageCount;

  out_swapchain->max_frame_in_flight = image_count > 1 ? image_count - 1 : 1;

  VkSwapchainCreateInfoKHR swapchain_info = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  swapchain_info.surface = ctx->surface;
  swapchain_info.minImageCount = image_count;
  swapchain_info.imageFormat = out_swapchain->image_format.format;
  swapchain_info.imageColorSpace = out_swapchain->image_format.colorSpace;
  swapchain_info.imageExtent = swapchain_extent;
  swapchain_info.imageArrayLayers = 1;
  swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  if (support.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
    swapchain_info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  if (support.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
    swapchain_info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

  if (ctx->device.queue_indexes.graphics_family_index != ctx->device.queue_indexes.present_family_index)
  {
    u32 queueFamilyIndices[] = {
        ctx->device.queue_indexes.graphics_family_index,
        ctx->device.queue_indexes.present_family_index,
    };
    swapchain_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT,
    swapchain_info.queueFamilyIndexCount = 2,
    swapchain_info.pQueueFamilyIndices = queueFamilyIndices;
  }
  else
  {
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
    swapchain_info.queueFamilyIndexCount = 0,
    swapchain_info.pQueueFamilyIndices = 0;
  }

  swapchain_info.preTransform = support.capabilities.currentTransform;
  swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swapchain_info.presentMode = present_mode;
  swapchain_info.clipped = VK_TRUE;

  VKCHECK(vkCreateSwapchainKHR(ctx->device.handle, &swapchain_info, NULL, &ctx->swapchain.handle))

  out_swapchain->image_count = 0;
  VKCHECK(vkGetSwapchainImagesKHR(ctx->device.handle, out_swapchain->handle, &out_swapchain->image_count, NULL))
  out_swapchain->images = MEMOALLOC(sizeof(VkImage) * out_swapchain->image_count, MEMO_RENDERER);
  out_swapchain->views = MEMOALLOC(sizeof(VkImageView) * out_swapchain->image_count, MEMO_RENDERER);
  VKCHECK(vkGetSwapchainImagesKHR(ctx->device.handle, out_swapchain->handle, &out_swapchain->image_count, out_swapchain->images))

  for (u32 i = 0; i < out_swapchain->image_count; i++)
  {
    VkImageViewCreateInfo image_view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    image_view_info.image = out_swapchain->images[i];
    image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    image_view_info.format = out_swapchain->image_format.format;
    image_view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_view_info.subresourceRange.baseMipLevel = 0;
    image_view_info.subresourceRange.levelCount = 1;
    image_view_info.subresourceRange.baseArrayLayer = 0;
    image_view_info.subresourceRange.layerCount = 1;

    VKCHECK(vkCreateImageView(ctx->device.handle, &image_view_info, ctx->allocator, &out_swapchain->views[i]))
  }

  vkrenderer_image_create(ctx, VK_IMAGE_TYPE_2D, swapchain_extent.width, swapchain_extent.height,
                          ctx->device.depth_format, VK_IMAGE_TILING_OPTIMAL,
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          true, VK_IMAGE_ASPECT_DEPTH_BIT, &out_swapchain->depth_attachment);

  return true;
}

void destroy(VkContext *ctx, VkRendererSwapchain *swapchain)
{
  vkDeviceWaitIdle(ctx->device.handle);

  if (swapchain->views)
  {
    for (u32 i = 0; i < swapchain->image_count; i++)
      vkDestroyImageView(ctx->device.handle, swapchain->views[i], ctx->allocator);
    MEMOFREE(swapchain->views);
  }

  if (swapchain->images)
    MEMOFREE(swapchain->images);

  if (swapchain->handle)
    vkDestroySwapchainKHR(ctx->device.handle, swapchain->handle, ctx->allocator);

  vkrenderer_destroy_image(ctx, &swapchain->depth_attachment);

  MEMOZERO(swapchain, sizeof(VkRendererSwapchain));
}