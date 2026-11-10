#include "vkrenderer_screenshot.h"
#include "vkrenderer_buffer.h"
#include "renderer/renderer.h"
#include "core/logger.h"
#include "core/memo.h"

#include <stdio.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wcast-qual"
#pragma clang diagnostic ignored "-Wcomma"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include "stb_image_write.h"
#pragma clang diagnostic pop

#define VKRENDERER_SCREENSHOT_PATH_MAX 1024

static char screenshot_path[VKRENDERER_SCREENSHOT_PATH_MAX];
static b8 screenshot_pending;

static VkBuffer screenshot_buffer;
static VkDeviceMemory screenshot_buffer_memory;
static u64 screenshot_buffer_capacity;

static b8 screenshot_ensure_buffer(VkContext *ctx, u64 required)
{
  if (required <= screenshot_buffer_capacity)
    return true;

  if (screenshot_buffer || screenshot_buffer_memory)
  {
    vkrenderer_buffer_destroy(ctx, &screenshot_buffer, &screenshot_buffer_memory);
    screenshot_buffer_capacity = 0;
  }

  if (!vkrenderer_buffer_create(ctx, required,
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                &screenshot_buffer, &screenshot_buffer_memory))
    return false;

  screenshot_buffer_capacity = required;
  return true;
}

b8 vkrenderer_screenshot_request(const char *path)
{
  if (!path || path[0] == '\0')
    return false;

  u64 length = (u64)strlen(path);
  if (length >= VKRENDERER_SCREENSHOT_PATH_MAX)
  {
    EGNERROR("renderer_screenshot: path is too long");
    return false;
  }

  MEMOCPY(screenshot_path, path, length + 1);
  screenshot_pending = true;
  return true;
}

static void screenshot_image_barrier(VkCommandBuffer command_buffer, VkImage image,
                                     VkImageLayout old_layout, VkImageLayout new_layout,
                                     VkAccessFlags src_access, VkAccessFlags dst_access,
                                     VkPipelineStageFlags src_stage,
                                     VkPipelineStageFlags dst_stage)
{
  VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = src_access;
  barrier.dstAccessMask = dst_access;

  vkCmdPipelineBarrier(command_buffer, src_stage, dst_stage, 0, 0, 0, 0, 0, 1, &barrier);
}

void vkrenderer_screenshot_record(VkContext *ctx, VkRendererCommandBuffer *command_buffer)
{
  if (!screenshot_pending || !ctx || !command_buffer)
    return;

  u32 width = ctx->framebuffer_width;
  u32 height = ctx->framebuffer_height;
  u64 required = (u64)width * (u64)height * 4;

  if (width == 0 || height == 0 || required == 0)
  {
    EGNERROR("renderer_screenshot: invalid framebuffer size");
    screenshot_pending = false;
    return;
  }

  if (!(ctx->device.swapchain_support.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
  {
    EGNERROR("renderer_screenshot: swapchain images do not support transfer-source usage");
    screenshot_pending = false;
    return;
  }

  if (!screenshot_ensure_buffer(ctx, required))
  {
    EGNERROR("renderer_screenshot: failed to allocate readback buffer");
    screenshot_pending = false;
    return;
  }

  VkImage image = ctx->swapchain.images[ctx->image_index];

  screenshot_image_barrier(command_buffer->handle, image,
                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                           VK_ACCESS_TRANSFER_READ_BIT,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT);

  VkBufferImageCopy region = {0};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset.x = 0;
  region.imageOffset.y = 0;
  region.imageOffset.z = 0;
  region.imageExtent.width = width;
  region.imageExtent.height = height;
  region.imageExtent.depth = 1;

  vkCmdCopyImageToBuffer(command_buffer->handle, image,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         screenshot_buffer, 1, &region);

  screenshot_image_barrier(command_buffer->handle, image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                           VK_ACCESS_TRANSFER_READ_BIT,
                           0,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}

void vkrenderer_screenshot_capture(VkContext *ctx)
{
  if (!screenshot_pending || !ctx)
    return;

  if (!screenshot_buffer || !screenshot_buffer_memory)
  {
    screenshot_pending = false;
    return;
  }

  vkDeviceWaitIdle(ctx->device.handle);

  void *mapped = 0;
  if (vkMapMemory(ctx->device.handle, screenshot_buffer_memory, 0,
                  VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
  {
    EGNERROR("renderer_screenshot: failed to map readback buffer");
    screenshot_pending = false;
    return;
  }

  u32 width = ctx->framebuffer_width;
  u32 height = ctx->framebuffer_height;
  u8 *pixels = (u8 *)mapped;

  VkFormat format = ctx->swapchain.image_format.format;
  if (format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB)
  {
    for (u64 i = 0; i < (u64)width * (u64)height; i++)
    {
      u8 blue = pixels[i * 4 + 0];
      u8 red = pixels[i * 4 + 2];
      pixels[i * 4 + 0] = red;
      pixels[i * 4 + 2] = blue;
    }
  }

  int result = stbi_write_png(screenshot_path, (int)width, (int)height, 4,
                              pixels, (int)(width * 4));

  vkUnmapMemory(ctx->device.handle, screenshot_buffer_memory);

  if (result)
  {
    EGNINFO("VKRENDERER screenshot saved to '%s'", screenshot_path);
  }
  else
  {
    EGNERROR("VKRENDERER failed to write screenshot '%s'", screenshot_path);
  }

  screenshot_pending = false;
}

void vkrenderer_screenshot_shutdown(VkContext *ctx)
{
  if (screenshot_buffer || screenshot_buffer_memory)
    vkrenderer_buffer_destroy(ctx, &screenshot_buffer, &screenshot_buffer_memory);

  screenshot_buffer_capacity = 0;
  screenshot_pending = false;
}

EGNAPI b8 renderer_screenshot(const char *path)
{
  return vkrenderer_screenshot_request(path);
}
