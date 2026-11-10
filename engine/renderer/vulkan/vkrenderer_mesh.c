#include "vkrenderer_mesh.h"
#include "vkrenderer_buffer.h"
#include "renderer/shapes.h"
#include "core/logger.h"
#include "core/memo.h"

static void destroy_submesh(VkContext *ctx, VkRendererSubMesh *submesh)
{
  if (!submesh)
    return;

  vkrenderer_buffer_destroy(ctx, &submesh->vertex_buffer, &submesh->vertex_memory);
  vkrenderer_buffer_destroy(ctx, &submesh->index_buffer, &submesh->index_memory);
  MEMOZERO(submesh, sizeof(VkRendererSubMesh));
}

static void destroy_mesh_data(VkContext *ctx, VkRendererSubMesh *submeshes,
                              u32 submesh_count, EgnMeshAsset *asset)
{
  if (submeshes)
  {
    for (u32 i = 0; i < submesh_count; i++)
      destroy_submesh(ctx, &submeshes[i]);
    MEMOFREE(submeshes);
  }

  if (asset)
    egn_mesh_asset_destroy(asset);
}

b8 vkrenderer_submesh_create(VkContext *ctx,
                             const MeshVertex *vertices, u32 vertex_count,
                             const u32 *indices, u32 index_count,
                             u32 texture, vec4 base_color,
                             VkRendererSubMesh *out_submesh)
{
  if (!ctx || !vertices || vertex_count == 0 || !out_submesh)
    return false;

  MEMOZERO(out_submesh, sizeof(VkRendererSubMesh));

  u64 vertex_size = sizeof(MeshVertex) * (u64)vertex_count;
  if (!vkrenderer_buffer_create_device_local(ctx, vertices, vertex_size,
                                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                             &out_submesh->vertex_buffer,
                                             &out_submesh->vertex_memory))
    return false;

  if (indices && index_count > 0)
  {
    u64 index_size = sizeof(u32) * (u64)index_count;
    if (!vkrenderer_buffer_create_device_local(ctx, indices, index_size,
                                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                               &out_submesh->index_buffer,
                                               &out_submesh->index_memory))
    {
      destroy_submesh(ctx, out_submesh);
      return false;
    }
  }

  out_submesh->index_count = index_count;
  out_submesh->texture = texture;
  out_submesh->base_color = base_color;
  return true;
}

u32 vkrenderer_mesh_register(VkContext *ctx, VkRendererSubMesh *submeshes,
                             u32 submesh_count, EgnMeshAsset *asset)
{
  if (!ctx || !submeshes || submesh_count == 0)
  {
    destroy_mesh_data(ctx, submeshes, submesh_count, asset);
    return 0;
  }

  for (u32 i = 0; i < VKRENDERER_MAX_MESHES; i++)
  {
    if (ctx->meshes[i].used)
      continue;

    VkRendererMesh *mesh = &ctx->meshes[i];
    MEMOZERO(mesh, sizeof(VkRendererMesh));
    mesh->submeshes = submeshes;
    mesh->submesh_count = submesh_count;
    mesh->asset = asset;
    mesh->used = true;
    return i + 1;
  }

  EGNERROR("VKRENDERER mesh table is full");
  destroy_mesh_data(ctx, submeshes, submesh_count, asset);
  return 0;
}

void vkrenderer_mesh_destroy(VkContext *ctx, u32 handle)
{
  if (!ctx || handle == 0 || handle > VKRENDERER_MAX_MESHES)
    return;

  VkRendererMesh *mesh = &ctx->meshes[handle - 1];
  if (!mesh->used)
    return;

  destroy_mesh_data(ctx, mesh->submeshes, mesh->submesh_count, mesh->asset);
  MEMOZERO(mesh, sizeof(VkRendererMesh));
}

static u32 create_primitive(VkContext *ctx, const MeshVertex *vertices,
                            u32 vertex_count, const u32 *indices,
                            u32 index_count)
{
  VkRendererSubMesh *submeshes = MEMOALLOC(sizeof(VkRendererSubMesh), MEMO_RENDERER);
  if (!submeshes)
    return 0;

  if (!vkrenderer_submesh_create(ctx, vertices, vertex_count, indices, index_count,
                                 0, vec4_one(), &submeshes[0]))
  {
    MEMOFREE(submeshes);
    return 0;
  }

  return vkrenderer_mesh_register(ctx, submeshes, 1, 0);
}

u32 vkrenderer_mesh_create_cube(VkContext *ctx)
{
  MeshVertex *vertices = 0;
  u32 vertex_count = 0;
  u32 *indices = 0;
  u32 index_count = 0;

  if (!shapes_build_cube(&vertices, &vertex_count, &indices, &index_count))
    return 0;

  u32 handle = create_primitive(ctx, vertices, vertex_count, indices, index_count);

  MEMOFREE(vertices);
  MEMOFREE(indices);

  return handle;
}

u32 vkrenderer_mesh_create_sphere(VkContext *ctx, u32 sectors, u32 stacks)
{
  MeshVertex *vertices = 0;
  u32 vertex_count = 0;
  u32 *indices = 0;
  u32 index_count = 0;

  if (!shapes_build_sphere(sectors, stacks, &vertices, &vertex_count, &indices, &index_count))
    return 0;

  u32 handle = create_primitive(ctx, vertices, vertex_count, indices, index_count);

  MEMOFREE(vertices);
  MEMOFREE(indices);

  return handle;
}

u32 vkrenderer_mesh_create_quad(VkContext *ctx)
{
  MeshVertex *vertices = 0;
  u32 vertex_count = 0;
  u32 *indices = 0;
  u32 index_count = 0;

  if (!shapes_build_quad(&vertices, &vertex_count, &indices, &index_count))
    return 0;

  u32 handle = create_primitive(ctx, vertices, vertex_count, indices, index_count);

  MEMOFREE(vertices);
  MEMOFREE(indices);

  return handle;
}

EGNAPI u32 renderer_mesh_cube(void)
{
  VkContext *ctx = vkrenderer_get_context();
  return ctx ? ctx->cube_mesh : 0;
}

EGNAPI u32 renderer_mesh_sphere(void)
{
  VkContext *ctx = vkrenderer_get_context();
  return ctx ? ctx->sphere_mesh : 0;
}

EGNAPI u32 renderer_mesh_animation_count(u32 mesh_handle)
{
  VkContext *ctx = vkrenderer_get_context();
  if (!ctx || mesh_handle == 0 || mesh_handle > VKRENDERER_MAX_MESHES)
    return 0;
  VkRendererMesh *mesh = &ctx->meshes[mesh_handle - 1];
  if (!mesh->used || !mesh->asset)
    return 0;
  return mesh->asset->animation_count;
}

EGNAPI const char *renderer_mesh_animation_name(u32 mesh_handle, u32 index)
{
  VkContext *ctx = vkrenderer_get_context();
  if (!ctx || mesh_handle == 0 || mesh_handle > VKRENDERER_MAX_MESHES)
    return 0;
  VkRendererMesh *mesh = &ctx->meshes[mesh_handle - 1];
  if (!mesh->used || !mesh->asset || index >= mesh->asset->animation_count)
    return 0;
  return mesh->asset->animations[index].name;
}

EGNAPI f32 renderer_mesh_animation_duration(u32 mesh_handle, u32 index)
{
  VkContext *ctx = vkrenderer_get_context();
  if (!ctx || mesh_handle == 0 || mesh_handle > VKRENDERER_MAX_MESHES)
    return 0.0f;
  VkRendererMesh *mesh = &ctx->meshes[mesh_handle - 1];
  if (!mesh->used || !mesh->asset || index >= mesh->asset->animation_count)
    return 0.0f;
  return mesh->asset->animations[index].duration;
}

EGNAPI b8 renderer_mesh_get_collision(u32 mesh_handle, const vec3 **positions,
                                      u32 *position_count, const u32 **indices,
                                      u32 *index_count)
{
  VkContext *ctx = vkrenderer_get_context();
  if (!ctx || mesh_handle == 0 || mesh_handle > VKRENDERER_MAX_MESHES)
    return false;

  VkRendererMesh *mesh = &ctx->meshes[mesh_handle - 1];
  if (!mesh->used || !mesh->asset)
    return false;

  if (positions)
    *positions = mesh->asset->positions;
  if (position_count)
    *position_count = mesh->asset->position_count;
  if (indices)
    *indices = mesh->asset->indices;
  if (index_count)
    *index_count = mesh->asset->index_count;
  return true;
}
