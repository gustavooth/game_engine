#include "vkrenderer_asset.h"
#include "vkrenderer_buffer.h"
#include "vkrenderer_descriptor.h"
#include "vkrenderer_mesh.h"
#include "vkrenderer_texture.h"
#include "core/logger.h"
#include "core/memo.h"
#include "platform/filemanager.h"
#include "renderer/renderer.h"

#include <stdio.h>
#include <string.h>

#define CGLTF_IMPLEMENTATION
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wcast-qual"
#pragma clang diagnostic ignored "-Wcomma"
#include "cgltf.h"
#pragma clang diagnostic pop

typedef struct GltfSubmeshBuild
{
  MeshVertex *vertices;
  u32 vertex_count;
  u32 *indices;
  u32 index_count;
  VkRendererMaterial material;
} GltfSubmeshBuild;

static void copy_name(char *destination, const char *source)
{
  if (!source)
    source = "";
  snprintf(destination, EGN_ANIMATION_NAME_MAX, "%s", source);
}

static i32 find_node_index(const cgltf_data *data, const cgltf_node *node)
{
  if (!node)
    return -1;
  for (cgltf_size i = 0; i < data->nodes_count; i++)
  {
    if (&data->nodes[i] == node)
      return (i32)i;
  }
  return -1;
}

static i32 find_skin_index(const cgltf_data *data, const cgltf_skin *skin)
{
  if (!skin)
    return -1;
  for (cgltf_size i = 0; i < data->skins_count; i++)
  {
    if (&data->skins[i] == skin)
      return (i32)i;
  }
  return -1;
}

static void node_trs(const cgltf_node *node, vec3 *translation, quat *rotation, vec3 *scale)
{
  *translation = vec3_zero();
  *rotation = quat_identity();
  *scale = vec3_one();

  if (node->has_matrix)
  {
    mat4 matrix;
    MEMOCPY(matrix.data, node->matrix, sizeof(f32) * 16);

    *translation = vec3_create(matrix.data[12], matrix.data[13], matrix.data[14]);

    vec3 c0 = vec3_create(matrix.data[0], matrix.data[1], matrix.data[2]);
    vec3 c1 = vec3_create(matrix.data[4], matrix.data[5], matrix.data[6]);
    vec3 c2 = vec3_create(matrix.data[8], matrix.data[9], matrix.data[10]);
    f32 sx = vec3_length(c0);
    f32 sy = vec3_length(c1);
    f32 sz = vec3_length(c2);
    *scale = vec3_create(sx, sy, sz);

    if (sx > EGN_FLOAT_EPSILON && sy > EGN_FLOAT_EPSILON && sz > EGN_FLOAT_EPSILON)
    {
      mat4 rotation_matrix = mat4_identity();
      c0 = vec3_mul_scalar(c0, 1.0f / sx);
      c1 = vec3_mul_scalar(c1, 1.0f / sy);
      c2 = vec3_mul_scalar(c2, 1.0f / sz);
      rotation_matrix.data[0] = c0.x;
      rotation_matrix.data[1] = c0.y;
      rotation_matrix.data[2] = c0.z;
      rotation_matrix.data[4] = c1.x;
      rotation_matrix.data[5] = c1.y;
      rotation_matrix.data[6] = c1.z;
      rotation_matrix.data[8] = c2.x;
      rotation_matrix.data[9] = c2.y;
      rotation_matrix.data[10] = c2.z;
      *rotation = quat_from_mat4(rotation_matrix);
    }
  }
  else
  {
    if (node->has_translation)
      *translation = vec3_create(node->translation[0], node->translation[1], node->translation[2]);
    if (node->has_rotation)
      *rotation = (quat){node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]};
    if (node->has_scale)
      *scale = vec3_create(node->scale[0], node->scale[1], node->scale[2]);
  }
}

static mat4 node_local_matrix(const cgltf_node *node)
{
  vec3 translation;
  quat rotation;
  vec3 scale;
  node_trs(node, &translation, &rotation, &scale);
  return mat4_mul(mat4_translation(translation),
                  mat4_mul(quat_to_mat4(rotation), mat4_scale(scale)));
}

static mat4 node_world_matrix(const cgltf_node *node)
{
  mat4 world = node_local_matrix(node);
  const cgltf_node *parent = node->parent;
  while (parent)
  {
    world = mat4_mul(node_local_matrix(parent), world);
    parent = parent->parent;
  }
  return world;
}

static vec3 transform_point(mat4 matrix, vec3 point)
{
  vec4 p = vec4_from_vec3(point, 1.0f);
  vec4 result;
  result.x = matrix.data[0] * p.x + matrix.data[4] * p.y + matrix.data[8] * p.z + matrix.data[12] * p.w;
  result.y = matrix.data[1] * p.x + matrix.data[5] * p.y + matrix.data[9] * p.z + matrix.data[13] * p.w;
  result.z = matrix.data[2] * p.x + matrix.data[6] * p.y + matrix.data[10] * p.z + matrix.data[14] * p.w;
  result.w = matrix.data[3] * p.x + matrix.data[7] * p.y + matrix.data[11] * p.z + matrix.data[15] * p.w;
  if (egnabs(result.w) > EGN_FLOAT_EPSILON)
    return vec3_create(result.x / result.w, result.y / result.w, result.z / result.w);
  return vec3_create(result.x, result.y, result.z);
}

static vec3 transform_direction(mat4 matrix, vec3 direction)
{
  vec3 result;
  result.x = matrix.data[0] * direction.x + matrix.data[4] * direction.y + matrix.data[8] * direction.z;
  result.y = matrix.data[1] * direction.x + matrix.data[5] * direction.y + matrix.data[9] * direction.z;
  result.z = matrix.data[2] * direction.x + matrix.data[6] * direction.y + matrix.data[10] * direction.z;
  return result;
}

static const cgltf_accessor *find_attribute(const cgltf_primitive *primitive,
                                            cgltf_attribute_type type, u32 index)
{
  for (cgltf_size i = 0; i < primitive->attributes_count; i++)
  {
    const cgltf_attribute *attribute = &primitive->attributes[i];
    if (attribute->type == type && attribute->index == index)
      return attribute->data;
  }
  return 0;
}

static u32 load_gltf_texture(VkContext *ctx, const cgltf_texture_view *view,
                             const char *base_dir)
{
  if (!view || !view->texture || !view->texture->image)
    return 0;

  cgltf_image *image = view->texture->image;

  if (image->buffer_view && image->buffer_view->buffer && image->buffer_view->buffer->data)
  {
    const u8 *data = (const u8 *)image->buffer_view->buffer->data + image->buffer_view->offset;
    return vkrenderer_texture_register_from_memory(ctx, data, image->buffer_view->size);
  }

  if (image->uri && image->uri[0])
  {
    char path[2048];
    if (image->uri[0] == '/')
      snprintf(path, sizeof(path), "%s", image->uri);
    else
      snprintf(path, sizeof(path), "%s/%s", base_dir, image->uri);
    return renderer_texture_load(path);
  }

  return 0;
}

static void primitive_material(VkContext *ctx, const cgltf_primitive *primitive,
                               const char *base_dir, VkRendererMaterial *out_material)
{
  MEMOZERO(out_material, sizeof(*out_material));
  out_material->base_color_factor = vec4_one();
  out_material->metallic_factor = 1.0f;
  out_material->roughness_factor = 1.0f;

  if (!primitive->material)
    return;

  cgltf_material *material = primitive->material;
  if (material->has_pbr_metallic_roughness)
  {
    const cgltf_pbr_metallic_roughness *pbr = &material->pbr_metallic_roughness;
    out_material->base_color_factor = vec4_create(pbr->base_color_factor[0],
                                                  pbr->base_color_factor[1],
                                                  pbr->base_color_factor[2],
                                                  pbr->base_color_factor[3]);
    out_material->base_color_texture = load_gltf_texture(ctx, &pbr->base_color_texture, base_dir);
    out_material->metallic_roughness_texture = load_gltf_texture(ctx, &pbr->metallic_roughness_texture, base_dir);
    out_material->metallic_factor = pbr->metallic_factor;
    out_material->roughness_factor = pbr->roughness_factor;
    out_material->has_pbr_factors = true;
  }

  out_material->normal_texture = load_gltf_texture(ctx, &material->normal_texture, base_dir);
  out_material->occlusion_texture = load_gltf_texture(ctx, &material->occlusion_texture, base_dir);
  out_material->emissive_texture = load_gltf_texture(ctx, &material->emissive_texture, base_dir);
  out_material->emissive_factor = vec4_create(material->emissive_factor[0],
                                              material->emissive_factor[1],
                                              material->emissive_factor[2],
                                              1.0f);
}

static b8 build_gltf_mesh_data(VkContext *ctx, const cgltf_data *gltf,
                               const char *base_dir, EgnMeshAsset *asset,
                               VkRendererSubMesh **out_submeshes,
                               u32 *out_submesh_count)
{
  *out_submeshes = 0;
  *out_submesh_count = 0;

  u32 build_count = 0;
  for (cgltf_size mesh_index = 0; mesh_index < gltf->meshes_count; mesh_index++)
  {
    const cgltf_mesh *mesh = &gltf->meshes[mesh_index];
    for (cgltf_size primitive_index = 0; primitive_index < mesh->primitives_count; primitive_index++)
    {
      const cgltf_primitive *primitive = &mesh->primitives[primitive_index];
      if (primitive->type != cgltf_primitive_type_triangles)
        continue;
      if (!find_attribute(primitive, cgltf_attribute_type_position, 0))
        continue;
      build_count++;
    }
  }

  if (build_count == 0)
    return false;

  GltfSubmeshBuild *builds = MEMOZALLOC(sizeof(GltfSubmeshBuild) * build_count, MEMO_RENDERER);
  if (!builds)
    return false;

  u32 build_index = 0;
  i32 skin_index = -1;

  for (cgltf_size mesh_index = 0; mesh_index < gltf->meshes_count; mesh_index++)
  {
    const cgltf_mesh *mesh = &gltf->meshes[mesh_index];

    const cgltf_node *mesh_node = 0;
    for (cgltf_size node_index = 0; node_index < gltf->nodes_count; node_index++)
    {
      if (gltf->nodes[node_index].mesh == mesh)
      {
        mesh_node = &gltf->nodes[node_index];
        break;
      }
    }

    mat4 mesh_transform = mat4_identity();
    b8 mesh_skinned = false;
    i32 mesh_skin = -1;
    if (mesh_node)
    {
      mesh_transform = node_world_matrix(mesh_node);
      mesh_skinned = mesh_node->skin != 0;
      mesh_skin = find_skin_index(gltf, mesh_node->skin);
    }

    if (mesh_skinned && mesh_skin >= 0)
    {
      if (skin_index < 0)
        skin_index = mesh_skin;
      else if (skin_index != mesh_skin)
        EGNWARN("VKRENDERER GLB has multiple skins; using the first one");
    }

    for (cgltf_size primitive_index = 0; primitive_index < mesh->primitives_count; primitive_index++)
    {
      const cgltf_primitive *primitive = &mesh->primitives[primitive_index];
      if (primitive->type != cgltf_primitive_type_triangles)
        continue;

      const cgltf_accessor *position_accessor = find_attribute(primitive, cgltf_attribute_type_position, 0);
      if (!position_accessor)
        continue;

      const cgltf_accessor *normal_accessor = find_attribute(primitive, cgltf_attribute_type_normal, 0);
      const cgltf_accessor *uv_accessor = find_attribute(primitive, cgltf_attribute_type_texcoord, 0);
      const cgltf_accessor *joints_accessor = find_attribute(primitive, cgltf_attribute_type_joints, 0);
      const cgltf_accessor *weights_accessor = find_attribute(primitive, cgltf_attribute_type_weights, 0);

      GltfSubmeshBuild *build = &builds[build_index];
      build->vertex_count = (u32)position_accessor->count;
      build->vertices = MEMOALLOC(sizeof(MeshVertex) * build->vertex_count, MEMO_RENDERER);
      if (!build->vertices)
        goto fail;

      for (u32 vertex = 0; vertex < build->vertex_count; vertex++)
      {
        MeshVertex *output = &build->vertices[vertex];
        f32 position[4] = {0};
        f32 normal[4] = {0, 1, 0, 0};
        f32 uv[4] = {0};

        cgltf_accessor_read_float(position_accessor, vertex, position, 3);
        if (normal_accessor)
          cgltf_accessor_read_float(normal_accessor, vertex, normal, 3);
        if (uv_accessor)
          cgltf_accessor_read_float(uv_accessor, vertex, uv, 2);

        vec3 vertex_position = vec3_create(position[0], position[1], position[2]);
        vec3 vertex_normal = vec3_create(normal[0], normal[1], normal[2]);
        if (!mesh_skinned)
        {
          vertex_position = transform_point(mesh_transform, vertex_position);
          vertex_normal = vec3_normalized(transform_direction(mesh_transform, vertex_normal));
        }

        output->position = vertex_position;
        output->normal = vec3_length(vertex_normal) > EGN_FLOAT_EPSILON ? vec3_normalized(vertex_normal) : vec3_up();
        output->uv = vec2_create(uv[0], uv[1]);
        output->joints[0] = 0;
        output->joints[1] = 0;
        output->joints[2] = 0;
        output->joints[3] = 0;
        output->weights[0] = 1.0f;
        output->weights[1] = 0.0f;
        output->weights[2] = 0.0f;
        output->weights[3] = 0.0f;

        if (joints_accessor)
        {
          cgltf_uint joints[4] = {0};
          cgltf_accessor_read_uint(joints_accessor, vertex, joints, 4);
          output->joints[0] = (u16)joints[0];
          output->joints[1] = (u16)joints[1];
          output->joints[2] = (u16)joints[2];
          output->joints[3] = (u16)joints[3];
        }

        if (weights_accessor)
        {
          f32 weights[4] = {0};
          cgltf_accessor_read_float(weights_accessor, vertex, weights, 4);
          f32 sum = weights[0] + weights[1] + weights[2] + weights[3];
          if (sum > EGN_FLOAT_EPSILON)
          {
            output->weights[0] = weights[0] / sum;
            output->weights[1] = weights[1] / sum;
            output->weights[2] = weights[2] / sum;
            output->weights[3] = weights[3] / sum;
          }
        }
      }

      if (primitive->indices)
      {
        build->index_count = (u32)primitive->indices->count;
        build->indices = MEMOALLOC(sizeof(u32) * build->index_count, MEMO_RENDERER);
        if (!build->indices)
          goto fail;
        for (u32 index = 0; index < build->index_count; index++)
          build->indices[index] = (u32)cgltf_accessor_read_index(primitive->indices, index);
      }
      else
      {
        build->index_count = build->vertex_count;
        build->indices = MEMOALLOC(sizeof(u32) * build->index_count, MEMO_RENDERER);
        if (!build->indices)
          goto fail;
        for (u32 index = 0; index < build->index_count; index++)
          build->indices[index] = index;
      }

      primitive_material(ctx, primitive, base_dir, &build->material);
      build_index++;
    }
  }

  if (build_index == 0)
    goto fail;

  u32 total_vertices = 0;
  u32 total_indices = 0;
  for (u32 i = 0; i < build_index; i++)
  {
    total_vertices += builds[i].vertex_count;
    total_indices += builds[i].index_count;
  }

  asset->positions = MEMOALLOC(sizeof(vec3) * total_vertices, MEMO_ASSET);
  asset->indices = MEMOALLOC(sizeof(u32) * total_indices, MEMO_ASSET);
  asset->position_count = total_vertices;
  asset->index_count = total_indices;
  asset->skinned = skin_index >= 0;
  asset->skin_index = skin_index;
  if (!asset->positions || !asset->indices)
    goto fail;

  VkRendererSubMesh *submeshes = MEMOALLOC(sizeof(VkRendererSubMesh) * build_index, MEMO_RENDERER);
  if (!submeshes)
    goto fail;

  u32 position_offset = 0;
  u32 index_offset = 0;
  for (u32 i = 0; i < build_index; i++)
  {
    GltfSubmeshBuild *build = &builds[i];
    if (!vkrenderer_submesh_create(ctx, build->vertices, build->vertex_count,
                                   build->indices, build->index_count,
                                   &build->material, &submeshes[i]))
    {
      for (u32 j = 0; j < i; j++)
      {
        vkrenderer_material_descriptor_free(ctx, submeshes[j].material_set);
        vkrenderer_buffer_destroy(ctx, &submeshes[j].vertex_buffer, &submeshes[j].vertex_memory);
        vkrenderer_buffer_destroy(ctx, &submeshes[j].index_buffer, &submeshes[j].index_memory);
      }
      MEMOFREE(submeshes);
      goto fail;
    }

    for (u32 vertex = 0; vertex < build->vertex_count; vertex++)
      asset->positions[position_offset + vertex] = build->vertices[vertex].position;
    for (u32 index = 0; index < build->index_count; index++)
      asset->indices[index_offset + index] = build->indices[index] + position_offset;

    position_offset += build->vertex_count;
    index_offset += build->index_count;
  }

  *out_submeshes = submeshes;
  *out_submesh_count = build_index;

  for (u32 i = 0; i < build_index; i++)
  {
    if (builds[i].vertices)
      MEMOFREE(builds[i].vertices);
    if (builds[i].indices)
      MEMOFREE(builds[i].indices);
  }
  MEMOFREE(builds);
  return true;

fail:
  for (u32 i = 0; i < build_count; i++)
  {
    if (builds[i].vertices)
      MEMOFREE(builds[i].vertices);
    if (builds[i].indices)
      MEMOFREE(builds[i].indices);
  }
  MEMOFREE(builds);
  return false;
}

EGNAPI u32 renderer_mesh_load_glb(const char *path)
{
  if (!path)
    return 0;

  VkContext *ctx = vkrenderer_get_context();
  if (!ctx)
    return 0;

  char resolved_path[2048];
  if (!filemanager_resolve_path(path, resolved_path, sizeof(resolved_path)))
  {
    EGNERROR("renderer_mesh_load_glb: file not found '%s'", path);
    return 0;
  }

  u8 *file_data = 0;
  u64 file_size = 0;
  if (!filemanager_read_all(resolved_path, &file_data, &file_size))
  {
    EGNERROR("renderer_mesh_load_glb: failed to read '%s'", path);
    return 0;
  }

  cgltf_options options = {0};
  cgltf_data *gltf = 0;
  cgltf_result result = cgltf_parse(&options, file_data, (cgltf_size)file_size, &gltf);
  if (result != cgltf_result_success)
  {
    EGNERROR("renderer_mesh_load_glb: cgltf_parse failed (%d)", (int)result);
    MEMOFREE(file_data);
    return 0;
  }

  result = cgltf_load_buffers(&options, gltf, resolved_path);
  if (result != cgltf_result_success)
  {
    EGNERROR("renderer_mesh_load_glb: cgltf_load_buffers failed (%d)", (int)result);
    cgltf_free(gltf);
    MEMOFREE(file_data);
    return 0;
  }

  EgnMeshAsset *asset = MEMOZALLOC(sizeof(EgnMeshAsset), MEMO_ASSET);
  if (!asset)
  {
    cgltf_free(gltf);
    MEMOFREE(file_data);
    return 0;
  }

  asset->skin_index = -1;

  asset->node_count = (u32)gltf->nodes_count;
  if (asset->node_count > 0)
  {
    asset->nodes = MEMOALLOC(sizeof(EgnAnimationNode) * asset->node_count, MEMO_ASSET);
    if (!asset->nodes)
      goto fail;
    for (u32 i = 0; i < asset->node_count; i++)
    {
      const cgltf_node *node = &gltf->nodes[i];
      EgnAnimationNode *output = &asset->nodes[i];
      MEMOZERO(output, sizeof(EgnAnimationNode));
      copy_name(output->name, node->name);
      output->parent = find_node_index(gltf, node->parent);
      node_trs(node, &output->translation, &output->rotation, &output->scale);
    }
  }

  asset->skin_count = (u32)gltf->skins_count;
  if (asset->skin_count > 0)
  {
    asset->skins = MEMOZALLOC(sizeof(EgnSkin) * asset->skin_count, MEMO_ASSET);
    if (!asset->skins)
      goto fail;

    for (u32 i = 0; i < asset->skin_count; i++)
    {
      const cgltf_skin *skin = &gltf->skins[i];
      EgnSkin *output = &asset->skins[i];
      output->joint_count = (u32)skin->joints_count;
      output->joints = MEMOALLOC(sizeof(i32) * output->joint_count, MEMO_ASSET);
      output->inverse_bind = MEMOALLOC(sizeof(mat4) * output->joint_count, MEMO_ASSET);
      if (!output->joints || !output->inverse_bind)
        goto fail;

      for (u32 joint = 0; joint < output->joint_count; joint++)
      {
        output->joints[joint] = find_node_index(gltf, skin->joints[joint]);
        mat4 inverse_bind = mat4_identity();
        if (skin->inverse_bind_matrices && joint < skin->inverse_bind_matrices->count)
          cgltf_accessor_read_float(skin->inverse_bind_matrices, joint, inverse_bind.data, 16);
        output->inverse_bind[joint] = inverse_bind;
      }
    }
  }

  asset->animation_count = (u32)gltf->animations_count;
  if (asset->animation_count > 0)
  {
    asset->animations = MEMOZALLOC(sizeof(EgnAnimation) * asset->animation_count, MEMO_ASSET);
    if (!asset->animations)
      goto fail;

    for (u32 i = 0; i < asset->animation_count; i++)
    {
      const cgltf_animation *animation = &gltf->animations[i];
      EgnAnimation *output = &asset->animations[i];
      copy_name(output->name, animation->name ? animation->name : "Animation");
      output->duration = 0.0f;

      u32 channel_count = 0;
      for (cgltf_size c = 0; c < animation->channels_count; c++)
      {
        const cgltf_animation_channel *channel = &animation->channels[c];
        if (!channel->target_node || !channel->sampler ||
            !channel->sampler->input || !channel->sampler->output)
          continue;
        if (channel->target_path != cgltf_animation_path_type_translation &&
            channel->target_path != cgltf_animation_path_type_rotation &&
            channel->target_path != cgltf_animation_path_type_scale)
          continue;
        channel_count++;
      }

      output->channel_count = channel_count;
      output->channels = MEMOZALLOC(sizeof(EgnAnimationChannel) * channel_count, MEMO_ASSET);
      if (channel_count > 0 && !output->channels)
        goto fail;

      u32 channel_index = 0;
      for (cgltf_size c = 0; c < animation->channels_count; c++)
      {
        const cgltf_animation_channel *channel = &animation->channels[c];
        if (!channel->target_node || !channel->sampler ||
            !channel->sampler->input || !channel->sampler->output)
          continue;
        if (channel->target_path != cgltf_animation_path_type_translation &&
            channel->target_path != cgltf_animation_path_type_rotation &&
            channel->target_path != cgltf_animation_path_type_scale)
          continue;

        EgnAnimationChannel *out_channel = &output->channels[channel_index++];
        out_channel->node = find_node_index(gltf, channel->target_node);
        out_channel->path = channel->target_path == cgltf_animation_path_type_translation
                                ? EGN_ANIMATION_PATH_TRANSLATION
                                : (channel->target_path == cgltf_animation_path_type_rotation
                                       ? EGN_ANIMATION_PATH_ROTATION
                                       : EGN_ANIMATION_PATH_SCALE);
        out_channel->interpolation = channel->sampler->interpolation == cgltf_interpolation_type_step
                                         ? EGN_ANIMATION_INTERPOLATION_STEP
                                         : (channel->sampler->interpolation == cgltf_interpolation_type_cubic_spline
                                                ? EGN_ANIMATION_INTERPOLATION_CUBICSPLINE
                                                : EGN_ANIMATION_INTERPOLATION_LINEAR);

        u32 components = out_channel->path == EGN_ANIMATION_PATH_ROTATION ? 4 : 3;
        out_channel->time_count = (u32)channel->sampler->input->count;
        out_channel->times = MEMOALLOC(sizeof(f32) * out_channel->time_count, MEMO_ASSET);
        if (!out_channel->times)
          goto fail;
        for (u32 t = 0; t < out_channel->time_count; t++)
        {
          f32 time = 0.0f;
          cgltf_accessor_read_float(channel->sampler->input, t, &time, 1);
          out_channel->times[t] = time;
        }

        u32 value_elements = (u32)channel->sampler->output->count;
        out_channel->value_count = value_elements * components;
        out_channel->values = MEMOALLOC(sizeof(f32) * out_channel->value_count, MEMO_ASSET);
        if (!out_channel->values)
          goto fail;
        for (u32 v = 0; v < value_elements; v++)
          cgltf_accessor_read_float(channel->sampler->output, v,
                                    &out_channel->values[v * components], components);

        if (out_channel->time_count > 0)
        {
          f32 last_time = out_channel->times[out_channel->time_count - 1];
          if (last_time > output->duration)
            output->duration = last_time;
        }
      }
    }
  }

  VkRendererSubMesh *submeshes = 0;
  u32 submesh_count = 0;
  size_t base_length = strlen(resolved_path);
  char base_dir[2048];
  snprintf(base_dir, sizeof(base_dir), "%s", resolved_path);
  for (size_t i = base_length; i > 0; i--)
  {
    if (base_dir[i - 1] == '/')
    {
      base_dir[i - 1] = '\0';
      break;
    }
  }

  if (!build_gltf_mesh_data(ctx, gltf, base_dir, asset, &submeshes, &submesh_count))
    goto fail;

  u32 handle = vkrenderer_mesh_register(ctx, submeshes, submesh_count, asset);

  cgltf_free(gltf);
  MEMOFREE(file_data);

  if (handle)
  {
    vec3 bounds_min = vec3_zero();
    vec3 bounds_max = vec3_zero();
    if (asset->position_count > 0)
    {
      bounds_min = asset->positions[0];
      bounds_max = asset->positions[0];
      for (u32 i = 1; i < asset->position_count; i++)
      {
        vec3 p = asset->positions[i];
        bounds_min.x = p.x < bounds_min.x ? p.x : bounds_min.x;
        bounds_min.y = p.y < bounds_min.y ? p.y : bounds_min.y;
        bounds_min.z = p.z < bounds_min.z ? p.z : bounds_min.z;
        bounds_max.x = p.x > bounds_max.x ? p.x : bounds_max.x;
        bounds_max.y = p.y > bounds_max.y ? p.y : bounds_max.y;
        bounds_max.z = p.z > bounds_max.z ? p.z : bounds_max.z;
      }
    }
    EGNINFO("VKRENDERER loaded GLB '%s' (%u submeshes, %u animations, skin %d, bounds %.2f..%.2f / %.2f..%.2f / %.2f..%.2f)",
            path, submesh_count, asset->animation_count, asset->skin_index,
            bounds_min.x, bounds_max.x, bounds_min.y, bounds_max.y,
            bounds_min.z, bounds_max.z);
    for (u32 i = 0; i < asset->animation_count; i++)
      EGNINFO("VKRENDERER   animation %u: '%s' (%.2fs)", i,
              asset->animations[i].name, asset->animations[i].duration);
  }
  return handle;

fail:
  if (asset)
    egn_mesh_asset_destroy(asset);
  cgltf_free(gltf);
  MEMOFREE(file_data);
  return 0;
}
