#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_effects;

layout(set = 0, binding = 0) uniform sampler2D u_scene_colour;
layout(set = 0, binding = 1) uniform sampler2D u_scene_normal;
layout(set = 0, binding = 2) uniform sampler2D u_scene_depth;

layout(set = 0, binding = 3) uniform EffectsParams
{
  mat4 inverse_view_projection;
  mat4 view_projection;
  vec4 camera_position;
  vec4 params;  // ao radius, ao intensity, gi intensity, gi distance
  vec4 params2; // samples, width, height, flags
} pc;

float hash12(vec2 p)
{
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}

vec3 reconstruct_world(vec2 uv, float depth)
{
  vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
  vec4 world = pc.inverse_view_projection * vec4(ndc, depth, 1.0);
  return world.xyz / world.w;
}

void main()
{
  float depth = texture(u_scene_depth, v_uv).r;
  vec3 normal = texture(u_scene_normal, v_uv).xyz;
  if (depth >= 0.9999 || dot(normal, normal) < 0.01)
  {
    out_effects = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }

  vec3 world_position = reconstruct_world(v_uv, depth);
  vec3 camera_position = pc.camera_position.xyz;
  float radius = pc.params.x;
  int samples = int(pc.params2.x);
  float gi_distance = pc.params.w;
  bool ao_enabled = (int(pc.params2.w) & 1) != 0;
  bool gi_enabled = (int(pc.params2.w) & 2) != 0;

  float ao = 0.0;
  float ao_weight = 0.0;
  vec3 gi = vec3(0.0);
  float gi_weight = 0.0;

  for (int i = 0; i < 32; i++)
  {
    if (i >= samples)
      break;

    float angle = hash12(gl_FragCoord.xy + float(i) * 17.0) * 6.2831853;
    float z = hash12(gl_FragCoord.yx + float(i) * 31.0);
    vec3 random_direction = normalize(vec3(cos(angle) * sqrt(max(1.0 - z * z, 0.0)),
                                           sin(angle) * sqrt(max(1.0 - z * z, 0.0)),
                                           z));
    vec3 tangent = normalize(random_direction - normal * dot(random_direction, normal));
    vec3 bitangent = cross(normal, tangent);
    vec3 sample_direction = normalize(tangent * random_direction.x +
                                      bitangent * random_direction.y +
                                      normal * z);

    float scale = hash12(gl_FragCoord.xy + float(i) * 7.0);
    vec3 sample_position = world_position + sample_direction * radius * (0.2 + 0.8 * scale);
    vec4 clip = pc.view_projection * vec4(sample_position, 1.0);
    vec3 ndc = clip.xyz / max(clip.w, 0.0001);
    if (ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0 ||
        ndc.z < 0.0 || ndc.z > 1.0)
      continue;

    vec2 sample_uv = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    float sample_depth = texture(u_scene_depth, sample_uv).r;
    if (sample_depth >= 0.9999)
      continue;

    vec3 scene_position = reconstruct_world(sample_uv, sample_depth);
    float sample_distance = length(scene_position - camera_position);
    float point_distance = length(sample_position - camera_position);
    float bias = radius * 0.08;
    float weight = max(dot(normal, sample_direction), 0.0);

    if (ao_enabled)
    {
      ao += point_distance > sample_distance + bias ? weight : 0.0;
      ao_weight += weight;
    }

    if (gi_enabled)
    {
      float distance_to_surface = length(scene_position - world_position);
      if (distance_to_surface < gi_distance)
      {
        float falloff = 1.0 - clamp(distance_to_surface / gi_distance, 0.0, 1.0);
        gi += texture(u_scene_colour, sample_uv).rgb * weight * falloff;
        gi_weight += weight * falloff;
      }
    }
  }

  float occlusion = ao_enabled ? 1.0 - ao / max(ao_weight, 0.0001) : 1.0;
  vec3 indirect = gi_enabled && gi_weight > 0.0001
                      ? gi / gi_weight * 0.35
                      : vec3(0.0);
  out_effects = vec4(indirect, occlusion);
}
