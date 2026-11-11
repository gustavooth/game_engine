#version 450

#define MAX_POINT_LIGHTS 8
#define MAX_SHADOW_CASCADES 4

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec4 v_tint;
layout(location = 3) in vec3 v_world_position;

layout(set = 0, binding = 0) uniform sampler2D u_texture;

// PBR maps: metallic-roughness, normal, occlusion, emissive.
layout(set = 3, binding = 0) uniform sampler2D u_metallic_roughness;
layout(set = 3, binding = 1) uniform sampler2D u_normal;
layout(set = 3, binding = 2) uniform sampler2D u_occlusion;
layout(set = 3, binding = 3) uniform sampler2D u_emissive;

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
  vec4 camera_position;
  mat4 sun_view_projection[MAX_SHADOW_CASCADES];
  vec4 cascade_splits;
  vec4 shadow_params;  // texel size, enabled, cascade count, softness
  vec4 point_count;
  vec4 fog_params;     // density, height falloff, enabled, atmosphere enabled
  vec4 fog_color;
  vec4 time;
  vec4 cascade_texel;  // world size of one shadow texel per cascade
  vec4 shadow_fade;    // shadow distance, fade length, cascade blend fraction
  vec4 sky_zenith;
  vec4 sky_horizon;
  vec4 sky_ground;
  PointLight point_lights[MAX_POINT_LIGHTS];
} lighting;

layout(set = 2, binding = 1) uniform sampler2DShadow shadow_map_0;
layout(set = 2, binding = 2) uniform sampler2DShadow shadow_map_1;
layout(set = 2, binding = 3) uniform sampler2DShadow shadow_map_2;
layout(set = 2, binding = 4) uniform sampler2DShadow shadow_map_3;
layout(set = 2, binding = 5) uniform sampler2D u_sky_lut;

layout(push_constant) uniform PushConstants
{
  mat4 mvp;
  mat4 model;
  vec4 tint;
  uint bone_offset;
  vec4 material; // metallic, roughness, map flags, PBR enabled
  vec4 emissive; // rgb, intensity
} pc;

layout(location = 0) out vec4 out_colour;
// xy = octahedral world normal, z = albedo luminance, w = 1 for lit geometry.
layout(location = 1) out vec4 out_normal;

const float PI = 3.14159265;

vec3 to_linear(vec3 colour)
{
  return pow(max(colour, vec3(0.0)), vec3(2.2));
}

float luminance(vec3 colour)
{
  return dot(colour, vec3(0.2126, 0.7152, 0.0722));
}

vec2 octahedral_encode(vec3 n)
{
  n /= abs(n.x) + abs(n.y) + abs(n.z);
  vec2 wrapped = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
  return (n.z >= 0.0 ? n.xy : wrapped) * 0.5 + 0.5;
}

// Interleaved gradient noise: a stable per-pixel value with a blue-noise-like
// spectrum, which the screen-space blur and bloom hide far better than white
// noise.
float interleaved_gradient_noise(vec2 pixel)
{
  return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// ---------------------------------------------------------------------------
// Shadows
// ---------------------------------------------------------------------------

const vec2 POISSON[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870), vec2(0.34495938, 0.29387760),
    vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379),
    vec2(0.44323325, -0.97511554), vec2(0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590),
    vec2(0.19984126, 0.78641367), vec2(0.14383161, -0.14100790));

float sample_shadow(int cascade, vec3 coord)
{
  if (cascade == 0)
    return texture(shadow_map_0, coord);
  if (cascade == 1)
    return texture(shadow_map_1, coord);
  if (cascade == 2)
    return texture(shadow_map_2, coord);
  return texture(shadow_map_3, coord);
}

// Light-space coordinates of a cascade; xy in [0, 1] inside the map.
vec3 cascade_coord(int cascade, vec3 position)
{
  vec4 clip = lighting.sun_view_projection[cascade] * vec4(position, 1.0);
  return vec3(clip.xy * 0.5 + 0.5, clip.z);
}

float filter_cascade(int cascade, vec3 coord, mat2 rotation)
{
  if (coord.z >= 1.0)
    return 1.0;

  // Keep the penumbra the same size in world units on every cascade.
  float texel = lighting.shadow_params.x;
  float world_texel = max(lighting.cascade_texel[cascade], 0.0001);
  float softness = lighting.shadow_params.w;
  float radius = clamp(softness * 0.035 / world_texel, 1.0, 5.0) * texel;

  // Four taps on the outside of the kernel first: when they agree the pixel
  // is fully lit or fully shadowed and the remaining taps are skipped. Only
  // penumbra pixels pay for the full kernel.
  float sum = 0.0;
  for (int i = 0; i < 4; i++)
    sum += sample_shadow(cascade, vec3(coord.xy + rotation * POISSON[i] * radius, coord.z));
  if (sum <= 0.0 || sum >= 4.0)
    return sum * 0.25;

  int taps = softness < 0.8 ? 8 : (softness < 1.2 ? 12 : 16);
  for (int i = 4; i < taps; i++)
    sum += sample_shadow(cascade, vec3(coord.xy + rotation * POISSON[i] * radius, coord.z));
  return sum / float(taps);
}

float sun_shadow(vec3 position, vec3 normal, float ndl)
{
  int cascade_count = int(lighting.shadow_params.z + 0.5);
  float view_distance = length(position - lighting.camera_position.xyz);
  float fade_start = lighting.shadow_fade.x - lighting.shadow_fade.y;
  if (view_distance >= lighting.shadow_fade.x)
    return 1.0;

  float angle = interleaved_gradient_noise(gl_FragCoord.xy) * 2.0 * PI;
  float s = sin(angle);
  float c = cos(angle);
  mat2 rotation = mat2(c, s, -s, c);

  // Pick the most detailed cascade that contains the pixel.
  float slope = 1.0 - ndl;
  int cascade = cascade_count - 1;
  vec3 coord = vec3(0.0);
  float edge = 1.0;
  for (int i = 0; i < cascade_count; i++)
  {
    // Normal offset: moves the lookup off the surface by about one texel,
    // more at grazing angles, which removes acne without a depth bias that
    // would detach the shadow from its caster.
    vec3 offset = normal * lighting.cascade_texel[i] * (1.0 + 2.5 * slope);
    vec3 candidate = cascade_coord(i, position + offset);
    vec2 centred = abs(candidate.xy * 2.0 - 1.0);
    float candidate_edge = max(centred.x, centred.y);
    if (candidate_edge < 1.0 || i == cascade_count - 1)
    {
      cascade = i;
      coord = candidate;
      edge = candidate_edge;
      break;
    }
  }

  if (edge >= 1.0)
    return 1.0;

  float shadow = filter_cascade(cascade, coord, rotation);

  // Cross-fade into the next cascade near the border to hide the seam.
  float band = lighting.shadow_fade.z;
  float blend = smoothstep(1.0 - band, 1.0, edge);
  if (blend > 0.0)
  {
    float next = 1.0;
    if (cascade + 1 < cascade_count)
    {
      vec3 offset = normal * lighting.cascade_texel[cascade + 1] * (1.0 + 2.5 * slope);
      next = filter_cascade(cascade + 1, cascade_coord(cascade + 1, position + offset), rotation);
    }
    shadow = mix(shadow, next, blend);
  }

  shadow = mix(shadow, 1.0, smoothstep(fade_start, lighting.shadow_fade.x, view_distance));
  return mix(1.0, shadow, lighting.sun_colour.a);
}

// ---------------------------------------------------------------------------
// Image based ambient
// ---------------------------------------------------------------------------

// Analytic sky gradient built from the colours the CPU samples from the
// atmosphere model.
vec3 sky_gradient(vec3 direction)
{
  vec3 sky = mix(lighting.sky_horizon.rgb, lighting.sky_zenith.rgb,
                 sqrt(clamp(direction.y, 0.0, 1.0)));
  return mix(sky, lighting.sky_ground.rgb, smoothstep(0.0, 0.25, -direction.y));
}

// The rendered sky itself (sun glow and sunset gradients included).
vec3 sky_reflection(vec3 direction)
{
  if (lighting.fog_params.w < 0.5)
    return sky_gradient(direction);
  float elevation = asin(clamp(direction.y, 0.0, 1.0));
  vec2 uv = vec2(atan(direction.z, direction.x) / (2.0 * PI) + 0.5,
                 sqrt(elevation / (0.5 * PI)));
  vec3 sky = textureLod(u_sky_lut, uv, 0.0).rgb + vec3(0.006, 0.009, 0.018);
  return mix(sky, lighting.sky_ground.rgb, smoothstep(0.0, 0.25, -direction.y));
}

// Cosine-weighted irradiance of a sky hemisphere over a ground hemisphere:
// for two uniform halves the blend factor is exactly (1 + n.y) / 2.
vec3 sky_irradiance(vec3 normal)
{
  vec3 sky = mix(lighting.sky_horizon.rgb, lighting.sky_zenith.rgb, 0.65);
  return mix(lighting.sky_ground.rgb, sky, normal.y * 0.5 + 0.5);
}

// Split-sum environment BRDF, analytic fit (Karis, "Physically Based Shading
// on Mobile").
vec3 environment_brdf(vec3 f0, float roughness, float nv)
{
  const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
  const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
  vec4 r = roughness * c0 + c1;
  float a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
  vec2 scale_bias = vec2(-1.04, 1.04) * a004 + r.zw;
  return f0 * scale_bias.x + scale_bias.y;
}

// ---------------------------------------------------------------------------
// Direct lighting
// ---------------------------------------------------------------------------

// Cook-Torrance with GGX, height-correlated Smith visibility and Schlick
// Fresnel. Light intensities are radiance scales (a white surface facing a
// light of intensity 1 has radiance 1), so the BRDF is premultiplied by pi.
vec3 pbr_light(vec3 diffuse_colour, vec3 f0, float roughness, vec3 n, vec3 v, vec3 l)
{
  float nl = max(dot(n, l), 0.0);
  if (nl <= 0.0)
    return vec3(0.0);

  vec3 h = normalize(v + l);
  float nv = max(dot(n, v), 0.0001);
  float nh = max(dot(n, h), 0.0);
  float vh = max(dot(v, h), 0.0);

  float a = max(roughness * roughness, 0.002);
  float a2 = a * a;
  float d = (nh * a2 - nh) * nh + 1.0;
  float distribution = a2 / (d * d); // GGX * pi
  float visibility = 0.5 / max(mix(2.0 * nl * nv, nl + nv, a), 0.0001);
  float f = pow(1.0 - vh, 5.0);
  vec3 fresnel = f0 + (vec3(1.0) - f0) * f;

  // The clamp keeps mirror-like highlights from turning into fireflies.
  vec3 specular = min(distribution * visibility * fresnel, vec3(64.0));
  vec3 diffuse = diffuse_colour * (vec3(1.0) - fresnel);
  return (diffuse + specular) * nl;
}

vec3 normal_from_map(vec3 n, vec3 world_position, vec2 uv)
{
  vec3 tangent_normal = texture(u_normal, uv).xyz * 2.0 - 1.0;
  vec3 dp1 = dFdx(world_position);
  vec3 dp2 = dFdy(world_position);
  vec2 duv1 = dFdx(uv);
  vec2 duv2 = dFdy(uv);
  vec3 dp2perp = cross(dp2, n);
  vec3 dp1perp = cross(n, dp1);
  vec3 tangent = dp2perp * duv1.x + dp1perp * duv2.x;
  vec3 bitangent = dp2perp * duv1.y + dp1perp * duv2.y;
  float scale = max(dot(tangent, tangent), dot(bitangent, bitangent));
  if (scale <= 1e-20)
    return n;
  float invmax = inversesqrt(scale);
  mat3 tbn = mat3(tangent * invmax, bitangent * invmax, n);
  return normalize(tbn * tangent_normal);
}

// Exponential height fog integrated along the view ray, tinted toward the
// sun the way haze scatters light forward.
vec3 apply_fog(vec3 colour, vec3 position)
{
  if (lighting.fog_params.z < 0.5)
    return colour;

  vec3 ray = position - lighting.camera_position.xyz;
  float distance_to_camera = length(ray);
  float falloff = lighting.fog_params.y;
  float rise = clamp(falloff * ray.y, -20.0, 20.0);
  float column = abs(rise) > 0.001 ? (1.0 - exp(-rise)) / rise : 1.0;
  float density = lighting.fog_params.x *
                  exp(-falloff * max(lighting.camera_position.y, 0.0)) * column;
  float fog = 1.0 - exp(-density * distance_to_camera);

  vec3 fog_colour = lighting.fog_color.rgb;
  if (lighting.fog_params.w > 0.5)
  {
    float toward_sun = max(dot(ray / max(distance_to_camera, 0.0001),
                               normalize(lighting.sun_direction.xyz)), 0.0);
    fog_colour += lighting.sun_colour.rgb * lighting.sun_direction.w *
                  (0.25 * pow(toward_sun, 8.0));
  }
  return mix(colour, fog_colour, clamp(fog, 0.0, 1.0));
}

// ---------------------------------------------------------------------------
// Procedural surfaces (material flags 32 = terrain, 64 = ocean)
// ---------------------------------------------------------------------------

float surface_hash(vec2 p)
{
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}

float surface_noise(vec2 p)
{
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(surface_hash(i), surface_hash(i + vec2(1.0, 0.0)), f.x),
             mix(surface_hash(i + vec2(0.0, 1.0)), surface_hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

// Fractal noise in roughly [0, 1].
float surface_fbm(vec2 p)
{
  float sum = 0.0;
  float amplitude = 0.5;
  for (int i = 0; i < 4; i++)
  {
    sum += amplitude * surface_noise(p);
    p = p * 2.03 + vec2(17.1, 9.2);
    amplitude *= 0.5;
  }
  return sum / 0.9375;
}

// Terrain: grass, rock, sand and snow chosen from the slope, the height and
// the weights stored in the UVs (u = sand, v = rock). Returns the sRGB albedo
// and writes the bumped normal and the roughness.
vec3 terrain_material(vec3 position, vec3 geometric_normal, vec2 weights,
                      out vec3 normal, out float roughness)
{
  float view_distance = length(lighting.camera_position.xyz - position);
  // Fine detail fades with distance so it does not shimmer.
  float detail_fade = 1.0 - smoothstep(60.0, 260.0, view_distance);

  float macro = surface_fbm(position.xz * 0.021 + 3.7);
  float patches = surface_fbm(position.xz * 0.17 + 11.3);
  float fine = mix(0.5, surface_fbm(position.xz * 2.7), detail_fade);
  float slope = 1.0 - geometric_normal.y;

  // Grass: lush to dry, with trampled earth patches.
  vec3 grass = mix(vec3(0.16, 0.33, 0.07), vec3(0.36, 0.47, 0.12), macro);
  grass = mix(grass, vec3(0.47, 0.44, 0.19), smoothstep(0.55, 0.85, patches) * 0.55);
  grass *= 0.72 + 0.56 * fine;

  // Rock: strata follow the height, cracks run down the face.
  float bend = surface_fbm(position.xz * 0.11) * 4.0;
  float strata = surface_fbm(vec2(position.y * 1.35 + bend, dot(position.xz, vec2(0.07, 0.05))));
  float cracks = surface_fbm(vec2(dot(position.xz, vec2(0.83, 0.56)) * 0.9, position.y * 0.35));
  vec3 rock = mix(vec3(0.21, 0.19, 0.17), vec3(0.56, 0.52, 0.46), strata);
  rock *= 0.62 + 0.62 * mix(0.5, cracks, 0.4 + 0.6 * detail_fade);
  // Moss creeps over the flatter rock.
  rock = mix(rock, grass * 0.8, smoothstep(0.55, 0.25, slope) * smoothstep(0.4, 0.7, patches) * 0.6);

  // Sand: pale and dry up the beach, dark and wet at the waterline.
  vec3 sand = mix(vec3(0.80, 0.70, 0.48), vec3(0.93, 0.86, 0.66), patches);
  sand *= 0.92 + 0.16 * fine;
  float wet = (1.0 - smoothstep(0.05, 0.55, position.y)) * step(position.y, 1.5);
  sand = mix(sand, sand * vec3(0.55, 0.52, 0.47), wet);

  vec3 snow = vec3(0.92, 0.94, 0.97) * (0.92 + 0.08 * fine);

  float sand_weight = clamp(weights.x + (patches - 0.5) * 0.35 * step(0.02, weights.x), 0.0, 1.0);
  sand_weight = smoothstep(0.25, 0.75, sand_weight);
  float rock_weight = smoothstep(0.20, 0.46, slope + (patches - 0.5) * 0.16 + weights.y * 0.10);
  // High ground turns from grass to bare rock, then to snow on the flats.
  float alpine = smoothstep(32.0, 50.0, position.y + (macro - 0.5) * 14.0);
  float snow_weight = smoothstep(47.0, 57.0, position.y + (patches - 0.5) * 12.0) *
                      (1.0 - smoothstep(0.38, 0.62, slope));

  vec3 albedo = mix(grass, rock * 0.92, alpine * 0.8);
  albedo = mix(albedo, sand, sand_weight);
  albedo = mix(albedo, rock, rock_weight);
  albedo = mix(albedo, snow, snow_weight);

  roughness = mix(0.92, 0.82, rock_weight);
  roughness = mix(roughness, mix(0.9, 0.35, wet), sand_weight * (1.0 - rock_weight));
  roughness = mix(roughness, 0.55, snow_weight);

  // Bump: derive a normal from a procedural height using screen derivatives.
  float bump = (strata * 0.55 + cracks * 0.45) * rock_weight * 0.9 +
               fine * mix(0.10, 0.03, sand_weight) * (1.0 - snow_weight * 0.7);
  vec3 dpx = dFdx(position);
  vec3 dpy = dFdy(position);
  float dbx = dFdx(bump);
  float dby = dFdy(bump);
  vec3 r1 = cross(dpy, geometric_normal);
  vec3 r2 = cross(geometric_normal, dpx);
  float det = dot(dpx, r1);
  vec3 gradient = sign(det) * (dbx * r1 + dby * r2);
  normal = normalize(abs(det) * geometric_normal - gradient * detail_fade);
  return albedo;
}

float ocean_height(vec2 p, float t)
{
  return surface_noise(p * 0.16 + vec2(t * 0.30, t * 0.19)) +
         surface_noise(p * 0.43 - vec2(t * 0.36, -t * 0.27)) * 0.45 +
         surface_noise(p * 1.21 + vec2(-t * 0.55, t * 0.48)) * 0.20 +
         surface_noise(p * 3.30 + vec2(t * 0.80, t * 0.70)) * 0.07;
}

// Ocean/lake: uv.x is the water depth in metres, uv.y the wave scale (1 open
// sea, 0 calm lake). The water is opaque; the depth drives the colour from the
// sand showing through the shallows to deep blue, and the foam at the shore.
vec3 ocean_colour(vec3 position, vec2 uv, vec3 view_direction, vec3 to_sun,
                  vec3 sun_radiance, float shadow)
{
  float t = lighting.time.x;
  float depth = max(uv.x, 0.0);
  float wave_scale = clamp(uv.y, 0.0, 1.0);
  float view_distance = length(lighting.camera_position.xyz - position);
  vec2 p = position.xz;

  float amplitude = mix(0.22, 0.75, wave_scale) / (1.0 + view_distance * 0.006);
  // Waves flatten as they run into the shallows.
  amplitude *= mix(0.35, 1.0, smoothstep(0.0, 2.5, depth));
  float epsilon = 0.25;
  float h0 = ocean_height(p, t);
  float hx = ocean_height(p + vec2(epsilon, 0.0), t);
  float hz = ocean_height(p + vec2(0.0, epsilon), t);
  vec3 normal = normalize(vec3((h0 - hx) / epsilon * amplitude, 1.0,
                               (h0 - hz) / epsilon * amplitude));

  // Linear-space water colours.
  vec3 bed = vec3(0.52, 0.42, 0.24) * vec3(0.62, 0.86, 0.80);
  vec3 shallow = vec3(0.045, 0.42, 0.40);
  vec3 middle = vec3(0.012, 0.19, 0.30);
  vec3 deep = vec3(0.004, 0.045, 0.12);
  vec3 body = mix(bed, shallow, 1.0 - exp(-depth * 1.5));
  body = mix(body, middle, 1.0 - exp(-depth * 0.42));
  body = mix(body, deep, smoothstep(2.5, 11.0, depth));

  vec3 irradiance = sky_irradiance(normal);
  vec3 sun_light = sun_radiance * max(dot(normal, to_sun), 0.0) * shadow;
  vec3 colour = body * (irradiance + sun_light);

  float nv = max(dot(normal, view_direction), 0.0);
  float fresnel = 0.02 + 0.98 * pow(1.0 - nv, 5.0);
  vec3 reflected = reflect(-view_direction, normal);
  reflected.y = abs(reflected.y);
  colour = mix(colour, sky_reflection(reflected), fresnel);
  colour += pbr_light(vec3(0.0), vec3(0.02), 0.07, normal, view_direction, to_sun) *
            sun_radiance * shadow;

  // Foam: a thin line at the waterline plus bands that roll in from the sea.
  float pattern = surface_fbm(p * 0.9 + vec2(t * 0.15, -t * 0.11));
  float shore = 1.0 - smoothstep(0.0, mix(0.35, 1.3, wave_scale), depth);
  float bands = sin(depth * 6.5 - t * 1.3 + pattern * 5.0) * 0.5 + 0.5;
  float foam = shore * smoothstep(0.55, 0.9, pattern * 0.55 + bands * 0.5 + shore * 0.45);
  foam = max(foam, (1.0 - smoothstep(0.02, 0.14, depth)) * 0.85);
  foam *= mix(0.3, 1.0, wave_scale) * step(0.0, uv.x + 0.05);
  // Whitecaps on the crests of the open sea.
  foam = max(foam, wave_scale * smoothstep(1.52, 1.66, h0) * smoothstep(3.0, 8.0, depth) * 0.5);
  colour = mix(colour, vec3(0.93) * (irradiance + sun_light), clamp(foam, 0.0, 1.0));
  return colour;
}

void main()
{
  vec4 base = texture(u_texture, v_uv) * v_tint;
  if (base.a <= 0.001)
    discard;

  vec3 geometric_normal = normalize(v_normal);
  vec3 normal = geometric_normal;

  vec3 to_sun = normalize(lighting.sun_direction.xyz);
  vec3 view_direction = normalize(lighting.camera_position.xyz - v_world_position + vec3(0.00001));

  int flags = int(pc.material.z + 0.5);
  bool has_metallic_roughness = (flags & 1) != 0;
  bool has_normal = (flags & 2) != 0;
  bool has_occlusion = (flags & 4) != 0;
  bool has_emissive = (flags & 8) != 0;
  bool is_water = (flags & 16) != 0;
  bool pbr_enabled = pc.material.w > 0.5;

  if (has_normal)
    normal = normal_from_map(normal, v_world_position, v_uv);

  bool is_terrain = (flags & 32) != 0;
  bool is_ocean = (flags & 64) != 0;
  float terrain_roughness = 1.0;
  if (is_terrain)
    base.rgb = v_tint.rgb * terrain_material(v_world_position, geometric_normal, v_uv,
                                             normal, terrain_roughness);

  float metallic = clamp(pc.material.x, 0.0, 1.0);
  float roughness = clamp(pc.material.y, 0.045, 1.0);
  if (has_metallic_roughness)
  {
    vec4 mr = texture(u_metallic_roughness, v_uv);
    roughness = clamp(pc.material.y * mr.g, 0.045, 1.0);
    metallic = clamp(pc.material.x * mr.b, 0.0, 1.0);
  }

  if (is_terrain)
  {
    roughness = terrain_roughness;
    metallic = 0.0;
  }

  float ao = has_occlusion ? texture(u_occlusion, v_uv).r : 1.0;
  vec3 albedo = to_linear(base.rgb);
  vec3 emissive = to_linear(pc.emissive.rgb) * pc.emissive.a;
  if (has_emissive)
    emissive *= to_linear(texture(u_emissive, v_uv).rgb);

  vec3 sun_radiance = lighting.sun_colour.rgb * lighting.sun_direction.w;
  float sun_ndl = max(dot(normal, to_sun), 0.0);
  float shadow = 1.0;
  if (lighting.shadow_params.y > 0.5 && sun_ndl > 0.0)
    shadow = sun_shadow(v_world_position, geometric_normal,
                        max(dot(geometric_normal, to_sun), 0.0));

  vec3 colour;
  if (is_ocean)
  {
    colour = ocean_colour(v_world_position, v_uv, view_direction, to_sun, sun_radiance, shadow);
    out_colour = vec4(apply_fog(colour, v_world_position), 1.0);
    out_normal = vec4(0.0);
    return;
  }
  if (is_water)
  {
    float t = lighting.time.x;
    vec2 p = v_world_position.xz;
    float wave_a = sin(p.x * 1.3 + t * 1.7) * cos(p.y * 1.1 - t * 1.3);
    float wave_b = sin(p.y * 1.7 - t * 1.1) * cos(p.x * 1.4 + t * 1.6);
    vec3 water_normal = normalize(vec3(wave_a * 0.10, 1.0, wave_b * 0.10));
    float nv = max(dot(water_normal, view_direction), 0.0);
    float fresnel = 0.02 + 0.98 * pow(1.0 - nv, 5.0);

    // Body colour lit by the sky and the sun, reflection of the sky on top.
    vec3 body = albedo * (sky_irradiance(water_normal) +
                          sun_radiance * max(dot(water_normal, to_sun), 0.0) * shadow);
    vec3 reflection = sky_reflection(reflect(-view_direction, water_normal));
    colour = mix(body, reflection, fresnel);
    colour += pbr_light(vec3(0.0), vec3(0.02), 0.06, water_normal, view_direction, to_sun) *
              sun_radiance * shadow;

    out_colour = vec4(apply_fog(colour, v_world_position), 1.0);
    out_normal = vec4(0.0);
    return;
  }

  vec3 diffuse_colour = albedo;
  if (pbr_enabled)
  {
    diffuse_colour = albedo * (1.0 - metallic);
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    float nv = max(dot(normal, view_direction), 0.0001);

    // Sun.
    colour = pbr_light(diffuse_colour, f0, roughness, normal, view_direction, to_sun) *
             sun_radiance * shadow;

    // Sky and ground: diffuse irradiance plus a reflection of the sky that
    // blurs into the irradiance as the surface gets rougher.
    vec3 irradiance = sky_irradiance(normal);
    vec3 reflected = reflect(-view_direction, normal);
    vec3 environment = mix(sky_reflection(reflected), irradiance, roughness * roughness);
    // Reflections that would come from below the surface are occluded by it.
    float horizon = clamp(1.0 + dot(reflected, geometric_normal), 0.0, 1.0);
    float specular_occlusion = clamp(pow(nv + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao,
                                     0.0, 1.0) * horizon * horizon;
    colour += diffuse_colour * irradiance * ao;
    colour += environment * environment_brdf(f0, roughness, nv) * specular_occlusion;

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
      float falloff = clamp(1.0 - distance_to_light / range, 0.0, 1.0);
      colour += pbr_light(diffuse_colour, f0, roughness, normal, view_direction, light_dir) *
                lighting.point_lights[i].colour.rgb *
                lighting.point_lights[i].colour.w * (falloff * falloff) * ao;
    }
  }
  else
  {
    colour = albedo * (sky_irradiance(normal) * ao + sun_radiance * sun_ndl * shadow);

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
      float falloff = clamp(1.0 - distance_to_light / range, 0.0, 1.0);
      colour += albedo * lighting.point_lights[i].colour.rgb *
                lighting.point_lights[i].colour.w * ndl * (falloff * falloff);
    }
  }

  colour += emissive;
  colour = apply_fog(colour, v_world_position);

  out_colour = vec4(colour, base.a);
  // The albedo luminance lets the screen-space GI weight the bounced light by
  // how reflective the receiving surface is.
  out_normal = vec4(octahedral_encode(normal), clamp(luminance(diffuse_colour), 0.0, 1.0), 1.0);
}
