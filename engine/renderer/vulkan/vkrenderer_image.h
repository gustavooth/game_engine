#pragma once
#include "vkrenderer_types.h"

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
    VkRendererImage *out_image);

void vkrenderer_image_create_view(
    VkContext *ctx,
    VkFormat format,
    VkRendererImage *image,
    VkImageAspectFlags aspect_flags);

void vkrenderer_destroy_image(VkContext *ctx, VkRendererImage *image);