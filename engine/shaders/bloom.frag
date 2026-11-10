#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_colour;

layout(set = 0, binding = 0) uniform sampler2D u_source;

layout(push_constant) uniform BloomConstants
{
  vec4 params;  // direction xy, threshold, unused
  vec4 params2; // texel size xy, radius, apply threshold
} pc;

void main()
{
  vec3 colour = texture(u_source, v_uv).rgb;
  if (pc.params2.w > 0.5)
    colour = max(colour - pc.params.z, vec3(0.0));

  vec2 step = pc.params.xy * pc.params2.xy;
  int radius = int(pc.params2.z + 0.5);
  vec3 sum = colour;
  float total = 1.0;
  for (int i = 1; i <= 8; i++)
  {
    if (i > radius)
      break;
    float weight = 1.0 / (1.0 + float(i));
    sum += (texture(u_source, v_uv + step * float(i)).rgb +
            texture(u_source, v_uv - step * float(i)).rgb) * weight;
    total += 2.0 * weight;
  }

  out_colour = vec4(sum / total, 1.0);
}
