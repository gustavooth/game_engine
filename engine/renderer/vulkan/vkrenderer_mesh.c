#include "vkrenderer_mesh.h"
#include "vkrenderer_buffer.h"
#include "vkrenderer_descriptor.h"
#include "renderer/renderer.h"
#include "renderer/shapes.h"
#include "core/logger.h"
#include "core/memo.h"

static void destroy_submesh(VkContext *ctx, VkRendererSubMesh *submesh)
{
  if (!submesh)
    return;

  if (submesh->material_set)
    vkrenderer_material_descriptor_free(ctx, submesh->material_set);
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
                             const VkRendererMaterial *material,
                             VkRendererSubMesh *out_submesh)
{
  if (!ctx || !vertices || vertex_count == 0 || !out_submesh)
    return false;

  // An index past the vertex buffer makes the GPU read unrelated memory and
  // crashes the physics mesh collider, so reject malformed meshes up front
  // (this is the single entry point for OBJ, glTF and game-created meshes).
  for (u32 i = 0; indices && i < index_count; i++)
  {
    if (indices[i] >= vertex_count)
    {
      EGNERROR("VKRENDERER mesh index %u is out of range (%u vertices)", indices[i], vertex_count);
      return false;
    }
  }

  MEMOZERO(out_submesh, sizeof(VkRendererSubMesh));
  if (material)
    out_submesh->material = *material;
  else
  {
    out_submesh->material.base_color_factor = vec4_one();
    out_submesh->material.metallic_factor = 0.0f;
    out_submesh->material.roughness_factor = 1.0f;
  }

  vec3 bounds_min = vertices[0].position;
  vec3 bounds_max = vertices[0].position;
  for (u32 i = 1; i < vertex_count; i++)
  {
    vec3 p = vertices[i].position;
    bounds_min.x = EGNMIN(bounds_min.x, p.x);
    bounds_min.y = EGNMIN(bounds_min.y, p.y);
    bounds_min.z = EGNMIN(bounds_min.z, p.z);
    bounds_max.x = EGNMAX(bounds_max.x, p.x);
    bounds_max.y = EGNMAX(bounds_max.y, p.y);
    bounds_max.z = EGNMAX(bounds_max.z, p.z);
  }
  out_submesh->bounds_min = bounds_min;
  out_submesh->bounds_max = bounds_max;

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
  if (!vkrenderer_material_descriptor_create(ctx, &out_submesh->material,
                                             &out_submesh->material_set))
  {
    destroy_submesh(ctx, out_submesh);
    return false;
  }
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

    vec3 bounds_min = submeshes[0].bounds_min;
    vec3 bounds_max = submeshes[0].bounds_max;
    for (u32 s = 1; s < submesh_count; s++)
    {
      bounds_min.x = EGNMIN(bounds_min.x, submeshes[s].bounds_min.x);
      bounds_min.y = EGNMIN(bounds_min.y, submeshes[s].bounds_min.y);
      bounds_min.z = EGNMIN(bounds_min.z, submeshes[s].bounds_min.z);
      bounds_max.x = EGNMAX(bounds_max.x, submeshes[s].bounds_max.x);
      bounds_max.y = EGNMAX(bounds_max.y, submeshes[s].bounds_max.y);
      bounds_max.z = EGNMAX(bounds_max.z, submeshes[s].bounds_max.z);
    }
    mesh->bounds_center = vec3_mul_scalar(vec3_add(bounds_min, bounds_max), 0.5f);
    mesh->bounds_radius = vec3_length(vec3_sub(bounds_max, bounds_min)) * 0.5f;
    mesh->cullable = !(asset && asset->skinned) && mesh->bounds_radius > 0.0f;
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

EGNAPI void renderer_mesh_release(u32 mesh)
{
  VkContext *ctx = vkrenderer_get_context();
  if (!ctx)
    return;

  // The primitive handles are renderer-owned and live until shutdown.
  if (mesh == 0 || mesh == ctx->cube_mesh || mesh == ctx->sphere_mesh)
    return;

  // Meshes are usually released between frames (island regeneration), so wait
  // for in-flight command buffers before destroying the vertex/index buffers.
  vkDeviceWaitIdle(ctx->device.handle);
  vkrenderer_mesh_destroy(ctx, mesh);
}

static u32 create_primitive(VkContext *ctx, const MeshVertex *vertices,
                            u32 vertex_count, const u32 *indices,
                            u32 index_count)
{
  VkRendererSubMesh *submeshes = MEMOALLOC(sizeof(VkRendererSubMesh), MEMO_RENDERER);
  if (!submeshes)
    return 0;

  VkRendererMaterial material = {0};
  material.base_color_factor = vec4_one();
  material.roughness_factor = 1.0f;

  if (!vkrenderer_submesh_create(ctx, vertices, vertex_count, indices, index_count,
                                 &material, &submeshes[0]))
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

EGNAPI u32 renderer_mesh_create_static(const f32 *positions, const f32 *normals,
                                      const f32 *uvs, u32 vertex_count,
                                      const u32 *indices, u32 index_count)
{
  VkContext *ctx = vkrenderer_get_context();
  if (!ctx || !ctx->device.handle || !positions || !normals || !uvs || vertex_count == 0 ||
      !indices || index_count < 3)
    return 0;

  MeshVertex *vertices = MEMOALLOC(sizeof(MeshVertex) * (u64)vertex_count,
                                   MEMO_RENDERER);
  if (!vertices)
    return 0;

  for (u32 i = 0; i < vertex_count; i++)
  {
    MeshVertex *vertex = &vertices[i];
    vertex->position = vec3_create(positions[i * 3 + 0],
                                   positions[i * 3 + 1],
                                   positions[i * 3 + 2]);
    vertex->normal = vec3_create(normals[i * 3 + 0],
                                 normals[i * 3 + 1],
                                 normals[i * 3 + 2]);
    vertex->uv = vec2_create(uvs[i * 2 + 0], uvs[i * 2 + 1]);

    // Non-skinned default: every vertex is fully weighted to the identity bone.
    vertex->joints[0] = 0;
    vertex->joints[1] = 0;
    vertex->joints[2] = 0;
    vertex->joints[3] = 0;
    vertex->weights[0] = 1.0f;
    vertex->weights[1] = 0.0f;
    vertex->weights[2] = 0.0f;
    vertex->weights[3] = 0.0f;
  }

  u32 handle = create_primitive(ctx, vertices, vertex_count, indices, index_count);

  MEMOFREE(vertices);
  return handle;
}
