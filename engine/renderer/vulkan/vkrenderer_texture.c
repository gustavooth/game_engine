#include "vkrenderer_texture.h"
#include "text/font.h"
#include "vkrenderer_buffer.h"
#include "vkrenderer_command_buffer.h"
#include "vkrenderer_descriptor.h"
#include "core/logger.h"
#include "core/memo.h"
#include "platform/filemanager.h"

#define STB_IMAGE_IMPLEMENTATION
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wcast-qual"
#pragma clang diagnostic ignored "-Wcomma"
#include "stb_image.h"
#pragma clang diagnostic pop

static void texture_transition_layout(VkContext *ctx, VkCommandBuffer command_buffer,
                                      VkImage image, VkImageLayout old_layout,
                                      VkImageLayout new_layout)
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

  VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

  if (new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
  {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  }
  else if (new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
  {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  }

  vkCmdPipelineBarrier(command_buffer, src_stage, dst_stage, 0, 0, 0,
                       0, 0, 1, &barrier);
}

static void texture_generate_mipmaps(VkCommandBuffer command_buffer, VkImage image,
                                     u32 width, u32 height, u32 mip_levels)
{
  if (mip_levels <= 1)
  {
    VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, 0, 0, 0, 1,
                         &barrier);
    return;
  }

  // Level 0 is already in TRANSFER_DST. Move it to TRANSFER_SRC for the first
  // blit; subsequent levels are generated in a chain.
  VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &barrier);

  u32 mip_width = width;
  u32 mip_height = height;
  for (u32 level = 1; level < mip_levels; level++)
  {
    u32 next_width = mip_width > 1 ? mip_width >> 1 : 1;
    u32 next_height = mip_height > 1 ? mip_height >> 1 : 1;

    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.subresourceRange.baseMipLevel = level;
    barrier.subresourceRange.levelCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &barrier);

    VkImageBlit blit = {0};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = level - 1;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[1].x = (i32)mip_width;
    blit.srcOffsets[1].y = (i32)mip_height;
    blit.srcOffsets[1].z = 1;
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.mipLevel = level;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[1].x = (i32)next_width;
    blit.dstOffsets[1].y = (i32)next_height;
    blit.dstOffsets[1].z = 1;
    vkCmdBlitImage(command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   VK_FILTER_LINEAR);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.subresourceRange.baseMipLevel = level;
    barrier.subresourceRange.levelCount = 1;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &barrier);

    mip_width = next_width;
    mip_height = next_height;
  }

  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = mip_levels;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, 0, 0, 0, 1,
                       &barrier);
}

b8 vkrenderer_texture_create(VkContext *ctx, const u8 *pixels, u32 width,
                             u32 height, VkRendererTexture *out_texture)
{
  if (!ctx || !pixels || width == 0 || height == 0 || !out_texture)
    return false;

  MEMOZERO(out_texture, sizeof(VkRendererTexture));
  out_texture->width = width;
  out_texture->height = height;

  u32 mip_levels = 1;
  for (u32 size = width > height ? width : height; size > 1; size >>= 1)
    mip_levels++;

  VkImageCreateInfo image_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.extent.width = width;
  image_info.extent.height = height;
  image_info.extent.depth = 1;
  image_info.mipLevels = mip_levels;
  image_info.arrayLayers = 1;
  image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_SAMPLED_BIT;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(ctx->device.handle, &image_info, ctx->allocator, &out_texture->image) != VK_SUCCESS)
  {
    EGNERROR("VKRENDERER failed to create texture image");
    return false;
  }

  VkMemoryRequirements requirements;
  vkGetImageMemoryRequirements(ctx->device.handle, out_texture->image, &requirements);

  u32 memory_type = 0;
  if (!ctx->find_memory_index(requirements.memoryTypeBits,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &memory_type))
  {
    vkrenderer_texture_destroy(ctx, out_texture);
    return false;
  }

  VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocate_info.allocationSize = requirements.size;
  allocate_info.memoryTypeIndex = memory_type;

  if (vkAllocateMemory(ctx->device.handle, &allocate_info, ctx->allocator, &out_texture->memory) != VK_SUCCESS)
  {
    EGNERROR("VKRENDERER failed to allocate texture memory");
    vkrenderer_texture_destroy(ctx, out_texture);
    return false;
  }

  if (vkBindImageMemory(ctx->device.handle, out_texture->image, out_texture->memory, 0) != VK_SUCCESS)
  {
    EGNERROR("VKRENDERER failed to bind texture memory");
    vkrenderer_texture_destroy(ctx, out_texture);
    return false;
  }

  u64 image_size = (u64)width * (u64)height * 4;

  VkBuffer staging_buffer = 0;
  VkDeviceMemory staging_memory = 0;
  if (!vkrenderer_buffer_create(ctx, image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                &staging_buffer, &staging_memory))
  {
    vkrenderer_texture_destroy(ctx, out_texture);
    return false;
  }

  if (!vkrenderer_buffer_upload(ctx, staging_buffer, staging_memory, pixels, image_size, 0))
  {
    vkrenderer_buffer_destroy(ctx, &staging_buffer, &staging_memory);
    vkrenderer_texture_destroy(ctx, out_texture);
    return false;
  }

  VkRendererCommandBuffer command_buffer = {0};
  vkrenderer_command_buffer_allocate_and_begin_single_use(ctx, ctx->device.graphics_command_pool, &command_buffer);

  texture_transition_layout(ctx, command_buffer.handle, out_texture->image,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  VkBufferImageCopy copy_region = {0};
  copy_region.bufferOffset = 0;
  copy_region.bufferRowLength = 0;
  copy_region.bufferImageHeight = 0;
  copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copy_region.imageSubresource.mipLevel = 0;
  copy_region.imageSubresource.baseArrayLayer = 0;
  copy_region.imageSubresource.layerCount = 1;
  copy_region.imageOffset.x = 0;
  copy_region.imageOffset.y = 0;
  copy_region.imageOffset.z = 0;
  copy_region.imageExtent.width = width;
  copy_region.imageExtent.height = height;
  copy_region.imageExtent.depth = 1;

  vkCmdCopyBufferToImage(command_buffer.handle, staging_buffer, out_texture->image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

  texture_generate_mipmaps(command_buffer.handle, out_texture->image, width, height,
                           mip_levels);

  vkrenderer_command_buffer_end_single_use(ctx, ctx->device.graphics_command_pool, &command_buffer, ctx->device.graphics);

  vkrenderer_buffer_destroy(ctx, &staging_buffer, &staging_memory);

  VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  view_info.image = out_texture->image;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view_info.subresourceRange.baseMipLevel = 0;
  view_info.subresourceRange.levelCount = mip_levels;
  view_info.subresourceRange.baseArrayLayer = 0;
  view_info.subresourceRange.layerCount = 1;

  if (vkCreateImageView(ctx->device.handle, &view_info, ctx->allocator, &out_texture->view) != VK_SUCCESS)
  {
    EGNERROR("VKRENDERER failed to create texture view");
    vkrenderer_texture_destroy(ctx, out_texture);
    return false;
  }

  VkSamplerCreateInfo sampler_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sampler_info.magFilter = VK_FILTER_LINEAR;
  sampler_info.minFilter = VK_FILTER_LINEAR;
  sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.compareOp = VK_COMPARE_OP_NEVER;
  sampler_info.minLod = 0.0f;
  sampler_info.maxLod = (f32)(mip_levels > 0 ? mip_levels - 1 : 0);
  sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

  if (ctx->device.features.samplerAnisotropy)
  {
    sampler_info.anisotropyEnable = VK_TRUE;
    sampler_info.maxAnisotropy = ctx->device.properties.limits.maxSamplerAnisotropy;
    if (sampler_info.maxAnisotropy > 8.0f)
      sampler_info.maxAnisotropy = 8.0f;
  }

  if (vkCreateSampler(ctx->device.handle, &sampler_info, ctx->allocator, &out_texture->sampler) != VK_SUCCESS)
  {
    EGNERROR("VKRENDERER failed to create texture sampler");
    vkrenderer_texture_destroy(ctx, out_texture);
    return false;
  }

  if (!vkrenderer_descriptor_allocate(ctx, &out_texture->descriptor_set))
  {
    vkrenderer_texture_destroy(ctx, out_texture);
    return false;
  }

  vkrenderer_descriptor_update(ctx, out_texture->descriptor_set,
                               out_texture->view, out_texture->sampler);

  out_texture->mip_levels = mip_levels;
  out_texture->used = true;
  return true;
}

b8 vkrenderer_texture_create_solid(VkContext *ctx, u8 r, u8 g, u8 b, u8 a,
                                   VkRendererTexture *out_texture)
{
  u8 pixel[4] = {r, g, b, a};
  return vkrenderer_texture_create(ctx, pixel, 1, 1, out_texture);
}

void vkrenderer_texture_destroy(VkContext *ctx, VkRendererTexture *texture)
{
  if (!ctx || !texture)
    return;

  if (ctx->device.handle)
  {
    if (texture->descriptor_set)
      vkrenderer_descriptor_free(ctx, texture->descriptor_set);

    if (texture->sampler)
      vkDestroySampler(ctx->device.handle, texture->sampler, ctx->allocator);

    if (texture->view)
      vkDestroyImageView(ctx->device.handle, texture->view, ctx->allocator);

    if (texture->memory)
      vkFreeMemory(ctx->device.handle, texture->memory, ctx->allocator);

    if (texture->image)
      vkDestroyImage(ctx->device.handle, texture->image, ctx->allocator);
  }

  MEMOZERO(texture, sizeof(VkRendererTexture));
}

// Public API ----------------------------------------------------------------

static u32 texture_register_rgba(VkContext *ctx, const u8 *pixels, u32 width,
                                 u32 height, const char *label)
{
  if (!ctx || !pixels || width == 0 || height == 0)
    return 0;

  u32 slot = VKRENDERER_MAX_TEXTURES;
  for (u32 i = 0; i < VKRENDERER_MAX_TEXTURES; i++)
  {
    if (!ctx->textures[i].used)
    {
      slot = i;
      break;
    }
  }

  if (slot == VKRENDERER_MAX_TEXTURES)
  {
    EGNERROR("VKRENDERER texture table is full");
    return 0;
  }

  if (!vkrenderer_texture_create(ctx, pixels, width, height, &ctx->textures[slot]))
    return 0;

  if (label)
    EGNINFO("VKRENDERER loaded texture '%s' (%ux%u)", label, width, height);

  return slot + 1;
}

u32 vkrenderer_texture_register_from_memory(VkContext *ctx, const u8 *data, u64 size)
{
  if (!ctx || !data || size == 0)
    return 0;

  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc *pixels = stbi_load_from_memory(data, (int)size, &width, &height, &channels, 4);
  if (!pixels)
  {
    EGNERROR("VKRENDERER failed to decode embedded texture");
    return 0;
  }

  u32 handle = texture_register_rgba(ctx, pixels, (u32)width, (u32)height, "<embedded>");
  stbi_image_free(pixels);
  return handle;
}

u32 renderer_texture_load(const char *path)
{
  if (!path)
    return 0;

  VkContext *ctx = vkrenderer_get_context();
  if (!ctx || !ctx->device.handle)
  {
    EGNERROR("renderer_texture_load: renderer is not initialized");
    return 0;
  }

  u8 *file_data = 0;
  u64 file_size = 0;
  if (!filemanager_read_all(path, &file_data, &file_size))
  {
    EGNERROR("renderer_texture_load: failed to read %s", path);
    return 0;
  }

  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc *pixels = stbi_load_from_memory(file_data, (int)file_size,
                                          &width, &height, &channels, 4);
  MEMOFREE(file_data);

  if (!pixels)
  {
    EGNERROR("renderer_texture_load: failed to decode %s", path);
    return 0;
  }

  u32 handle = texture_register_rgba(ctx, pixels, (u32)width, (u32)height, path);
  stbi_image_free(pixels);
  return handle;
}

void renderer_texture_release(u32 texture)
{
  if (texture == 0 || texture > VKRENDERER_MAX_TEXTURES)
    return;

  VkContext *ctx = vkrenderer_get_context();
  if (!ctx || !ctx->device.handle)
    return;

  VkRendererTexture *slot = &ctx->textures[texture - 1];
  if (!slot->used)
    return;

  // Textures are usually released during load/unload, so it is safe to wait
  // for in-flight frames before destroying the underlying Vulkan objects.
  vkDeviceWaitIdle(ctx->device.handle);
  vkrenderer_texture_destroy(ctx, slot);
}

u32 renderer_text_texture_load(const char *font_path, const EgnString *text, f32 pixel_height, u32 *width, u32 *height)
{
  if (!font_path || !text || !width || !height) return 0;
  VkContext *ctx = vkrenderer_get_context();
  if (!ctx || !ctx->device.handle) return 0;
  u8 *font = 0; u64 size = 0;
  if (!filemanager_read_all(font_path, &font, &size)) return 0;
  u8 *pixels = size <= 0xffffffffu ? egn_font_rasterize(font, (u32)size, text, pixel_height, width, height) : 0;
  MEMOFREE(font);
  if (!pixels) return 0;
  u32 texture = texture_register_rgba(ctx, pixels, *width, *height, 0);
  egn_font_pixels_free(pixels);
  return texture;
}

EGNAPI b8 renderer_texture_set_repeat(u32 texture, b8 repeat)
{
  VkContext *ctx = vkrenderer_get_context();
  if (!ctx || !ctx->device.handle || texture == 0 ||
      texture > VKRENDERER_MAX_TEXTURES)
    return false;

  VkRendererTexture *slot = &ctx->textures[texture - 1];
  if (!slot->used)
    return false;

  VkSamplerCreateInfo sampler_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sampler_info.magFilter = VK_FILTER_LINEAR;
  sampler_info.minFilter = VK_FILTER_LINEAR;
  sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  VkSamplerAddressMode address_mode = repeat
                                          ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                                          : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeU = address_mode;
  sampler_info.addressModeV = address_mode;
  sampler_info.addressModeW = address_mode;
  sampler_info.compareOp = VK_COMPARE_OP_NEVER;
  sampler_info.minLod = 0.0f;
  sampler_info.maxLod = slot->mip_levels > 0 ? (f32)(slot->mip_levels - 1) : 0.0f;
  sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

  if (ctx->device.features.samplerAnisotropy)
  {
    sampler_info.anisotropyEnable = VK_TRUE;
    sampler_info.maxAnisotropy = ctx->device.properties.limits.maxSamplerAnisotropy;
    if (sampler_info.maxAnisotropy > 8.0f)
      sampler_info.maxAnisotropy = 8.0f;
  }

  VkSampler new_sampler = 0;
  if (vkCreateSampler(ctx->device.handle, &sampler_info, ctx->allocator,
                      &new_sampler) != VK_SUCCESS)
  {
    EGNERROR("VKRENDERER failed to recreate texture sampler");
    return false;
  }

  vkDeviceWaitIdle(ctx->device.handle);
  VkSampler old_sampler = slot->sampler;
  slot->sampler = new_sampler;
  vkrenderer_descriptor_update(ctx, slot->descriptor_set, slot->view, slot->sampler);
  if (old_sampler)
    vkDestroySampler(ctx->device.handle, old_sampler, ctx->allocator);
  return true;
}
