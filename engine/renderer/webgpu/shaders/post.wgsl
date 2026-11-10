struct PostConstants {
  exposure_tonemap: vec4f,
  grading: vec4f,
  lift: vec4f,
  gamma: vec4f,
  gain: vec4f,
  effects: vec4f,
  bloom: vec4f,
  exposureMode: vec4f,
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

fn tonemap_reinhard(colour: vec3f) -> vec3f {
  return colour / (vec3f(1.0) + colour);
}

fn tonemap_aces(colour: vec3f) -> vec3f {
  let a = 2.51;
  let b = 0.03;
  let c = 2.43;
  let d = 0.59;
  let e = 0.14;
  return clamp((colour * (a * colour + b)) / (colour * (c * colour + d) + e), vec3f(0.0), vec3f(1.0));
}

fn agx_contrast(x: vec3f) -> vec3f {
  let x2 = x * x;
  let x4 = x2 * x2;
  return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 -
      6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

fn tonemap_agx(colour: vec3f) -> vec3f {
  let agx_mat = mat3x3f(
      0.842479062253094, 0.0423282422610123, 0.0423756549057051,
      0.0784335999999992, 0.878468636469772, 0.0784336,
      0.0792237451477643, 0.0791661274605434, 0.879142973793104);
  let min_ev = -12.47393;
  let max_ev = 4.026069;
  var value = agx_mat * max(colour, vec3f(0.0));
  value = clamp(log2(max(value, vec3f(1e-6))), vec3f(min_ev), vec3f(max_ev));
  value = (value - vec3f(min_ev)) / (max_ev - min_ev);
  return clamp(agx_contrast(value), vec3f(0.0), vec3f(1.0));
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

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
  var colour = textureSample(sceneTexture, sceneSampler, input.uv).rgb;
  let effects = textureSample(effectsTexture, sceneSampler, input.uv);
  let ao = mix(1.0, effects.a, pc.effects.x);
  let gi = effects.rgb * pc.effects.y * pc.effects.z;
  colour = colour * ao + gi;
  colour += textureSample(bloomTexture, sceneSampler, input.uv).rgb *
      pc.bloom.x * pc.bloom.y;

  let exposure = select(pc.exposureMode.y, exposureBuffer.exposure,
      pc.exposureMode.x > 0.5);
  let mapper = i32(pc.exposure_tonemap.y + 0.5);
  let tone_mapping = pc.exposure_tonemap.z > 0.5;
  let hdr = pc.exposure_tonemap.w > 0.5;

  if (hdr) {
    colour *= exposure;
    colour = select(clamp(colour, vec3f(0.0), vec3f(1.0)), apply_tonemap(colour, mapper), tone_mapping);
  } else {
    colour = clamp(colour, vec3f(0.0), vec3f(1.0));
  }

  let temperature = pc.grading.z;
  let tint = pc.grading.w;
  colour.r *= 1.0 + temperature * 0.18;
  colour.b *= 1.0 - temperature * 0.18;
  colour.g *= 1.0 + tint * 0.12;
  colour.r *= 1.0 - tint * 0.06;
  colour.b *= 1.0 - tint * 0.06;

  let lift = pc.lift.rgb;
  let gamma = max(pc.gamma.rgb, vec3f(0.01));
  let gain = pc.gain.rgb;
  colour = pow(max(colour * gain + lift, vec3f(0.0)), vec3f(1.0) / gamma);

  let luma = dot(colour, vec3f(0.2126, 0.7152, 0.0722));
  colour = mix(vec3f(luma), colour, pc.grading.y);
  colour = (colour - vec3f(0.5)) * pc.grading.x + vec3f(0.5);
  colour = clamp(colour, vec3f(0.0), vec3f(1.0));

  if (pc.gain.w > 0.5 && pc.gamma.w > 0.0) {
    let graded = textureSample(lutTexture, sceneSampler, clamp(colour, vec3f(0.0), vec3f(1.0))).rgb;
    colour = mix(colour, graded, clamp(pc.gamma.w, 0.0, 1.0));
  }

  let centered = input.uv * 2.0 - 1.0;
  let vignette = smoothstep(0.35, 1.25, dot(centered, centered));
  colour *= 1.0 - vignette * pc.lift.a;

  colour = pow(max(colour, vec3f(0.0)), vec3f(1.0 / 2.2));
  return vec4f(colour, 1.0);
}
