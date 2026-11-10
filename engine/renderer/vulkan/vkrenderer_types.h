#pragma once

#include "core/asserts.h"
#include "defines.h"
#include "math/math.h"
#include "renderer/renderer_types.h"
#include "renderer/vulkan/vkrenderer_asset_types.h"
#include <vulkan/vulkan.h>

#define VKCHECK(expr)                                                          \
  { EGNASSERT(expr == VK_SUCCESS); }

// Hard limits for renderer-owned resources. Handles returned to games are
// 1-based indexes into these tables; 0 always means "invalid".
#define VKRENDERER_MAX_TEXTURES 512
#define VKRENDERER_MAX_MESHES 512
#define VKRENDERER_MAX_BONES 8192
#define VKRENDERER_BONE_FRAMES 8
#define VKRENDERER_MAX_PARTICLE_SYSTEMS 32
#define VKRENDERER_PARTICLE_WORKGROUP 64

typedef struct VkRendererQueueIndexes {
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

typedef struct VkRendererSwapchainSupport {
  VkSurfaceCapabilitiesKHR capabilities;
  u32 format_count;
  VkSurfaceFormatKHR *formats;
  u32 present_mode_count;
  VkPresentModeKHR *present_modes;
} VkRendererSwapchainSupport;

typedef struct VkRendererDevice {
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

typedef struct VkRendererImage {
  VkImage handle;
  VkDeviceMemory memory;
  VkImageView view;
  u32 width;
  u32 height;
} VkRendererImage;

typedef struct VkRendererSwapchain {
  VkSwapchainKHR handle;

  u32 image_count;
  VkImage *images;
  VkImageView *views;

  VkSurfaceFormatKHR image_format;
  u32 max_frame_in_flight;
  VkRendererImage depth_attachment;

} VkRendererSwapchain;

typedef enum VkRendererState {
  VKRENDERER_STATE_NOT_ALLOCATED,
  VKRENDERER_STATE_READY,
  VKRENDERER_STATE_RECORDING,
  VKRENDERER_STATE_IN_RENDERPASS,
  VKRENDERER_STATE_RECORDING_ENDED,
  VKRENDERER_STATE_SUBMITTED
} VkRendererState;

typedef struct VkRendererRenderpass {
  VkRenderPass handle;

  f32 x, y, w, h;
  f32 r, g, b, a;
  f32 depth;
  u32 stencil;

  VkRendererState state;
} VkRendererRenderpass;

typedef struct VkRendererCommandBuffer {
  VkCommandBuffer handle;

  VkRendererState state;
} VkRendererCommandBuffer;

typedef struct VkRendererFramebuffer {
  VkFramebuffer handle;

  u32 attachment_count;
  VkImageView *attachments;
  VkRendererRenderpass *renderpass;
} VkRendererFramebuffer;

typedef struct VkRendererFence {
  VkFence handle;
  b8 is_signaled;
} VkRendererFence;

typedef struct VkRendererShaderStage {
  VkShaderModule handle;
  VkShaderModuleCreateInfo module_info;
  VkPipelineShaderStageCreateInfo pipeline_stage_info;
} VkRendererShaderStage;

typedef struct VkRendererPipeline {
  VkPipeline handle;
  VkPipelineLayout layout;
} VkRendererPipeline;

#define VKRENDERER_SHADER_STATGE_TYPE_COUNT 2

typedef struct VkRendererShader {
  VkRendererShaderStage stages[VKRENDERER_SHADER_STATGE_TYPE_COUNT];
  VkRendererPipeline pipeline;
} VkRendererShader;

typedef struct VkRendererTexture {
  VkImage image;
  VkDeviceMemory memory;
  VkImageView view;
  VkSampler sampler;
  VkDescriptorSet descriptor_set;
  u32 width;
  u32 height;
  u32 mip_levels;
  b8 used;
} VkRendererTexture;

typedef struct VkRendererMaterial
{
  u32 base_color_texture;
  u32 metallic_roughness_texture;
  u32 normal_texture;
  u32 occlusion_texture;
  u32 emissive_texture;
  vec4 base_color_factor;
  vec4 emissive_factor; // rgb + intensity
  f32 metallic_factor;
  f32 roughness_factor;
  b8 has_pbr_factors;
  b8 is_water;
} VkRendererMaterial;

typedef struct VkRendererSubMesh
{
  VkBuffer vertex_buffer;
  VkDeviceMemory vertex_memory;
  VkBuffer index_buffer;
  VkDeviceMemory index_memory;
  u32 index_count;
  VkRendererMaterial material;
  VkDescriptorSet material_set;
} VkRendererSubMesh;

typedef struct VkRendererMesh
{
  VkRendererSubMesh *submeshes;
  u32 submesh_count;
  EgnMeshAsset *asset;
  b8 used;
} VkRendererMesh;

typedef struct VkRendererParticleSystem
{
  VkBuffer particle_buffer;
  VkDeviceMemory particle_memory;
  VkBuffer state_buffer;
  VkDeviceMemory state_memory;
  VkDescriptorSet descriptor_set;
  u32 max_particles;
  u32 texture;
  b8 used;
} VkRendererParticleSystem;

typedef struct VkContext {
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
  b8 vsync;
  f32 elapsed_time;

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

  // Shared descriptor set layout: binding 0 is a combined image sampler.
  VkDescriptorSetLayout descriptor_set_layout;
  VkDescriptorPool descriptor_pool;

  // PBR material maps (metallic-roughness, normal, occlusion, emissive).
  VkDescriptorSetLayout material_set_layout;
  VkDescriptorPool material_descriptor_pool;

  // Skinning: per-frame storage buffer with bone matrices.
  VkDescriptorSetLayout bone_set_layout;
  VkDescriptorPool bone_descriptor_pool;
  VkBuffer bone_buffers[VKRENDERER_BONE_FRAMES];
  VkDeviceMemory bone_memories[VKRENDERER_BONE_FRAMES];
  void *bone_mapped[VKRENDERER_BONE_FRAMES];
  VkDescriptorSet bone_sets[VKRENDERER_BONE_FRAMES];
  u32 bone_cursor;

  // Lighting and directional shadow map.
  VkDescriptorSetLayout lighting_set_layout;
  VkDescriptorPool lighting_descriptor_pool;
  VkBuffer lighting_buffers[VKRENDERER_BONE_FRAMES];
  VkDeviceMemory lighting_memories[VKRENDERER_BONE_FRAMES];
  void *lighting_mapped[VKRENDERER_BONE_FRAMES];
  VkDescriptorSet lighting_sets[VKRENDERER_BONE_FRAMES];

  VkRendererImage shadow_images[4];
  VkSampler shadow_sampler;
  VkRenderPass shadow_renderpass;
  VkFramebuffer shadow_framebuffers[4];
  u32 shadow_size;
  u32 shadow_cascade_count;

  VkRendererShader shader_3d;
  VkRendererShader shader_sprite;
  VkRendererShader shadow_shader;

  // GPU particles.
  VkDescriptorSetLayout particle_set_layout;
  VkDescriptorPool particle_descriptor_pool;
  VkPipelineLayout particle_compute_layout;
  VkPipeline particle_compute_pipeline;
  VkRendererShader particle_shader;
  VkRendererParticleSystem particle_systems[VKRENDERER_MAX_PARTICLE_SYSTEMS];

  VkRendererTexture white_texture;
  VkRendererTexture black_texture;
  VkRendererTexture flat_normal_texture;
  VkRendererTexture textures[VKRENDERER_MAX_TEXTURES];
  VkRendererMesh meshes[VKRENDERER_MAX_MESHES];

  // Handles for renderer-owned primitive meshes.
  u32 cube_mesh;
  u32 sphere_mesh;
  u32 sprite_mesh;

  // Animation states allocated for skinned models (freed at renderer shutdown).
  void **animation_states;
  u32 animation_state_count;
  u32 animation_state_capacity;

  b8 (*find_memory_index)(u32 type_filter, u32 property_flags, u32 *out_index);
} VkContext;

// Implemented in vkrenderer.c so renderer submodules can reach the singleton
// context without exposing it as a global symbol.
VkContext *vkrenderer_get_context(void);
