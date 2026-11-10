#pragma once

#include "defines.h"
#include "core/asserts.h"
#include "math/math.h"

typedef struct RenderPacket
{
  f64 delta_time;
} RenderPacket;

/**
 * Vertex format shared by all 3D geometry (primitives and meshes loaded from
 * disk). It is tightly packed and mirrors the vertex input declared by the 3D
 * shader.
 */
typedef struct MeshVertex
{
  vec3 position;
  vec3 normal;
  vec2 uv;
  u16 joints[4];
  f32 weights[4];
} MeshVertex;
