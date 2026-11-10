#include "vkrenderer_lut.h"
#include "vkrenderer_buffer.h"
#include "vkrenderer_command_buffer.h"
#include "platform/filemanager.h"
#include "core/logger.h"
#include "core/memo.h"

#include <stdio.h>
#include <string.h>

typedef struct VkRendererLutSlot
{
  VkImage image;
  VkDeviceMemory memory;
  VkImageView view;
  VkSampler sampler;
  b8 used;
} VkRendererLutSlot;

static VkRendererLutSlot lut_slots[VKRENDERER_MAX_LUTS];

static void destroy_slot(VkContext *ctx, VkRendererLutSlot *slot)
{
  if (slot->sampler)
    vkDestroySampler(ctx->device.handle, slot->sampler, ctx->allocator);
  if (slot->view)
    vkDestroyImageView(ctx->device.handle, slot->view, ctx->allocator);
  if (slot->memory)
    vkFreeMemory(ctx->device.handle, slot->memory, ctx->allocator);
  if (slot->image)
    vkDestroyImage(ctx->device.handle, slot->image, ctx->allocator);
  MEMOZERO(slot, sizeof(*slot));
}

static void lut_barrier(VkCommandBuffer command_buffer, VkImage image,
                        VkImageLayout old_layout, VkImageLayout new_layout,
                        VkAccessFlags src_access, VkAccessFlags dst_access,
                        VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage)
{
  VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = src_access;
  barrier.dstAccessMask = dst_access;
  vkCmdPipelineBarrier(command_buffer, src_stage, dst_stage, 0, 0, 0, 0, 0, 1, &barrier);
}

static b8 create_lut_image(VkContext *ctx, u32 size, const u8 *rgba, u32 slot_index)
{
  VkRendererLutSlot *slot = &lut_slots[slot_index];
  MEMOZERO(slot, sizeof(*slot));

  VkImageCreateInfo image_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  image_info.imageType = VK_IMAGE_TYPE_3D;
  image_info.extent.width = size;
  image_info.extent.height = size;
  image_info.extent.depth = size;
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateImage(ctx->device.handle, &image_info, ctx->allocator,
                    &slot->image) != VK_SUCCESS)
    return false;

  VkMemoryRequirements requirements;
  vkGetImageMemoryRequirements(ctx->device.handle, slot->image, &requirements);
  u32 memory_type = 0;
  if (!ctx->find_memory_index(requirements.memoryTypeBits,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &memory_type))
    return false;

  VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocate_info.allocationSize = requirements.size;
  allocate_info.memoryTypeIndex = memory_type;
  if (vkAllocateMemory(ctx->device.handle, &allocate_info, ctx->allocator,
                       &slot->memory) != VK_SUCCESS)
    return false;
  if (vkBindImageMemory(ctx->device.handle, slot->image, slot->memory, 0) != VK_SUCCESS)
    return false;

  u64 byte_count = (u64)size * size * size * 4;
  VkBuffer staging_buffer = 0;
  VkDeviceMemory staging_memory = 0;
  if (!vkrenderer_buffer_create(ctx, byte_count, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                &staging_buffer, &staging_memory))
    return false;
  if (!vkrenderer_buffer_upload(ctx, staging_buffer, staging_memory,
                                rgba, byte_count, 0))
  {
    vkrenderer_buffer_destroy(ctx, &staging_buffer, &staging_memory);
    return false;
  }

  VkRendererCommandBuffer command_buffer = {0};
  vkrenderer_command_buffer_allocate_and_begin_single_use(ctx, ctx->device.graphics_command_pool,
                                                         &command_buffer);
  lut_barrier(command_buffer.handle, slot->image, VK_IMAGE_LAYOUT_UNDEFINED,
              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
              VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT);

  VkBufferImageCopy region = {0};
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageExtent.width = size;
  region.imageExtent.height = size;
  region.imageExtent.depth = size;
  vkCmdCopyBufferToImage(command_buffer.handle, staging_buffer, slot->image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  lut_barrier(command_buffer.handle, slot->image,
              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
              VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  vkrenderer_command_buffer_end_single_use(ctx, ctx->device.graphics_command_pool,
                                           &command_buffer, ctx->device.graphics);
  vkrenderer_buffer_destroy(ctx, &staging_buffer, &staging_memory);

  VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  view_info.image = slot->image;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
  view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.layerCount = 1;
  if (vkCreateImageView(ctx->device.handle, &view_info, ctx->allocator,
                        &slot->view) != VK_SUCCESS)
    return false;

  VkSamplerCreateInfo sampler_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sampler_info.magFilter = VK_FILTER_LINEAR;
  sampler_info.minFilter = VK_FILTER_LINEAR;
  sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.maxLod = 0.0f;
  if (vkCreateSampler(ctx->device.handle, &sampler_info, ctx->allocator,
                      &slot->sampler) != VK_SUCCESS)
    return false;

  slot->used = true;
  return true;
}

static u32 allocate_slot(VkContext *ctx, u32 size, const u8 *rgba)
{
  for (u32 i = 0; i < VKRENDERER_MAX_LUTS; i++)
  {
    if (lut_slots[i].used)
      continue;
    if (!create_lut_image(ctx, size, rgba, i))
    {
      destroy_slot(ctx, &lut_slots[i]);
      return 0;
    }
    return VKRENDERER_LUT_HANDLE_BASE + i;
  }
  return 0;
}

u32 vkrenderer_lut_create_identity(VkContext *ctx)
{
  u8 pixels[2 * 2 * 2 * 4];
  u32 cursor = 0;
  for (u32 b = 0; b < 2; b++)
  {
    for (u32 g = 0; g < 2; g++)
    {
      for (u32 r = 0; r < 2; r++)
      {
        pixels[cursor++] = (u8)(r * 255);
        pixels[cursor++] = (u8)(g * 255);
        pixels[cursor++] = (u8)(b * 255);
        pixels[cursor++] = 255;
      }
    }
  }
  return allocate_slot(ctx, 2, pixels);
}

u32 vkrenderer_lut_load(VkContext *ctx, const char *path)
{
  if (!ctx || !path)
    return 0;

  u8 *file_data = 0;
  u64 length = 0;
  if (!filemanager_read_all(path, &file_data, &length) || length == 0)
  {
    EGNERROR("VKRENDERER could not read LUT '%s'", path);
    return 0;
  }

  char *text = MEMOALLOC(length + 1, MEMO_RENDERER);
  if (!text)
  {
    MEMOFREE(file_data);
    return 0;
  }
  MEMOCPY(text, file_data, length);
  text[length] = '\0';

  u32 size = 0;
  char *line_save = 0;
  char *line = strtok_r(text, "\r\n", &line_save);
  while (line)
  {
    if (strncmp(line, "LUT_3D_SIZE", 11) == 0)
    {
      u32 parsed = 0;
      if (sscanf(line, "LUT_3D_SIZE %u", &parsed) == 1)
        size = parsed;
    }
    line = strtok_r(0, "\r\n", &line_save);
  }

  MEMOFREE(text);

  if (size < 2 || size > 256)
  {
    EGNERROR("VKRENDERER LUT '%s' has an unsupported size", path);
    MEMOFREE(file_data);
    return 0;
  }

  u64 texel_count = (u64)size * size * size;
  u8 *pixels = MEMOALLOC(texel_count * 4, MEMO_RENDERER);
  if (!pixels)
  {
    MEMOFREE(file_data);
    return 0;
  }

  // The first pass replaced CR/LF with NULs; re-read from the original file
  // contents is not possible, so parse the values during the same walk by
  // reading the file again through a second buffer.
  u8 *file_data_2 = 0;
  u64 length_2 = 0;
  if (!filemanager_read_all(path, &file_data_2, &length_2))
  {
    MEMOFREE(pixels);
    MEMOFREE(file_data);
    return 0;
  }

  char *text_2 = MEMOALLOC(length_2 + 1, MEMO_RENDERER);
  if (!text_2)
  {
    MEMOFREE(pixels);
    MEMOFREE(file_data_2);
    MEMOFREE(file_data);
    return 0;
  }
  MEMOCPY(text_2, file_data_2, length_2);
  text_2[length_2] = '\0';

  u32 cursor = 0;
  char *save = 0;
  char *value_line = strtok_r(text_2, "\r\n", &save);
  while (value_line && cursor < texel_count * 4)
  {
    f32 r, g, b;
    if (sscanf(value_line, " %f %f %f", &r, &g, &b) == 3 &&
        (value_line[0] == '-' || value_line[0] == '.' || value_line[0] == '+' ||
         (value_line[0] >= '0' && value_line[0] <= '9')))
    {
      f32 clamp_r = r < 0.0f ? 0.0f : (r > 1.0f ? 1.0f : r);
      f32 clamp_g = g < 0.0f ? 0.0f : (g > 1.0f ? 1.0f : g);
      f32 clamp_b = b < 0.0f ? 0.0f : (b > 1.0f ? 1.0f : b);
      pixels[cursor++] = (u8)(clamp_r * 255.0f + 0.5f);
      pixels[cursor++] = (u8)(clamp_g * 255.0f + 0.5f);
      pixels[cursor++] = (u8)(clamp_b * 255.0f + 0.5f);
      pixels[cursor++] = 255;
    }
    value_line = strtok_r(0, "\r\n", &save);
  }

  u32 handle = 0;
  if (cursor == texel_count * 4)
    handle = allocate_slot(ctx, size, pixels);
  else
    EGNERROR("VKRENDERER LUT '%s' is incomplete", path);

  MEMOFREE(text_2);
  MEMOFREE(file_data_2);
  MEMOFREE(pixels);
  MEMOFREE(file_data);
  return handle;
}

b8 vkrenderer_lut_get(VkContext *ctx, u32 handle, VkImageView *out_view,
                      VkSampler *out_sampler)
{
  if (!ctx || handle < VKRENDERER_LUT_HANDLE_BASE)
    return false;
  u32 index = handle - VKRENDERER_LUT_HANDLE_BASE;
  if (index >= VKRENDERER_MAX_LUTS || !lut_slots[index].used)
    return false;
  *out_view = lut_slots[index].view;
  *out_sampler = lut_slots[index].sampler;
  return true;
}

void vkrenderer_lut_destroy_all(VkContext *ctx)
{
  for (u32 i = 0; i < VKRENDERER_MAX_LUTS; i++)
  {
    if (lut_slots[i].used)
      destroy_slot(ctx, &lut_slots[i]);
  }
}
