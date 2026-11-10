#version 450

#define MAX_POINT_LIGHTS 8

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec4 v_tint;
layout(location = 3) in vec3 v_world_position;
layout(location = 4) in vec4 v_shadow_position;

layout(set = 0, binding = 0) uniform sampler2D u_texture;

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

layout(set = 2, binding = 1) uniform sampler2DShadow shadow_map;

layout(location = 0) out vec4 out_colour;

void main()
{
  vec4 base = texture(u_texture, v_uv) * v_tint;
  if (base.a <= 0.001)
    discard;

  vec3 normal = normalize(v_normal);
  vec3 to_sun = normalize(lighting.sun_direction.xyz);
  float sun_ndl = max(dot(normal, to_sun), 0.0);

  float shadow = 1.0;
  if (lighting.point_count.y > 0.5 && sun_ndl > 0.0)
  {
    vec3 projected = v_shadow_position.xyz / v_shadow_position.w;
    vec2 uv = projected.xy * 0.5 + 0.5;
    if (projected.z <= 1.0 && uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0)
    {
      vec2 texel = vec2(lighting.point_count.z);
      float bias = 0.0015;
      float sum = 0.0;
      int radius = int(lighting.point_count.w + 0.5);
      for (int y = -radius; y <= radius; y++)
      {
        for (int x = -radius; x <= radius; x++)
        {
          sum += texture(shadow_map, vec3(uv + vec2(x, y) * texel, projected.z - bias));
        }
      }
      float samples = float((radius * 2 + 1) * (radius * 2 + 1));
      shadow = mix(1.0, sum / samples, lighting.sun_colour.a);
    }
  }

  vec3 colour = base.rgb * (lighting.ambient.rgb * lighting.ambient.a +
                            lighting.sun_colour.rgb * lighting.sun_direction.w * sun_ndl * shadow);

  int light_count = int(lighting.point_count.x);
  for (int i = 0; i < MAX_POINT_LIGHTS; i++)
  {
    if (i >= light_count)
      break;

    vec3 to_light = lighting.point_lights[i].position.xyz - v_world_position;
    float distance_to_light = length(to_light);
    float range = lighting.point_lights[i].position.w;
    if (distance_to_light >= range)
      continue;

    vec3 light_dir = to_light / max(distance_to_light, 0.0001);
    float ndl = max(dot(normal, light_dir), 0.0);
    float attenuation = pow(clamp(1.0 - distance_to_light / range, 0.0, 1.0), 2.0);
    colour += base.rgb * lighting.point_lights[i].colour.rgb *
              lighting.point_lights[i].colour.w * ndl * attenuation;
  }

  out_colour = vec4(colour, base.a);
}
