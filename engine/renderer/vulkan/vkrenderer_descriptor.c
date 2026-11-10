#include "vkrenderer_descriptor.h"
#include "core/logger.h"

b8 vkrenderer_descriptors_create(VkContext *ctx)
{
  VkDescriptorSetLayoutBinding binding = {0};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layout_info.bindingCount = 1;
  layout_info.pBindings = &binding;

  VKCHECK(vkCreateDescriptorSetLayout(ctx->device.handle, &layout_info, ctx->allocator, &ctx->descriptor_set_layout))

  VkDescriptorPoolSize pool_size = {0};
  pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  pool_size.descriptorCount = VKRENDERER_MAX_TEXTURES + 1;

  VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = VKRENDERER_MAX_TEXTURES + 1;
  pool_info.poolSizeCount = 1;
  pool_info.pPoolSizes = &pool_size;

  VKCHECK(vkCreateDescriptorPool(ctx->device.handle, &pool_info, ctx->allocator, &ctx->descriptor_pool))

  return true;
}

void vkrenderer_descriptors_destroy(VkContext *ctx)
{
  if (ctx->descriptor_pool)
  {
    vkDestroyDescriptorPool(ctx->device.handle, ctx->descriptor_pool, ctx->allocator);
    ctx->descriptor_pool = 0;
  }

  if (ctx->descriptor_set_layout)
  {
    vkDestroyDescriptorSetLayout(ctx->device.handle, ctx->descriptor_set_layout, ctx->allocator);
    ctx->descriptor_set_layout = 0;
  }
}

b8 vkrenderer_descriptor_allocate(VkContext *ctx, VkDescriptorSet *out_set)
{
  VkDescriptorSetAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocate_info.descriptorPool = ctx->descriptor_pool;
  allocate_info.descriptorSetCount = 1;
  allocate_info.pSetLayouts = &ctx->descriptor_set_layout;

  VkResult result = vkAllocateDescriptorSets(ctx->device.handle, &allocate_info, out_set);
  if (result != VK_SUCCESS)
  {
    EGNERROR("VKRENDERER failed to allocate descriptor set");
    *out_set = 0;
    return false;
  }

  return true;
}

void vkrenderer_descriptor_update(VkContext *ctx, VkDescriptorSet set,
                                  VkImageView view, VkSampler sampler)
{
  VkDescriptorImageInfo image_info = {0};
  image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  image_info.imageView = view;
  image_info.sampler = sampler;

  VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = set;
  write.dstBinding = 0;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = &image_info;

  vkUpdateDescriptorSets(ctx->device.handle, 1, &write, 0, 0);
}

void vkrenderer_descriptor_free(VkContext *ctx, VkDescriptorSet set)
{
  if (ctx->descriptor_pool && set)
    vkFreeDescriptorSets(ctx->device.handle, ctx->descriptor_pool, 1, &set);
}
