#pragma once

#include "defines.h"
#include "core/asserts.h"
#include <vulkan/vulkan.h>

#define VKCHECK(expr)              \
  {                                \
    EGNASSERT(expr == VK_SUCCESS); \
  }

typedef struct VkRendererDevice {
  VkPhysicalDevice physical;
  VkDevice handle;
}VkRendererDevice;

typedef struct VKContext {
  VkInstance instance;
  VkDebugUtilsMessengerEXT debug_messenger;

  VkSurfaceKHR surface;

  VkRendererDevice device;
  VkQueue graphics_queue;
  VkQueue present_queue;
  VkQueue transfer_queue;
} VKContext;