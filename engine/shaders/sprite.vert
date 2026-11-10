#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(push_constant) uniform PushConstants
{
  mat4 mvp;
  mat4 model;
  vec4 tint;
  uint bone_offset;
} pc;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_tint;

void main()
{
  gl_Position = pc.mvp * vec4(in_position, 1.0);
  v_uv = in_uv;
  v_tint = pc.tint;
}
