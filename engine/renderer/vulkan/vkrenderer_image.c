#include "vkrenderer_image.h"

void vkrenderer_image_create(
    VkContext *ctx,
    VkImageType image_type,
    u32 width,
    u32 height,
    VkFormat format,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags memory_flags,
    b32 create_view,
    VkImageAspectFlags view_aspect_flags,
    VkRendererImage *out_image)
{
  // Copy params
  out_image->width = width;
  out_image->height = height;

  // Creation info.
  VkImageCreateInfo image_create_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  image_create_info.imageType = VK_IMAGE_TYPE_2D;
  image_create_info.extent.width = width;
  image_create_info.extent.height = height;
  image_create_info.extent.depth = 1; // TODO: Support configurable depth.
  image_create_info.mipLevels = 4;    // TODO: Support mip mapping
  image_create_info.arrayLayers = 1;  // TODO: Support number of layers in the image.
  image_create_info.format = format;
  image_create_info.tiling = tiling;
  image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  image_create_info.usage = usage;
  image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;         // TODO: Configurable sample count.
  image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // TODO: Configurable sharing mode.
  VKCHECK(vkCreateImage(ctx->device.handle, &image_create_info, ctx->allocator, &out_image->handle));

  // Query memory requirements.
  VkMemoryRequirements memory_requirements;
  vkGetImageMemoryRequirements(ctx->device.handle, out_image->handle, &memory_requirements);

  u32 memory_type;
  ctx->find_memory_index(memory_requirements.memoryTypeBits, memory_flags, &memory_type);

  // Allocate memory
  VkMemoryAllocateInfo memory_allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  memory_allocate_info.allocationSize = memory_requirements.size;
  memory_allocate_info.memoryTypeIndex = memory_type;
  VKCHECK(vkAllocateMemory(ctx->device.handle, &memory_allocate_info, ctx->allocator, &out_image->memory));

  // Bind the memory
  VKCHECK(vkBindImageMemory(ctx->device.handle, out_image->handle, out_image->memory, 0)); // TODO: configurable memory offset.

  // Create view
  if (create_view)
  {
    out_image->view = 0;
    vkrenderer_image_create_view(ctx, format, out_image, view_aspect_flags);
  }
}

void vkrenderer_image_create_view(
    VkContext *ctx,
    VkFormat format,
    VkRendererImage *image,
    VkImageAspectFlags aspect_flags)
{
  VkImageViewCreateInfo view_create_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  view_create_info.image = image->handle;
  view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D; // TODO: Make configurable.
  view_create_info.format = format;
  view_create_info.subresourceRange.aspectMask = aspect_flags;
  // TODO: Make configurable
  view_create_info.subresourceRange.baseMipLevel = 0;
  view_create_info.subresourceRange.levelCount = 1;
  view_create_info.subresourceRange.baseArrayLayer = 0;
  view_create_info.subresourceRange.layerCount = 1;
  VKCHECK(vkCreateImageView(ctx->device.handle, &view_create_info, ctx->allocator, &image->view));
}

void vkrenderer_destroy_image(VkContext *ctx, VkRendererImage *image)
{
  if (image->view)
  {
    vkDestroyImageView(ctx->device.handle, image->view, ctx->allocator);
    image->view = 0;
  }
  if (image->memory)
  {
    vkFreeMemory(ctx->device.handle, image->memory, ctx->allocator);
    image->memory = 0;
  }
  if (image->handle)
  {
    vkDestroyImage(ctx->device.handle, image->handle, ctx->allocator);
    image->handle = 0;
  }

  image->width = 0;
  image->height = 0;
}
