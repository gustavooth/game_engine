#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;

layout(set = 0, binding = 2) uniform sampler2D particle_texture;

layout(location = 0) out vec4 out_colour;
layout(location = 1) out vec4 out_normal;

void main()
{
  vec4 texel = texture(particle_texture, v_uv);
  out_colour = texel * v_color;
  if (out_colour.a <= 0.001)
    discard;
  out_normal = vec4(0.0);
}
