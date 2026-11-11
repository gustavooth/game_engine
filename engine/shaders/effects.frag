#version 450

// Screen-space ambient occlusion + one bounce of screen-space global
// illumination, rendered at half resolution. rgb = bounced light, a = AO.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_effects;

layout(set = 0, binding = 0) uniform sampler2D u_scene_colour;
layout(set = 0, binding = 1) uniform sampler2D u_scene_normal;
layout(set = 0, binding = 2) uniform sampler2D u_scene_depth;

layout(push_constant) uniform EffectsParams
{
  mat4 inverse_view_projection;
  mat4 view_projection;
  vec4 camera_position;
  vec4 params;  // ao radius, ao intensity, gi intensity, gi distance
  vec4 params2; // samples, projection scale y, aspect, flags
} pc;

const float PI = 3.14159265;
const float GOLDEN_ANGLE = 2.39996323;

float interleaved_gradient_noise(vec2 pixel)
{
  return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

vec3 octahedral_decode(vec2 encoded)
{
  vec2 f = encoded * 2.0 - 1.0;
  vec3 n = vec3(f, 1.0 - abs(f.x) - abs(f.y));
  float t = clamp(-n.z, 0.0, 1.0);
  n.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
  return normalize(n);
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
  vec4 surface = texture(u_scene_normal, v_uv);
  if (depth >= 0.99999 || surface.a < 0.5)
  {
    out_effects = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }

  vec3 normal = octahedral_decode(surface.xy);
  vec3 position = reconstruct_world(v_uv, depth);
  float view_depth = max((pc.view_projection * vec4(position, 1.0)).w, 0.05);

  int flags = int(pc.params2.w);
  bool ao_enabled = (flags & 1) != 0;
  bool gi_enabled = (flags & 2) != 0;
  int samples = int(pc.params2.x);

  // World radii projected to UV space at this depth.
  vec2 projected = vec2(pc.params2.y / pc.params2.z, pc.params2.y) * (0.5 / view_depth);
  float ao_radius = pc.params.x;
  // Bounced light is gathered from a wider area than the contact occlusion.
  float gi_radius = min(pc.params.w, max(ao_radius * 3.0, 0.12 * view_depth / pc.params2.y * 4.0));
  vec2 ao_uv_radius = min(projected * ao_radius, vec2(0.12));
  vec2 gi_uv_radius = min(projected * gi_radius, vec2(0.30));

  float rotation = interleaved_gradient_noise(gl_FragCoord.xy) * 2.0 * PI;
  float bias = 0.02 + 0.0015 * view_depth;

  float occlusion = 0.0;
  vec3 bounce = vec3(0.0);
  int ao_samples = gi_enabled ? (ao_enabled ? (samples + 1) / 2 : 0) : samples;
  int gi_samples = gi_enabled ? samples - ao_samples : 0;

  for (int i = 0; i < 32; i++)
  {
    if (i >= ao_samples)
      break;
    // Spiral over the disc; sqrt keeps the samples uniform in area.
    float radius = sqrt((float(i) + 0.5) / float(ao_samples));
    float angle = float(i) * GOLDEN_ANGLE + rotation;
    vec2 sample_uv = v_uv + vec2(cos(angle), sin(angle)) * radius * ao_uv_radius;
    if (sample_uv.x <= 0.0 || sample_uv.x >= 1.0 || sample_uv.y <= 0.0 || sample_uv.y >= 1.0)
      continue;
    float sample_depth = textureLod(u_scene_depth, sample_uv, 0.0).r;
    if (sample_depth >= 0.99999)
      continue;

    // Obscurance estimator (McGuire et al.): each visible point occludes by
    // how far it rises above the tangent plane, falling off with distance so
    // geometry far behind a silhouette does not create dark halos.
    vec3 v = reconstruct_world(sample_uv, sample_depth) - position;
    float vv = dot(v, v);
    float vn = dot(v, normal);
    float range = max(1.0 - vv / (ao_radius * ao_radius), 0.0);
    occlusion += max(vn - bias, 0.0) / (vv + 0.0001) * range * range;
  }

  for (int i = 0; i < 32; i++)
  {
    if (i >= gi_samples)
      break;
    float radius = sqrt((float(i) + 0.5) / float(gi_samples));
    float angle = float(i) * GOLDEN_ANGLE + rotation + 1.7;
    vec2 sample_uv = v_uv + vec2(cos(angle), sin(angle)) * radius * gi_uv_radius;
    if (sample_uv.x <= 0.0 || sample_uv.x >= 1.0 || sample_uv.y <= 0.0 || sample_uv.y >= 1.0)
      continue;
    float sample_depth = textureLod(u_scene_depth, sample_uv, 0.0).r;
    vec4 sample_surface = textureLod(u_scene_normal, sample_uv, 0.0);
    if (sample_depth >= 0.99999 || sample_surface.a < 0.5)
      continue;

    vec3 v = reconstruct_world(sample_uv, sample_depth) - position;
    float distance_to_sample = length(v);
    if (distance_to_sample < 0.02 || distance_to_sample > pc.params.w)
      continue;
    vec3 direction = v / distance_to_sample;

    // Light leaves the sender toward the receiver and arrives above the
    // receiver's horizon; both cosines must be positive.
    float receive = max(dot(normal, direction), 0.0);
    float send = max(dot(octahedral_decode(sample_surface.xy), -direction), 0.0);
    float falloff = 1.0 - distance_to_sample / pc.params.w;
    vec3 radiance = min(textureLod(u_scene_colour, sample_uv, 0.0).rgb, vec3(6.0));
    bounce += radiance * (receive * send * falloff * falloff);
  }

  float ao = 1.0;
  if (ao_enabled && ao_samples > 0)
    ao = clamp(1.0 - occlusion * (2.0 * ao_radius / float(ao_samples)), 0.0, 1.0);
  vec3 indirect = gi_samples > 0 ? bounce * (2.0 / float(gi_samples)) : vec3(0.0);
  out_effects = vec4(indirect, ao);
}
