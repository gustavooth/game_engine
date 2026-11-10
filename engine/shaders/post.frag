#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_colour;

layout(set = 0, binding = 0) uniform sampler2D u_scene;
layout(set = 0, binding = 1) uniform sampler2D u_effects;
layout(set = 0, binding = 2) uniform sampler2D u_bloom;
layout(set = 0, binding = 4) uniform sampler3D u_lut;
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

vec3 tonemap_reinhard(vec3 colour)
{
  return colour / (vec3(1.0) + colour);
}

vec3 tonemap_aces(vec3 colour)
{
  // Narkowicz ACES approximation.
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return clamp((colour * (a * colour + b)) / (colour * (c * colour + d) + e), 0.0, 1.0);
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
  const float min_ev = -12.47393;
  const float max_ev = 4.026069;
  colour = agx_mat * max(colour, vec3(0.0));
  colour = clamp(log2(max(colour, vec3(1e-6))), min_ev, max_ev);
  colour = (colour - min_ev) / (max_ev - min_ev);
  return clamp(agx_contrast(colour), 0.0, 1.0);
}

vec3 apply_tonemap(vec3 colour, int mapper)
{
  if (mapper == 1)
    return tonemap_aces(colour);
  if (mapper == 2)
    return tonemap_agx(colour);
  return tonemap_reinhard(colour);
}

void main()
{
  vec3 colour = texture(u_scene, v_uv).rgb;
  vec4 effects = texture(u_effects, v_uv);
  float ao = mix(1.0, effects.a, pc.effects.x);
  vec3 gi = effects.rgb * pc.effects.y * pc.effects.z;
  colour = colour * ao + gi;
  colour += texture(u_bloom, v_uv).rgb * pc.bloom.x * pc.bloom.y;

  float exposure = pc.exposure_mode.x > 0.5 ? exposure_data.exposure_value
                                             : pc.exposure_mode.y;
  int mapper = int(pc.exposure_tonemap.y + 0.5);
  bool tone_mapping = pc.exposure_tonemap.z > 0.5;
  bool hdr = pc.exposure_tonemap.w > 0.5;

  if (hdr)
  {
    colour *= exposure;
    colour = tone_mapping ? apply_tonemap(colour, mapper) : clamp(colour, 0.0, 1.0);
  }
  else
  {
    colour = clamp(colour, 0.0, 1.0);
  }

  // Color grading: temperature/tint, lift/gamma/gain, contrast/saturation.
  float temperature = pc.grading.z;
  float tint = pc.grading.w;
  colour.r *= 1.0 + temperature * 0.18;
  colour.b *= 1.0 - temperature * 0.18;
  colour.g *= 1.0 + tint * 0.12;
  colour.r *= 1.0 - tint * 0.06;
  colour.b *= 1.0 - tint * 0.06;

  vec3 lift = pc.lift.rgb;
  vec3 gamma = max(pc.gamma.rgb, vec3(0.01));
  vec3 gain = pc.gain.rgb;
  colour = pow(max(colour * gain + lift, vec3(0.0)), vec3(1.0) / gamma);

  float luma = dot(colour, vec3(0.2126, 0.7152, 0.0722));
  colour = mix(vec3(luma), colour, pc.grading.y);
  colour = (colour - vec3(0.5)) * pc.grading.x + vec3(0.5);
  colour = clamp(colour, 0.0, 1.0);

  if (pc.gain.w > 0.5 && pc.gamma.w > 0.0)
  {
    vec3 graded = texture(u_lut, clamp(colour, 0.0, 1.0)).rgb;
    colour = mix(colour, graded, clamp(pc.gamma.w, 0.0, 1.0));
  }

  vec2 centered = v_uv * 2.0 - 1.0;
  float vignette = smoothstep(0.35, 1.25, dot(centered, centered));
  colour *= 1.0 - vignette * pc.lift.a;

  // The swapchain format is UNORM, so encode sRGB manually.
  colour = pow(max(colour, vec3(0.0)), vec3(1.0 / 2.2));
  out_colour = vec4(colour, 1.0);
}
