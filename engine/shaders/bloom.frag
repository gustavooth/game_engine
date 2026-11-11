#version 450

// Mip-chain bloom (Jimenez, "Next Generation Post Processing in Call of
// Duty: Advanced Warfare"): a 13-tap downsample builds progressively blurrier
// mips and a 9-tap tent upsample adds them back together. The result is a
// wide, smooth glow without the blocky look of a single separable blur.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_colour;

layout(set = 0, binding = 0) uniform sampler2D u_source;

layout(push_constant) uniform BloomConstants
{
  vec4 params;  // source texel size xy, mode, upsample radius
  vec4 params2; // threshold, knee, firefly clamp, unused
} pc;

float luminance(vec3 colour)
{
  return dot(colour, vec3(0.2126, 0.7152, 0.0722));
}

// Soft-knee threshold: keeps the hue and fades in smoothly instead of
// clipping, so highlights do not pop when they cross the threshold.
vec3 prefilter(vec3 colour)
{
  colour = min(colour, vec3(pc.params2.z));
  float brightness = max(colour.r, max(colour.g, colour.b));
  float threshold = pc.params2.x;
  float knee = max(pc.params2.y, 0.0001);
  float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
  soft = soft * soft / (4.0 * knee);
  float contribution = max(soft, brightness - threshold) / max(brightness, 0.0001);
  return colour * contribution;
}

// Weighting each 2x2 block by 1 / (1 + luma) stops a single very bright
// pixel from flickering across the whole mip chain.
vec3 karis_box(vec3 a, vec3 b, vec3 c, vec3 d)
{
  float wa = 1.0 / (1.0 + luminance(a));
  float wb = 1.0 / (1.0 + luminance(b));
  float wc = 1.0 / (1.0 + luminance(c));
  float wd = 1.0 / (1.0 + luminance(d));
  return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
}

void main()
{
  vec2 texel = pc.params.xy;
  int mode = int(pc.params.z + 0.5);

  if (mode == 2)
  {
    // 3x3 tent filter.
    vec2 r = texel * pc.params.w;
    vec3 sum = texture(u_source, v_uv).rgb * 4.0;
    sum += (texture(u_source, v_uv + vec2(-r.x, 0.0)).rgb +
            texture(u_source, v_uv + vec2(r.x, 0.0)).rgb +
            texture(u_source, v_uv + vec2(0.0, -r.y)).rgb +
            texture(u_source, v_uv + vec2(0.0, r.y)).rgb) * 2.0;
    sum += texture(u_source, v_uv + vec2(-r.x, -r.y)).rgb +
           texture(u_source, v_uv + vec2(r.x, -r.y)).rgb +
           texture(u_source, v_uv + vec2(-r.x, r.y)).rgb +
           texture(u_source, v_uv + vec2(r.x, r.y)).rgb;
    out_colour = vec4(sum / 16.0, 1.0);
    return;
  }

  vec3 a = texture(u_source, v_uv + texel * vec2(-2.0, -2.0)).rgb;
  vec3 b = texture(u_source, v_uv + texel * vec2(0.0, -2.0)).rgb;
  vec3 c = texture(u_source, v_uv + texel * vec2(2.0, -2.0)).rgb;
  vec3 d = texture(u_source, v_uv + texel * vec2(-2.0, 0.0)).rgb;
  vec3 e = texture(u_source, v_uv).rgb;
  vec3 f = texture(u_source, v_uv + texel * vec2(2.0, 0.0)).rgb;
  vec3 g = texture(u_source, v_uv + texel * vec2(-2.0, 2.0)).rgb;
  vec3 h = texture(u_source, v_uv + texel * vec2(0.0, 2.0)).rgb;
  vec3 i = texture(u_source, v_uv + texel * vec2(2.0, 2.0)).rgb;
  vec3 j = texture(u_source, v_uv + texel * vec2(-1.0, -1.0)).rgb;
  vec3 k = texture(u_source, v_uv + texel * vec2(1.0, -1.0)).rgb;
  vec3 l = texture(u_source, v_uv + texel * vec2(-1.0, 1.0)).rgb;
  vec3 m = texture(u_source, v_uv + texel * vec2(1.0, 1.0)).rgb;

  vec3 colour;
  if (mode == 0)
  {
    // First downsample: threshold, then Karis-weighted blocks.
    a = prefilter(a); b = prefilter(b); c = prefilter(c); d = prefilter(d);
    e = prefilter(e); f = prefilter(f); g = prefilter(g); h = prefilter(h);
    i = prefilter(i); j = prefilter(j); k = prefilter(k); l = prefilter(l);
    m = prefilter(m);
    colour = karis_box(j, k, l, m) * 0.5 +
             (karis_box(a, b, d, e) + karis_box(b, c, e, f) +
              karis_box(d, e, g, h) + karis_box(e, f, h, i)) * 0.125;
  }
  else
  {
    colour = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 +
             (j + k + l + m) * 0.125;
  }

  out_colour = vec4(max(colour, vec3(0.0)), 1.0);
}
