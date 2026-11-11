#include "renderer/renderer.h"
#include "renderer/renderer_settings.h"
#include "vkrenderer_types.h"
#include "vkrenderer_device.h"
#include "vkrenderer_platform.h"
#include "vkrenderer_swapchain.h"
#include "vkrenderer_renderpass.h"
#include "vkrenderer_command_buffer.h"
#include "vkrenderer_framebuffer.h"
#include "vkrenderer_fence.h"
#include "vkrenderer_utils.h"
#include "vkrenderer_buffer.h"
#include "vkrenderer_descriptor.h"
#include "vkrenderer_scene.h"
#include "vkrenderer_skin.h"
#include "vkrenderer_lighting.h"
#include "vkrenderer_post.h"
#include "vkrenderer_sky.h"
#include "vkrenderer_lut.h"
#include "vkrenderer_particles.h"
#include "vkrenderer_mesh.h"
#include "vkrenderer_texture.h"
#include "vkrenderer_screenshot.h"
#include "shader/vkrenderer_shader.h"
#include "core/logger.h"
#include "core/memo.h"
#include "platform/window.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static VkContext ctx;

// The requested settings can change at any time. They are copied into the
// active set by apply_graphics_settings() at the start of a frame, before any
// command buffer is recorded, so a runtime batch is never observed halfway.
static RendererGraphicsSettings graphics_settings;
static RendererGraphicsSettings active_graphics_settings;
static b8 graphics_settings_initialized;
static b8 graphics_settings_dirty;

b8 renderer_set_graphics_settings(const RendererGraphicsSettings *settings)
{
  if (!renderer_graphics_settings_valid(settings))
    return false;

  graphics_settings = *settings;
  graphics_settings.shadows_enabled = !!settings->shadows_enabled;
  graphics_settings.hdr_enabled = !!settings->hdr_enabled;
  graphics_settings.auto_exposure_enabled = !!settings->auto_exposure_enabled;
  graphics_settings.tone_mapping_enabled = !!settings->tone_mapping_enabled;
  graphics_settings.color_grading_enabled = !!settings->color_grading_enabled;
  graphics_settings.bloom_enabled = !!settings->bloom_enabled;
  graphics_settings.pbr_enabled = !!settings->pbr_enabled;
  graphics_settings.ao_enabled = !!settings->ao_enabled;
  graphics_settings.gi_enabled = !!settings->gi_enabled;
  graphics_settings.atmosphere_enabled = !!settings->atmosphere_enabled;
  graphics_settings.fog_enabled = !!settings->fog_enabled;
  graphics_settings.lut_enabled = !!settings->lut_enabled;

  if (!graphics_settings_initialized)
  {
    // Before the first frame there is nothing to recreate, so the requested
    // batch is also the active one.
    active_graphics_settings = graphics_settings;
    graphics_settings_initialized = true;
  }

  graphics_settings_dirty = true;
  return true;
}

RendererGraphicsSettings renderer_get_graphics_settings(void)
{
  if (!graphics_settings_initialized)
  {
    graphics_settings = renderer_graphics_settings_default();
    active_graphics_settings = graphics_settings;
    graphics_settings_initialized = true;
  }
  return graphics_settings;
}

RendererGraphicsSettings renderer_get_active_graphics_settings(void)
{
  if (!graphics_settings_initialized)
    renderer_get_graphics_settings();
  return active_graphics_settings;
}

static void apply_graphics_settings()
{
  if (!graphics_settings_initialized)
  {
    graphics_settings = renderer_graphics_settings_default();
    active_graphics_settings = graphics_settings;
    graphics_settings_initialized = true;
    graphics_settings_dirty = false;
    return;
  }

  if (!graphics_settings_dirty)
    return;

  if (renderer_graphics_settings_equal(&graphics_settings, &active_graphics_settings))
  {
    graphics_settings_dirty = false;
    return;
  }

  // Stage hooks (HDR targets, CSM resources, post pipelines, ...) are added
  // here as the corresponding features are initialized. Waiting once is
  // cheaper than reconfiguring every subsystem at a different point in time.
  vkDeviceWaitIdle(ctx.device.handle);
  active_graphics_settings = graphics_settings;
  graphics_settings_dirty = false;

  vkrenderer_post_apply_settings(&ctx);
  vkrenderer_lighting_apply_settings(&ctx);
}

VkContext *vkrenderer_get_context(void)
{
  return &ctx;
}
static u32 cached_framebuffer_width;
static u32 cached_framebuffer_height;
static u64 next_framebuffer_generation;
static b8 validation_layer_enabled;
static PFN_vkDestroyDebugUtilsMessengerEXT debug_messenger_destroy_fn;

b8 create_instance();
b8 setup_debug_messenger();
b8 find_memory_index(u32 type_filter, u32 property_flags, u32 *out_index);
void create_command_buffers();
void destroy_command_buffers();
void create_framebuffers();
void destroy_framebuffers();
void create_sync_objects();
void destroy_sync_objects();

b8 begin_frame();
b8 end_frame();

b8 handle_recreate_swapchain();

static b8 create_render_pipelines(void)
{
  VkVertexInputBindingDescription binding_3d = {0};
  binding_3d.binding = 0;
  binding_3d.stride = sizeof(MeshVertex);
  binding_3d.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription attributes_3d[5] = {
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, position)},
      {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, normal)},
      {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(MeshVertex, uv)},
      {3, 0, VK_FORMAT_R16G16B16A16_UINT, offsetof(MeshVertex, joints)},
      {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(MeshVertex, weights)},
  };

  VkRendererShaderParams params_3d = {0};
  params_3d.vert_path = "shaders/3d.vert.spv";
  params_3d.frag_path = "shaders/3d.frag.spv";
  params_3d.binding = &binding_3d;
  params_3d.attributes = attributes_3d;
  params_3d.attribute_count = 5;
  params_3d.push_constant_size = 192; // scene push + material + emissive (std140 padding)

  VkDescriptorSetLayout model_set_layouts[4] = {
      ctx.descriptor_set_layout,
      vkrenderer_skin_set_layout(&ctx),
      vkrenderer_lighting_set_layout(&ctx),
      ctx.material_set_layout,
  };
  params_3d.set_layouts = model_set_layouts;
  params_3d.set_layout_count = 4;
  params_3d.depth_test = true;
  params_3d.depth_write = true;
  params_3d.blending = true;
  params_3d.cull_mode = VK_CULL_MODE_NONE;
  params_3d.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  params_3d.render_pass = vkrenderer_post_scene_renderpass(&ctx);
  params_3d.color_attachment_count = 2;

  if (!vkrenderer_shader_create(&ctx, &params_3d, &ctx.shader_3d))
    return false;

  VkRendererShaderParams params_sprite = {0};
  params_sprite.vert_path = "shaders/sprite.vert.spv";
  params_sprite.frag_path = "shaders/sprite.frag.spv";
  params_sprite.binding = &binding_3d;
  params_sprite.attributes = attributes_3d;
  params_sprite.attribute_count = 5;
  params_sprite.push_constant_size = 148; // mat4 mvp + mat4 model + vec4 tint + uint
  params_sprite.depth_test = true;
  params_sprite.depth_write = true;
  params_sprite.blending = true;
  params_sprite.cull_mode = VK_CULL_MODE_NONE;
  params_sprite.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  params_sprite.render_pass = vkrenderer_post_scene_renderpass(&ctx);
  params_sprite.color_attachment_count = 2;

  if (!vkrenderer_shader_create(&ctx, &params_sprite, &ctx.shader_sprite))
    return false;

  VkRendererShaderParams params_shadow = {0};
  params_shadow.vert_path = "shaders/shadow.vert.spv";
  params_shadow.frag_path = "shaders/shadow.frag.spv";
  params_shadow.binding = &binding_3d;
  params_shadow.attributes = attributes_3d;
  params_shadow.attribute_count = 5;
  params_shadow.set_layouts = &ctx.bone_set_layout;
  params_shadow.set_layout_count = 1;
  params_shadow.push_constant_size = 68; // mat4 mvp + uint bone_offset
  params_shadow.depth_test = true;
  params_shadow.depth_write = true;
  params_shadow.blending = false;
  // Slope-scaled bias in the rasterizer removes acne without the large
  // constant bias in the shader that detached shadows from their casters.
  params_shadow.depth_bias_constant = 1.5f;
  params_shadow.depth_bias_slope = 2.0f;
  params_shadow.depth_clamp = true;
  params_shadow.cull_mode = VK_CULL_MODE_NONE;
  params_shadow.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  params_shadow.render_pass = vkrenderer_lighting_shadow_renderpass(&ctx);
  params_shadow.color_attachment_count = 0;

  if (!vkrenderer_shader_create(&ctx, &params_shadow, &ctx.shadow_shader))
    return false;

  return true;
}

b8 renderer_initialize(Window *win, u32 width, u32 height, b8 vsync)
{
  EGNDEBUG("VKRENDERER initializing...")

  ctx.allocator = NULL;
  ctx.find_memory_index = find_memory_index;
  ctx.framebuffer_width = width;
  ctx.framebuffer_height = height;
  ctx.vsync = vsync;

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
  create_sync_objects();

  if (!vkrenderer_descriptors_create(&ctx))
    return false;

  if (!vkrenderer_skin_create(&ctx))
    return false;

  if (!vkrenderer_lighting_create(&ctx))
    return false;

  if (!vkrenderer_post_create(&ctx))
    return false;

  if (!vkrenderer_material_layout_create(&ctx))
  {
    EGNERROR("VKRENDERER failed to create the PBR material descriptor layout");
    return false;
  }

  if (!vkrenderer_sky_create(&ctx))
    return false;

  if (ctx.device.properties.limits.maxPushConstantsSize < 192)
  {
    EGNERROR("VKRENDERER device does not support 192 bytes of push constants");
    return false;
  }

  if (!create_render_pipelines())
    return false;

  if (!vkrenderer_texture_create_solid(&ctx, 255, 255, 255, 255, &ctx.white_texture))
    return false;
  if (!vkrenderer_texture_create_solid(&ctx, 0, 0, 0, 255, &ctx.black_texture))
    return false;
  if (!vkrenderer_texture_create_solid(&ctx, 128, 128, 255, 255, &ctx.flat_normal_texture))
    return false;

  if (!vkrenderer_particles_create(&ctx))
  {
    EGNERROR("VKRENDERER failed to create the particle pipelines");
    return false;
  }

  ctx.cube_mesh = vkrenderer_mesh_create_cube(&ctx);
  ctx.sphere_mesh = vkrenderer_mesh_create_sphere(&ctx, 32, 16);
  ctx.sprite_mesh = vkrenderer_mesh_create_quad(&ctx);
  if (ctx.cube_mesh == 0 || ctx.sphere_mesh == 0 || ctx.sprite_mesh == 0)
  {
    EGNERROR("VKRENDERER failed to create primitive meshes");
    return false;
  }

  if (!graphics_settings_initialized)
  {
    if (!graphics_settings_dirty)
      graphics_settings = renderer_graphics_settings_default();
    active_graphics_settings = graphics_settings;
    graphics_settings_initialized = true;
    graphics_settings_dirty = false;
  }

  EGNINFO("VKRENDERER Initialized!");

  return true;
}

void renderer_shutdown()
{
  vkDeviceWaitIdle(ctx.device.handle);

  vkrenderer_scene_shutdown(&ctx);
  vkrenderer_screenshot_shutdown(&ctx);

  // Release every registered mesh, including OBJ/GLB assets loaded by the
  // game. Destroying only the built-in primitives leaked their vertex/index
  // buffers and backing memory on shutdown.
  for (u32 i = 0; i < VKRENDERER_MAX_MESHES; i++)
    vkrenderer_mesh_destroy(&ctx, i + 1);
  ctx.cube_mesh = 0;
  ctx.sphere_mesh = 0;
  ctx.sprite_mesh = 0;

  for (u32 i = 0; i < VKRENDERER_MAX_TEXTURES; i++)
  {
    if (ctx.textures[i].used)
      vkrenderer_texture_destroy(&ctx, &ctx.textures[i]);
  }

  vkrenderer_texture_destroy(&ctx, &ctx.white_texture);
  vkrenderer_texture_destroy(&ctx, &ctx.black_texture);
  vkrenderer_texture_destroy(&ctx, &ctx.flat_normal_texture);
  vkrenderer_material_layout_destroy(&ctx);

  vkrenderer_shader_destroy(&ctx, &ctx.shader_3d);
  vkrenderer_shader_destroy(&ctx, &ctx.shader_sprite);
  vkrenderer_shader_destroy(&ctx, &ctx.shadow_shader);

  vkrenderer_particles_destroy(&ctx);
  vkrenderer_sky_destroy(&ctx);
  vkrenderer_post_destroy(&ctx);
  vkrenderer_lighting_destroy(&ctx);
  vkrenderer_skin_destroy(&ctx);
  vkrenderer_descriptors_destroy(&ctx);
  vkrenderer_renderpass_destroy(&ctx, &ctx.main_renderpass);

  destroy_sync_objects();
  destroy_command_buffers();
  destroy_framebuffers();
  vkrenderer_swapchain_destroy(&ctx, &ctx.swapchain);
  vkrenderer_device_destroy(&ctx);
  vkrenderer_surface_destroy(&ctx);
  if (ctx.debug_messenger && debug_messenger_destroy_fn)
    debug_messenger_destroy_fn(ctx.instance, ctx.debug_messenger, ctx.allocator);
  vkDestroyInstance(ctx.instance, ctx.allocator);
}

void renderer_set_vsync(b8 vsync)
{
  if (ctx.vsync == vsync)
    return;

  ctx.vsync = vsync;
  renderer_resize(ctx.framebuffer_width, ctx.framebuffer_height);
}

void renderer_resize(u32 width, u32 height)
{
  cached_framebuffer_width = width;
  cached_framebuffer_height = height;
  next_framebuffer_generation++;

  EGNDEBUG("VKRENDERER resizing requested - w/h/gen: %u/%u/%llu", width, height, next_framebuffer_generation);
}

b8 renderer_draw_frame(RenderPacket packet)
{
  ctx.elapsed_time += (f32)packet.delta_time;
  apply_graphics_settings();

  if (!begin_frame())
    return false;

  VkRendererCommandBuffer *command_buffer = &ctx.graphics_command_buffers[ctx.current_frame];

  if (vkrenderer_scene_prepare(&ctx, command_buffer, packet.delta_time))
    vkrenderer_scene_render_shadows(&ctx, command_buffer);
  vkrenderer_sky_update(&ctx, command_buffer);

  vkrenderer_post_begin_scene(&ctx, command_buffer);
  vkrenderer_scene_render(&ctx, command_buffer);
  vkrenderer_post_end_scene(&ctx, command_buffer);

  vkrenderer_post_process(&ctx, command_buffer, packet.delta_time);

  if (!end_frame())
    return false;

  vkrenderer_screenshot_capture(&ctx);

  return true;
}

static b8 instance_layer_available(const char *layer_name)
{
  u32 layer_count = 0;
  if (vkEnumerateInstanceLayerProperties(&layer_count, NULL) != VK_SUCCESS || layer_count == 0)
    return false;

  VkLayerProperties *layers = MEMOALLOC(sizeof(VkLayerProperties) * layer_count, MEMO_RENDERER);
  if (vkEnumerateInstanceLayerProperties(&layer_count, layers) != VK_SUCCESS)
  {
    MEMOFREE(layers);
    return false;
  }

  b8 found = false;
  for (u32 i = 0; i < layer_count; i++)
  {
    if (!strcmp(layers[i].layerName, layer_name))
    {
      found = true;
      break;
    }
  }

  MEMOFREE(layers);
  return found;
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

  // The validation layer intercepts every Vulkan call and costs a large part
  // of the CPU frame time, so it is opt-in: run with EGN_VK_VALIDATION=1.
  const char *validation_layer = "VK_LAYER_KHRONOS_validation";
  const char *validation_request = getenv("EGN_VK_VALIDATION");
  validation_layer_enabled = false;
  if (validation_request && validation_request[0] != '\0' && validation_request[0] != '0')
  {
    validation_layer_enabled = instance_layer_available(validation_layer);
    if (!validation_layer_enabled)
    {
      EGNWARN("VKRENDERER validation layer requested but not available");
    }
    else
    {
      EGNINFO("VKRENDERER validation layer enabled");
    }
  }

  const char *instance_extensions[3];
  u32 instance_ext_count = 0;
  instance_extensions[instance_ext_count++] = VK_KHR_SURFACE_EXTENSION_NAME;
#ifdef EGNPLATFORM_WAYLAND
  instance_extensions[instance_ext_count++] = "VK_KHR_wayland_surface";
#elif EGNPLATFORM_WIN32
  instance_extensions[instance_ext_count++] = "VK_KHR_win32_surface";
#endif
  if (validation_layer_enabled)
    instance_extensions[instance_ext_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

  instance_info.enabledExtensionCount = instance_ext_count;
  instance_info.ppEnabledExtensionNames = instance_extensions;

  if (validation_layer_enabled)
  {
    instance_info.enabledLayerCount = 1;
    instance_info.ppEnabledLayerNames = &validation_layer;
  }

  VKCHECK(vkCreateInstance(&instance_info, ctx.allocator, &ctx.instance))

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
    // Never use the driver text as the format string: it can contain '%'.
    EGNERROR("%s", pCallbackData->pMessage);
    break;
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
    EGNWARN("%s", pCallbackData->pMessage);
    break;
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
    EGNINFO("%s", pCallbackData->pMessage);
    break;
  default:
    break;
  }

  return VK_FALSE;
}

b8 setup_debug_messenger()
{
  if (!validation_layer_enabled)
    return true;

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

  debug_messenger_destroy_fn =
      (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(ctx.instance, "vkDestroyDebugUtilsMessengerEXT");

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

void create_sync_objects()
{
  EGNDEBUG("VKRENDERER creating sync objects...");

  ctx.image_available_semaphores = MEMOALLOC(sizeof(VkSemaphore) * ctx.swapchain.max_frame_in_flight, MEMO_RENDERER);
  ctx.queue_complete_semaphores = MEMOALLOC(sizeof(VkSemaphore) * ctx.swapchain.image_count, MEMO_RENDERER);
  ctx.in_flight_fences = MEMOALLOC(sizeof(VkRendererFence) * ctx.swapchain.max_frame_in_flight, MEMO_RENDERER);
  ctx.images_in_flight = MEMOZALLOC(sizeof(VkSemaphore) * ctx.swapchain.image_count, MEMO_RENDERER);

  VkSemaphoreCreateInfo semaphore_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

  for (u32 i = 0; i < ctx.swapchain.max_frame_in_flight; i++)
  {
    vkCreateSemaphore(ctx.device.handle, &semaphore_info, ctx.allocator, &ctx.image_available_semaphores[i]);
    vkrenderer_fence_create(&ctx, true, &ctx.in_flight_fences[i]);
  }

  // One semaphore per swapchain image so it is only reused after that image
  // has been acquired again (i.e. its previous presentation has finished).
  for (u32 i = 0; i < ctx.swapchain.image_count; i++)
    vkCreateSemaphore(ctx.device.handle, &semaphore_info, ctx.allocator, &ctx.queue_complete_semaphores[i]);
}

void destroy_sync_objects()
{
  for (u32 i = 0; i < ctx.swapchain.max_frame_in_flight; i++)
  {
    vkDestroySemaphore(ctx.device.handle, ctx.image_available_semaphores[i], ctx.allocator);
    vkrenderer_fence_destroy(&ctx, &ctx.in_flight_fences[i]);
  }

  for (u32 i = 0; i < ctx.swapchain.image_count; i++)
    vkDestroySemaphore(ctx.device.handle, ctx.queue_complete_semaphores[i], ctx.allocator);

  MEMOFREE(ctx.image_available_semaphores);
  MEMOFREE(ctx.queue_complete_semaphores);
  MEMOFREE(ctx.in_flight_fences);
  MEMOFREE(ctx.images_in_flight);

  ctx.image_available_semaphores = 0;
  ctx.queue_complete_semaphores = 0;
  ctx.in_flight_fences = 0;
  ctx.images_in_flight = 0;
}

b8 begin_frame()
{
  if (ctx.recreating_swapchain)
  {
    VkResult result = vkDeviceWaitIdle(ctx.device.handle);
    if (!vkrenderer_check_vulkan_result(result))
    {
      EGNERROR("VKRENDERER begin_frame() 1: vkDeviceWaitIdle result error: %s", vkrenderer_vulkan_result_to_str(result, false));
      return false;
    }
  }

  if (ctx.framebuffer_generation != next_framebuffer_generation)
  {
    handle_recreate_swapchain();
    return false;
  }

  if (!vkrenderer_fence_wait(&ctx, &ctx.in_flight_fences[ctx.current_frame], UINT64_MAX))
    return false;

  if (!vkrenderer_swapchain_acquire_next_image_index(&ctx, &ctx.swapchain, UINT64_MAX,
                                                     ctx.image_available_semaphores[ctx.current_frame], 0, &ctx.image_index))
    return false;

  VkRendererCommandBuffer *command_buffer = &ctx.graphics_command_buffers[ctx.current_frame];
  vkrenderer_command_buffer_reset(command_buffer);
  vkrenderer_command_buffer_begin(command_buffer, false, false, false);

  VkViewport viewport;
  viewport.x = 0.0f;
  viewport.y = (f32)ctx.framebuffer_height;
  viewport.width = (f32)ctx.framebuffer_width;
  viewport.height = -(f32)ctx.framebuffer_height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor;
  scissor.offset.x = scissor.offset.y = 0;
  scissor.extent.width = ctx.framebuffer_width;
  scissor.extent.height = ctx.framebuffer_height;

  vkCmdSetViewport(command_buffer->handle, VK_FALSE, 1, &viewport);
  vkCmdSetScissor(command_buffer->handle, VK_FALSE, 1, &scissor);

  return true;
}

b8 end_frame()
{
  VkRendererCommandBuffer *command_buffer = &ctx.graphics_command_buffers[ctx.current_frame];

  vkrenderer_renderpass_end(command_buffer, &ctx.main_renderpass);

  vkrenderer_screenshot_record(&ctx, command_buffer);

  vkrenderer_command_buffer_end(command_buffer);

  vkrenderer_fence_reset(&ctx, &ctx.in_flight_fences[ctx.current_frame]);

  VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffer->handle;
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores = &ctx.queue_complete_semaphores[ctx.image_index];
  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores = &ctx.image_available_semaphores[ctx.current_frame];

  VkPipelineStageFlags stage_flags = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submit_info.pWaitDstStageMask = &stage_flags;

  VkResult result = vkQueueSubmit(ctx.device.graphics, 1, &submit_info, ctx.in_flight_fences[ctx.current_frame].handle);

  if (result != VK_SUCCESS)
  {
    EGNERROR("VKRENDERER end_frame(): VkQueueSubmit() failed with result: %s", vkrenderer_vulkan_result_to_str(result, false));
    return false;
  }

  vkrenderer_command_buffer_update_submitted(command_buffer);

  vkrenderer_swapchain_present(&ctx, &ctx.swapchain, ctx.device.graphics,
                               ctx.device.present, ctx.queue_complete_semaphores[ctx.image_index], ctx.image_index);

  if (ctx.swapchain.max_frame_in_flight > 0)
    ctx.current_frame = (ctx.current_frame + 1) % ctx.swapchain.max_frame_in_flight;

  return true;
}

b8 handle_recreate_swapchain()
{
  if (ctx.recreating_swapchain)
    return false;
  
  if(cached_framebuffer_width == 0 || cached_framebuffer_height == 0)
    return false;
  
  EGNDEBUG("VKRENDERER Recreating swapchain...");

  ctx.recreating_swapchain = true;

  vkDeviceWaitIdle(ctx.device.handle);

  destroy_sync_objects();
  destroy_command_buffers();
  destroy_framebuffers();

  vkrenderer_device_load_swapchain_support(&ctx);

  if (!vkrenderer_device_detect_depth_format(&ctx.device))
  {
    ctx.recreating_swapchain = false;
    return false;
  }

  ctx.framebuffer_width = cached_framebuffer_width;
  ctx.framebuffer_height = cached_framebuffer_height;
  ctx.main_renderpass.w = cached_framebuffer_width;
  ctx.main_renderpass.h = cached_framebuffer_height;

  vkrenderer_swapchain_recreate(&ctx, cached_framebuffer_width, cached_framebuffer_height, &ctx.swapchain);

  create_command_buffers();
  create_framebuffers();
  create_sync_objects();
  vkrenderer_post_resize(&ctx);

  ctx.current_frame = 0;
  ctx.framebuffer_generation = next_framebuffer_generation;
  cached_framebuffer_width = 0;
  cached_framebuffer_height = 0;
  
  ctx.recreating_swapchain = false;
  return true;
}

u32 renderer_lut_load(const char *path)
{
  if (!path || path[0] == '\0')
    return 0;
  return vkrenderer_lut_load(&ctx, path);
}
