#include "renderer/renderer.h"
#include "vkrenderer_types.h"
#include "core/logger.h"
#include "core/memo.h"
#include "vkrenderer_device.h"
#include "vkrenderer_platform.h"
#include "platform/window.h"
#include "vkrenderer_swapchain.h"
#include "vkrenderer_renderpass.h"
#include "vkrenderer_command_buffer.h"
#include "vkrenderer_framebuffer.h"

static VkContext ctx;

b8 create_instance();
b8 setup_debug_messenger();
b8 find_memory_index(u32 type_filter, u32 property_flags, u32 *out_index);

void create_command_buffers();
void destroy_command_buffers();
void create_framebuffers();
void destroy_framebuffers();

b8 renderer_initialize(Window *win, u32 width, u32 height)
{
  EGNDEBUG("VKRENDERER initializing...")

  ctx.allocator = NULL;
  ctx.find_memory_index = find_memory_index;
  ctx.framebuffer_width = width;
  ctx.framebuffer_height = height;

  if (!create_instance())
    return false;
  if (!setup_debug_messenger())
    return false;
  if (!vkrenderer_surface_create(&ctx, win))
    return false;
  if (!vkrenderer_device_create(&ctx))
    return false;
  if (!vkrenderer_swapchain_create(&ctx, width, height, &ctx.swapchain))
    return false;
  if (!vkrenderer_renderpass_create(&ctx, &ctx.main_renderpass, 0.0f, 0.0f, width, height, 0.0f, 0.0f, 0.1f, 1.0f, 1.0f, 0))
    return false;

  create_framebuffers();
  create_command_buffers();

  EGNINFO("VKRENDERER Initialized!");

  return true;
}

void renderer_shutdown()
{
  destroy_command_buffers();
  destroy_framebuffers();
  vkrenderer_swapchain_destroy(&ctx, &ctx.swapchain);
  vkrenderer_device_destroy(&ctx);
  vkrenderer_surface_destroy(&ctx);
  vkDestroyInstance(ctx.instance, ctx.allocator);
}

b8 create_instance()
{
  EGNDEBUG("VKRENDERER Creating instance...");

  VkApplicationInfo app_info = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app_info.pApplicationName = "EGN";
  app_info.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
  app_info.pEngineName = "EGN";
  app_info.engineVersion = VK_MAKE_VERSION(0, 0, 1);
  app_info.apiVersion = VK_API_VERSION_1_3;

  VkInstanceCreateInfo instance_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  instance_info.pApplicationInfo = &app_info;

  u32 instance_ext_count = 3;
  const char **instance_extensions = MEMOALLOC(sizeof(const char *) * instance_ext_count, MEMO_RENDERER);
  const char *surface_ext = VK_KHR_SURFACE_EXTENSION_NAME;
  const char *debug_ext = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
#ifdef EGNPLATFORM_WAYLAND
  const char *platform_ext = "VK_KHR_wayland_surface";
#elif EGNPLATFORM_WIN32
  const char *platform_ext = "VK_KHR_win32_surface";
#endif
  instance_extensions[0] = surface_ext;
  instance_extensions[1] = debug_ext;
  instance_extensions[2] = platform_ext;

  instance_info.enabledExtensionCount = instance_ext_count;
  instance_info.ppEnabledExtensionNames = instance_extensions;

  u32 instance_layers_count = 1;
  const char **instance_layers = MEMOALLOC(sizeof(const char *) * instance_layers_count, MEMO_RENDERER);
  const char *validation_layer = "VK_LAYER_KHRONOS_validation";
  instance_layers[0] = validation_layer;

  instance_info.enabledLayerCount = instance_layers_count;
  instance_info.ppEnabledLayerNames = instance_layers;

  VKCHECK(vkCreateInstance(&instance_info, ctx.allocator, &ctx.instance))

  MEMOFREE(instance_extensions);
  MEMOFREE(instance_layers);

  return true;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *pUserData)
{

  switch (messageSeverity)
  {
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
    EGNERROR(pCallbackData->pMessage);
    break;
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
    EGNWARN(pCallbackData->pMessage);
    break;
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
    EGNINFO(pCallbackData->pMessage);
    break;
  default:
    break;
  }

  return VK_FALSE;
}

b8 setup_debug_messenger()
{
  EGNDEBUG("VKRENDERER Creating debug messenger...");
  VkDebugUtilsMessengerCreateInfoEXT debug_info = {VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
  debug_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  debug_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  debug_info.pfnUserCallback = debug_callback;

  PFN_vkCreateDebugUtilsMessengerEXT func =
      (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(ctx.instance, "vkCreateDebugUtilsMessengerEXT");

  if (func == 0)
  {
    EGNFATAL("VKRENDERER failed to get debug messenger function!");
    return false;
  }

  VKCHECK(func(ctx.instance, &debug_info, ctx.allocator, &ctx.debug_messenger))

  return true;
}

b8 find_memory_index(u32 type_filter, u32 property_flags, u32 *out_index)
{
  VkPhysicalDeviceMemoryProperties memory_properties;
  vkGetPhysicalDeviceMemoryProperties(ctx.device.physical, &memory_properties);
  for (u32 i = 0; i < memory_properties.memoryTypeCount; ++i)
  {
    // Check each memory type to see if its bit is set to 1.
    if (type_filter & (1 << i) && (memory_properties.memoryTypes[i].propertyFlags & property_flags) == property_flags)
    {
      *out_index = i;
      return true;
    }
  }

  EGNWARN("VKRENDERER unable to find suitable memory type!");
  return false;
}

void create_command_buffers()
{
  EGNDEBUG("VKRENDERER Creating command buffers...")

  ctx.graphics_command_buffers = MEMOALLOC(sizeof(VkRendererCommandBuffer) * ctx.swapchain.image_count, MEMO_RENDERER);

  for (u32 i = 0; i < ctx.swapchain.image_count; i++)
  {
    vkrenderer_command_buffer_allocate(&ctx, ctx.device.graphics_command_pool, true, &ctx.graphics_command_buffers[i]);
  }
}

void destroy_command_buffers()
{
  if (!ctx.graphics_command_buffers)
    return;

  for (u32 i = 0; i < ctx.swapchain.image_count; i++)
  {
    vkrenderer_command_buffer_free(&ctx, ctx.device.graphics_command_pool, &ctx.graphics_command_buffers[i]);
  }

  MEMOFREE(ctx.graphics_command_buffers);
  ctx.graphics_command_buffers = 0;
}

void create_framebuffers()
{
  EGNDEBUG("VKRENDERER Creating framebuffers...")

  ctx.framebuffers = MEMOALLOC(sizeof(VkRendererFramebuffer) * ctx.swapchain.image_count, MEMO_RENDERER);

  for (u32 i = 0; i < ctx.swapchain.image_count; i++)
  {
    u32 attachment_count = 2;
    VkImageView attachments[] = {
        ctx.swapchain.views[i],
        ctx.swapchain.depth_attachment.view};

    vkrenderer_framebuffer_create(&ctx, &ctx.main_renderpass,
                                  ctx.framebuffer_width, ctx.framebuffer_height,
                                  attachment_count, attachments, &ctx.framebuffers[i]);
  }
}

void destroy_framebuffers()
{
  for (u32 i = 0; i < ctx.swapchain.image_count; i++)
  {
    vkrenderer_framebuffer_destroy(&ctx, &ctx.framebuffers[i]);
  }
  
  MEMOFREE(ctx.framebuffers);
}