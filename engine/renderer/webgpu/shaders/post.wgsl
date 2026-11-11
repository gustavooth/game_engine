// Final composite: AO/GI, bloom, exposure, tone mapping and colour grading.
// Keep in sync with engine/shaders/post.frag.

struct PostConstants {
  exposure_tonemap: vec4f, // exposure, tone mapper, tone mapping enabled, HDR
  grading: vec4f,          // contrast, saturation, temperature, tint
  lift: vec4f,             // lift rgb, vignette
  gamma: vec4f,            // gamma rgb, lut strength
  gain: vec4f,             // gain rgb, lut enabled
  effects: vec4f,          // ao enabled, gi enabled, gi intensity, ao intensity
  bloom: vec4f,            // enabled, intensity, threshold, radius
  exposureMode: vec4f,     // auto exposure enabled, manual multiplier, unused
};

struct ExposureBuffer {
  exposure: f32,
  averageLuminance: f32,
  targetExposure: f32,
  padding: f32,
};

@group(0) @binding(0) var sceneTexture: texture_2d<f32>;
@group(0) @binding(1) var sceneSampler: sampler;
@group(0) @binding(2) var<uniform> pc: PostConstants;
@group(0) @binding(3) var effectsTexture: texture_2d<f32>;
@group(0) @binding(4) var bloomTexture: texture_2d<f32>;
@group(0) @binding(5) var<storage, read> exposureBuffer: ExposureBuffer;
@group(0) @binding(6) var lutTexture: texture_3d<f32>;
@group(0) @binding(7) var normalTexture: texture_2d<f32>; // oct normal, albedo luma, lit flag
@group(0) @binding(8) var depthTexture: texture_depth_2d;

struct VertexOutput {
  @builtin(position) position: vec4f,
  @location(0) uv: vec2f,
};

@vertex
fn vs_main(@builtin(vertex_index) index: u32) -> VertexOutput {
  var output: VertexOutput;
  let uv = vec2f(f32((index << 1u) & 2u), f32(index & 2u));
  output.position = vec4f(uv * 2.0 - 1.0, 0.0, 1.0);
  output.uv = uv;
  return output;
}

fn luminance(colour: vec3f) -> f32 {
  return dot(colour, vec3f(0.2126, 0.7152, 0.0722));
}

fn tonemap_reinhard(colour: vec3f) -> vec3f {
  // Luminance-based extended Reinhard: keeps hues, whites out at 4.0.
  let l = luminance(colour);
  let mapped = l * (1.0 + l / 16.0) / (1.0 + l);
  return clamp(colour * (mapped / max(l, 1e-5)), vec3f(0.0), vec3f(1.0));
}

// ACES fitted (Stephen Hill): the RRT+ODT curve applied in the ACES AP1
// space, so bright saturated colours roll off toward white naturally.
fn tonemap_aces(colour: vec3f) -> vec3f {
  let inputMatrix = mat3x3f(
      0.59719, 0.07600, 0.02840,
      0.35458, 0.90834, 0.13383,
      0.04823, 0.01566, 0.83777);
  let outputMatrix = mat3x3f(
      1.60475, -0.10208, -0.00327,
      -0.53108, 1.10813, -0.07276,
      -0.07367, -0.00605, 1.07602);
  // The fitted curve maps middle grey darker than the display expects; this
  // pre-scale restores the usual exposure.
  let v = inputMatrix * (colour * 1.6);
  let a = v * (v + vec3f(0.0245786)) - vec3f(0.000090537);
  let b = v * (0.983729 * v + vec3f(0.4329510)) + vec3f(0.238081);
  return clamp(outputMatrix * (a / b), vec3f(0.0), vec3f(1.0));
}

fn agx_contrast(x: vec3f) -> vec3f {
  let x2 = x * x;
  let x4 = x2 * x2;
  return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 -
      6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - vec3f(0.00232);
}

fn tonemap_agx(colourIn: vec3f) -> vec3f {
  let agxMat = mat3x3f(
      0.842479062253094, 0.0423282422610123, 0.0423756549057051,
      0.0784335999999992, 0.878468636469772, 0.0784336,
      0.0792237451477643, 0.0791661274605434, 0.879142973793104);
  let agxMatInverse = mat3x3f(
      1.19687900512017, -0.0528968517574562, -0.0529716355144438,
      -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
      -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
  let minEv = -12.47393;
  let maxEv = 4.026069;
  var colour = agxMat * max(colourIn, vec3f(0.0));
  colour = clamp(log2(max(colour, vec3f(1e-6))), vec3f(minEv), vec3f(maxEv));
  colour = (colour - vec3f(minEv)) / (maxEv - minEv);
  colour = agx_contrast(colour);
  // Back to sRGB primaries and to linear light.
  colour = agxMatInverse * colour;
  return pow(clamp(colour, vec3f(0.0), vec3f(1.0)), vec3f(2.2));
}

fn apply_tonemap(colour: vec3f, mapper: i32) -> vec3f {
  if (mapper == 1) {
    return tonemap_aces(colour);
  }
  if (mapper == 2) {
    return tonemap_agx(colour);
  }
  return tonemap_reinhard(colour);
}

fn srgb_encode(colourIn: vec3f) -> vec3f {
  let colour = clamp(colourIn, vec3f(0.0), vec3f(1.0));
  return select(colour * 12.92,
      1.055 * pow(colour, vec3f(1.0 / 2.4)) - vec3f(0.055),
      colour >= vec3f(0.0031308));
}

fn interleavedGradientNoise(pixel: vec2f) -> f32 {
  return fract(52.9829189 * fract(dot(pixel, vec2f(0.06711056, 0.00583715))));
}

fn loadDepth(uv: vec2f) -> f32 {
  let dims = vec2f(textureDimensions(depthTexture, 0));
  return textureLoad(depthTexture, vec2i(clamp(uv * dims, vec2f(0.0), dims - vec2f(1.0))), 0);
}

// Depth-aware upsample of the half-resolution AO/GI target. Taps across a
// depth discontinuity are rejected, so occlusion and bounced light do not
// bleed over silhouettes.
fn upsampleEffects(uv: vec2f, depth: f32) -> vec4f {
  let texel = 1.0 / vec2f(textureDimensions(effectsTexture, 0));
  // (1 - depth) is proportional to 1 / view distance, so comparing it
  // relatively gives a depth-independent edge threshold.
  let reference = max(1.0 - depth, 1e-6);
  var sum = vec4f(0.0);
  var total = 0.0;
  for (var y = -1; y <= 1; y = y + 1) {
    for (var x = -1; x <= 1; x = x + 1) {
      let tapUv = uv + vec2f(f32(x), f32(y)) * texel * 1.5;
      let difference = abs((1.0 - loadDepth(tapUv)) - reference) / reference;
      let weight = max(1.0 - difference * 12.0, 0.0) + 0.0001;
      sum += textureSampleLevel(effectsTexture, sceneSampler, tapUv, 0.0) * weight;
      total += weight;
    }
  }
  return sum / total;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
  let uv = vec2f(input.uv.x, 1.0 - input.uv.y);
  var colour = textureSampleLevel(sceneTexture, sceneSampler, uv, 0.0).rgb;

  let exposure = select(pc.exposureMode.y,
      exposureBuffer.exposure * pc.exposureMode.y, pc.exposureMode.x > 0.5);

  if (pc.effects.x > 0.5 || pc.effects.y > 0.5) {
    let dims = vec2f(textureDimensions(normalTexture, 0));
    let surface = textureLoad(normalTexture, vec2i(clamp(uv * dims, vec2f(0.0), dims - vec2f(1.0))), 0);
    if (surface.a > 0.5) {
      let effects = upsampleEffects(uv, loadDepth(uv));

      // Ambient occlusion only blocks ambient light. The forward pass has
      // already summed direct and ambient, so the occlusion is weakened on
      // pixels that are clearly lit directly.
      let lit = smoothstep(0.25, 1.2, luminance(colour) * exposure);
      var occlusion = mix(1.0, effects.a, pc.effects.x * pc.effects.w);
      occlusion = mix(occlusion, 1.0, lit * 0.65);

      // Bounced light is tinted by the receiving surface: its hue comes from
      // the shaded colour and its reflectance from the stored albedo luma.
      let hue = colour / max(luminance(colour), 1e-4);
      let albedo = surface.b * mix(vec3f(1.0), clamp(hue, vec3f(0.0), vec3f(2.5)), 0.75);
      let bounce = effects.rgb * albedo * (pc.effects.y * pc.effects.z);

      colour = colour * occlusion + bounce;
    }
  }

  if (pc.bloom.x > 0.5) {
    colour += textureSampleLevel(bloomTexture, sceneSampler, uv, 0.0).rgb * pc.bloom.y;
  }

  let mapper = i32(pc.exposure_tonemap.y + 0.5);
  let tone_mapping = pc.exposure_tonemap.z > 0.5;
  let hdr = pc.exposure_tonemap.w > 0.5;

  // White balance in scene-linear light, before the tone curve.
  let temperature = pc.grading.z;
  let tint = pc.grading.w;
  colour *= vec3f(1.0 + temperature * 0.18 - tint * 0.06,
      1.0 + tint * 0.12,
      1.0 - temperature * 0.18 - tint * 0.06);

  if (hdr) {
    colour *= exposure;
    if (tone_mapping) {
      colour = apply_tonemap(colour, mapper);
    } else {
      colour = clamp(colour, vec3f(0.0), vec3f(1.0));
    }
  } else {
    colour = clamp(colour, vec3f(0.0), vec3f(1.0));
  }

  // Lift / gamma / gain on the display-referred linear image.
  let lift = pc.lift.rgb;
  let gamma = max(pc.gamma.rgb, vec3f(0.01));
  let gain = pc.gain.rgb;
  colour = pow(max(colour * gain + lift, vec3f(0.0)), vec3f(1.0) / gamma);

  // The remaining grading runs on the sRGB-encoded image, which is
  // perceptually uniform: contrast pivots on middle grey and 3D LUTs are
  // authored for encoded input.
  colour = srgb_encode(colour);

  // Saturation above 1 behaves as vibrance: muted colours are boosted the
  // most while already saturated ones are protected from clipping.
  let luma = luminance(colour);
  var saturation = pc.grading.y;
  if (saturation > 1.0) {
    let chroma = max(colour.r, max(colour.g, colour.b)) - min(colour.r, min(colour.g, colour.b));
    saturation = 1.0 + (saturation - 1.0) * (1.0 - chroma * 0.75);
  }
  colour = mix(vec3f(luma), colour, saturation);

  // S-shaped contrast around middle grey; it never pushes values out of range.
  let contrast = pc.grading.x;
  if (abs(contrast - 1.0) > 0.001) {
    let x = clamp(colour, vec3f(0.0), vec3f(1.0));
    let below = 0.5 * pow(2.0 * x, vec3f(contrast));
    let above = vec3f(1.0) - 0.5 * pow(2.0 * (vec3f(1.0) - x), vec3f(contrast));
    colour = select(below, above, x >= vec3f(0.5));
  }
  colour = clamp(colour, vec3f(0.0), vec3f(1.0));

  if (pc.gain.w > 0.5 && pc.gamma.w > 0.0) {
    // Sample texel centres so the LUT edges are not extrapolated.
    let size = f32(textureDimensions(lutTexture, 0).x);
    let coord = colour * ((size - 1.0) / size) + vec3f(0.5 / size);
    let graded = textureSampleLevel(lutTexture, sceneSampler, coord, 0.0).rgb;
    colour = mix(colour, graded, clamp(pc.gamma.w, 0.0, 1.0));
  }

  let centered = uv * 2.0 - 1.0;
  let vignette = smoothstep(0.35, 1.25, dot(centered, centered));
  colour *= 1.0 - vignette * pc.lift.a;

  // Half an 8-bit step of noise breaks up banding in the sky gradient.
  colour += vec3f((interleavedGradientNoise(input.position.xy) - 0.5) / 255.0);

  // The canvas format is not sRGB and the colour is already encoded.
  return vec4f(clamp(colour, vec3f(0.0), vec3f(1.0)), 1.0);
}
