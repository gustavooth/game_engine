#version 450

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec4 v_tint;

layout(set = 0, binding = 0) uniform sampler2D u_texture;

layout(location = 0) out vec4 out_colour;

void main()
{
  vec3 normal = normalize(v_normal);
  vec3 light_direction = normalize(vec3(0.45, 0.80, 0.55));
  float diffuse = max(dot(normal, light_direction), 0.0);
  float lighting = 0.28 + 0.72 * diffuse;

  vec4 base = texture(u_texture, v_uv) * v_tint;
  out_colour = vec4(base.rgb * lighting, base.a);
}
