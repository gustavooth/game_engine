#include "vkrenderer_types.h"

b8 vkrenderer_check_vulkan_result(VkResult result);

const char *vkrenderer_vulkan_result_to_str(VkResult result, b8 get_extended);