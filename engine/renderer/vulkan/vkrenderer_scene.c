#include "vkrenderer_scene.h"
#include "vkrenderer_skin.h"
#include "vkrenderer_lighting.h"
#include "vkrenderer_particles.h"
#include "shader/vkrenderer_shader.h"
#include "scene/scene_internal.h"
#include "core/logger.h"
#include "core/memo.h"

#include <math.h>
#include <string.h>

#define VKRENDERER_MAX_CAMERAS 64

typedef struct ScenePushConstants
{
  mat4 mvp;
  mat4 model;
  vec4 tint;
  u32 bone_offset;
} ScenePushConstants;

typedef char ScenePushConstants_size_check[(sizeof(ScenePushConstants) == 148) ? 1 : -1];

typedef struct SceneCameraInfo
{
  EgnObject *object;
  EgnCameraData *camera;
  i32 order;
  u32 sequence;
} SceneCameraInfo;

static f32 clampf(f32 value, f32 minimum, f32 maximum)
{
  if (value < minimum)
    return minimum;
  if (value > maximum)
    return maximum;
  return value;
}

static VkDescriptorSet resolve_texture(VkContext *ctx, u32 texture)
{
  if (texture > 0 && texture <= VKRENDERER_MAX_TEXTURES)
  {
    VkRendererTexture *slot = &ctx->textures[texture - 1];
    if (slot->used)
      return slot->descriptor_set;
  }

  return ctx->white_texture.descriptor_set;
}


static void draw_mesh(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                      VkRendererShader *shader, u32 mesh_handle, u32 texture_handle,
                      mat4 model, mat4 view_projection, vec4 tint, u32 bone_offset,
                      VkDescriptorSet *last_texture)
{
  if (mesh_handle == 0 || mesh_handle > VKRENDERER_MAX_MESHES)
    return;

  VkRendererMesh *mesh = &ctx->meshes[mesh_handle - 1];
  if (!mesh->used || !mesh->submeshes)
    return;

  ScenePushConstants base_push = {0};
  base_push.mvp = mat4_mul(view_projection, model);
  base_push.model = model;

  for (u32 i = 0; i < mesh->submesh_count; i++)
  {
    VkRendererSubMesh *submesh = &mesh->submeshes[i];
    if (submesh->index_count == 0)
      continue;

    ScenePushConstants push = base_push;
    push.tint = vec4_mul(tint, submesh->base_color);
    push.bone_offset = bone_offset;

    u32 resolved_texture = texture_handle != 0 ? texture_handle : submesh->texture;
    VkDescriptorSet texture = resolve_texture(ctx, resolved_texture);
    if (texture != *last_texture)
    {
      vkCmdBindDescriptorSets(command_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              shader->pipeline.layout, 0, 1, &texture, 0, 0);
      *last_texture = texture;
    }

    vkCmdPushConstants(command_buffer->handle, shader->pipeline.layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(ScenePushConstants), &push);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command_buffer->handle, 0, 1, &submesh->vertex_buffer, &offset);
    vkCmdBindIndexBuffer(command_buffer->handle, submesh->index_buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(command_buffer->handle, submesh->index_count, 1, 0, 0, 0);
  }
}


// ---------------------------------------------------------------------------
// Skinning / animation
// ---------------------------------------------------------------------------

typedef struct AnimationState
{
  const EgnMeshAsset *asset;
  u32 node_count;

  vec3 *translations;
  quat *rotations;
  vec3 *scales;
  mat4 *worlds;
  b8 *computed;

  mat4 *bone_matrices;
  u32 bone_capacity;
} AnimationState;

static void animation_state_clear(AnimationState *state)
{
  if (!state)
    return;

  if (state->translations)
    MEMOFREE(state->translations);
  if (state->rotations)
    MEMOFREE(state->rotations);
  if (state->scales)
    MEMOFREE(state->scales);
  if (state->worlds)
    MEMOFREE(state->worlds);
  if (state->computed)
    MEMOFREE(state->computed);
  if (state->bone_matrices)
    MEMOFREE(state->bone_matrices);

  state->translations = 0;
  state->rotations = 0;
  state->scales = 0;
  state->worlds = 0;
  state->computed = 0;
  state->bone_matrices = 0;
  state->bone_capacity = 0;
  state->asset = 0;
  state->node_count = 0;
}

static b8 animation_state_ensure(AnimationState *state, const EgnMeshAsset *asset)
{
  if (!state || !asset)
    return false;

  u32 joint_count = 0;
  if (asset->skin_index >= 0 && (u32)asset->skin_index < asset->skin_count)
    joint_count = asset->skins[asset->skin_index].joint_count;

  if (state->asset == asset && state->node_count == asset->node_count &&
      state->bone_capacity >= joint_count)
    return true;

  animation_state_clear(state);

  state->asset = asset;
  state->node_count = asset->node_count;

  if (asset->node_count > 0)
  {
    u64 node_count = asset->node_count;
    state->translations = MEMOALLOC(sizeof(vec3) * node_count, MEMO_RENDERER);
    state->rotations = MEMOALLOC(sizeof(quat) * node_count, MEMO_RENDERER);
    state->scales = MEMOALLOC(sizeof(vec3) * node_count, MEMO_RENDERER);
    state->worlds = MEMOALLOC(sizeof(mat4) * node_count, MEMO_RENDERER);
    state->computed = MEMOZALLOC(sizeof(b8) * node_count, MEMO_RENDERER);

    if (!state->translations || !state->rotations || !state->scales ||
        !state->worlds || !state->computed)
    {
      animation_state_clear(state);
      return false;
    }
  }

  if (joint_count > 0)
  {
    state->bone_matrices = MEMOALLOC(sizeof(mat4) * joint_count, MEMO_RENDERER);
    if (!state->bone_matrices)
    {
      animation_state_clear(state);
      return false;
    }
    state->bone_capacity = joint_count;
  }

  return true;
}

static b8 track_animation_state(VkContext *ctx, AnimationState *state)
{
  if (ctx->animation_state_count == ctx->animation_state_capacity)
  {
    u32 new_capacity = ctx->animation_state_capacity ? ctx->animation_state_capacity * 2 : 16;
    void **new_states = MEMOALLOC(sizeof(void *) * new_capacity, MEMO_RENDERER);
    if (!new_states)
      return false;

    if (ctx->animation_states)
    {
      MEMOCPY(new_states, ctx->animation_states,
              sizeof(void *) * ctx->animation_state_count);
      MEMOFREE(ctx->animation_states);
    }

    ctx->animation_states = new_states;
    ctx->animation_state_capacity = new_capacity;
  }

  ctx->animation_states[ctx->animation_state_count++] = state;
  return true;
}

static AnimationState *get_animation_state(VkContext *ctx, EgnModelData *model,
                                           const EgnMeshAsset *asset)
{
  AnimationState *state = (AnimationState *)model->anim_state;

  if (state)
  {
    if (animation_state_ensure(state, asset))
      return state;
    return 0;
  }

  state = MEMOZALLOC(sizeof(AnimationState), MEMO_RENDERER);
  if (!state)
    return 0;

  if (!animation_state_ensure(state, asset))
  {
    MEMOFREE(state);
    return 0;
  }

  if (!track_animation_state(ctx, state))
  {
    animation_state_clear(state);
    MEMOFREE(state);
    return 0;
  }

  model->anim_state = state;
  return state;
}

static u32 channel_components(const EgnAnimationChannel *channel)
{
  return channel->path == EGN_ANIMATION_PATH_ROTATION ? 4 : 3;
}

static u32 channel_key_index(const EgnAnimationChannel *channel, u32 key)
{
  u32 components = channel_components(channel);
  if (channel->interpolation == EGN_ANIMATION_INTERPOLATION_CUBICSPLINE)
    return (key * 3 + 1) * components;
  return key * components;
}

static void channel_find_key(const EgnAnimationChannel *channel, f32 time,
                             u32 *out_key, f32 *out_t)
{
  *out_key = 0;
  *out_t = 0.0f;

  if (channel->time_count == 0)
    return;

  if (time <= channel->times[0])
    return;

  if (time >= channel->times[channel->time_count - 1])
  {
    *out_key = channel->time_count - 1;
    return;
  }

  for (u32 i = 0; i + 1 < channel->time_count; i++)
  {
    if (time < channel->times[i + 1])
    {
      f32 span = channel->times[i + 1] - channel->times[i];
      *out_key = i;
      *out_t = span > EGN_FLOAT_EPSILON ? (time - channel->times[i]) / span : 0.0f;
      return;
    }
  }
}

static void sample_channel(const EgnAnimationChannel *channel, f32 time,
                           vec3 *out_vector, quat *out_rotation)
{
  u32 components = channel_components(channel);
  if (channel->time_count == 0 || channel->value_count < components)
    return;

  u32 key = 0;
  f32 t = 0.0f;
  channel_find_key(channel, time, &key, &t);

  u32 index_0 = channel_key_index(channel, key);
  u32 index_1 = index_0;

  if (channel->interpolation != EGN_ANIMATION_INTERPOLATION_STEP &&
      key + 1 < channel->time_count)
    index_1 = channel_key_index(channel, key + 1);

  if (index_0 + components > channel->value_count)
    return;
  if (index_1 + components > channel->value_count)
    index_1 = index_0;

  if (channel->path == EGN_ANIMATION_PATH_ROTATION)
  {
    quat q0 = (quat){channel->values[index_0 + 0], channel->values[index_0 + 1],
                     channel->values[index_0 + 2], channel->values[index_0 + 3]};
    if (channel->interpolation == EGN_ANIMATION_INTERPOLATION_STEP || index_1 == index_0)
    {
      *out_rotation = quat_normalize(q0);
      return;
    }
    quat q1 = (quat){channel->values[index_1 + 0], channel->values[index_1 + 1],
                     channel->values[index_1 + 2], channel->values[index_1 + 3]};
    *out_rotation = quat_slerp(q0, q1, t);
    return;
  }

  vec3 v0 = vec3_create(channel->values[index_0 + 0],
                        channel->values[index_0 + 1],
                        channel->values[index_0 + 2]);
  if (channel->interpolation == EGN_ANIMATION_INTERPOLATION_STEP || index_1 == index_0)
  {
    *out_vector = v0;
    return;
  }

  vec3 v1 = vec3_create(channel->values[index_1 + 0],
                        channel->values[index_1 + 1],
                        channel->values[index_1 + 2]);
  *out_vector = vec3_add(v0, vec3_mul_scalar(vec3_sub(v1, v0), t));
}

static mat4 animation_node_world(const EgnMeshAsset *asset, AnimationState *state,
                                 u32 index)
{
  if (index >= state->node_count)
    return mat4_identity();
  if (state->computed[index])
    return state->worlds[index];

  mat4 local = mat4_mul(mat4_translation(state->translations[index]),
                        mat4_mul(quat_to_mat4(state->rotations[index]),
                                 mat4_scale(state->scales[index])));

  i32 parent = asset->nodes[index].parent;
  mat4 world = parent >= 0 ? mat4_mul(animation_node_world(asset, state, (u32)parent), local) : local;
  state->worlds[index] = world;
  state->computed[index] = true;
  return world;
}

static u32 evaluate_animation(const EgnMeshAsset *asset, AnimationState *state,
                              const EgnAnimationData *animation)
{
  if (!asset || !state || state->node_count == 0)
    return 0;

  for (u32 i = 0; i < state->node_count; i++)
  {
    state->translations[i] = asset->nodes[i].translation;
    state->rotations[i] = asset->nodes[i].rotation;
    state->scales[i] = asset->nodes[i].scale;
    state->computed[i] = false;
  }

  if (animation && animation->clip >= 0 &&
      (u32)animation->clip < asset->animation_count)
  {
    const EgnAnimation *clip = &asset->animations[animation->clip];
    f32 time = animation->time;
    if (clip->duration > EGN_FLOAT_EPSILON)
    {
      if (animation->loop)
      {
        time = fmodf(time, clip->duration);
        if (time < 0.0f)
          time += clip->duration;
      }
      else if (time > clip->duration)
      {
        time = clip->duration;
      }
    }
    else
    {
      time = 0.0f;
    }

    for (u32 c = 0; c < clip->channel_count; c++)
    {
      const EgnAnimationChannel *channel = &clip->channels[c];
      if (channel->node < 0 || (u32)channel->node >= state->node_count)
        continue;

      if (channel->path == EGN_ANIMATION_PATH_TRANSLATION)
        sample_channel(channel, time, &state->translations[channel->node], 0);
      else if (channel->path == EGN_ANIMATION_PATH_ROTATION)
        sample_channel(channel, time, 0, &state->rotations[channel->node]);
      else if (channel->path == EGN_ANIMATION_PATH_SCALE)
        sample_channel(channel, time, &state->scales[channel->node], 0);
    }
  }

  for (u32 i = 0; i < state->node_count; i++)
    animation_node_world(asset, state, i);

  if (asset->skin_index < 0 || (u32)asset->skin_index >= asset->skin_count)
    return 0;

  const EgnSkin *skin = &asset->skins[asset->skin_index];
  if (!state->bone_matrices || state->bone_capacity < skin->joint_count)
    return 0;

  for (u32 joint = 0; joint < skin->joint_count; joint++)
  {
    i32 node = skin->joints[joint];
    mat4 world = (node >= 0 && (u32)node < state->node_count)
                     ? state->worlds[node]
                     : mat4_identity();
    state->bone_matrices[joint] = mat4_mul(world, skin->inverse_bind[joint]);
  }

  return skin->joint_count;
}

static void prepare_skinning_recursive(VkContext *ctx, EgnObject *object)
{
  if (!object || !object->alive || !object->active)
    return;

  for (EgnComponent *component = object->first_component; component; component = component->next)
  {
    if (!component->alive || !component->enabled ||
        component->type != EGN_COMPONENT_TYPE_MODEL)
      continue;

    EgnModelData *model = &component->data.model;
    model->bone_offset = 0;

    if (model->mesh == 0 || model->mesh > VKRENDERER_MAX_MESHES)
      continue;

    VkRendererMesh *mesh = &ctx->meshes[model->mesh - 1];
    if (!mesh->used || !mesh->asset || !mesh->asset->skinned)
      continue;

    const EgnMeshAsset *asset = mesh->asset;
    EgnComponent *animation_component = egn_object_get_component(object, EGN_COMPONENT_TYPE_ANIMATION);
    EgnAnimationData *animation = (animation_component && animation_component->alive && animation_component->enabled)
                                      ? &animation_component->data.animation
                                      : 0;

    AnimationState *state = get_animation_state(ctx, model, asset);
    if (!state)
      continue;

    u32 bone_count = evaluate_animation(asset, state, animation);
    if (bone_count == 0)
      continue;

    u32 offset = vkrenderer_skin_allocate(ctx, bone_count);
    if (offset == 0)
      continue;

    vkrenderer_skin_write(ctx, offset, state->bone_matrices, bone_count);
    model->bone_offset = offset;
  }

  for (EgnObject *child = object->first_child; child; child = child->next_sibling)
    prepare_skinning_recursive(ctx, child);
}

static void collect_lights_recursive(EgnObject *object, VkRendererLighting *lighting,
                                    b8 *has_sun)
{
  if (!object || !object->alive || !object->active)
    return;

  for (EgnComponent *component = object->first_component; component; component = component->next)
  {
    if (!component->alive || !component->enabled ||
        component->type != EGN_COMPONENT_TYPE_LIGHT)
      continue;

    EgnLightData *light = &component->data.light;
    if (light->light_type == EGN_LIGHT_DIRECTIONAL)
    {
      if (*has_sun)
        continue;

      quat world_rotation = egn_object_get_world_rotation(object);
      mat4 rotation = quat_to_mat4(world_rotation);
      vec3 shine = vec3_create(-rotation.data[8], -rotation.data[9], -rotation.data[10]);
      shine = vec3_normalized(shine);

      vec3 to_sun = vec3_mul_scalar(shine, -1.0f);
      lighting->sun_direction = vec4_create(to_sun.x, to_sun.y, to_sun.z, light->intensity);
      lighting->sun_color = vec4_create(light->r, light->g, light->b, 0.68f);

      vec3 center = vec3_zero();
      vec3 up = vec3_up();
      if (egnabs(vec3_dot(shine, up)) > 0.99f)
        up = vec3_back();

      vec3 light_position = vec3_sub(center, vec3_mul_scalar(shine, 45.0f));
      mat4 view = mat4_look_at(light_position, center, up);
      mat4 projection = mat4_orthographic(-25.0f, 25.0f, -25.0f, 25.0f, 0.1f, 100.0f);
      lighting->sun_view_projection = mat4_mul(projection, view);
      lighting->point_count.y = light->cast_shadows ? 1.0f : 0.0f;
      *has_sun = true;
    }
    else
    {
      u32 index = (u32)lighting->point_count.x;
      if (index >= VKRENDERER_MAX_POINT_LIGHTS)
        continue;

      vec3 position = egn_object_get_world_position(object);
      lighting->point_lights[index].position = vec4_create(position.x, position.y, position.z, light->range);
      lighting->point_lights[index].color = vec4_create(light->r, light->g, light->b, light->intensity);
      lighting->point_count.x += 1.0f;
    }
  }

  for (EgnObject *child = object->first_child; child; child = child->next_sibling)
    collect_lights_recursive(child, lighting, has_sun);
}

static void prepare_particles_recursive(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                                       EgnObject *object, f32 delta_time)
{
  if (!object || !object->alive || !object->active)
    return;

  for (EgnComponent *component = object->first_component; component; component = component->next)
  {
    if (!component->alive || !component->enabled ||
        component->type != EGN_COMPONENT_TYPE_PARTICLES)
      continue;

    EgnParticleData *particles = &component->data.particles;
    if (particles->system == 0)
    {
      particles->system = vkrenderer_particle_system_create(ctx, particles->max_particles,
                                                            particles->texture);
      if (particles->system == 0)
        continue;
    }

    vec3 position = egn_object_get_world_position(object);
    quat rotation = egn_object_get_world_rotation(object);
    mat4 rotation_matrix = quat_to_mat4(rotation);
    vec3 direction = particles->direction;
    vec3 world_direction = vec3_create(
        rotation_matrix.data[0] * direction.x + rotation_matrix.data[4] * direction.y + rotation_matrix.data[8] * direction.z,
        rotation_matrix.data[1] * direction.x + rotation_matrix.data[5] * direction.y + rotation_matrix.data[9] * direction.z,
        rotation_matrix.data[2] * direction.x + rotation_matrix.data[6] * direction.y + rotation_matrix.data[10] * direction.z);

    VkRendererParticleParams params = {0};
    params.position = position;
    params.direction = world_direction;
    params.spawn_rate = particles->spawn_rate;
    if (particles->burst > 0.0f && delta_time > 0.0001f)
      params.spawn_rate += particles->burst / delta_time;
    particles->burst = 0.0f;
    params.lifetime = particles->lifetime;
    params.speed = particles->speed;
    params.speed_variation = particles->speed_variation;
    params.gravity = particles->gravity;
    params.drag = particles->drag;
    params.spread = particles->spread;
    params.radius = particles->radius;
    params.size_start = particles->size_start;
    params.size_end = particles->size_end;
    params.size_scale = particles->size_scale;
    params.fade_in = particles->fade_in;
    params.fade_out = particles->fade_out;
    params.color_start = particles->color_start;
    params.color_end = particles->color_end;

    vkrenderer_particles_dispatch(ctx, command_buffer, particles->system, &params, delta_time);
  }

  for (EgnObject *child = object->first_child; child; child = child->next_sibling)
    prepare_particles_recursive(ctx, command_buffer, child, delta_time);
}

static void render_particles_recursive(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                                       EgnObject *object, const SceneCameraInfo *camera,
                                       mat4 view, mat4 view_projection)
{
  if (!object || !object->alive || !object->active)
    return;

  b8 layer_visible = object->layer < EGN_SCENE_MAX_LAYERS &&
                     (camera->camera->layer_mask & (1u << object->layer)) != 0;

  if (layer_visible)
  {
    for (EgnComponent *component = object->first_component; component; component = component->next)
    {
      if (!component->alive || !component->enabled ||
          component->type != EGN_COMPONENT_TYPE_PARTICLES)
        continue;

      EgnParticleData *particles = &component->data.particles;
      if (particles->system == 0)
        continue;

      vec3 camera_right = vec3_create(view.data[0], view.data[4], view.data[8]);
      vec3 camera_up = vec3_create(view.data[1], view.data[5], view.data[9]);
      camera_right = vec3_normalized(camera_right);
      camera_up = vec3_normalized(camera_up);

      VkRendererParticleParams params = {0};
      params.size_start = particles->size_start;
      params.size_end = particles->size_end;
      params.size_scale = particles->size_scale;
      params.fade_in = particles->fade_in;
      params.fade_out = particles->fade_out;
      params.color_start = particles->color_start;
      params.color_end = particles->color_end;

      vkrenderer_particle_system_render(ctx, command_buffer, particles->system, &params,
                                        view_projection, camera_right, camera_up);
    }
  }

  for (EgnObject *child = object->first_child; child; child = child->next_sibling)
    render_particles_recursive(ctx, command_buffer, child, camera, view, view_projection);
}

b8 vkrenderer_scene_prepare(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                            f64 delta_time)
{
  (void)delta_time;

  vkrenderer_skin_begin_frame(ctx);

  EgnScene *scene = egn_scene_get();
  if (scene && scene->root)
    prepare_skinning_recursive(ctx, scene->root);

  EgnScene *particle_scene = egn_scene_get();
  if (particle_scene && particle_scene->root)
    prepare_particles_recursive(ctx, command_buffer, particle_scene->root, (f32)delta_time);

  VkRendererLighting lighting = {0};
  lighting.ambient = vec4_create(0.09f, 0.10f, 0.13f, 1.0f);
  lighting.sun_direction = vec4_create(0.0f, -1.0f, 0.0f, 0.0f);
  lighting.sun_color = vec4_create(0.0f, 0.0f, 0.0f, 0.0f);
  lighting.sun_view_projection = mat4_identity();
  lighting.point_count = vec4_create(0.0f, 0.0f,
                                     1.0f / (f32)VKRENDERER_SHADOW_SIZE,
                                     1.0f / (f32)VKRENDERER_SHADOW_SIZE);

  b8 has_sun = false;
  if (scene && scene->root)
    collect_lights_recursive(scene->root, &lighting, &has_sun);

  vkrenderer_lighting_write(ctx, &lighting);
  vkrenderer_skin_barrier(ctx, command_buffer);

  return has_sun && lighting.point_count.y > 0.5f;
}

typedef struct ShadowPushConstants
{
  mat4 mvp;
  u32 bone_offset;
} ShadowPushConstants;

typedef char ShadowPushConstants_size_check[(sizeof(ShadowPushConstants) == 68) ? 1 : -1];

static void render_shadows_recursive(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                                     EgnObject *object, const VkRendererLighting *lighting)
{
  if (!object || !object->alive || !object->active)
    return;

  for (EgnComponent *component = object->first_component; component; component = component->next)
  {
    if (!component->alive || !component->enabled ||
        component->type != EGN_COMPONENT_TYPE_MODEL)
      continue;

    EgnModelData *model = &component->data.model;
    if (!model->cast_shadows || model->mesh == 0 || model->mesh > VKRENDERER_MAX_MESHES)
      continue;

    VkRendererMesh *mesh = &ctx->meshes[model->mesh - 1];
    if (!mesh->used || !mesh->submeshes)
      continue;

    mat4 world;
    egn_object_get_world_matrix_internal(object, &world);

    ShadowPushConstants push = {0};
    push.mvp = mat4_mul(lighting->sun_view_projection, world);
    push.bone_offset = model->bone_offset;

    vkCmdPushConstants(command_buffer->handle, ctx->shadow_shader.pipeline.layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(ShadowPushConstants), &push);

    for (u32 i = 0; i < mesh->submesh_count; i++)
    {
      VkRendererSubMesh *submesh = &mesh->submeshes[i];
      if (submesh->index_count == 0)
        continue;

      VkDeviceSize offset = 0;
      vkCmdBindVertexBuffers(command_buffer->handle, 0, 1, &submesh->vertex_buffer, &offset);
      vkCmdBindIndexBuffer(command_buffer->handle, submesh->index_buffer, 0, VK_INDEX_TYPE_UINT32);
      vkCmdDrawIndexed(command_buffer->handle, submesh->index_count, 1, 0, 0, 0);
    }
  }

  for (EgnObject *child = object->first_child; child; child = child->next_sibling)
    render_shadows_recursive(ctx, command_buffer, child, lighting);
}

void vkrenderer_scene_render_shadows(VkContext *ctx, VkRendererCommandBuffer *command_buffer)
{
  const VkRendererLighting *lighting = vkrenderer_lighting_current();
  if (!lighting || lighting->point_count.y < 0.5f)
    return;

  u32 size = vkrenderer_lighting_shadow_size(ctx);

  VkClearValue clear_value = {0};
  clear_value.depthStencil.depth = 1.0f;
  clear_value.depthStencil.stencil = 0;

  VkRenderPassBeginInfo begin_info = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  begin_info.renderPass = vkrenderer_lighting_shadow_renderpass(ctx);
  begin_info.framebuffer = vkrenderer_lighting_shadow_framebuffer(ctx);
  begin_info.renderArea.offset.x = 0;
  begin_info.renderArea.offset.y = 0;
  begin_info.renderArea.extent.width = size;
  begin_info.renderArea.extent.height = size;
  begin_info.clearValueCount = 1;
  begin_info.pClearValues = &clear_value;

  vkCmdBeginRenderPass(command_buffer->handle, &begin_info, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport;
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = (f32)size;
  viewport.height = (f32)size;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor;
  scissor.offset.x = 0;
  scissor.offset.y = 0;
  scissor.extent.width = size;
  scissor.extent.height = size;

  vkCmdSetViewport(command_buffer->handle, 0, 1, &viewport);
  vkCmdSetScissor(command_buffer->handle, 0, 1, &scissor);

  vkCmdBindPipeline(command_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    ctx->shadow_shader.pipeline.handle);

  VkDescriptorSet bone_set = vkrenderer_skin_descriptor(ctx);
  vkCmdBindDescriptorSets(command_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          ctx->shadow_shader.pipeline.layout, 0, 1, &bone_set, 0, 0);

  EgnScene *scene = egn_scene_get();
  if (scene && scene->root)
    render_shadows_recursive(ctx, command_buffer, scene->root, lighting);

  vkCmdEndRenderPass(command_buffer->handle);

  VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = ctx->shadow_image.handle;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier(command_buffer->handle,
                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       0, 0, 0, 0, 0, 1, &barrier);
}

void vkrenderer_scene_shutdown(VkContext *ctx)
{
  if (!ctx)
    return;

  if (ctx->animation_states)
  {
    for (u32 i = 0; i < ctx->animation_state_count; i++)
    {
      AnimationState *state = (AnimationState *)ctx->animation_states[i];
      if (state)
      {
        animation_state_clear(state);
        MEMOFREE(state);
      }
    }
    MEMOFREE(ctx->animation_states);
  }

  ctx->animation_states = 0;
  ctx->animation_state_count = 0;
  ctx->animation_state_capacity = 0;
}

static void collect_cameras(EgnObject *object, SceneCameraInfo *cameras,
                            u32 *camera_count, u32 *sequence)
{
  if (!object || !object->alive || !object->active)
    return;

  for (EgnComponent *component = object->first_component; component; component = component->next)
  {
    if (!component->alive || !component->enabled ||
        component->type != EGN_COMPONENT_TYPE_CAMERA)
      continue;

    if (*camera_count < VKRENDERER_MAX_CAMERAS)
    {
      SceneCameraInfo *info = &cameras[*camera_count];
      info->object = object;
      info->camera = &component->data.camera;
      info->order = component->data.camera.order;
      info->sequence = (*sequence)++;
      (*camera_count)++;
    }
  }

  for (EgnObject *child = object->first_child; child; child = child->next_sibling)
    collect_cameras(child, cameras, camera_count, sequence);
}

static void sort_cameras(SceneCameraInfo *cameras, u32 camera_count)
{
  for (u32 i = 1; i < camera_count; i++)
  {
    SceneCameraInfo key = cameras[i];
    i32 j = (i32)i - 1;

    while (j >= 0 &&
           (cameras[j].order > key.order ||
            (cameras[j].order == key.order && cameras[j].sequence > key.sequence)))
    {
      cameras[j + 1] = cameras[j];
      j--;
    }

    cameras[j + 1] = key;
  }
}

static mat4 camera_view_matrix(EgnObject *object)
{
  mat4 world;
  egn_object_get_world_matrix_internal(object, &world);

  vec3 position = vec3_create(world.data[12], world.data[13], world.data[14]);

  vec3 right = vec3_create(world.data[0], world.data[1], world.data[2]);
  vec3 up = vec3_create(world.data[4], world.data[5], world.data[6]);
  vec3 back = vec3_create(world.data[8], world.data[9], world.data[10]);

  if (vec3_length(right) <= EGN_FLOAT_EPSILON)
    right = vec3_right();
  if (vec3_length(up) <= EGN_FLOAT_EPSILON)
    up = vec3_up();
  if (vec3_length(back) <= EGN_FLOAT_EPSILON)
    back = vec3_back();

  right = vec3_normalized(right);
  up = vec3_normalized(up);
  back = vec3_normalized(back);

  vec3 forward = vec3_mul_scalar(back, -1.0f);
  return mat4_look_at(position, vec3_add(position, forward), up);
}

static b8 camera_viewport_pixels(const EgnCameraData *camera, u32 framebuffer_width,
                                 u32 framebuffer_height, u32 *out_x, u32 *out_y,
                                 u32 *out_width, u32 *out_height)
{
  f32 x = clampf(camera->viewport_x, 0.0f, 1.0f);
  f32 y = clampf(camera->viewport_y, 0.0f, 1.0f);
  f32 width = clampf(camera->viewport_width, 0.0f, 1.0f - x);
  f32 height = clampf(camera->viewport_height, 0.0f, 1.0f - y);

  if (width <= 0.0f || height <= 0.0f)
    return false;

  u32 pixel_x = (u32)(x * (f32)framebuffer_width);
  u32 pixel_y = (u32)(y * (f32)framebuffer_height);
  u32 pixel_width = (u32)(width * (f32)framebuffer_width);
  u32 pixel_height = (u32)(height * (f32)framebuffer_height);

  if (pixel_x >= framebuffer_width)
    pixel_x = framebuffer_width - 1;
  if (pixel_y >= framebuffer_height)
    pixel_y = framebuffer_height - 1;

  if (pixel_width == 0)
    pixel_width = 1;
  if (pixel_height == 0)
    pixel_height = 1;

  if (pixel_x + pixel_width > framebuffer_width)
    pixel_width = framebuffer_width - pixel_x;
  if (pixel_y + pixel_height > framebuffer_height)
    pixel_height = framebuffer_height - pixel_y;

  *out_x = pixel_x;
  *out_y = pixel_y;
  *out_width = pixel_width;
  *out_height = pixel_height;
  return true;
}

static void clear_camera_area(VkRendererCommandBuffer *command_buffer,
                              const EgnCameraData *camera,
                              u32 x, u32 y, u32 width, u32 height)
{
  VkClearAttachment attachments[2];
  MEMOZERO(attachments, sizeof(attachments));

  u32 attachment_count = 0;

  if (camera->clear_color)
  {
    VkClearAttachment *attachment = &attachments[attachment_count++];
    attachment->aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    attachment->colorAttachment = 0;
    attachment->clearValue.color.float32[0] = camera->clear_r;
    attachment->clearValue.color.float32[1] = camera->clear_g;
    attachment->clearValue.color.float32[2] = camera->clear_b;
    attachment->clearValue.color.float32[3] = camera->clear_a;
  }

  if (camera->clear_depth)
  {
    VkClearAttachment *attachment = &attachments[attachment_count++];
    attachment->aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    attachment->colorAttachment = 0;
    attachment->clearValue.depthStencil.depth = 1.0f;
    attachment->clearValue.depthStencil.stencil = 0;
  }

  if (attachment_count == 0)
    return;

  VkClearRect clear_rect = {0};
  clear_rect.rect.offset.x = (i32)x;
  clear_rect.rect.offset.y = (i32)y;
  clear_rect.rect.extent.width = width;
  clear_rect.rect.extent.height = height;
  clear_rect.baseArrayLayer = 0;
  clear_rect.layerCount = 1;

  vkCmdClearAttachments(command_buffer->handle, attachment_count, attachments,
                        1, &clear_rect);
}

static void render_models(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                          EgnObject *object, const SceneCameraInfo *camera,
                          mat4 view_projection, VkDescriptorSet *last_texture)
{
  if (!object || !object->alive || !object->active)
    return;

  b8 layer_visible = object->layer < EGN_SCENE_MAX_LAYERS &&
                     (camera->camera->layer_mask & (1u << object->layer)) != 0;

  if (layer_visible)
  {
    mat4 world;
    b8 world_ready = false;

    for (EgnComponent *component = object->first_component; component; component = component->next)
    {
      if (!component->alive || !component->enabled ||
          component->type != EGN_COMPONENT_TYPE_MODEL)
        continue;

      if (!world_ready)
      {
        egn_object_get_world_matrix_internal(object, &world);
        world_ready = true;
      }

      EgnModelData *model = &component->data.model;
      vec4 tint = vec4_create(model->r, model->g, model->b, model->a);
      draw_mesh(ctx, command_buffer, &ctx->shader_3d, model->mesh, model->texture,
                world, view_projection, tint, model->bone_offset, last_texture);
    }
  }

  for (EgnObject *child = object->first_child; child; child = child->next_sibling)
    render_models(ctx, command_buffer, child, camera, view_projection, last_texture);
}

static void render_sprites(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                           EgnObject *object, const SceneCameraInfo *camera,
                           mat4 view_projection, VkDescriptorSet *last_texture)
{
  if (!object || !object->alive || !object->active)
    return;

  b8 layer_visible = object->layer < EGN_SCENE_MAX_LAYERS &&
                     (camera->camera->layer_mask & (1u << object->layer)) != 0;

  if (layer_visible)
  {
    mat4 world;
    b8 world_ready = false;

    for (EgnComponent *component = object->first_component; component; component = component->next)
    {
      if (!component->alive || !component->enabled ||
          component->type != EGN_COMPONENT_TYPE_SPRITE)
        continue;

      if (!world_ready)
      {
        egn_object_get_world_matrix_internal(object, &world);
        world_ready = true;
      }

      EgnSpriteData *sprite = &component->data.sprite;
      if (sprite->width <= 0.0f || sprite->height <= 0.0f)
        continue;

      f32 offset_x = (0.5f - sprite->pivot_x) * sprite->width;
      f32 offset_y = (0.5f - sprite->pivot_y) * sprite->height;

      mat4 local = mat4_mul(
          mat4_translation(vec3_create(offset_x, offset_y, 0.0f)),
          mat4_scale(vec3_create(sprite->width, sprite->height, 1.0f)));
      mat4 model = mat4_mul(world, local);

      vec4 tint = vec4_create(sprite->r, sprite->g, sprite->b, sprite->a);
      draw_mesh(ctx, command_buffer, &ctx->shader_sprite, ctx->sprite_mesh,
                sprite->texture, model, view_projection, tint, 0, last_texture);
    }
  }

  for (EgnObject *child = object->first_child; child; child = child->next_sibling)
    render_sprites(ctx, command_buffer, child, camera, view_projection, last_texture);
}

void vkrenderer_scene_render(VkContext *ctx, VkRendererCommandBuffer *command_buffer)
{
  EgnScene *scene = egn_scene_get();
  if (!scene || !scene->root)
    return;

  SceneCameraInfo cameras[VKRENDERER_MAX_CAMERAS];
  MEMOZERO(cameras, sizeof(cameras));

  u32 camera_count = 0;
  u32 sequence = 0;
  collect_cameras(scene->root, cameras, &camera_count, &sequence);

  if (camera_count == 0)
    return;

  sort_cameras(cameras, camera_count);

  for (u32 i = 0; i < camera_count; i++)
  {
    SceneCameraInfo *camera = &cameras[i];
    if (!camera->object || !camera->camera)
      continue;

    u32 x, y, width, height;
    if (!camera_viewport_pixels(camera->camera, ctx->framebuffer_width,
                                ctx->framebuffer_height, &x, &y, &width, &height))
      continue;

    VkViewport viewport;
    viewport.x = (f32)x;
    viewport.y = (f32)(y + height);
    viewport.width = (f32)width;
    viewport.height = -(f32)height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor;
    scissor.offset.x = (i32)x;
    scissor.offset.y = (i32)y;
    scissor.extent.width = width;
    scissor.extent.height = height;

    vkCmdSetViewport(command_buffer->handle, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer->handle, 0, 1, &scissor);

    clear_camera_area(command_buffer, camera->camera, x, y, width, height);

    mat4 view = camera_view_matrix(camera->object);
    f32 aspect = (f32)width / (f32)height;

    mat4 projection;
    if (camera->camera->projection == EGN_PROJECTION_ORTHOGRAPHIC)
    {
      f32 half_height = camera->camera->ortho_size * 0.5f;
      f32 half_width = half_height * aspect;
      projection = mat4_orthographic(-half_width, half_width,
                                     -half_height, half_height,
                                     camera->camera->near_clip,
                                     camera->camera->far_clip);
    }
    else
    {
      projection = mat4_perspective(deg_to_rad(camera->camera->fov_degrees),
                                    aspect,
                                    camera->camera->near_clip,
                                    camera->camera->far_clip);
    }

    mat4 view_projection = mat4_mul(projection, view);

    vkrenderer_shader_bind(ctx, &ctx->shader_3d, command_buffer->handle);

    VkDescriptorSet bone_set = vkrenderer_skin_descriptor(ctx);
    vkCmdBindDescriptorSets(command_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            ctx->shader_3d.pipeline.layout, 1, 1, &bone_set, 0, 0);

    VkDescriptorSet lighting_set = vkrenderer_lighting_descriptor(ctx);
    vkCmdBindDescriptorSets(command_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            ctx->shader_3d.pipeline.layout, 2, 1, &lighting_set, 0, 0);

    VkDescriptorSet last_texture = 0;
    render_models(ctx, command_buffer, scene->root, camera, view_projection, &last_texture);
    render_particles_recursive(ctx, command_buffer, scene->root, camera, view, view_projection);

    vkrenderer_shader_bind(ctx, &ctx->shader_sprite, command_buffer->handle);
    last_texture = 0;
    render_sprites(ctx, command_buffer, scene->root, camera, view_projection, &last_texture);
  }
}
