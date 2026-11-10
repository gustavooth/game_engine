#pragma once

#include "vkrenderer_types.h"

b8 vkrenderer_descriptors_create(VkContext *ctx);
void vkrenderer_descriptors_destroy(VkContext *ctx);

// Allocates one descriptor set from the renderer pool.
b8 vkrenderer_descriptor_allocate(VkContext *ctx, VkDescriptorSet *out_set);

// Points a descriptor set at a combined image sampler.
void vkrenderer_descriptor_update(VkContext *ctx, VkDescriptorSet set,
                                  VkImageView view, VkSampler sampler);

// Releases a descriptor set back to the pool.
void vkrenderer_descriptor_free(VkContext *ctx, VkDescriptorSet set);
