#pragma once

#include "defines.h"
#include "core/asserts.h"
#include <vulkan/vulkan.h>

#define VKCHECK(expr)              \
  {                                \
    EGNASSERT(expr == VK_SUCCESS); \
  }

typedef struct VkRendererQueueIndexes
{
  u32 graphics_family_index; // queue family index
  u32 graphics_index;        // queue index in family

  u32 present_family_index;
  u32 present_index;

  u32 compute_family_index;
  u32 compute_index;

  u32 transfer_family_index;
  u32 transfer_index;

  // I need to know the index of queue families.
  // I need to know how many queues will be needed in each queue family.
  // It will be useful in creating the logical device.
  // Will be destroyed after creation.
  u32 *queue_family_indexes; // EGNARRAY
  u32 *queue_count;          // EGNARRAY
} VkRendererQueueIndexes;

typedef struct VkRendererSwapchainSupport
{
  VkSurfaceCapabilitiesKHR capabilities;
  u32 format_count;
  VkSurfaceFormatKHR *formats;
  u32 present_mode_count;
  VkPresentModeKHR *present_modes;
} VkRendererSwapchainSupport;

typedef struct VkRendererDevice
{
  VkPhysicalDevice physical;
  VkDevice handle;

  VkQueue graphics;
  VkQueue present;
  VkQueue transfer;

  VkCommandPool graphics_command_pool;

  VkFormat depth_format;
  VkRendererQueueIndexes queue_indexes;
  VkPhysicalDeviceProperties properties;
  VkPhysicalDeviceFeatures features;
  VkPhysicalDeviceMemoryProperties memory;
  VkRendererSwapchainSupport swapchain_support;
} VkRendererDevice;

typedef struct VkRendererImage
{
  VkImage handle;
  VkDeviceMemory memory;
  VkImageView view;
  u32 width;
  u32 height;
} VkRendererImage;

typedef struct VkRendererSwapchain
{
  VkSwapchainKHR handle;

  u32 image_count;
  VkImage *images;
  VkImageView *views;

  VkSurfaceFormatKHR image_format;
  u32 max_frame_in_flight;
  VkRendererImage depth_attachment;

} VkRendererSwapchain;

typedef enum VkRendererState
{
  VKRENDERER_STATE_NOT_ALLOCATED,
  VKRENDERER_STATE_READY,
  VKRENDERER_STATE_RECORDING,
  VKRENDERER_STATE_IN_RENDERPASS,
  VKRENDERER_STATE_RECORDING_ENDED,
  VKRENDERER_STATE_SUBMITTED
} VkRendererState;

typedef struct VkRendererRenderpass
{
  VkRenderPass handle;

  f32 x, y, w, h;
  f32 r, g, b, a;
  f32 depth;
  u32 stencil;

  VkRendererState state;
} VkRendererRenderpass;

typedef struct VkRendererCommandBuffer
{
  VkCommandBuffer handle;

  VkRendererState state;
} VkRendererCommandBuffer;

typedef struct VkRendererFramebuffer
{
  VkFramebuffer handle;

  u32 attachment_count;
  VkImageView *attachments;
  VkRendererRenderpass *renderpass;
} VkRendererFramebuffer;

typedef struct VkRendererFence
{
  VkFence handle;
  b8 is_signaled;
} VkRendererFence;

typedef struct VkContext
{
  VkInstance instance;
  VkDebugUtilsMessengerEXT debug_messenger;
  VkAllocationCallbacks *allocator;

  VkSurfaceKHR surface;
  VkRendererDevice device;

  VkRendererSwapchain swapchain;
  u32 image_index;
  u32 current_frame;
  b8 recreating_swapchain;

  VkRendererRenderpass main_renderpass;

  u32 framebuffer_width;
  u32 framebuffer_height;
  u64 framebuffer_generation;

  // image count
  VkRendererCommandBuffer *graphics_command_buffers;
  VkRendererFramebuffer *framebuffers;

  // max frames in flight
  VkSemaphore *image_available_semaphores;
  VkSemaphore *queue_complete_semaphores;

  // max frames in flight
  VkRendererFence *in_flight_fences;
  // image count
  VkRendererFence **images_in_flight;

  b8 (*find_memory_index)(u32 type_filter, u32 property_flags, u32 *out_index);
} VkContext;