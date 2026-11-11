#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in uvec4 in_joints;
layout(location = 4) in vec4 in_weights;

layout(set = 1, binding = 0) readonly buffer BoneBuffer
{
  mat4 bones[];
} bone_buffer;

layout(push_constant) uniform PushConstants
{
  mat4 mvp;
  mat4 model;
  vec4 tint;
  uint bone_offset;
} pc;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec2 v_uv;
layout(location = 2) out vec4 v_tint;
layout(location = 3) out vec3 v_world_position;

void main()
{
  vec4 local_position = vec4(in_position, 1.0);
  vec3 local_normal = in_normal;

  // Static meshes (bone_offset 0) skip the four bone matrix fetches.
  if (pc.bone_offset != 0u)
  {
    mat4 skin =
        bone_buffer.bones[pc.bone_offset + in_joints.x] * in_weights.x +
        bone_buffer.bones[pc.bone_offset + in_joints.y] * in_weights.y +
        bone_buffer.bones[pc.bone_offset + in_joints.z] * in_weights.z +
        bone_buffer.bones[pc.bone_offset + in_joints.w] * in_weights.w;
    local_position = skin * local_position;
    local_normal = mat3(skin) * local_normal;
  }

  vec4 world_position = pc.model * local_position;

  // Normal matrix as the cofactor of the model matrix: the same direction as
  // transpose(inverse(M)) without a per-vertex matrix inversion, and it stays
  // correct under non-uniform scale.
  mat3 m = mat3(pc.model);
  mat3 normal_matrix = mat3(cross(m[1], m[2]), cross(m[2], m[0]), cross(m[0], m[1]));

  gl_Position = pc.mvp * local_position;
  v_normal = normal_matrix * local_normal;
  v_uv = in_uv;
  v_tint = pc.tint;
  v_world_position = world_position.xyz;
}
