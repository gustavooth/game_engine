#pragma once

#include "vkrenderer_types.h"

/**
 * Creates GPU buffers for a mesh. When @p indices is NULL, the mesh is drawn
 * with vkCmdDraw instead of vkCmdDrawIndexed.
 *
 * @return A 1-based mesh handle, or 0 on failure.
 */
u32 vkrenderer_mesh_create(VkContext *ctx, const MeshVertex *vertices,
                           u32 vertex_count, const u32 *indices,
                           u32 index_count);

void vkrenderer_mesh_destroy(VkContext *ctx, u32 handle);

// Renderer-owned primitives. @return A 1-based mesh handle, or 0 on failure.
u32 vkrenderer_mesh_create_cube(VkContext *ctx);
u32 vkrenderer_mesh_create_sphere(VkContext *ctx, u32 sectors, u32 stacks);
u32 vkrenderer_mesh_create_quad(VkContext *ctx);
