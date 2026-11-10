#include "vkrenderer_descriptor.h"
#include "core/logger.h"
#include "core/memo.h"

#define VKRENDERER_MAX_MATERIALS 4096

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

b8 vkrenderer_material_layout_create(VkContext *ctx)
{
  VkDescriptorSetLayoutBinding bindings[4];
  MEMOZERO(bindings, sizeof(bindings));
  for (u32 i = 0; i < 4; i++)
  {
    bindings[i].binding = i;
    bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[i].descriptorCount = 1;
    bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  }

  VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layout_info.bindingCount = 4;
  layout_info.pBindings = bindings;
  if (vkCreateDescriptorSetLayout(ctx->device.handle, &layout_info, ctx->allocator,
                                  &ctx->material_set_layout) != VK_SUCCESS)
    return false;

  VkDescriptorPoolSize pool_size = {0};
  pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  pool_size.descriptorCount = VKRENDERER_MAX_MATERIALS * 4;

  VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = VKRENDERER_MAX_MATERIALS;
  pool_info.poolSizeCount = 1;
  pool_info.pPoolSizes = &pool_size;
  if (vkCreateDescriptorPool(ctx->device.handle, &pool_info, ctx->allocator,
                             &ctx->material_descriptor_pool) != VK_SUCCESS)
    return false;

  return true;
}

void vkrenderer_material_layout_destroy(VkContext *ctx)
{
  if (ctx->material_descriptor_pool)
  {
    vkDestroyDescriptorPool(ctx->device.handle, ctx->material_descriptor_pool, ctx->allocator);
    ctx->material_descriptor_pool = 0;
  }
  if (ctx->material_set_layout)
  {
    vkDestroyDescriptorSetLayout(ctx->device.handle, ctx->material_set_layout, ctx->allocator);
    ctx->material_set_layout = 0;
  }
}

static void resolve_material_texture(VkContext *ctx, u32 handle,
                                     VkRendererTexture *fallback,
                                     VkImageView *out_view, VkSampler *out_sampler)
{
  if (handle > 0 && handle <= VKRENDERER_MAX_TEXTURES)
  {
    VkRendererTexture *slot = &ctx->textures[handle - 1];
    if (slot->used && slot->view && slot->sampler)
    {
      *out_view = slot->view;
      *out_sampler = slot->sampler;
      return;
    }
  }

  *out_view = fallback->view;
  *out_sampler = fallback->sampler;
}

b8 vkrenderer_material_descriptor_create(VkContext *ctx,
                                         const VkRendererMaterial *material,
                                         VkDescriptorSet *out_set)
{
  if (!ctx || !material || !out_set || !ctx->material_set_layout)
    return false;

  VkDescriptorSetAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocate_info.descriptorPool = ctx->material_descriptor_pool;
  allocate_info.descriptorSetCount = 1;
  allocate_info.pSetLayouts = &ctx->material_set_layout;
  if (vkAllocateDescriptorSets(ctx->device.handle, &allocate_info, out_set) != VK_SUCCESS)
  {
    EGNERROR("VKRENDERER failed to allocate a material descriptor set");
    *out_set = 0;
    return false;
  }

  VkDescriptorImageInfo images[4];
  MEMOZERO(images, sizeof(images));
  resolve_material_texture(ctx, material->metallic_roughness_texture,
                           &ctx->white_texture, &images[0].imageView, &images[0].sampler);
  resolve_material_texture(ctx, material->normal_texture,
                           &ctx->flat_normal_texture, &images[1].imageView, &images[1].sampler);
  resolve_material_texture(ctx, material->occlusion_texture,
                           &ctx->white_texture, &images[2].imageView, &images[2].sampler);
  resolve_material_texture(ctx, material->emissive_texture,
                           &ctx->black_texture, &images[3].imageView, &images[3].sampler);

  VkWriteDescriptorSet writes[4];
  MEMOZERO(writes, sizeof(writes));
  for (u32 i = 0; i < 4; i++)
  {
    images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = *out_set;
    writes[i].dstBinding = i;
    writes[i].descriptorCount = 1;
    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[i].pImageInfo = &images[i];
  }

  vkUpdateDescriptorSets(ctx->device.handle, 4, writes, 0, 0);
  return true;
}

void vkrenderer_material_descriptor_free(VkContext *ctx, VkDescriptorSet set)
{
  if (ctx->material_descriptor_pool && set)
    vkFreeDescriptorSets(ctx->device.handle, ctx->material_descriptor_pool, 1, &set);
}
