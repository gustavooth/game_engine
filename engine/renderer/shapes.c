#include "shapes.h"
#include "core/memo.h"

static void vertex_set(MeshVertex *vertex, f32 x, f32 y, f32 z,
                       f32 nx, f32 ny, f32 nz, f32 u, f32 v)
{
  vertex->position = vec3_create(x, y, z);
  vertex->normal = vec3_create(nx, ny, nz);
  vertex->uv = vec2_create(u, v);

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

b8 shapes_build_cube(MeshVertex **out_vertices, u32 *out_vertex_count,
                     u32 **out_indices, u32 *out_index_count)
{
  if (!out_vertices || !out_vertex_count || !out_indices || !out_index_count)
    return false;

  const u32 face_count = 6;
  const u32 vertices_per_face = 4;
  const u32 indices_per_face = 6;
  const u32 vertex_count = face_count * vertices_per_face;
  const u32 index_count = face_count * indices_per_face;

  MeshVertex *vertices = MEMOALLOC(sizeof(MeshVertex) * vertex_count, MEMO_RENDERER);
  u32 *indices = MEMOALLOC(sizeof(u32) * index_count, MEMO_RENDERER);
  if (!vertices || !indices)
  {
    if (vertices)
      MEMOFREE(vertices);
    if (indices)
      MEMOFREE(indices);
    return false;
  }

  const f32 h = 0.5f;

  // Each face is described by its four corners (counter-clockwise seen from
  // outside) and its outward normal. UVs are laid out so the texture is
  // upright on every face.
  struct
  {
    f32 p[4][3];
    f32 n[3];
  } faces[6] = {
      // +Z (front)
      {{{-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}}, {0, 0, 1}},
      // -Z (back)
      {{{h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}}, {0, 0, -1}},
      // +X (right)
      {{{h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}}, {1, 0, 0}},
      // -X (left)
      {{{-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}}, {-1, 0, 0}},
      // +Y (top)
      {{{-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}}, {0, 1, 0}},
      // -Y (bottom)
      {{{-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}}, {0, -1, 0}},
  };

  const f32 uvs[4][2] = {{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};

  for (u32 face = 0; face < face_count; face++)
  {
    u32 base_vertex = face * vertices_per_face;
    for (u32 i = 0; i < vertices_per_face; i++)
    {
      vertex_set(&vertices[base_vertex + i],
                 faces[face].p[i][0], faces[face].p[i][1], faces[face].p[i][2],
                 faces[face].n[0], faces[face].n[1], faces[face].n[2],
                 uvs[i][0], uvs[i][1]);
    }

    u32 base_index = face * indices_per_face;
    indices[base_index + 0] = base_vertex + 0;
    indices[base_index + 1] = base_vertex + 1;
    indices[base_index + 2] = base_vertex + 2;
    indices[base_index + 3] = base_vertex + 2;
    indices[base_index + 4] = base_vertex + 3;
    indices[base_index + 5] = base_vertex + 0;
  }

  *out_vertices = vertices;
  *out_vertex_count = vertex_count;
  *out_indices = indices;
  *out_index_count = index_count;
  return true;
}

b8 shapes_build_sphere(u32 sectors, u32 stacks,
                       MeshVertex **out_vertices, u32 *out_vertex_count,
                       u32 **out_indices, u32 *out_index_count)
{
  if (!out_vertices || !out_vertex_count || !out_indices || !out_index_count)
    return false;

  if (sectors < 3)
    sectors = 3;
  if (stacks < 2)
    stacks = 2;

  const u32 vertex_count = (sectors + 1) * (stacks + 1);
  const u32 index_count = sectors * stacks * 6;

  MeshVertex *vertices = MEMOALLOC(sizeof(MeshVertex) * vertex_count, MEMO_RENDERER);
  u32 *indices = MEMOALLOC(sizeof(u32) * index_count, MEMO_RENDERER);
  if (!vertices || !indices)
  {
    if (vertices)
      MEMOFREE(vertices);
    if (indices)
      MEMOFREE(indices);
    return false;
  }

  const f32 radius = 0.5f;

  for (u32 stack = 0; stack <= stacks; stack++)
  {
    f32 v = (f32)stack / (f32)stacks;
    f32 phi = v * EGN_PI;
    f32 sin_phi = egnsin(phi);
    f32 cos_phi = egncos(phi);

    for (u32 sector = 0; sector <= sectors; sector++)
    {
      f32 u = (f32)sector / (f32)sectors;
      f32 theta = u * EGN_TWO_PI;

      f32 nx = egncos(theta) * sin_phi;
      f32 ny = cos_phi;
      f32 nz = egnsin(theta) * sin_phi;

      vertex_set(&vertices[stack * (sectors + 1) + sector],
                 nx * radius, ny * radius, nz * radius,
                 nx, ny, nz, u, v);
    }
  }

  u32 index = 0;
  for (u32 stack = 0; stack < stacks; stack++)
  {
    for (u32 sector = 0; sector < sectors; sector++)
    {
      u32 a = stack * (sectors + 1) + sector;
      u32 b = a + 1;
      u32 c = a + (sectors + 1);
      u32 d = c + 1;

      indices[index++] = a;
      indices[index++] = c;
      indices[index++] = b;
      indices[index++] = b;
      indices[index++] = c;
      indices[index++] = d;
    }
  }

  *out_vertices = vertices;
  *out_vertex_count = vertex_count;
  *out_indices = indices;
  *out_index_count = index_count;
  return true;
}

b8 shapes_build_quad(MeshVertex **out_vertices, u32 *out_vertex_count,
                     u32 **out_indices, u32 *out_index_count)
{
  if (!out_vertices || !out_vertex_count || !out_indices || !out_index_count)
    return false;

  const u32 vertex_count = 4;
  const u32 index_count = 6;

  MeshVertex *vertices = MEMOALLOC(sizeof(MeshVertex) * vertex_count, MEMO_RENDERER);
  u32 *indices = MEMOALLOC(sizeof(u32) * index_count, MEMO_RENDERER);
  if (!vertices || !indices)
  {
    if (vertices)
      MEMOFREE(vertices);
    if (indices)
      MEMOFREE(indices);
    return false;
  }

  vertex_set(&vertices[0], -0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f);
  vertex_set(&vertices[1], 0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
  vertex_set(&vertices[2], 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f);
  vertex_set(&vertices[3], -0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);

  indices[0] = 0;
  indices[1] = 1;
  indices[2] = 2;
  indices[3] = 2;
  indices[4] = 3;
  indices[5] = 0;

  *out_vertices = vertices;
  *out_vertex_count = vertex_count;
  *out_indices = indices;
  *out_index_count = index_count;
  return true;
}
