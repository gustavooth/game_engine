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

// PBR material maps: metallic-roughness, normal, occlusion and emissive.
b8 vkrenderer_material_layout_create(VkContext *ctx);
void vkrenderer_material_layout_destroy(VkContext *ctx);
b8 vkrenderer_material_descriptor_create(VkContext *ctx,
                                         const VkRendererMaterial *material,
                                         VkDescriptorSet *out_set);
void vkrenderer_material_descriptor_free(VkContext *ctx, VkDescriptorSet set);
