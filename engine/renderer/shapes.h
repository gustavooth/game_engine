#pragma once

#include "renderer_types.h"

/**
 * Builds CPU-side primitive geometry.
 *
 * The returned arrays are allocated with MEMOALLOC(MEMO_RENDERER). The caller
 * owns them and must MEMOFREE() both when done (after uploading to the GPU).
 */
b8 shapes_build_cube(MeshVertex **out_vertices, u32 *out_vertex_count,
                     u32 **out_indices, u32 *out_index_count);

b8 shapes_build_sphere(u32 sectors, u32 stacks,
                       MeshVertex **out_vertices, u32 *out_vertex_count,
                       u32 **out_indices, u32 *out_index_count);

// Unit quad in the XY plane, centered at the origin, facing +Z. Used by
// sprites; the component size is applied through the object transform.
b8 shapes_build_quad(MeshVertex **out_vertices, u32 *out_vertex_count,
                     u32 **out_indices, u32 *out_index_count);
