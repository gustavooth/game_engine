#include "vkrenderer_particles.h"
#include "vkrenderer_buffer.h"
#include "shader/vkrenderer_shader.h"
#include "shader/vkrenderer_shader_utils.h"
#include "core/logger.h"
#include "core/memo.h"

#include <stddef.h>
#include <string.h>

typedef struct Particle
{
  vec4 position_age;
  vec4 velocity_life;
} Particle;

typedef struct ParticleState
{
  u32 rng;
  u32 frame;
  u32 active_count;
  u32 padding;
} ParticleState;

typedef struct ParticleComputePush
{
  vec4 emitter_position;  // xyz + spawn rate
  vec4 emitter_direction; // xyz + spread
  vec4 color_start;
  vec4 color_end;
  vec4 params; // lifetime, speed, gravity, drag
  vec4 size;   // radius, speed variation, dt, unused
  u32 max_particles;
} ParticleComputePush;

typedef char ParticleComputePush_size_check[(sizeof(ParticleComputePush) == 100) ? 1 : -1];

typedef struct ParticleGraphicsPush
{
  mat4 view_projection;
  vec4 camera_right;
  vec4 camera_up;
  vec4 color_start;
  vec4 color_end;
  vec4 params; // size start, size end, fade in, fade out
} ParticleGraphicsPush;

typedef char ParticleGraphicsPush_size_check[(sizeof(ParticleGraphicsPush) == 144) ? 1 : -1];

static VkRendererParticleSystem *particle_system(VkContext *ctx, u32 handle)
{
  if (!ctx || handle == 0 || handle > VKRENDERER_MAX_PARTICLE_SYSTEMS)
    return 0;
  VkRendererParticleSystem *system = &ctx->particle_systems[handle - 1];
  return system->used ? system : 0;
}

static b8 create_particle_layout(VkContext *ctx)
{
  VkDescriptorSetLayoutBinding bindings[3] = {0};

  bindings[0].binding = 0;
  bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  bindings[0].descriptorCount = 1;
  bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT;

  bindings[1].binding = 1;
  bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  bindings[1].descriptorCount = 1;
  bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

  bindings[2].binding = 2;
  bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bindings[2].descriptorCount = 1;
  bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layout_info.bindingCount = 3;
  layout_info.pBindings = bindings;
  VKCHECK(vkCreateDescriptorSetLayout(ctx->device.handle, &layout_info, ctx->allocator,
                                      &ctx->particle_set_layout))

  VkDescriptorPoolSize pool_sizes[2] = {0};
  pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  pool_sizes[0].descriptorCount = VKRENDERER_MAX_PARTICLE_SYSTEMS * 2;
  pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  pool_sizes[1].descriptorCount = VKRENDERER_MAX_PARTICLE_SYSTEMS;

  VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = VKRENDERER_MAX_PARTICLE_SYSTEMS;
  pool_info.poolSizeCount = 2;
  pool_info.pPoolSizes = pool_sizes;
  VKCHECK(vkCreateDescriptorPool(ctx->device.handle, &pool_info, ctx->allocator,
                                 &ctx->particle_descriptor_pool))
  return true;
}

static b8 create_compute_pipeline(VkContext *ctx)
{
  VkPushConstantRange range = {0};
  range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  range.offset = 0;
  range.size = sizeof(ParticleComputePush);

  VkPipelineLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout_info.setLayoutCount = 1;
  layout_info.pSetLayouts = &ctx->particle_set_layout;
  layout_info.pushConstantRangeCount = 1;
  layout_info.pPushConstantRanges = &range;
  VKCHECK(vkCreatePipelineLayout(ctx->device.handle, &layout_info, ctx->allocator,
                                 &ctx->particle_compute_layout))

  VkRendererShaderStage stage = {0};
  if (!vkrenderer_shader_create_stage(ctx, "shaders/particle.comp.spv",
                                      VK_SHADER_STAGE_COMPUTE_BIT, &stage))
    return false;

  VkComputePipelineCreateInfo pipeline_info = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pipeline_info.stage = stage.pipeline_stage_info;
  pipeline_info.layout = ctx->particle_compute_layout;
  VkResult result = vkCreateComputePipelines(ctx->device.handle, 0, 1,
                                             &pipeline_info, ctx->allocator,
                                             &ctx->particle_compute_pipeline);
  vkrenderer_shader_destroy_stage(ctx, &stage);
  if (result != VK_SUCCESS)
  {
    EGNERROR("VKRENDERER failed to create the particle compute pipeline");
    return false;
  }
  return true;
}

static b8 create_graphics_pipeline(VkContext *ctx)
{
  VkRendererShaderParams params = {0};
  params.vert_path = "shaders/particle.vert.spv";
  params.frag_path = "shaders/particle.frag.spv";
  params.set_layouts = &ctx->particle_set_layout;
  params.set_layout_count = 1;
  params.push_constant_size = sizeof(ParticleGraphicsPush);
  params.depth_test = true;
  params.depth_write = false;
  params.blending = true;
  params.cull_mode = VK_CULL_MODE_NONE;
  params.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  params.color_attachment_count = 1;
  return vkrenderer_shader_create(ctx, &params, &ctx->particle_shader);
}

b8 vkrenderer_particles_create(VkContext *ctx)
{
  if (!create_particle_layout(ctx))
    return false;
  if (!create_compute_pipeline(ctx))
    return false;
  if (!create_graphics_pipeline(ctx))
    return false;
  return true;
}

void vkrenderer_particle_system_destroy(VkContext *ctx, u32 handle)
{
  VkRendererParticleSystem *system = particle_system(ctx, handle);
  if (!system)
    return;

  if (system->descriptor_set)
    vkFreeDescriptorSets(ctx->device.handle, ctx->particle_descriptor_pool, 1,
                         &system->descriptor_set);

  vkrenderer_buffer_destroy(ctx, &system->particle_buffer, &system->particle_memory);
  vkrenderer_buffer_destroy(ctx, &system->state_buffer, &system->state_memory);
  MEMOZERO(system, sizeof(VkRendererParticleSystem));
}

void vkrenderer_particles_destroy(VkContext *ctx)
{
  for (u32 i = 0; i < VKRENDERER_MAX_PARTICLE_SYSTEMS; i++)
    vkrenderer_particle_system_destroy(ctx, i + 1);

  vkrenderer_shader_destroy(ctx, &ctx->particle_shader);

  if (ctx->particle_compute_pipeline)
  {
    vkDestroyPipeline(ctx->device.handle, ctx->particle_compute_pipeline, ctx->allocator);
    ctx->particle_compute_pipeline = 0;
  }

  if (ctx->particle_compute_layout)
  {
    vkDestroyPipelineLayout(ctx->device.handle, ctx->particle_compute_layout, ctx->allocator);
    ctx->particle_compute_layout = 0;
  }

  if (ctx->particle_descriptor_pool)
  {
    vkDestroyDescriptorPool(ctx->device.handle, ctx->particle_descriptor_pool, ctx->allocator);
    ctx->particle_descriptor_pool = 0;
  }

  if (ctx->particle_set_layout)
  {
    vkDestroyDescriptorSetLayout(ctx->device.handle, ctx->particle_set_layout, ctx->allocator);
    ctx->particle_set_layout = 0;
  }
}

u32 vkrenderer_particle_system_create(VkContext *ctx, u32 max_particles, u32 texture)
{
  if (!ctx || max_particles == 0)
    return 0;

  if (max_particles > 65536)
    max_particles = 65536;

  u32 slot = VKRENDERER_MAX_PARTICLE_SYSTEMS;
  for (u32 i = 0; i < VKRENDERER_MAX_PARTICLE_SYSTEMS; i++)
  {
    if (!ctx->particle_systems[i].used)
    {
      slot = i;
      break;
    }
  }

  if (slot == VKRENDERER_MAX_PARTICLE_SYSTEMS)
  {
    EGNERROR("VKRENDERER particle system table is full");
    return 0;
  }

  VkRendererParticleSystem *system = &ctx->particle_systems[slot];
  MEMOZERO(system, sizeof(VkRendererParticleSystem));

  u64 particle_size = sizeof(Particle) * (u64)max_particles;

  if (!vkrenderer_buffer_create(ctx, particle_size,
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                &system->particle_buffer, &system->particle_memory))
  {
    MEMOZERO(system, sizeof(VkRendererParticleSystem));
    return 0;
  }

  if (!vkrenderer_buffer_create(ctx, sizeof(ParticleState),
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                &system->state_buffer, &system->state_memory))
  {
    vkrenderer_buffer_destroy(ctx, &system->particle_buffer, &system->particle_memory);
    MEMOZERO(system, sizeof(VkRendererParticleSystem));
    return 0;
  }

  void *mapped = 0;
  if (vkMapMemory(ctx->device.handle, system->particle_memory, 0, VK_WHOLE_SIZE, 0,
                  &mapped) == VK_SUCCESS)
  {
    MEMOSET(mapped, 0, particle_size);
    vkUnmapMemory(ctx->device.handle, system->particle_memory);
  }

  if (vkMapMemory(ctx->device.handle, system->state_memory, 0, VK_WHOLE_SIZE, 0,
                  &mapped) == VK_SUCCESS)
  {
    MEMOSET(mapped, 0, sizeof(ParticleState));
    vkUnmapMemory(ctx->device.handle, system->state_memory);
  }

  VkDescriptorSetAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocate_info.descriptorPool = ctx->particle_descriptor_pool;
  allocate_info.descriptorSetCount = 1;
  allocate_info.pSetLayouts = &ctx->particle_set_layout;
  if (vkAllocateDescriptorSets(ctx->device.handle, &allocate_info,
                               &system->descriptor_set) != VK_SUCCESS)
  {
    EGNERROR("VKRENDERER failed to allocate particle descriptor set");
    vkrenderer_buffer_destroy(ctx, &system->particle_buffer, &system->particle_memory);
    vkrenderer_buffer_destroy(ctx, &system->state_buffer, &system->state_memory);
    MEMOZERO(system, sizeof(VkRendererParticleSystem));
    return 0;
  }

  VkRendererTexture *tex = &ctx->white_texture;
  if (texture > 0 && texture <= VKRENDERER_MAX_TEXTURES && ctx->textures[texture - 1].used)
    tex = &ctx->textures[texture - 1];

  VkDescriptorBufferInfo particle_info = {0};
  particle_info.buffer = system->particle_buffer;
  particle_info.offset = 0;
  particle_info.range = VK_WHOLE_SIZE;

  VkDescriptorBufferInfo state_info = {0};
  state_info.buffer = system->state_buffer;
  state_info.offset = 0;
  state_info.range = VK_WHOLE_SIZE;

  VkDescriptorImageInfo image_info = {0};
  image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  image_info.imageView = tex->view;
  image_info.sampler = tex->sampler;

  VkWriteDescriptorSet writes[3] = {0};
  writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[0].dstSet = system->descriptor_set;
  writes[0].dstBinding = 0;
  writes[0].descriptorCount = 1;
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  writes[0].pBufferInfo = &particle_info;

  writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[1].dstSet = system->descriptor_set;
  writes[1].dstBinding = 1;
  writes[1].descriptorCount = 1;
  writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  writes[1].pBufferInfo = &state_info;

  writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[2].dstSet = system->descriptor_set;
  writes[2].dstBinding = 2;
  writes[2].descriptorCount = 1;
  writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  writes[2].pImageInfo = &image_info;

  vkUpdateDescriptorSets(ctx->device.handle, 3, writes, 0, 0);

  system->max_particles = max_particles;
  system->texture = texture;
  system->used = true;
  return slot + 1;
}

void vkrenderer_particles_dispatch(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                                   u32 handle, const VkRendererParticleParams *params,
                                   f32 delta_time)
{
  VkRendererParticleSystem *system = particle_system(ctx, handle);
  if (!system || !params || !command_buffer)
    return;

  ParticleComputePush push = {0};
  push.emitter_position = vec4_create(params->position.x, params->position.y,
                                      params->position.z, params->spawn_rate);
  push.emitter_direction = vec4_create(params->direction.x, params->direction.y,
                                       params->direction.z, params->spread);
  push.color_start = params->color_start;
  push.color_end = params->color_end;
  push.params = vec4_create(params->lifetime, params->speed, params->gravity, params->drag);
  push.size = vec4_create(params->radius, params->speed_variation, delta_time, 0.0f);
  push.max_particles = system->max_particles;

  VkCommandBuffer cmd = command_buffer->handle;
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->particle_compute_pipeline);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          ctx->particle_compute_layout, 0, 1, &system->descriptor_set, 0, 0);
  vkCmdPushConstants(cmd, ctx->particle_compute_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                     0, sizeof(push), &push);

  u32 groups = (system->max_particles + VKRENDERER_PARTICLE_WORKGROUP - 1) /
               VKRENDERER_PARTICLE_WORKGROUP;
  vkCmdDispatch(cmd, groups, 1, 1);

  VkBufferMemoryBarrier barrier = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer = system->particle_buffer;
  barrier.offset = 0;
  barrier.size = VK_WHOLE_SIZE;

  vkCmdPipelineBarrier(cmd,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       0, 0, 0, 1, &barrier, 0, 0);
}

void vkrenderer_particle_system_render(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                                       u32 handle, const VkRendererParticleParams *params,
                                       mat4 view_projection, vec3 camera_right, vec3 camera_up)
{
  VkRendererParticleSystem *system = particle_system(ctx, handle);
  if (!system || !params || !command_buffer)
    return;

  ParticleGraphicsPush push = {0};
  push.view_projection = view_projection;
  push.camera_right = vec4_create(camera_right.x, camera_right.y, camera_right.z,
                                  params->size_scale);
  push.camera_up = vec4_create(camera_up.x, camera_up.y, camera_up.z, 0.0f);
  push.color_start = params->color_start;
  push.color_end = params->color_end;
  push.params = vec4_create(params->size_start, params->size_end,
                            params->fade_in, params->fade_out);

  VkCommandBuffer cmd = command_buffer->handle;
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->particle_shader.pipeline.handle);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          ctx->particle_shader.pipeline.layout, 0, 1,
                          &system->descriptor_set, 0, 0);
  vkCmdPushConstants(cmd, ctx->particle_shader.pipeline.layout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, sizeof(push), &push);
  vkCmdDraw(cmd, 6, system->max_particles, 0, 0);
}
