#pragma once

#include "vkrenderer_types.h"

/** Creates the GPU buffers for one submesh. */
b8 vkrenderer_submesh_create(VkContext *ctx,
                             const MeshVertex *vertices, u32 vertex_count,
                             const u32 *indices, u32 index_count,
                             const VkRendererMaterial *material,
                             VkRendererSubMesh *out_submesh);

/**
 * Registers a mesh in the renderer table. Takes ownership of @p submeshes and
 * @p asset (both are freed if registration fails).
 *
 * @return A 1-based mesh handle, or 0 on failure.
 */
u32 vkrenderer_mesh_register(VkContext *ctx, VkRendererSubMesh *submeshes,
                             u32 submesh_count, EgnMeshAsset *asset);

void vkrenderer_mesh_destroy(VkContext *ctx, u32 handle);

// Renderer-owned primitives. @return A 1-based mesh handle, or 0 on failure.
u32 vkrenderer_mesh_create_cube(VkContext *ctx);
u32 vkrenderer_mesh_create_sphere(VkContext *ctx, u32 sectors, u32 stacks);
u32 vkrenderer_mesh_create_quad(VkContext *ctx);
