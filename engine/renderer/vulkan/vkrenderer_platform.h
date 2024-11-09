#pragma once

#include "defines.h"
#include "vkrenderer_types.h"
#include "platform/window.h"

b8 vkrenderer_surface_create(VkContext *ctx, Window *win);
void vkrenderer_surface_destroy(VkContext *ctx);
