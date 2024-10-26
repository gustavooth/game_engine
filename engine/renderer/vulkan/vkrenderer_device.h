#pragma once

#include "defines.h"
#include "vkrenderer_types.h"

b8 vkrenderer_create_device(VKContext *ctx);

void vkrenderer_destroy_device(VKContext *ctx);
