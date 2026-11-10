#pragma once

#include "vkrenderer_types.h"

/**
 * Creates a sampled texture from RGBA8 pixels.
 *
 * @param pixels Tightly packed RGBA8 data, width * height * 4 bytes.
 */
b8 vkrenderer_texture_create(VkContext *ctx, const u8 *pixels, u32 width,
                             u32 height, VkRendererTexture *out_texture);

/**
 * Creates a 1x1 solid texture. Used as the "no texture" fallback so solid
 * colour draws can share the textured pipeline.
 */
b8 vkrenderer_texture_create_solid(VkContext *ctx, u8 r, u8 g, u8 b, u8 a,
                                   VkRendererTexture *out_texture);

void vkrenderer_texture_destroy(VkContext *ctx, VkRendererTexture *texture);

// Decodes an encoded image (PNG/JPEG/...) from memory and registers it in the
// renderer texture table. Returns a 1-based texture handle, or 0.
u32 vkrenderer_texture_register_from_memory(VkContext *ctx, const u8 *data, u64 size);
