#include "vkrenderer_scene.h"
#include "renderer/renderer.h"
#include "renderer/renderer_settings.h"
#include "vkrenderer_skin.h"
#include "vkrenderer_lighting.h"
#include "vkrenderer_particles.h"
#include "vkrenderer_sky.h"
#include "renderer/atmosphere.h"
#include "shader/vkrenderer_shader.h"
#include "scene/scene_internal.h"
#include "core/logger.h"
#include "core/memo.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

#define VKRENDERER_MAX_CAMERAS 64

typedef struct ScenePushConstants
{
  mat4 mvp;
  mat4 model;
  vec4 tint;
  u32 bone_offset;
} ScenePushConstants;

typedef char ScenePushConstants_size_check[(sizeof(ScenePushConstants) == 148) ? 1 : -1];

typedef struct ModelPushConstants
{
  mat4 mvp;
  mat4 model;
  vec4 tint;
  u32 bone_offset;
  u32 padding[3]; // match std140 alignment of the following vec4s
  vec4 material;  // metallic, roughness, map flags, PBR enabled
  vec4 emissive;  // rgb, intensity
} ModelPushConstants;

typedef char ModelPushConstants_size_check[(sizeof(ModelPushConstants) == 192) ? 1 : -1];

typedef struct SceneCameraInfo
{
  EgnObject *object;
  EgnCameraData *camera;
  i32 domain;
  u32 width, height;
  i32 order;
  u32 sequence;
} SceneCameraInfo;

static b8 primary_camera_shadow_info(VkContext *ctx, mat4 *out_view, f32 *out_fov,
                                     f32 *out_aspect, f32 *out_near, f32 *out_far,
                                     vec3 *out_position);
static void compute_shadow_cascades(const RendererGraphicsSettings *settings,
                                    mat4 view, f32 fov, f32 aspect, f32 camera_near,
                                    f32 camera_far, u32 shadow_size, vec3 sun_direction,
                                    VkRendererLighting *lighting);

// ---------------------------------------------------------------------------
// Per-frame render list
//
// The scene hierarchy is walked once per frame. Every visible Model becomes a
// flat item carrying its world matrix and bounding sphere, so the shadow
// cascades and every camera reuse the same data (and can cull it) instead of
// re-walking the hierarchy and recomputing matrices for each pass.
// ---------------------------------------------------------------------------

typedef struct RenderItem
{
  mat4 world;
  vec3 center;  // world-space bounding sphere
  f32 radius;   // < 0 = never culled (skinned meshes)
  EgnModelData *model;
  i32 domain;
  u32 layer;
} RenderItem;

static RenderItem *render_items;
static u32 render_item_count;
static u32 render_item_capacity;

// Shadow volume of each cascade, used to cull casters on the CPU.
typedef struct CascadeVolume
{
  vec3 center;
  vec3 right;
  vec3 up;
  vec3 toward_sun;
  f32 radius;
  f32 pullback;
} CascadeVolume;

static CascadeVolume cascade_volumes[VKRENDERER_MAX_SHADOW_CASCADES];

typedef struct Frustum
{
  vec4 planes[6];
} Frustum;

static Frustum frustum_from_view_projection(mat4 m)
{
  // Gribb/Hartmann plane extraction for a [0, 1] clip-space depth range.
  Frustum frustum;
  const f32 *d = m.data;
  vec4 row0 = vec4_create(d[0], d[4], d[8], d[12]);
  vec4 row1 = vec4_create(d[1], d[5], d[9], d[13]);
  vec4 row2 = vec4_create(d[2], d[6], d[10], d[14]);
  vec4 row3 = vec4_create(d[3], d[7], d[11], d[15]);
  frustum.planes[0] = vec4_add(row3, row0);
  frustum.planes[1] = vec4_sub(row3, row0);
  frustum.planes[2] = vec4_add(row3, row1);
  frustum.planes[3] = vec4_sub(row3, row1);
  frustum.planes[4] = row2;
  frustum.planes[5] = vec4_sub(row3, row2);
  for (u32 i = 0; i < 6; i++)
  {
    vec4 *p = &frustum.planes[i];
    f32 length = sqrtf(p->x * p->x + p->y * p->y + p->z * p->z);
    if (length > EGN_FLOAT_EPSILON)
    {
      f32 inverse = 1.0f / length;
      p->x *= inverse;
      p->y *= inverse;
      p->z *= inverse;
      p->w *= inverse;
    }
  }
  return frustum;
}

static b8 frustum_sphere_visible(const Frustum *frustum, vec3 center, f32 radius)
{
  if (radius < 0.0f)
    return true;
  for (u32 i = 0; i < 6; i++)
  {
    const vec4 *p = &frustum->planes[i];
    if (p->x * center.x + p->y * center.y + p->z * center.z + p->w < -radius)
      return false;
  }
  return true;
}

static b8 cascade_sphere_visible(const CascadeVolume *volume, vec3 center, f32 radius)
{
  if (radius < 0.0f)
    return true;
  vec3 offset = vec3_sub(center, volume->center);
  f32 reach = volume->radius + radius;
  if (egnabs(vec3_dot(offset, volume->right)) > reach)
    return false;
  if (egnabs(vec3_dot(offset, volume->up)) > reach)
    return false;
  // Casters between the volume and the sun still shadow it.
  f32 along = vec3_dot(offset, volume->toward_sun);
  return along > -reach && along < volume->pullback + radius;
}

static b8 render_item_push(const RenderItem *item)
{
  if (render_item_count == render_item_capacity)
  {
    u32 capacity = render_item_capacity ? render_item_capacity * 2 : 256;
    RenderItem *items = MEMOALLOC(sizeof(RenderItem) * capacity, MEMO_RENDERER);
    if (!items)
      return false;
    if (render_items)
    {
      MEMOCPY(items, render_items, sizeof(RenderItem) * render_item_count);
      MEMOFREE(render_items);
    }
    render_items = items;
    render_item_capacity = capacity;
  }
  render_items[render_item_count++] = *item;
  return true;
}

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
                      f32 model_metallic, f32 model_roughness, b8 use_material,
                      b8 water, u32 surface, f32 pbr_enabled,
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

    VkRendererMaterial *material = &submesh->material;
    ScenePushConstants push = base_push;
    push.tint = vec4_mul(tint, material->base_color_factor);
    push.bone_offset = bone_offset;

    u32 resolved_texture = texture_handle != 0 ? texture_handle : material->base_color_texture;
    VkDescriptorSet texture = resolve_texture(ctx, resolved_texture);
    if (texture != *last_texture)
    {
      vkCmdBindDescriptorSets(command_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              shader->pipeline.layout, 0, 1, &texture, 0, 0);
      *last_texture = texture;
    }

    if (use_material)
    {
      u32 flags = 0;
      if (material->metallic_roughness_texture)
        flags |= 1u;
      if (material->normal_texture)
        flags |= 2u;
      if (material->occlusion_texture)
        flags |= 4u;
      if (material->emissive_texture)
        flags |= 8u;
      if (water || material->is_water)
        flags |= 16u;
      if (surface == EGN_MODEL_SURFACE_TERRAIN)
        flags |= 32u;
      else if (surface == EGN_MODEL_SURFACE_OCEAN)
        flags |= 64u;

      f32 metallic = material->has_pbr_factors ? material->metallic_factor : model_metallic;
      f32 roughness = material->has_pbr_factors ? material->roughness_factor : model_roughness;

      ModelPushConstants model_push;
      MEMOZERO(&model_push, sizeof(model_push));
      MEMOCPY(&model_push, &push, sizeof(push));
      model_push.material = vec4_create(metallic, roughness, (f32)flags, pbr_enabled);
      model_push.emissive = material->emissive_factor;

      if (submesh->material_set)
        vkCmdBindDescriptorSets(command_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                shader->pipeline.layout, 3, 1, &submesh->material_set, 0, 0);

      vkCmdPushConstants(command_buffer->handle, shader->pipeline.layout,
                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                         0, sizeof(ModelPushConstants), &model_push);
    }
    else
    {
      vkCmdPushConstants(command_buffer->handle, shader->pipeline.layout,
                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                         0, sizeof(ScenePushConstants), &push);
    }

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

// ---------------------------------------------------------------------------
// Renderer resources owned by scene components
//
// Components are destroyed by the scene, which knows nothing about the GPU.
// The renderer remembers which components it created resources for and
// releases them once the component dies. Without this, every destroyed
// Particles component kept one of the 32 GPU particle systems forever, and
// every destroyed skinned Model kept its animation buffers until shutdown.
// (Component memory itself stays valid until the scene shuts down, so
// checking `alive` on a tracked pointer is safe.)
// ---------------------------------------------------------------------------

typedef struct TrackedParticles
{
  EgnComponent *component;
  u32 system;
  u32 release_countdown; // frames to wait before freeing the GPU buffers
} TrackedParticles;

typedef struct TrackedAnimation
{
  EgnComponent *component;
  AnimationState *state;
} TrackedAnimation;

static TrackedParticles tracked_particles[VKRENDERER_MAX_PARTICLE_SYSTEMS];
static u32 tracked_particle_count;
static TrackedAnimation *tracked_animations;
static u32 tracked_animation_count;
static u32 tracked_animation_capacity;

static void track_particles(EgnComponent *component, u32 system)
{
  if (tracked_particle_count < VKRENDERER_MAX_PARTICLE_SYSTEMS)
  {
    TrackedParticles *entry = &tracked_particles[tracked_particle_count++];
    entry->component = component;
    entry->system = system;
    entry->release_countdown = 0;
  }
}

static void track_animation(EgnComponent *component, AnimationState *state)
{
  if (tracked_animation_count == tracked_animation_capacity)
  {
    u32 capacity = tracked_animation_capacity ? tracked_animation_capacity * 2 : 32;
    TrackedAnimation *entries = MEMOALLOC(sizeof(TrackedAnimation) * capacity, MEMO_RENDERER);
    if (!entries)
      return;
    if (tracked_animations)
    {
      MEMOCPY(entries, tracked_animations, sizeof(TrackedAnimation) * tracked_animation_count);
      MEMOFREE(tracked_animations);
    }
    tracked_animations = entries;
    tracked_animation_capacity = capacity;
  }
  tracked_animations[tracked_animation_count].component = component;
  tracked_animations[tracked_animation_count].state = state;
  tracked_animation_count++;
}

static void release_dead_component_resources(VkContext *ctx)
{
  for (u32 i = 0; i < tracked_particle_count;)
  {
    TrackedParticles *entry = &tracked_particles[i];
    if (entry->component->alive)
    {
      i++;
      continue;
    }

    // Frames still in flight may reference the buffers: stop using the
    // system now and free it once those frames have certainly completed.
    if (entry->release_countdown == 0)
    {
      entry->component->data.particles.system = 0;
      entry->release_countdown = VKRENDERER_BONE_FRAMES + 1;
    }
    if (--entry->release_countdown > 0)
    {
      i++;
      continue;
    }

    vkrenderer_particle_system_destroy(ctx, entry->system);
    tracked_particles[i] = tracked_particles[--tracked_particle_count];
  }

  for (u32 i = 0; i < tracked_animation_count;)
  {
    TrackedAnimation *entry = &tracked_animations[i];
    if (entry->component->alive)
    {
      i++;
      continue;
    }

    for (u32 s = 0; s < ctx->animation_state_count; s++)
    {
      if (ctx->animation_states[s] == entry->state)
      {
        ctx->animation_states[s] = ctx->animation_states[--ctx->animation_state_count];
        break;
      }
    }
    entry->component->data.model.anim_state = 0;
    animation_state_clear(entry->state);
    MEMOFREE(entry->state);
    tracked_animations[i] = tracked_animations[--tracked_animation_count];
  }
}

static void collect_models_recursive(VkContext *ctx, EgnObject *object)
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
    if (!mesh->used || !mesh->submeshes)
      continue;

    if (mesh->asset && mesh->asset->skinned)
    {
      const EgnMeshAsset *asset = mesh->asset;
      EgnComponent *animation_component = egn_object_get_component(object, EGN_COMPONENT_TYPE_ANIMATION);
      EgnAnimationData *animation = (animation_component && animation_component->alive && animation_component->enabled)
                                        ? &animation_component->data.animation
                                        : 0;

      b8 had_state = model->anim_state != 0;
      AnimationState *state = get_animation_state(ctx, model, asset);
      if (state && !had_state)
        track_animation(component, state);
      u32 bone_count = state ? evaluate_animation(asset, state, animation) : 0;
      u32 offset = bone_count ? vkrenderer_skin_allocate(ctx, bone_count) : 0;
      if (offset != 0)
      {
        vkrenderer_skin_write(ctx, offset, state->bone_matrices, bone_count);
        model->bone_offset = offset;
      }
    }

    RenderItem item;
    egn_object_get_world_matrix_internal(object, &item.world);
    item.model = model;
    item.domain = object->render_domain;
    item.layer = object->layer;
    item.radius = -1.0f;
    item.center = vec3_create(item.world.data[12], item.world.data[13], item.world.data[14]);

    if (mesh->cullable)
    {
      const f32 *w = item.world.data;
      vec3 c = mesh->bounds_center;
      item.center = vec3_create(w[0] * c.x + w[4] * c.y + w[8] * c.z + w[12],
                                w[1] * c.x + w[5] * c.y + w[9] * c.z + w[13],
                                w[2] * c.x + w[6] * c.y + w[10] * c.z + w[14]);
      f32 sx = w[0] * w[0] + w[1] * w[1] + w[2] * w[2];
      f32 sy = w[4] * w[4] + w[5] * w[5] + w[6] * w[6];
      f32 sz = w[8] * w[8] + w[9] * w[9] + w[10] * w[10];
      f32 max_scale = sqrtf(EGNMAX(sx, EGNMAX(sy, sz)));
      item.radius = mesh->bounds_radius * max_scale;
    }

    render_item_push(&item);
  }

  for (EgnObject *child = object->first_child; child; child = child->next_sibling)
    collect_models_recursive(ctx, child);
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
      // w = shadow strength. Shadows fully block the sun; what keeps them from
      // being black is the sky/ground ambient, as in reality.
      lighting->sun_color = vec4_create(light->r, light->g, light->b, 1.0f);
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
      track_particles(component, particles->system);
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

  b8 layer_visible = object->render_domain == camera->domain && object->layer < EGN_SCENE_MAX_LAYERS &&
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

// Primary 3D camera of the frame, resolved once in vkrenderer_scene_prepare().
static b8 primary_camera_valid;
static mat4 primary_view_projection;
static vec3 primary_position;
static f32 primary_projection_scale;
static f32 primary_aspect;

static vec3 vec3_lerp(vec3 a, vec3 b, f32 t)
{
  return vec3_add(a, vec3_mul_scalar(vec3_sub(b, a), t));
}

// Derives the sun colour, sky/ground ambient and fog colour from the same
// atmosphere model the sky shader renders, so the lighting always matches the
// visible sky (blue shade at noon, warm light and red horizon at sunset).
static void apply_atmosphere_lighting(const RendererGraphicsSettings *graphics,
                                      VkRendererLighting *lighting, b8 has_sun)
{
  const vec3 night = vec3_create(0.006f, 0.009f, 0.018f);
  vec3 sun_color = vec3_create(lighting->sun_color.x, lighting->sun_color.y, lighting->sun_color.z);
  f32 sun_intensity = lighting->sun_direction.w;

  vec3 zenith = night;
  vec3 horizon = night;
  vec3 ground_sun = vec3_zero();
  vkrenderer_sky_set_sun(has_sun ? vec3_mul_scalar(sun_color, sun_intensity) : vec3_zero());

  if (has_sun && sun_intensity > 0.0f)
  {
    AtmosphereParams atmosphere;
    atmosphere.sun_direction = vec3_normalized(vec3_create(lighting->sun_direction.x,
                                                           lighting->sun_direction.y,
                                                           lighting->sun_direction.z));
    atmosphere.sun_color = vec3_mul_scalar(sun_color, sun_intensity);
    atmosphere.rayleigh = graphics->atmosphere_rayleigh;
    atmosphere.mie = graphics->atmosphere_mie;
    atmosphere.turbidity = graphics->atmosphere_turbidity;
    atmosphere.mie_anisotropy = graphics->atmosphere_mie_anisotropy;
    atmosphere.sun_intensity = graphics->atmosphere_sun_intensity;
    atmosphere.altitude_km = lighting->camera_position.y * 0.001f;

    vec3 transmittance = atmosphere_sun_transmittance(&atmosphere, 12);
    // Fade the direct light out as the sun sinks below the horizon.
    f32 above = clampf(atmosphere.sun_direction.y * 12.0f + 0.6f, 0.0f, 1.0f);
    sun_color = vec3_mul_scalar(vec3_mul(sun_color, transmittance), above);
    lighting->sun_color.x = sun_color.x;
    lighting->sun_color.y = sun_color.y;
    lighting->sun_color.z = sun_color.z;

    zenith = vec3_add(zenith, atmosphere_sky_radiance(&atmosphere, vec3_up(), 10, 4));

    // Average the horizon ring so the ambient does not depend on the view.
    vec3 ring = vec3_zero();
    for (u32 i = 0; i < 6; i++)
    {
      f32 angle = (f32)i * (EGN_PI / 3.0f);
      vec3 direction = vec3_normalized(vec3_create(cosf(angle), 0.12f, sinf(angle)));
      ring = vec3_add(ring, atmosphere_sky_radiance(&atmosphere, direction, 10, 4));
    }
    horizon = vec3_add(horizon, vec3_mul_scalar(ring, 1.0f / 6.0f));

    ground_sun = vec3_mul_scalar(sun_color, sun_intensity * EGNMAX(atmosphere.sun_direction.y, 0.0f));
  }

  // Light bounced by the terrain: a neutral soil albedo lit by sun and sky.
  const vec3 ground_albedo = vec3_create(0.22f, 0.20f, 0.17f);
  // The bright horizon ring covers little solid angle and arrives at grazing
  // angles, so the zenith dominates the sky irradiance.
  vec3 sky_average = vec3_lerp(horizon, zenith, 0.65f);
  vec3 ground = vec3_mul(ground_albedo, vec3_add(ground_sun, sky_average));

  lighting->sky_zenith = vec4_create(zenith.x, zenith.y, zenith.z, 1.0f);
  lighting->sky_horizon = vec4_create(horizon.x, horizon.y, horizon.z, 1.0f);
  lighting->sky_ground = vec4_create(ground.x, ground.y, ground.z, 1.0f);
  lighting->ambient = vec4_create(sky_average.x, sky_average.y, sky_average.z, 1.0f);
  lighting->fog_color = vec4_create(horizon.x, horizon.y, horizon.z, 0.0f);
}

b8 vkrenderer_scene_prepare(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                            f64 delta_time)
{
  vkrenderer_skin_begin_frame(ctx);
  release_dead_component_resources(ctx);

  render_item_count = 0;

  EgnScene *scene = egn_scene_get();
  if (scene && scene->root)
  {
    collect_models_recursive(ctx, scene->root);
    prepare_particles_recursive(ctx, command_buffer, scene->root, (f32)delta_time);
  }

  VkRendererLighting lighting = {0};
  lighting.ambient = vec4_create(0.09f, 0.10f, 0.13f, 1.0f);
  lighting.sun_direction = vec4_create(0.0f, -1.0f, 0.0f, 0.0f);
  lighting.sun_color = vec4_create(0.0f, 0.0f, 0.0f, 1.0f);
  lighting.camera_position = vec4_create(0.0f, 0.0f, 0.0f, 0.0f);
  for (u32 cascade = 0; cascade < VKRENDERER_MAX_SHADOW_CASCADES; cascade++)
    lighting.sun_view_projection[cascade] = mat4_identity();
  lighting.cascade_splits = vec4_create(0.0f, 0.0f, 0.0f, 0.0f);
  lighting.point_count = vec4_create(0.0f, 0.0f, 0.0f, 0.0f);

  b8 has_sun = false;
  if (scene && scene->root)
    collect_lights_recursive(scene->root, &lighting, &has_sun);

  RendererGraphicsSettings graphics = renderer_get_active_graphics_settings();
  u32 shadow_size = renderer_graphics_settings_shadow_size(&graphics);
  u32 cascades = renderer_graphics_settings_cascades(&graphics);
  lighting.shadow_params = vec4_create(1.0f / (f32)shadow_size, 0.0f,
                                       (f32)cascades, graphics.shadow_softness);

  // The camera position feeds specular, fog and the sky, so it must be
  // resolved every frame (it used to be written only when shadows were on,
  // which left the view vector pointing at the world origin otherwise).
  mat4 view = mat4_identity();
  f32 fov = 1.0f, aspect = 1.0f, camera_near = 0.1f, camera_far = 100.0f;
  vec3 camera_position = vec3_zero();
  primary_camera_valid = primary_camera_shadow_info(ctx, &view, &fov, &aspect, &camera_near,
                                                    &camera_far, &camera_position);
  if (primary_camera_valid)
  {
    lighting.camera_position = vec4_create(camera_position.x, camera_position.y,
                                           camera_position.z, 1.0f);
    primary_position = camera_position;
    primary_projection_scale = 1.0f / egntan(fov * 0.5f);
    primary_aspect = aspect;
    primary_view_projection = mat4_mul(mat4_perspective(fov, aspect, camera_near, camera_far), view);
  }

  if (graphics.shadows_enabled && has_sun && lighting.point_count.y > 0.5f &&
      primary_camera_valid)
  {
    vec3 sun_direction = vec3_create(lighting.sun_direction.x,
                                     lighting.sun_direction.y,
                                     lighting.sun_direction.z);
    compute_shadow_cascades(&graphics, view, fov, aspect, camera_near, camera_far,
                            shadow_size, sun_direction, &lighting);
    lighting.shadow_params.y = 1.0f;
  }

  if (graphics.atmosphere_enabled)
  {
    apply_atmosphere_lighting(&graphics, &lighting, has_sun);
  }
  else
  {
    vec3 ambient = vec3_mul_scalar(vec3_create(lighting.ambient.x, lighting.ambient.y,
                                               lighting.ambient.z), 2.2f);
    lighting.sky_zenith = vec4_create(ambient.x, ambient.y, ambient.z, 1.0f);
    lighting.sky_horizon = lighting.sky_zenith;
    lighting.sky_ground = vec4_create(ambient.x * 0.5f, ambient.y * 0.5f, ambient.z * 0.5f, 1.0f);
    lighting.fog_color = vec4_create(ambient.x, ambient.y, ambient.z, 0.0f);
  }

  lighting.fog_params = vec4_create(graphics.fog_density, graphics.fog_height_falloff,
                                    graphics.fog_enabled ? 1.0f : 0.0f,
                                    graphics.atmosphere_enabled ? 1.0f : 0.0f);
  lighting.time = vec4_create(ctx->elapsed_time, 0.0f, 0.0f, 0.0f);

  vkrenderer_lighting_write(ctx, &lighting);
  vkrenderer_skin_barrier(ctx, command_buffer);

  return has_sun && lighting.shadow_params.y > 0.5f;
}

typedef struct ShadowPushConstants
{
  mat4 mvp;
  u32 bone_offset;
} ShadowPushConstants;

typedef char ShadowPushConstants_size_check[(sizeof(ShadowPushConstants) == 68) ? 1 : -1];

static void render_shadow_items(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                                const CascadeVolume *volume, mat4 light_view_projection)
{
  for (u32 index = 0; index < render_item_count; index++)
  {
    const RenderItem *item = &render_items[index];
    const EgnModelData *model = item->model;
    if (item->domain != EGN_RENDER_3D || !model->cast_shadows)
      continue;
    if (!cascade_sphere_visible(volume, item->center, item->radius))
      continue;

    VkRendererMesh *mesh = &ctx->meshes[model->mesh - 1];

    ShadowPushConstants push = {0};
    push.mvp = mat4_mul(light_view_projection, item->world);
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
}

void vkrenderer_scene_render_shadows(VkContext *ctx, VkRendererCommandBuffer *command_buffer)
{
  const VkRendererLighting *lighting = vkrenderer_lighting_current();
  if (!lighting || lighting->shadow_params.y < 0.5f)
    return;

  u32 size = vkrenderer_lighting_shadow_size(ctx);
  u32 cascade_count = (u32)(lighting->shadow_params.z + 0.5f);
  if (cascade_count < 1)
    cascade_count = 1;
  if (cascade_count > VKRENDERER_MAX_SHADOW_CASCADES)
    cascade_count = VKRENDERER_MAX_SHADOW_CASCADES;

  VkClearValue clear_value = {0};
  clear_value.depthStencil.depth = 1.0f;
  clear_value.depthStencil.stencil = 0;

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

  VkDescriptorSet bone_set = vkrenderer_skin_descriptor(ctx);

  // The render pass leaves each map in SHADER_READ_ONLY and its subpass
  // dependency already orders the depth writes before the sampling in the
  // scene pass, so no extra per-cascade barrier is needed.
  for (u32 cascade = 0; cascade < cascade_count; cascade++)
  {
    VkRenderPassBeginInfo begin_info = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin_info.renderPass = vkrenderer_lighting_shadow_renderpass(ctx);
    begin_info.framebuffer = vkrenderer_lighting_shadow_framebuffer(ctx, cascade);
    begin_info.renderArea.offset.x = 0;
    begin_info.renderArea.offset.y = 0;
    begin_info.renderArea.extent.width = size;
    begin_info.renderArea.extent.height = size;
    begin_info.clearValueCount = 1;
    begin_info.pClearValues = &clear_value;

    vkCmdBeginRenderPass(command_buffer->handle, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(command_buffer->handle, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer->handle, 0, 1, &scissor);
    vkCmdBindPipeline(command_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      ctx->shadow_shader.pipeline.handle);
    vkCmdBindDescriptorSets(command_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            ctx->shadow_shader.pipeline.layout, 0, 1, &bone_set, 0, 0);

    render_shadow_items(ctx, command_buffer, &cascade_volumes[cascade],
                        lighting->sun_view_projection[cascade]);

    vkCmdEndRenderPass(command_buffer->handle);
  }
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

  if (tracked_animations)
    MEMOFREE(tracked_animations);
  tracked_animations = 0;
  tracked_animation_count = 0;
  tracked_animation_capacity = 0;
  tracked_particle_count = 0;

  if (render_items)
    MEMOFREE(render_items);
  render_items = 0;
  render_item_count = 0;
  render_item_capacity = 0;
  primary_camera_valid = false;
}

static void collect_cameras(EgnObject *object, SceneCameraInfo *cameras,
                            u32 *camera_count, u32 *sequence)
{
  if (!object || !object->alive || !object->active)
    return;

  for (EgnComponent *component = object->first_component; component; component = component->next)
  {
    if (!component->alive || !component->enabled ||
        (component->type != EGN_COMPONENT_TYPE_CAMERA &&
         component->type != EGN_COMPONENT_TYPE_CAMERA_2D &&
         component->type != EGN_COMPONENT_TYPE_CANVAS))
      continue;

    if (*camera_count < VKRENDERER_MAX_CAMERAS)
    {
      SceneCameraInfo *info = &cameras[*camera_count];
      info->object = object;
      info->camera = &component->data.camera;
      info->domain = component->type == EGN_COMPONENT_TYPE_CANVAS ? EGN_RENDER_CANVAS :
                     component->type == EGN_COMPONENT_TYPE_CAMERA_2D ? EGN_RENDER_2D : EGN_RENDER_3D;
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

static b8 primary_camera_shadow_info(VkContext *ctx, mat4 *out_view, f32 *out_fov,
                                     f32 *out_aspect, f32 *out_near, f32 *out_far,
                                     vec3 *out_position)
{
  EgnScene *scene = egn_scene_get();
  if (!scene || !scene->root)
    return false;

  SceneCameraInfo cameras[VKRENDERER_MAX_CAMERAS];
  MEMOZERO(cameras, sizeof(cameras));
  u32 camera_count = 0;
  u32 sequence = 0;
  collect_cameras(scene->root, cameras, &camera_count, &sequence);
  sort_cameras(cameras, camera_count);

  for (u32 i = 0; i < camera_count; i++)
  {
    SceneCameraInfo *camera = &cameras[i];
    if (!camera->camera || camera->domain != EGN_RENDER_3D ||
        camera->camera->projection != EGN_PROJECTION_PERSPECTIVE)
      continue;

    u32 x, y, width, height;
    if (!camera_viewport_pixels(camera->camera, ctx->framebuffer_width,
                                ctx->framebuffer_height, &x, &y, &width, &height))
      continue;

    *out_view = camera_view_matrix(camera->object);
    *out_fov = deg_to_rad(camera->camera->fov_degrees);
    *out_aspect = (f32)width / (f32)height;
    *out_near = camera->camera->near_clip;
    *out_far = camera->camera->far_clip;
    *out_position = egn_object_get_world_position(camera->object);
    return true;
  }

  return false;
}

b8 vkrenderer_scene_primary_camera(VkContext *ctx, mat4 *out_view_projection,
                                   vec3 *out_position)
{
  (void)ctx;
  if (!primary_camera_valid)
    return false;
  *out_view_projection = primary_view_projection;
  *out_position = primary_position;
  return true;
}

b8 vkrenderer_scene_primary_projection(f32 *out_scale_y, f32 *out_aspect)
{
  if (!primary_camera_valid)
    return false;
  *out_scale_y = primary_projection_scale;
  *out_aspect = primary_aspect;
  return true;
}

static void compute_shadow_cascades(const RendererGraphicsSettings *settings,
                                    mat4 view, f32 fov, f32 aspect, f32 camera_near,
                                    f32 camera_far, u32 shadow_size, vec3 sun_direction,
                                    VkRendererLighting *lighting)
{
  if (!settings || !lighting)
    return;

  u32 cascade_count = renderer_graphics_settings_cascades(settings);
  if (cascade_count < 1)
    cascade_count = 1;
  if (cascade_count > VKRENDERER_MAX_SHADOW_CASCADES)
    cascade_count = VKRENDERER_MAX_SHADOW_CASCADES;

  f32 near_clip = camera_near > 0.05f ? camera_near : 0.05f;
  f32 far_clip = camera_far;
  if (settings->shadow_distance > near_clip && settings->shadow_distance < far_clip)
    far_clip = settings->shadow_distance;
  if (far_clip <= near_clip + 0.1f)
    far_clip = near_clip + 0.1f;

  // Practical split scheme. A higher lambda concentrates resolution close to
  // the camera, where shadow detail is actually visible.
  f32 splits[VKRENDERER_MAX_SHADOW_CASCADES];
  const f32 lambda = 0.82f;
  for (u32 i = 0; i < cascade_count; i++)
  {
    f32 p = (f32)(i + 1) / (f32)cascade_count;
    f32 logarithmic = near_clip * powf(far_clip / near_clip, p);
    f32 linear = near_clip + (far_clip - near_clip) * p;
    splits[i] = lambda * logarithmic + (1.0f - lambda) * linear;
    if (splits[i] > far_clip)
      splits[i] = far_clip;
  }

  sun_direction = vec3_normalized(sun_direction);

  // Light basis. It only depends on the sun direction, so the shadow texels
  // keep a fixed orientation in the world while the camera moves.
  vec3 reference_up = egnabs(sun_direction.y) > 0.99f ? vec3_create(0.0f, 0.0f, 1.0f) : vec3_up();
  vec3 light_right = vec3_normalized(vec3_cross(reference_up, sun_direction));
  vec3 light_up = vec3_cross(sun_direction, light_right);

  // Camera basis and frustum half extents at unit distance.
  mat4 camera_world = mat4_inverse(view);
  const f32 *cw = camera_world.data;
  vec3 camera_position = vec3_create(cw[12], cw[13], cw[14]);
  vec3 camera_forward = vec3_normalized(vec3_create(-cw[8], -cw[9], -cw[10]));
  f32 tan_y = egntan(fov * 0.5f);
  f32 tan_x = tan_y * aspect;

  // Distance kept between the cascade volume and the light so tall casters
  // outside the camera frustum still reach the shadow map.
  const f32 pullback = 150.0f;
  f32 texels[VKRENDERER_MAX_SHADOW_CASCADES] = {0.0f, 0.0f, 0.0f, 0.0f};

  for (u32 cascade = 0; cascade < cascade_count; cascade++)
  {
    f32 cascade_near = cascade == 0 ? near_clip : splits[cascade - 1];
    f32 cascade_far = splits[cascade];

    // Minimal bounding sphere of the frustum slice. Its radius does not
    // change when the camera rotates, which (together with the texel
    // snapping below) removes the shimmering of the shadow edges.
    f32 k = tan_x * tan_x + tan_y * tan_y;
    f32 center_distance;
    f32 radius;
    if (k >= (cascade_far - cascade_near) / (cascade_far + cascade_near))
    {
      center_distance = cascade_far;
      radius = cascade_far * sqrtf(k);
    }
    else
    {
      center_distance = 0.5f * (cascade_far + cascade_near) * (1.0f + k);
      radius = 0.5f * sqrtf((cascade_far - cascade_near) * (cascade_far - cascade_near) +
                            2.0f * (cascade_far * cascade_far + cascade_near * cascade_near) * k +
                            (cascade_far + cascade_near) * (cascade_far + cascade_near) * k * k);
    }
    radius = ceilf(radius * 16.0f) / 16.0f;
    if (radius < 0.5f)
      radius = 0.5f;

    vec3 center = vec3_add(camera_position, vec3_mul_scalar(camera_forward, center_distance));

    // Snap the volume to whole shadow texels in light space.
    f32 texel = (radius * 2.0f) / (f32)shadow_size;
    f32 along_right = floorf(vec3_dot(center, light_right) / texel) * texel;
    f32 along_up = floorf(vec3_dot(center, light_up) / texel) * texel;
    f32 along_sun = vec3_dot(center, sun_direction);
    center = vec3_add(vec3_add(vec3_mul_scalar(light_right, along_right),
                               vec3_mul_scalar(light_up, along_up)),
                      vec3_mul_scalar(sun_direction, along_sun));

    vec3 light_position = vec3_add(center, vec3_mul_scalar(sun_direction, radius + pullback));
    mat4 light_view = mat4_look_at(light_position, center, light_up);
    mat4 light_projection = mat4_orthographic(-radius, radius, -radius, radius,
                                              0.0f, radius * 2.0f + pullback);
    lighting->sun_view_projection[cascade] = mat4_mul(light_projection, light_view);

    CascadeVolume *volume = &cascade_volumes[cascade];
    volume->center = center;
    volume->right = light_right;
    volume->up = light_up;
    volume->toward_sun = sun_direction;
    volume->radius = radius;
    volume->pullback = radius + pullback;
    texels[cascade] = texel;
  }

  lighting->cascade_splits = vec4_create(
      splits[0],
      cascade_count > 1 ? splits[1] : splits[0],
      cascade_count > 2 ? splits[2] : splits[cascade_count - 1],
      cascade_count > 3 ? splits[3] : splits[cascade_count - 1]);
  lighting->cascade_texel = vec4_create(texels[0], texels[1], texels[2], texels[3]);
  lighting->shadow_fade = vec4_create(far_clip, far_clip * 0.12f, 0.12f, 0.0f);
  lighting->shadow_params.z = (f32)cascade_count;
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
                          const SceneCameraInfo *camera, mat4 view_projection,
                          f32 pbr_enabled, VkDescriptorSet *last_texture)
{
  Frustum frustum = frustum_from_view_projection(view_projection);
  u32 layer_mask = camera->camera->layer_mask;

  for (u32 index = 0; index < render_item_count; index++)
  {
    const RenderItem *item = &render_items[index];
    if (item->domain != camera->domain || item->layer >= EGN_SCENE_MAX_LAYERS ||
        (layer_mask & (1u << item->layer)) == 0)
      continue;
    if (!frustum_sphere_visible(&frustum, item->center, item->radius))
      continue;

    const EgnModelData *model = item->model;
    vec4 tint = vec4_create(model->r, model->g, model->b, model->a);
    draw_mesh(ctx, command_buffer, &ctx->shader_3d, model->mesh, model->texture,
              item->world, view_projection, tint, model->bone_offset,
              model->metallic, model->roughness, true, model->water, model->surface,
              pbr_enabled, last_texture);
  }
}

static void render_sprites(VkContext *ctx, VkRendererCommandBuffer *command_buffer,
                           EgnObject *object, const SceneCameraInfo *camera,
                           mat4 view_projection, VkDescriptorSet *last_texture)
{
  if (!object || !object->alive || !object->active)
    return;

  b8 layer_visible = object->render_domain == camera->domain && object->layer < EGN_SCENE_MAX_LAYERS &&
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
      if (camera->domain == EGN_RENDER_CANVAS)
      {
        f32 ax = ((object->canvas_anchor % 3) - 1) * (f32)camera->width * 0.5f;
        f32 ay = ((object->canvas_anchor / 3) - 1) * (f32)camera->height * 0.5f;
        model = mat4_mul(mat4_translation(vec3_create(ax, ay, 0)), model);
      }

      vec4 tint = vec4_create(sprite->r, sprite->g, sprite->b, sprite->a);
      draw_mesh(ctx, command_buffer, &ctx->shader_sprite, ctx->sprite_mesh,
                sprite->texture, model, view_projection, tint, 0,
                0.0f, 1.0f, false, false, EGN_MODEL_SURFACE_STANDARD, 0.0f, last_texture);
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

  RendererGraphicsSettings graphics = renderer_get_active_graphics_settings();

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

    camera->width = width;
    camera->height = height;
    mat4 view = camera->domain == EGN_RENDER_CANVAS ? mat4_identity() : camera_view_matrix(camera->object);
    f32 aspect = (f32)width / (f32)height;

    mat4 projection;
    if (camera->domain != EGN_RENDER_3D || camera->camera->projection == EGN_PROJECTION_ORTHOGRAPHIC)
    {
      f32 half_height = camera->domain == EGN_RENDER_CANVAS ? height * 0.5f : camera->camera->ortho_size * 0.5f;
      f32 half_width = half_height * aspect;
      projection = mat4_orthographic(-half_width, half_width,
                                     -half_height, half_height,
                                     camera->domain == EGN_RENDER_3D ? camera->camera->near_clip : -1000.0f,
                                     camera->domain == EGN_RENDER_3D ? camera->camera->far_clip : 1000.0f);
    }
    else
    {
      projection = mat4_perspective(deg_to_rad(camera->camera->fov_degrees),
                                    aspect,
                                    camera->camera->near_clip,
                                    camera->camera->far_clip);
    }

    mat4 view_projection = mat4_mul(projection, view);

    if (camera->domain == EGN_RENDER_3D && graphics.atmosphere_enabled)
      vkrenderer_sky_render(ctx, command_buffer, mat4_inverse(view_projection));

    vkrenderer_shader_bind(ctx, &ctx->shader_3d, command_buffer->handle);

    VkDescriptorSet bone_set = vkrenderer_skin_descriptor(ctx);
    vkCmdBindDescriptorSets(command_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            ctx->shader_3d.pipeline.layout, 1, 1, &bone_set, 0, 0);

    VkDescriptorSet lighting_set = vkrenderer_lighting_descriptor(ctx);
    vkCmdBindDescriptorSets(command_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            ctx->shader_3d.pipeline.layout, 2, 1, &lighting_set, 0, 0);

    VkDescriptorSet last_texture = 0;
    if (camera->domain == EGN_RENDER_3D) {
      render_models(ctx, command_buffer, camera, view_projection,
                    graphics.pbr_enabled ? 1.0f : 0.0f, &last_texture);
      render_particles_recursive(ctx, command_buffer, scene->root, camera, view, view_projection);
    }

    vkrenderer_shader_bind(ctx, &ctx->shader_sprite, command_buffer->handle);
    last_texture = 0;
    render_sprites(ctx, command_buffer, scene->root, camera, view_projection, &last_texture);
  }
}
