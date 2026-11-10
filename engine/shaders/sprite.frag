#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_tint;

layout(set = 0, binding = 0) uniform sampler2D u_texture;

layout(location = 0) out vec4 out_colour;
layout(location = 1) out vec4 out_normal;

void main()
{
  out_colour = texture(u_texture, v_uv) * v_tint;
  out_normal = vec4(0.0);
}
