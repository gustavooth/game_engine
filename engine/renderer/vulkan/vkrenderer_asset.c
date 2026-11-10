#include "vkrenderer_asset.h"
#include "vkrenderer_buffer.h"
#include "vkrenderer_mesh.h"
#include "core/logger.h"
#include "core/memo.h"
#include "platform/filemanager.h"
#include "renderer/renderer.h"

#include <string.h>

#define FAST_OBJ_IMPLEMENTATION
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wcast-qual"
#pragma clang diagnostic ignored "-Wcomma"
#include "fast_obj.h"
#pragma clang diagnostic pop

typedef struct ObjSubmeshBuild
{
  MeshVertex *vertices;
  u32 vertex_count;
  u32 vertex_cursor;

  u32 *indices;
  u32 index_count;
  u32 index_cursor;

  u32 texture;
  vec4 base_color;
} ObjSubmeshBuild;

void egn_mesh_asset_destroy(EgnMeshAsset *asset)
{
  if (!asset)
    return;

  if (asset->positions)
    MEMOFREE(asset->positions);
  if (asset->indices)
    MEMOFREE(asset->indices);
  if (asset->nodes)
    MEMOFREE(asset->nodes);

  if (asset->skins)
  {
    for (u32 i = 0; i < asset->skin_count; i++)
    {
      if (asset->skins[i].joints)
        MEMOFREE(asset->skins[i].joints);
      if (asset->skins[i].inverse_bind)
        MEMOFREE(asset->skins[i].inverse_bind);
    }
    MEMOFREE(asset->skins);
  }

  if (asset->animations)
  {
    for (u32 i = 0; i < asset->animation_count; i++)
    {
      EgnAnimation *animation = &asset->animations[i];
      if (animation->channels)
      {
        for (u32 j = 0; j < animation->channel_count; j++)
        {
          if (animation->channels[j].times)
            MEMOFREE(animation->channels[j].times);
          if (animation->channels[j].values)
            MEMOFREE(animation->channels[j].values);
        }
        MEMOFREE(animation->channels);
      }
    }
    MEMOFREE(asset->animations);
  }

  MEMOFREE(asset);
}

static vec3 obj_position(const fastObjMesh *obj, fastObjUInt index)
{
  return vec3_create(obj->positions[index * 3 + 0],
                     obj->positions[index * 3 + 1],
                     obj->positions[index * 3 + 2]);
}

static vec2 obj_texcoord(const fastObjMesh *obj, fastObjUInt index)
{
  if (index == 0)
    return vec2_zero();
  return vec2_create(obj->texcoords[index * 2 + 0],
                     obj->texcoords[index * 2 + 1]);
}

static vec3 obj_normal(const fastObjMesh *obj, fastObjUInt index)
{
  if (index == 0)
    return vec3_zero();
  return vec3_create(obj->normals[index * 3 + 0],
                     obj->normals[index * 3 + 1],
                     obj->normals[index * 3 + 2]);
}

static void obj_vertex(MeshVertex *vertex, vec3 position, vec3 normal, vec2 uv)
{
  vertex->position = position;
  vertex->normal = normal;
  vertex->uv = uv;
  vertex->joints[0] = 0;
  vertex->joints[1] = 0;
  vertex->joints[2] = 0;
  vertex->joints[3] = 0;
  vertex->weights[0] = 1.0f;
  vertex->weights[1] = 0.0f;
  vertex->weights[2] = 0.0f;
  vertex->weights[3] = 0.0f;
}

EGNAPI u32 renderer_mesh_load_obj(const char *path)
{
  if (!path)
    return 0;

  VkContext *ctx = vkrenderer_get_context();
  if (!ctx)
    return 0;

  char resolved_path[2048];
  if (!filemanager_resolve_path(path, resolved_path, sizeof(resolved_path)))
  {
    EGNERROR("renderer_mesh_load_obj: file not found '%s'", path);
    return 0;
  }

  fastObjMesh *obj = fast_obj_read(resolved_path);
  if (!obj)
  {
    EGNERROR("renderer_mesh_load_obj: failed to parse '%s'", path);
    return 0;
  }

  u32 material_count = obj->material_count > 0 ? obj->material_count : 1;
  u32 *triangle_counts = MEMOZALLOC(sizeof(u32) * material_count, MEMO_RENDERER);
  if (!triangle_counts)
  {
    fast_obj_destroy(obj);
    return 0;
  }

  u32 index_cursor = 0;
  for (u32 face = 0; face < obj->face_count; face++)
  {
    u32 vertex_count = obj->face_vertices[face];
    if (vertex_count >= 3)
    {
      u32 material = obj->face_materials[face];
      if (material >= material_count)
        material = 0;
      triangle_counts[material] += vertex_count - 2;
    }
    index_cursor += vertex_count;
  }

  ObjSubmeshBuild *builds = MEMOZALLOC(sizeof(ObjSubmeshBuild) * material_count, MEMO_RENDERER);
  if (!builds)
  {
    MEMOFREE(triangle_counts);
    fast_obj_destroy(obj);
    return 0;
  }

  u32 active_submeshes = 0;
  for (u32 material = 0; material < material_count; material++)
  {
    u32 triangles = triangle_counts[material];
    if (triangles == 0)
      continue;

    ObjSubmeshBuild *build = &builds[material];
    build->vertex_count = triangles * 3;
    build->index_count = triangles * 3;
    build->vertices = MEMOALLOC(sizeof(MeshVertex) * build->vertex_count, MEMO_RENDERER);
    build->indices = MEMOALLOC(sizeof(u32) * build->index_count, MEMO_RENDERER);
    if (!build->vertices || !build->indices)
    {
      EGNERROR("renderer_mesh_load_obj: out of memory");
      goto fail;
    }

    build->texture = 0;
    build->base_color = vec4_one();

    if (obj->material_count > 0)
    {
      fastObjMaterial *material_data = &obj->materials[material];
      f32 alpha = material_data->d > 0.0f ? material_data->d : 1.0f;
      if (material_data->Kd[0] != 0.0f || material_data->Kd[1] != 0.0f || material_data->Kd[2] != 0.0f)
        build->base_color = vec4_create(material_data->Kd[0], material_data->Kd[1],
                                        material_data->Kd[2], alpha);

      if (material_data->map_Kd > 0 && material_data->map_Kd < obj->texture_count)
      {
        fastObjTexture *texture = &obj->textures[material_data->map_Kd];
        if (texture->path)
          build->texture = renderer_texture_load(texture->path);
      }
    }

    active_submeshes++;
  }

  index_cursor = 0;
  for (u32 face = 0; face < obj->face_count; face++)
  {
    u32 corner_count = obj->face_vertices[face];
    u32 material = obj->face_materials[face];
    if (material >= material_count)
      material = 0;

    if (corner_count >= 3 && triangle_counts[material] > 0)
    {
      ObjSubmeshBuild *build = &builds[material];
      fastObjIndex *corners = &obj->indices[index_cursor];

      for (u32 i = 1; i + 1 < corner_count; i++)
      {
        fastObjIndex a = corners[0];
        fastObjIndex b = corners[i];
        fastObjIndex c = corners[i + 1];

        vec3 pa = obj_position(obj, a.p);
        vec3 pb = obj_position(obj, b.p);
        vec3 pc = obj_position(obj, c.p);

        vec3 face_normal = vec3_cross(vec3_sub(pb, pa), vec3_sub(pc, pa));
        if (vec3_length(face_normal) <= EGN_FLOAT_EPSILON)
          face_normal = vec3_up();
        face_normal = vec3_normalized(face_normal);

        vec3 na = a.n ? obj_normal(obj, a.n) : face_normal;
        vec3 nb = b.n ? obj_normal(obj, b.n) : face_normal;
        vec3 nc = c.n ? obj_normal(obj, c.n) : face_normal;

        u32 base_vertex = build->vertex_cursor;
        obj_vertex(&build->vertices[base_vertex + 0], pa, na, obj_texcoord(obj, a.t));
        obj_vertex(&build->vertices[base_vertex + 1], pb, nb, obj_texcoord(obj, b.t));
        obj_vertex(&build->vertices[base_vertex + 2], pc, nc, obj_texcoord(obj, c.t));
        build->vertex_cursor += 3;

        build->indices[build->index_cursor + 0] = base_vertex + 0;
        build->indices[build->index_cursor + 1] = base_vertex + 1;
        build->indices[build->index_cursor + 2] = base_vertex + 2;
        build->index_cursor += 3;
      }
    }

    index_cursor += corner_count;
  }

  VkRendererSubMesh *submeshes = MEMOALLOC(sizeof(VkRendererSubMesh) * active_submeshes, MEMO_RENDERER);
  EgnMeshAsset *asset = MEMOZALLOC(sizeof(EgnMeshAsset), MEMO_ASSET);
  if (active_submeshes == 0 || !submeshes || !asset)
  {
    if (submeshes)
      MEMOFREE(submeshes);
    if (asset)
      MEMOFREE(asset);
    goto fail;
  }

  u32 total_vertices = 0;
  u32 total_indices = 0;
  for (u32 material = 0; material < material_count; material++)
  {
    if (triangle_counts[material] == 0)
      continue;
    total_vertices += builds[material].vertex_count;
    total_indices += builds[material].index_count;
  }

  asset->positions = MEMOALLOC(sizeof(vec3) * total_vertices, MEMO_ASSET);
  asset->indices = MEMOALLOC(sizeof(u32) * total_indices, MEMO_ASSET);
  asset->position_count = total_vertices;
  asset->index_count = total_indices;
  asset->skinned = false;
  if (!asset->positions || !asset->indices)
  {
    MEMOFREE(submeshes);
    egn_mesh_asset_destroy(asset);
    goto fail;
  }

  u32 submesh_index = 0;
  u32 position_offset = 0;
  u32 index_offset = 0;
  for (u32 material = 0; material < material_count; material++)
  {
    if (triangle_counts[material] == 0)
      continue;

    ObjSubmeshBuild *build = &builds[material];
    if (!vkrenderer_submesh_create(ctx, build->vertices, build->vertex_count,
                                   build->indices, build->index_count,
                                   build->texture, build->base_color,
                                   &submeshes[submesh_index]))
    {
      for (u32 i = 0; i < submesh_index; i++)
      {
        vkrenderer_buffer_destroy(ctx, &submeshes[i].vertex_buffer, &submeshes[i].vertex_memory);
        vkrenderer_buffer_destroy(ctx, &submeshes[i].index_buffer, &submeshes[i].index_memory);
      }
      MEMOFREE(submeshes);
      egn_mesh_asset_destroy(asset);
      goto fail;
    }

    for (u32 i = 0; i < build->vertex_count; i++)
      asset->positions[position_offset + i] = build->vertices[i].position;
    for (u32 i = 0; i < build->index_count; i++)
      asset->indices[index_offset + i] = build->indices[i] + position_offset;

    position_offset += build->vertex_count;
    index_offset += build->index_count;
    submesh_index++;
  }

  u32 handle = vkrenderer_mesh_register(ctx, submeshes, active_submeshes, asset);

  for (u32 material = 0; material < material_count; material++)
  {
    if (builds[material].vertices)
      MEMOFREE(builds[material].vertices);
    if (builds[material].indices)
      MEMOFREE(builds[material].indices);
  }
  MEMOFREE(builds);
  MEMOFREE(triangle_counts);
  fast_obj_destroy(obj);

  if (handle)
    EGNINFO("VKRENDERER loaded OBJ '%s' (%u submeshes, %u triangles)",
            path, active_submeshes, total_indices / 3);
  return handle;

fail:
  for (u32 material = 0; material < material_count; material++)
  {
    if (builds[material].vertices)
      MEMOFREE(builds[material].vertices);
    if (builds[material].indices)
      MEMOFREE(builds[material].indices);
  }
  MEMOFREE(builds);
  MEMOFREE(triangle_counts);
  fast_obj_destroy(obj);
  return 0;
}
