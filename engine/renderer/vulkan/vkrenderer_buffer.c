#include "vkrenderer_buffer.h"
#include "vkrenderer_command_buffer.h"
#include "core/logger.h"
#include "core/memo.h"

#include <string.h>

b8 vkrenderer_buffer_create(VkContext *ctx, u64 size, VkBufferUsageFlags usage,
                            VkMemoryPropertyFlags properties,
                            VkBuffer *out_buffer, VkDeviceMemory *out_memory)
{
  if (!ctx || !out_buffer || !out_memory || size == 0)
    return false;

  *out_buffer = 0;
  *out_memory = 0;

  VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  buffer_info.size = size;
  buffer_info.usage = usage;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VKCHECK(vkCreateBuffer(ctx->device.handle, &buffer_info, ctx->allocator, out_buffer))

  VkMemoryRequirements requirements;
  vkGetBufferMemoryRequirements(ctx->device.handle, *out_buffer, &requirements);

  u32 memory_type = 0;
  if (!ctx->find_memory_index(requirements.memoryTypeBits, properties, &memory_type))
  {
    vkDestroyBuffer(ctx->device.handle, *out_buffer, ctx->allocator);
    *out_buffer = 0;
    return false;
  }

  VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocate_info.allocationSize = requirements.size;
  allocate_info.memoryTypeIndex = memory_type;

  if (vkAllocateMemory(ctx->device.handle, &allocate_info, ctx->allocator, out_memory) != VK_SUCCESS)
  {
    EGNERROR("VKRENDERER failed to allocate buffer memory");
    vkDestroyBuffer(ctx->device.handle, *out_buffer, ctx->allocator);
    *out_buffer = 0;
    return false;
  }

  if (vkBindBufferMemory(ctx->device.handle, *out_buffer, *out_memory, 0) != VK_SUCCESS)
  {
    EGNERROR("VKRENDERER failed to bind buffer memory");
    vkFreeMemory(ctx->device.handle, *out_memory, ctx->allocator);
    vkDestroyBuffer(ctx->device.handle, *out_buffer, ctx->allocator);
    *out_memory = 0;
    *out_buffer = 0;
    return false;
  }

  return true;
}

void vkrenderer_buffer_destroy(VkContext *ctx, VkBuffer *buffer,
                               VkDeviceMemory *memory)
{
  if (!ctx)
    return;

  if (buffer && *buffer)
  {
    vkDestroyBuffer(ctx->device.handle, *buffer, ctx->allocator);
    *buffer = 0;
  }

  if (memory && *memory)
  {
    vkFreeMemory(ctx->device.handle, *memory, ctx->allocator);
    *memory = 0;
  }
}

b8 vkrenderer_buffer_upload(VkContext *ctx, VkBuffer buffer,
                            VkDeviceMemory memory, const void *data, u64 size,
                            u64 offset)
{
  if (!ctx || !buffer || !memory || !data || size == 0)
    return false;

  void *mapped = 0;
  if (vkMapMemory(ctx->device.handle, memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
  {
    EGNERROR("VKRENDERER failed to map buffer memory");
    return false;
  }

  MEMOCPY((u8 *)mapped + offset, data, size);
  vkUnmapMemory(ctx->device.handle, memory);

  return true;
}

b8 vkrenderer_buffer_create_device_local(VkContext *ctx, const void *data,
                                         u64 size, VkBufferUsageFlags usage,
                                         VkBuffer *out_buffer,
                                         VkDeviceMemory *out_memory)
{
  if (!ctx || !data || size == 0)
    return false;

  VkBuffer staging_buffer = 0;
  VkDeviceMemory staging_memory = 0;
  if (!vkrenderer_buffer_create(ctx, size,
                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                &staging_buffer, &staging_memory))
    return false;

  if (!vkrenderer_buffer_upload(ctx, staging_buffer, staging_memory, data, size, 0))
  {
    vkrenderer_buffer_destroy(ctx, &staging_buffer, &staging_memory);
    return false;
  }

  if (!vkrenderer_buffer_create(ctx, size,
                                usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                out_buffer, out_memory))
  {
    vkrenderer_buffer_destroy(ctx, &staging_buffer, &staging_memory);
    return false;
  }

  VkRendererCommandBuffer command_buffer = {0};
  vkrenderer_command_buffer_allocate_and_begin_single_use(ctx, ctx->device.graphics_command_pool, &command_buffer);

  VkBufferCopy copy_region = {0};
  copy_region.srcOffset = 0;
  copy_region.dstOffset = 0;
  copy_region.size = size;
  vkCmdCopyBuffer(command_buffer.handle, staging_buffer, *out_buffer, 1, &copy_region);

  vkrenderer_command_buffer_end_single_use(ctx, ctx->device.graphics_command_pool, &command_buffer, ctx->device.graphics);

  vkrenderer_buffer_destroy(ctx, &staging_buffer, &staging_memory);

  return true;
}
