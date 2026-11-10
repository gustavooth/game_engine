#include "vkrenderer_skin.h"
#include "vkrenderer_buffer.h"
#include "core/logger.h"
#include "core/memo.h"

static u32 bone_frame_index(VkContext *ctx)
{
  return ctx->current_frame % VKRENDERER_BONE_FRAMES;
}

b8 vkrenderer_skin_create(VkContext *ctx)
{
  VkDescriptorSetLayoutBinding binding = {0};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layout_info.bindingCount = 1;
  layout_info.pBindings = &binding;
  VKCHECK(vkCreateDescriptorSetLayout(ctx->device.handle, &layout_info, ctx->allocator, &ctx->bone_set_layout))

  VkDescriptorPoolSize pool_size = {0};
  pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  pool_size.descriptorCount = VKRENDERER_BONE_FRAMES;

  VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool_info.maxSets = VKRENDERER_BONE_FRAMES;
  pool_info.poolSizeCount = 1;
  pool_info.pPoolSizes = &pool_size;
  VKCHECK(vkCreateDescriptorPool(ctx->device.handle, &pool_info, ctx->allocator, &ctx->bone_descriptor_pool))

  u64 buffer_size = sizeof(mat4) * (u64)VKRENDERER_MAX_BONES;

  for (u32 frame = 0; frame < VKRENDERER_BONE_FRAMES; frame++)
  {
    if (!vkrenderer_buffer_create(ctx, buffer_size,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  &ctx->bone_buffers[frame],
                                  &ctx->bone_memories[frame]))
      return false;

    if (vkMapMemory(ctx->device.handle, ctx->bone_memories[frame], 0,
                    VK_WHOLE_SIZE, 0, &ctx->bone_mapped[frame]) != VK_SUCCESS)
    {
      EGNERROR("VKRENDERER failed to map bone buffer");
      return false;
    }

    VkDescriptorSetAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate_info.descriptorPool = ctx->bone_descriptor_pool;
    allocate_info.descriptorSetCount = 1;
    allocate_info.pSetLayouts = &ctx->bone_set_layout;
    VKCHECK(vkAllocateDescriptorSets(ctx->device.handle, &allocate_info,
                                     &ctx->bone_sets[frame]))

    VkDescriptorBufferInfo buffer_info = {0};
    buffer_info.buffer = ctx->bone_buffers[frame];
    buffer_info.offset = 0;
    buffer_info.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = ctx->bone_sets[frame];
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &buffer_info;
    vkUpdateDescriptorSets(ctx->device.handle, 1, &write, 0, 0);
  }

  return true;
}

void vkrenderer_skin_destroy(VkContext *ctx)
{
  for (u32 frame = 0; frame < VKRENDERER_BONE_FRAMES; frame++)
  {
    if (ctx->bone_mapped[frame])
    {
      vkUnmapMemory(ctx->device.handle, ctx->bone_memories[frame]);
      ctx->bone_mapped[frame] = 0;
    }
    vkrenderer_buffer_destroy(ctx, &ctx->bone_buffers[frame],
                              &ctx->bone_memories[frame]);
    ctx->bone_sets[frame] = 0;
  }

  if (ctx->bone_descriptor_pool)
  {
    vkDestroyDescriptorPool(ctx->device.handle, ctx->bone_descriptor_pool, ctx->allocator);
    ctx->bone_descriptor_pool = 0;
  }

  if (ctx->bone_set_layout)
  {
    vkDestroyDescriptorSetLayout(ctx->device.handle, ctx->bone_set_layout, ctx->allocator);
    ctx->bone_set_layout = 0;
  }

  ctx->bone_cursor = 0;
}

void vkrenderer_skin_begin_frame(VkContext *ctx)
{
  u32 frame = bone_frame_index(ctx);
  mat4 identity = mat4_identity();
  MEMOCPY(ctx->bone_mapped[frame], &identity, sizeof(mat4));
  ctx->bone_cursor = 1;
}

u32 vkrenderer_skin_allocate(VkContext *ctx, u32 bone_count)
{
  if (bone_count == 0)
    return 0;

  if (ctx->bone_cursor + bone_count > VKRENDERER_MAX_BONES)
  {
    EGNWARN("VKRENDERER bone buffer is full (%u bones requested, %u available)",
            bone_count, VKRENDERER_MAX_BONES - ctx->bone_cursor);
    return 0;
  }

  u32 offset = ctx->bone_cursor;
  ctx->bone_cursor += bone_count;
  return offset;
}

void vkrenderer_skin_write(VkContext *ctx, u32 offset, const mat4 *bones, u32 count)
{
  if (!bones || count == 0 || offset + count > VKRENDERER_MAX_BONES)
    return;

  u32 frame = bone_frame_index(ctx);
  MEMOCPY((u8 *)ctx->bone_mapped[frame] + (u64)offset * sizeof(mat4),
          bones, (u64)count * sizeof(mat4));
}

VkDescriptorSet vkrenderer_skin_descriptor(VkContext *ctx)
{
  return ctx->bone_sets[bone_frame_index(ctx)];
}

VkDescriptorSetLayout vkrenderer_skin_set_layout(VkContext *ctx)
{
  return ctx->bone_set_layout;
}

void vkrenderer_skin_barrier(VkContext *ctx, VkRendererCommandBuffer *command_buffer)
{
  (void)ctx;

  VkMemoryBarrier barrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier(command_buffer->handle,
                       VK_PIPELINE_STAGE_HOST_BIT,
                       VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       0, 1, &barrier, 0, 0, 0, 0);
}
