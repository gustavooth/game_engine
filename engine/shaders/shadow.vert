#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in uvec4 in_joints;
layout(location = 4) in vec4 in_weights;

layout(set = 0, binding = 0) readonly buffer BoneBuffer
{
  mat4 bones[];
} bone_buffer;

layout(push_constant) uniform PushConstants
{
  mat4 mvp;
  uint bone_offset;
} pc;

void main()
{
  mat4 skin =
      bone_buffer.bones[pc.bone_offset + in_joints.x] * in_weights.x +
      bone_buffer.bones[pc.bone_offset + in_joints.y] * in_weights.y +
      bone_buffer.bones[pc.bone_offset + in_joints.z] * in_weights.z +
      bone_buffer.bones[pc.bone_offset + in_joints.w] * in_weights.w;

  gl_Position = pc.mvp * skin * vec4(in_position, 1.0);
}
