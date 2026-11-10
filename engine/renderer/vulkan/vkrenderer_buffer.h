#pragma once

#include "vkrenderer_types.h"

/**
 * Creates a Vulkan buffer and its backing memory.
 *
 * The caller is responsible for mapping the memory when @p properties contains
 * VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT.
 */
b8 vkrenderer_buffer_create(VkContext *ctx, u64 size, VkBufferUsageFlags usage,
                            VkMemoryPropertyFlags properties,
                            VkBuffer *out_buffer, VkDeviceMemory *out_memory);

void vkrenderer_buffer_destroy(VkContext *ctx, VkBuffer *buffer,
                               VkDeviceMemory *memory);

/**
 * Copies data into a host-visible, host-coherent buffer (map/copy/unmap).
 */
b8 vkrenderer_buffer_upload(VkContext *ctx, VkBuffer buffer,
                            VkDeviceMemory memory, const void *data, u64 size,
                            u64 offset);

/**
 * Creates a device-local buffer and fills it through a temporary staging
 * buffer. Suitable for static vertex and index data.
 */
b8 vkrenderer_buffer_create_device_local(VkContext *ctx, const void *data,
                                         u64 size, VkBufferUsageFlags usage,
                                         VkBuffer *out_buffer,
                                         VkDeviceMemory *out_memory);
