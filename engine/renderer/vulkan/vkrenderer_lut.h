#pragma once

#include "vkrenderer_types.h"

#define VKRENDERER_MAX_LUTS 8
#define VKRENDERER_LUT_HANDLE_BASE 1000

// Loads a .cube 3D LUT. Returns a handle >= 1000, or 0 on failure.
u32 vkrenderer_lut_load(VkContext *ctx, const char *path);

// Creates a 2x2x2 identity LUT. Returns a handle, or 0 on failure.
u32 vkrenderer_lut_create_identity(VkContext *ctx);

b8 vkrenderer_lut_get(VkContext *ctx, u32 handle, VkImageView *out_view,
                      VkSampler *out_sampler);

void vkrenderer_lut_destroy_all(VkContext *ctx);
