#include "vkrenderer_device.h"
#include "core/logger.h"
#include "core/memo.h"
#include "containers/egnarray.h"
#include <string.h>

typedef struct PhysicalDeviceRequirements
{
  b8 graphics_queue;
  b8 present_queue;
  b8 compute_queue;
  b8 transfer_queue;

  u32 device_extensions_count;
  const char **device_extensions;

  b8 sampler_anisotropy;
} PhysicalDeviceRequirements;

b8 pick_physical_device(VkContext *ctx, VkPhysicalDevice *out_device, VkRendererQueueIndexes *out_queue_indexes);

b8 physical_device_meets_requirements(VkPhysicalDevice device, VkSurfaceKHR surface,
                                      PhysicalDeviceRequirements *requirements, VkRendererQueueIndexes *out_queue_indexes);

void destroy_queue_family_indexes(VkRendererQueueIndexes *indexes);

b8 vkrenderer_device_create(VkContext *ctx)
{
  VkRendererQueueIndexes indexes;
  EGNASSERT(pick_physical_device(ctx, &ctx->device.physical, &indexes) == true)

  EGNDEBUG("VKRENDERER creating logical device...");

  u32 queue_count = egnarray_len(indexes.queue_count);
  VkDeviceQueueCreateInfo *queue_info = MEMOZALLOC(sizeof(VkDeviceQueueCreateInfo) * queue_count, MEMO_RENDERER);

  f32 **priorities = MEMOALLOC(sizeof(f32 *) * queue_count, MEMO_RENDERER);

  for (u32 i = 0; i < queue_count; i++)
  {
    queue_info[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info[i].queueFamilyIndex = indexes.queue_family_indexes[i];
    queue_info[i].queueCount = indexes.queue_count[i];

    // create an array with the priority of each queue
    f32 *priorities2 = MEMOALLOC(sizeof(f32) * queue_info[i].queueCount, MEMO_RENDERER);
    MEMOSET(priorities2, (i32)1.0f, sizeof(f32) * queue_info[i].queueCount);

    queue_info[i].pQueuePriorities = priorities2;

    // saves array pointer to free memory later
    priorities[i] = priorities2;
  }

  VkPhysicalDeviceFeatures device_features = {0};
  device_features.samplerAnisotropy = VK_TRUE;

  const char *swapchain_ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

  VkDeviceCreateInfo device_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  device_info.queueCreateInfoCount = queue_count;
  device_info.pQueueCreateInfos = queue_info;
  device_info.pEnabledFeatures = &device_features;
  device_info.enabledExtensionCount = 1;
  device_info.ppEnabledExtensionNames = &swapchain_ext;

  VKCHECK(vkCreateDevice(ctx->device.physical, &device_info, ctx->allocator, &ctx->device.handle))

  vkGetDeviceQueue(ctx->device.handle, indexes.graphics_family_index, indexes.graphics_index, &ctx->device.graphics);
  vkGetDeviceQueue(ctx->device.handle, indexes.present_family_index, indexes.present_index, &ctx->device.present);
  vkGetDeviceQueue(ctx->device.handle, indexes.transfer_family_index, indexes.transfer_index, &ctx->device.transfer);

  ctx->device.queue_indexes = indexes;

  vkGetPhysicalDeviceProperties(ctx->device.physical, &ctx->device.properties);
  vkGetPhysicalDeviceFeatures(ctx->device.physical, &ctx->device.features);
  vkGetPhysicalDeviceMemoryProperties(ctx->device.physical, &ctx->device.memory);

  vkrenderer_device_load_swapchain_support(ctx);
  EGNASSERT(vkrenderer_device_detect_depth_format(&ctx->device) == true);

  EGNDEBUG("VKRENDERER Creating command pool...");

  VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool_info.queueFamilyIndex = indexes.graphics_family_index;
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

  VKCHECK(vkCreateCommandPool(ctx->device.handle, &pool_info, ctx->allocator, &ctx->device.graphics_command_pool));

  destroy_queue_family_indexes(&indexes);

  for (u32 i = 0; i < queue_count; i++)
    MEMOFREE(priorities[i]);
  MEMOFREE(priorities);

  return true;
}

void vkrenderer_device_destroy(VkContext *ctx)
{
  vkDestroyDevice(ctx->device.handle, ctx->allocator);
}

void vkrenderer_device_load_swapchain_support(VkContext *ctx)
{
  if (ctx->device.swapchain_support.formats)
    MEMOFREE(ctx->device.swapchain_support.formats);

  if (ctx->device.swapchain_support.present_modes)
    MEMOFREE(ctx->device.swapchain_support.present_modes);

  VKCHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->device.physical, ctx->surface, &ctx->device.swapchain_support.capabilities))

  VKCHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->device.physical, ctx->surface, &ctx->device.swapchain_support.format_count, NULL))
  ctx->device.swapchain_support.formats = MEMOALLOC(sizeof(VkSurfaceFormatKHR) * ctx->device.swapchain_support.format_count, MEMO_RENDERER);
  VKCHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->device.physical, ctx->surface, &ctx->device.swapchain_support.format_count, ctx->device.swapchain_support.formats))

  VKCHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->device.physical, ctx->surface, &ctx->device.swapchain_support.present_mode_count, NULL))
  ctx->device.swapchain_support.present_modes = MEMOALLOC(sizeof(VkPresentModeKHR) * ctx->device.swapchain_support.present_mode_count, MEMO_RENDERER);
  VKCHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->device.physical, ctx->surface, &ctx->device.swapchain_support.present_mode_count, ctx->device.swapchain_support.present_modes))
}

b8 vkrenderer_device_detect_depth_format(VkRendererDevice *device)
{
  const u64 candidate_count = 3;

  VkFormat candidates[3] = {
      VK_FORMAT_D32_SFLOAT,
      VK_FORMAT_D32_SFLOAT_S8_UINT,
      VK_FORMAT_D24_UNORM_S8_UINT};

  u32 flags = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
  VkFormatProperties properties;
  for (u64 i = 0; i < candidate_count; i++)
  {
    vkGetPhysicalDeviceFormatProperties(device->physical, candidates[i], &properties);

    if ((properties.linearTilingFeatures & flags) == flags)
    {
      device->depth_format = candidates[i];
      return true;
    }
    else if ((properties.optimalTilingFeatures & flags) == flags)
    {
      device->depth_format = candidates[i];
      return true;
    }
  }

  EGNERROR("VKRENDERER failed to detect depth format!");

  return false;
}

b8 physical_device_meets_requirements(VkPhysicalDevice device, VkSurfaceKHR surface,
                                      PhysicalDeviceRequirements *requirements, VkRendererQueueIndexes *out_queue_indexes)
{
  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(device, &properties);

  EGNDEBUG("VKRENDERER checking device %s...", properties.deviceName);

  u32 queue_family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, NULL);
  VkQueueFamilyProperties *queue_families = MEMOALLOC(sizeof(VkQueueFamilyProperties) * queue_family_count, MEMO_RENDERER);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families);

  u32 required_queues_count = 0;
  if (requirements->graphics_queue)
    required_queues_count++;
  if (requirements->transfer_queue)
    required_queues_count++;
  if (requirements->compute_queue)
    required_queues_count++;
  if (requirements->present_queue)
    required_queues_count++;

  MEMOSET(out_queue_indexes, -1, sizeof(VkRendererQueueIndexes));
  out_queue_indexes->queue_family_indexes = egnarray_reserve(u32, required_queues_count);
  out_queue_indexes->queue_count = egnarray_reserve(u32, required_queues_count);

  u32 queues_found = 0;

  for (u32 i = 0; i < queue_family_count; i++)
  {
    if (queues_found == required_queues_count)
      break;

    // i = family index
    // queue_index = queue index in family
    u32 queue_index = 0;

    u32 found = queues_found;

    if (requirements->graphics_queue &&
        out_queue_indexes->graphics_family_index == -1 &&
        queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
    {
      out_queue_indexes->graphics_family_index = i;
      out_queue_indexes->graphics_index = queue_index;
      queue_index++;
      queues_found++;
      if (queue_families[i].queueCount == queue_index)
      {
        egnarray_push(out_queue_indexes->queue_count, &queue_index);
        egnarray_push(out_queue_indexes->queue_family_indexes, &i);
        continue;
      };
    }

    if (requirements->transfer_queue &&
        out_queue_indexes->transfer_family_index == -1 &&
        queue_families[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
    {
      out_queue_indexes->transfer_family_index = i;
      out_queue_indexes->transfer_index = queue_index;
      queue_index++;
      queues_found++;
      if (queue_families[i].queueCount == queue_index)
      {
        egnarray_push(out_queue_indexes->queue_count, &queue_index);
        egnarray_push(out_queue_indexes->queue_family_indexes, &i);
        continue;
      };
    }

    if (requirements->compute_queue &&
        out_queue_indexes->compute_family_index == -1 &&
        queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
    {
      out_queue_indexes->compute_family_index = i;
      out_queue_indexes->compute_index = queue_index;
      queue_index++;
      queues_found++;
      if (queue_families[i].queueCount == queue_index)
      {
        egnarray_push(out_queue_indexes->queue_count, &queue_index);
        egnarray_push(out_queue_indexes->queue_family_indexes, &i);
        continue;
      };
    }

    if (requirements->present_queue &&
        out_queue_indexes->present_family_index == -1)
    {
      VkBool32 supports_present = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &supports_present);

      if (supports_present)
      {
        out_queue_indexes->present_family_index = i;
        out_queue_indexes->present_index = queue_index;
        queue_index++;
        queues_found++;
        if (queue_families[i].queueCount == queue_index)
        {
          egnarray_push(out_queue_indexes->queue_count, &queue_index);
          egnarray_push(out_queue_indexes->queue_family_indexes, &i);
          continue;
        };
      }
    }

    if (queues_found != found)
    {
      egnarray_push(out_queue_indexes->queue_count, &queue_index);
      egnarray_push(out_queue_indexes->queue_family_indexes, &i);
    }
  }

  EGNDEBUG("___Queue family index : Queue index _____");
  EGNDEBUG(" Graphics | Compute | Transfer | Present |");
  EGNDEBUG("   %i:%i    |   %i:%i   |   %i:%i    |   %i:%i   |", out_queue_indexes->graphics_family_index, out_queue_indexes->graphics_index, out_queue_indexes->compute_family_index, out_queue_indexes->compute_index, out_queue_indexes->transfer_family_index, out_queue_indexes->transfer_index, out_queue_indexes->present_family_index, out_queue_indexes->present_index);
  EGNDEBUG("_________________________________________");

  if (queues_found != required_queues_count)
  {
    EGNDEBUG("VKRENDERER queue family not found!")
    return false;
  }

  u32 ext_count = 0;
  VKCHECK(vkEnumerateDeviceExtensionProperties(device, NULL, &ext_count, NULL))
  VkExtensionProperties *ext_properties = MEMOALLOC(sizeof(VkExtensionProperties) * ext_count, MEMO_RENDERER);
  VKCHECK(vkEnumerateDeviceExtensionProperties(device, NULL, &ext_count, ext_properties))

  for (u32 i = 0; i < requirements->device_extensions_count; i++)
  {
    b8 found = false;
    for (u32 j = 0; j < ext_count; j++)
    {
      if (strcmp(requirements->device_extensions[i], (const char *)ext_properties[j].extensionName))
      {
        found = true;
        break;
      }
    }

    if (!found)
    {
      EGNDEBUG("VKRENDERER required device extension not found: %s", requirements->device_extensions[i]);
      return false;
    }
  }

  VkPhysicalDeviceFeatures features;
  vkGetPhysicalDeviceFeatures(device, &features);

  if (requirements->sampler_anisotropy && !features.samplerAnisotropy)
  {
    EGNDEBUG("VKRENDERER required device feature not found");
    return false;
  }

  EGNDEBUG("This device meets all the requirements.");
  return true;
}

b8 pick_physical_device(VkContext *ctx, VkPhysicalDevice *out_device, VkRendererQueueIndexes *out_queue_indexes)
{
  EGNDEBUG("VKRENDERER Choosing physical device...");

  u32 device_count;
  VKCHECK(vkEnumeratePhysicalDevices(ctx->instance, &device_count, NULL))
  VkPhysicalDevice *devices = MEMOALLOC(sizeof(VkPhysicalDevice) * device_count, MEMO_RENDERER);
  VKCHECK(vkEnumeratePhysicalDevices(ctx->instance, &device_count, devices))

  PhysicalDeviceRequirements requirements;
  requirements.graphics_queue = true;
  requirements.present_queue = true;
  requirements.transfer_queue = true;
  requirements.compute_queue = false;
  const char *ext_swapchain = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
  requirements.device_extensions = MEMOALLOC(sizeof(void *), MEMO_RENDERER);
  requirements.device_extensions[0] = ext_swapchain;
  requirements.device_extensions_count = 1;
  requirements.sampler_anisotropy = true;

  b8 found = false;
  for (u32 i = 0; i < device_count; i++)
  {
    if (!physical_device_meets_requirements(devices[i], ctx->surface, &requirements, out_queue_indexes))
      continue;

    found = true;

    *out_device = devices[i];

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(devices[i], &properties);
    EGNINFO("VKRENDERER device selected %s", properties.deviceName);
    break;
  }

  memo_free(requirements.device_extensions);

  if (!found)
  {
    EGNERROR("no physical devices meet the requirements!");
    return false;
  }

  return true;
}

void destroy_queue_family_indexes(VkRendererQueueIndexes *indexes)
{
  egnarray_destroy(indexes->queue_count);
  egnarray_destroy(indexes->queue_family_indexes);
}
