#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_colour;

layout(set = 0, binding = 0) uniform sampler2D u_scene;
layout(set = 0, binding = 1) uniform sampler2D u_effects;
layout(set = 0, binding = 2) uniform sampler2D u_bloom;
layout(set = 0, binding = 4) uniform sampler3D u_lut;
layout(set = 0, binding = 5) uniform sampler2D u_normal; // oct normal, albedo luma, lit flag
layout(set = 0, binding = 6) uniform sampler2D u_depth;
layout(std430, set = 0, binding = 3) readonly buffer ExposureBuffer
{
  float exposure_value;
  float average_luminance;
  float target_exposure;
  float exposure_padding;
} exposure_data;

layout(push_constant) uniform PostConstants
{
  vec4 exposure_tonemap; // exposure, tone mapper, tone mapping enabled, HDR
  vec4 grading;          // contrast, saturation, temperature, tint
  vec4 lift;             // lift rgb, vignette
  vec4 gamma;            // gamma rgb, lut strength
  vec4 gain;             // gain rgb, lut enabled
  vec4 effects;          // ao enabled, gi enabled, gi intensity, ao intensity
  vec4 bloom;            // enabled, intensity, threshold, radius
  vec4 exposure_mode;    // auto exposure enabled, manual multiplier, unused
} pc;

float luminance(vec3 colour)
{
  return dot(colour, vec3(0.2126, 0.7152, 0.0722));
}

vec3 tonemap_reinhard(vec3 colour)
{
  // Luminance-based extended Reinhard: keeps hues, whites out at 4.0.
  float l = luminance(colour);
  float mapped = l * (1.0 + l / 16.0) / (1.0 + l);
  return clamp(colour * (mapped / max(l, 1e-5)), 0.0, 1.0);
}

// ACES fitted (Stephen Hill): the RRT+ODT curve applied in the ACES AP1
// space. Unlike a per-channel fit applied directly in sRGB, bright saturated
// colours roll off toward white naturally instead of clipping to pure
// red/green/blue, while mid-tones keep their saturation.
vec3 tonemap_aces(vec3 colour)
{
  const mat3 input_matrix = mat3(
      0.59719, 0.07600, 0.02840,
      0.35458, 0.90834, 0.13383,
      0.04823, 0.01566, 0.83777);
  const mat3 output_matrix = mat3(
      1.60475, -0.10208, -0.00327,
      -0.53108, 1.10813, -0.07276,
      -0.07367, -0.00605, 1.07602);
  // The fitted curve maps middle grey darker than the display expects; this
  // pre-scale restores the usual exposure (same convention as the widely used
  // single-curve ACES approximation).
  vec3 v = input_matrix * (colour * 1.6);
  vec3 a = v * (v + 0.0245786) - 0.000090537;
  vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
  return clamp(output_matrix * (a / b), 0.0, 1.0);
}

vec3 agx_contrast(vec3 x)
{
  vec3 x2 = x * x;
  vec3 x4 = x2 * x2;
  return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 -
         6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 tonemap_agx(vec3 colour)
{
  const mat3 agx_mat = mat3(
      0.842479062253094, 0.0423282422610123, 0.0423756549057051,
      0.0784335999999992, 0.878468636469772, 0.0784336,
      0.0792237451477643, 0.0791661274605434, 0.879142973793104);
  const mat3 agx_mat_inverse = mat3(
      1.19687900512017, -0.0528968517574562, -0.0529716355144438,
      -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
      -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
  const float min_ev = -12.47393;
  const float max_ev = 4.026069;
  colour = agx_mat * max(colour, vec3(0.0));
  colour = clamp(log2(max(colour, vec3(1e-6))), min_ev, max_ev);
  colour = (colour - min_ev) / (max_ev - min_ev);
  colour = agx_contrast(colour);
  // The curve outputs display-encoded values in the AgX working space: go
  // back to sRGB primaries (the outset the original shader skipped, which
  // left the image desaturated) and to linear light.
  colour = agx_mat_inverse * colour;
  return pow(clamp(colour, 0.0, 1.0), vec3(2.2));
}

vec3 apply_tonemap(vec3 colour, int mapper)
{
  if (mapper == 1)
    return tonemap_aces(colour);
  if (mapper == 2)
    return tonemap_agx(colour);
  return tonemap_reinhard(colour);
}

vec3 srgb_encode(vec3 colour)
{
  colour = clamp(colour, 0.0, 1.0);
  return mix(colour * 12.92, 1.055 * pow(colour, vec3(1.0 / 2.4)) - 0.055,
             step(vec3(0.0031308), colour));
}

float interleaved_gradient_noise(vec2 pixel)
{
  return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// Depth-aware upsample of the half-resolution AO/GI target. Taps across a
// depth discontinuity are rejected, so occlusion and bounced light do not
// bleed over silhouettes.
vec4 upsample_effects(float depth)
{
  vec2 texel = 1.0 / vec2(textureSize(u_effects, 0));
  // (1 - depth) is proportional to 1 / view distance, so comparing it
  // relatively gives a depth-independent edge threshold.
  float reference = max(1.0 - depth, 1e-6);
  vec4 sum = vec4(0.0);
  float total = 0.0;
  for (int y = -1; y <= 1; y++)
  {
    for (int x = -1; x <= 1; x++)
    {
      vec2 uv = v_uv + vec2(float(x), float(y)) * texel * 1.5;
      float tap_depth = textureLod(u_depth, uv, 0.0).r;
      float difference = abs((1.0 - tap_depth) - reference) / reference;
      float weight = max(1.0 - difference * 12.0, 0.0) + 0.0001;
      sum += textureLod(u_effects, uv, 0.0) * weight;
      total += weight;
    }
  }
  return sum / total;
}

void main()
{
  vec3 colour = texture(u_scene, v_uv).rgb;

  float exposure = pc.exposure_mode.x > 0.5
                       ? exposure_data.exposure_value * pc.exposure_mode.y
                       : pc.exposure_mode.y;

  if (pc.effects.x > 0.5 || pc.effects.y > 0.5)
  {
    vec4 surface = texture(u_normal, v_uv);
    if (surface.a > 0.5)
    {
      vec4 effects = upsample_effects(texture(u_depth, v_uv).r);

      // Ambient occlusion only blocks ambient light. The forward pass has
      // already summed direct and ambient, so the occlusion is weakened on
      // pixels that are clearly lit directly.
      float lit = smoothstep(0.25, 1.2, luminance(colour) * exposure);
      float occlusion = mix(1.0, effects.a, pc.effects.x * pc.effects.w);
      occlusion = mix(occlusion, 1.0, lit * 0.65);

      // Bounced light is tinted by the receiving surface: its hue comes from
      // the shaded colour and its reflectance from the stored albedo luma.
      vec3 hue = colour / max(luminance(colour), 1e-4);
      vec3 albedo = surface.b * mix(vec3(1.0), clamp(hue, 0.0, 2.5), 0.75);
      vec3 bounce = effects.rgb * albedo * (pc.effects.y * pc.effects.z);

      colour = colour * occlusion + bounce;
    }
  }

  if (pc.bloom.x > 0.5)
    colour += texture(u_bloom, v_uv).rgb * pc.bloom.y;

  int mapper = int(pc.exposure_tonemap.y + 0.5);
  bool tone_mapping = pc.exposure_tonemap.z > 0.5;
  bool hdr = pc.exposure_tonemap.w > 0.5;

  // White balance in scene-linear light, before the tone curve.
  float temperature = pc.grading.z;
  float tint = pc.grading.w;
  colour *= vec3(1.0 + temperature * 0.18 - tint * 0.06,
                 1.0 + tint * 0.12,
                 1.0 - temperature * 0.18 - tint * 0.06);

  if (hdr)
  {
    colour *= exposure;
    colour = tone_mapping ? apply_tonemap(colour, mapper) : clamp(colour, 0.0, 1.0);
  }
  else
  {
    colour = clamp(colour, 0.0, 1.0);
  }

  // Lift / gamma / gain on the display-referred linear image.
  vec3 lift = pc.lift.rgb;
  vec3 gamma = max(pc.gamma.rgb, vec3(0.01));
  vec3 gain = pc.gain.rgb;
  colour = pow(max(colour * gain + lift, vec3(0.0)), vec3(1.0) / gamma);

  // The remaining grading runs on the sRGB-encoded image, which is
  // perceptually uniform: contrast pivots on middle grey and 3D LUTs are
  // authored for encoded input.
  colour = srgb_encode(colour);

  // Saturation above 1 behaves as vibrance: muted colours are boosted the
  // most while already saturated ones are protected from clipping.
  float luma = luminance(colour);
  float saturation = pc.grading.y;
  if (saturation > 1.0)
  {
    float chroma = max(colour.r, max(colour.g, colour.b)) - min(colour.r, min(colour.g, colour.b));
    saturation = 1.0 + (saturation - 1.0) * (1.0 - chroma * 0.75);
  }
  colour = mix(vec3(luma), colour, saturation);

  // S-shaped contrast around middle grey; it never pushes values out of
  // range, so highlights and shadows keep their detail.
  float contrast = pc.grading.x;
  if (abs(contrast - 1.0) > 0.001)
  {
    vec3 x = clamp(colour, 0.0, 1.0);
    vec3 below = 0.5 * pow(2.0 * x, vec3(contrast));
    vec3 above = 1.0 - 0.5 * pow(2.0 * (1.0 - x), vec3(contrast));
    colour = mix(below, above, step(vec3(0.5), x));
  }
  colour = clamp(colour, 0.0, 1.0);

  if (pc.gain.w > 0.5 && pc.gamma.w > 0.0)
  {
    // Sample texel centres so the LUT edges are not extrapolated.
    float size = float(textureSize(u_lut, 0).x);
    vec3 coord = colour * ((size - 1.0) / size) + 0.5 / size;
    vec3 graded = texture(u_lut, coord).rgb;
    colour = mix(colour, graded, clamp(pc.gamma.w, 0.0, 1.0));
  }

  vec2 centered = v_uv * 2.0 - 1.0;
  float vignette = smoothstep(0.35, 1.25, dot(centered, centered));
  colour *= 1.0 - vignette * pc.lift.a;

  // Half an 8-bit step of noise breaks up banding in the sky gradient.
  colour += (interleaved_gradient_noise(gl_FragCoord.xy) - 0.5) / 255.0;

  // The swapchain format is UNORM and the colour is already sRGB encoded.
  out_colour = vec4(clamp(colour, 0.0, 1.0), 1.0);
}
