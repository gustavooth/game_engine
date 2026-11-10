#include "vkrenderer_mesh.h"
#include "vkrenderer_buffer.h"
#include "renderer/shapes.h"
#include "core/logger.h"
#include "core/memo.h"

static VkRendererMesh *mesh_reserve_slot(VkContext *ctx)
{
  for (u32 i = 0; i < VKRENDERER_MAX_MESHES; i++)
  {
    if (!ctx->meshes[i].used)
    {
      MEMOZERO(&ctx->meshes[i], sizeof(VkRendererMesh));
      return &ctx->meshes[i];
    }
  }

  return 0;
}

u32 vkrenderer_mesh_create(VkContext *ctx, const MeshVertex *vertices,
                           u32 vertex_count, const u32 *indices,
                           u32 index_count)
{
  if (!ctx || !vertices || vertex_count == 0)
    return 0;

  VkRendererMesh *mesh = mesh_reserve_slot(ctx);
  if (!mesh)
  {
    EGNERROR("VKRENDERER mesh table is full");
    return 0;
  }

  u64 vertex_size = sizeof(MeshVertex) * (u64)vertex_count;
  if (!vkrenderer_buffer_create_device_local(ctx, vertices, vertex_size,
                                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                             &mesh->vertex_buffer,
                                             &mesh->vertex_memory))
    return 0;

  mesh->vertex_count = vertex_count;

  if (indices && index_count > 0)
  {
    u64 index_size = sizeof(u32) * (u64)index_count;
    if (!vkrenderer_buffer_create_device_local(ctx, indices, index_size,
                                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                               &mesh->index_buffer,
                                               &mesh->index_memory))
    {
      vkrenderer_buffer_destroy(ctx, &mesh->vertex_buffer, &mesh->vertex_memory);
      mesh->vertex_count = 0;
      return 0;
    }

    mesh->index_count = index_count;
  }

  mesh->used = true;

  u32 handle = (u32)(mesh - ctx->meshes) + 1;
  return handle;
}

void vkrenderer_mesh_destroy(VkContext *ctx, u32 handle)
{
  if (!ctx || handle == 0 || handle > VKRENDERER_MAX_MESHES)
    return;

  VkRendererMesh *mesh = &ctx->meshes[handle - 1];
  if (!mesh->used)
    return;

  vkrenderer_buffer_destroy(ctx, &mesh->vertex_buffer, &mesh->vertex_memory);
  vkrenderer_buffer_destroy(ctx, &mesh->index_buffer, &mesh->index_memory);
  MEMOZERO(mesh, sizeof(VkRendererMesh));
}

u32 vkrenderer_mesh_create_cube(VkContext *ctx)
{
  MeshVertex *vertices = 0;
  u32 vertex_count = 0;
  u32 *indices = 0;
  u32 index_count = 0;

  if (!shapes_build_cube(&vertices, &vertex_count, &indices, &index_count))
    return 0;

  u32 handle = vkrenderer_mesh_create(ctx, vertices, vertex_count, indices, index_count);

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

  u32 handle = vkrenderer_mesh_create(ctx, vertices, vertex_count, indices, index_count);

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

  u32 handle = vkrenderer_mesh_create(ctx, vertices, vertex_count, indices, index_count);

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
