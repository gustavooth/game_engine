#version 450

#define MAX_POINT_LIGHTS 8

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in uvec4 in_joints;
layout(location = 4) in vec4 in_weights;

layout(set = 1, binding = 0) readonly buffer BoneBuffer
{
  mat4 bones[];
} bone_buffer;

struct PointLight
{
  vec4 position;
  vec4 colour;
};

layout(set = 2, binding = 0) uniform LightingBlock
{
  vec4 ambient;
  vec4 sun_direction;
  vec4 sun_colour;
  mat4 sun_view_projection;
  vec4 point_count;
  PointLight point_lights[MAX_POINT_LIGHTS];
} lighting;

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
layout(location = 4) out vec4 v_shadow_position;

void main()
{
  mat4 skin =
      bone_buffer.bones[pc.bone_offset + in_joints.x] * in_weights.x +
      bone_buffer.bones[pc.bone_offset + in_joints.y] * in_weights.y +
      bone_buffer.bones[pc.bone_offset + in_joints.z] * in_weights.z +
      bone_buffer.bones[pc.bone_offset + in_joints.w] * in_weights.w;

  vec4 skinned_position = skin * vec4(in_position, 1.0);
  vec4 world_position = pc.model * skinned_position;

  mat3 normal_matrix = transpose(inverse(mat3(pc.model)));
  vec3 skinned_normal = mat3(skin) * in_normal;

  gl_Position = pc.mvp * skinned_position;
  v_normal = normal_matrix * skinned_normal;
  v_uv = in_uv;
  v_tint = pc.tint;
  v_world_position = world_position.xyz;
  v_shadow_position = lighting.sun_view_projection * world_position;
}
