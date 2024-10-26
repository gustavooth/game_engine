#include "defines.h"

#ifdef EGNPLATFORM_WAYLAND

#define VK_USE_PLATFORM_WAYLAND_KHR
#include "../vkrenderer_platform.h"
#include "platform/linux/window_wayland.h"
#include "platform/window.h"
#include "core/logger.h"

b8 vkrenderer_create_surface(VKContext *ctx, Window *win)
{
  EGNDEBUG("VKRENDERER Creating wayland surface...");
  WaylandState *state = (WaylandState *)win->internal_state;
  VkWaylandSurfaceCreateInfoKHR surface_info = {VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR};
  surface_info.display = state->display;
  surface_info.surface = state->surface;

  VKCHECK(vkCreateWaylandSurfaceKHR(ctx->instance, &surface_info, 0, &ctx->surface))

  return true;
}

#endif
