#pragma once

#include "defines.h"
#include "vkrenderer_types.h"

b8 vkrenderer_device_create(VkContext *ctx);

void vkrenderer_device_destroy(VkContext *ctx);

void vkrenderer_device_load_swapchain_support(VkContext *ctx);

b8 vkrenderer_device_detect_depth_format(VkRendererDevice *device);
