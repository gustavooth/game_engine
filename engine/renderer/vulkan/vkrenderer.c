#include "renderer/renderer.h"
#include "vkrenderer_types.h"
#include "core/logger.h"
#include "core/memo.h"
#include "vkrenderer_device.h"
#include "vkrenderer_platform.h"
#include "platform/window.h"

static VKContext ctx;

b8 create_instance();
b8 setup_debug_messenger();

b8 renderer_initialize(Window *win)
{
  EGNDEBUG("VKRENDERER initializing...")

  if (!create_instance())
    return false;
  if (!setup_debug_messenger())
    return false;
  if (!vkrenderer_create_surface(&ctx, win))
    return false;
  if (!vkrenderer_create_device(&ctx))
    return false;

  EGNINFO("VKRENDERER Initialized!");

  return true;
}

void renderer_shutdown()
{
  vkDestroyInstance(ctx.instance, NULL);
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

  VKCHECK(vkCreateInstance(&instance_info, 0, &ctx.instance))

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

  VKCHECK(func(ctx.instance, &debug_info, 0, &ctx.debug_messenger))

  return true;
}
