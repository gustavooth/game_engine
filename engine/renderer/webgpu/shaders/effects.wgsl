// Screen-space ambient occlusion + one bounce of screen-space global
// illumination, rendered at half resolution. rgb = bounced light, a = AO.
// Keep in sync with engine/shaders/effects.frag.

struct EffectsParams {
  inverseViewProjection: mat4x4f,
  viewProjection: mat4x4f,
  cameraPosition: vec4f,
  params: vec4f,  // ao radius, ao intensity, gi intensity, gi distance
  params2: vec4f, // samples, projection scale y, aspect, flags
};

@group(0) @binding(0) var sceneColour: texture_2d<f32>;
@group(0) @binding(1) var sceneNormal: texture_2d<f32>;
@group(0) @binding(2) var sceneDepth: texture_depth_2d;
@group(0) @binding(3) var colourSampler: sampler;
@group(0) @binding(4) var depthSampler: sampler;
@group(0) @binding(5) var<uniform> pc: EffectsParams;

const PI = 3.14159265;
const GOLDEN_ANGLE = 2.39996323;

fn interleavedGradientNoise(pixel: vec2f) -> f32 {
  return fract(52.9829189 * fract(dot(pixel, vec2f(0.06711056, 0.00583715))));
}

fn octahedralDecode(encoded: vec2f) -> vec3f {
  let f = encoded * 2.0 - vec2f(1.0);
  var n = vec3f(f, 1.0 - abs(f.x) - abs(f.y));
  let t = clamp(-n.z, 0.0, 1.0);
  n.x += select(t, -t, n.x >= 0.0);
  n.y += select(t, -t, n.y >= 0.0);
  return normalize(n);
}

fn texelCoord(uv: vec2f) -> vec2i {
  let dims = vec2f(textureDimensions(sceneDepth, 0));
  return vec2i(clamp(uv * dims, vec2f(0.0), dims - vec2f(1.0)));
}

fn reconstructWorld(uv: vec2f, depth: f32) -> vec3f {
  let ndc = vec2f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
  let world = pc.inverseViewProjection * vec4f(ndc, depth, 1.0);
  return world.xyz / world.w;
}

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

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
  let uv = vec2f(input.uv.x, 1.0 - input.uv.y);
  let centre = texelCoord(uv);
  let depth = textureLoad(sceneDepth, centre, 0);
  let surface = textureLoad(sceneNormal, centre, 0);
  if (depth >= 0.99999 || surface.a < 0.5) {
    return vec4f(0.0, 0.0, 0.0, 1.0);
  }

  let normal = octahedralDecode(surface.xy);
  let position = reconstructWorld(uv, depth);
  let viewDepth = max((pc.viewProjection * vec4f(position, 1.0)).w, 0.05);

  let flags = i32(pc.params2.w);
  let aoEnabled = (flags & 1) != 0;
  let giEnabled = (flags & 2) != 0;
  let samples = i32(pc.params2.x);

  // World radii projected to UV space at this depth.
  let projected = vec2f(pc.params2.y / pc.params2.z, pc.params2.y) * (0.5 / viewDepth);
  let aoRadius = pc.params.x;
  // Bounced light is gathered from a wider area than the contact occlusion.
  let giRadius = min(pc.params.w, max(aoRadius * 3.0, 0.48 * viewDepth / pc.params2.y));
  let aoUvRadius = min(projected * aoRadius, vec2f(0.12));
  let giUvRadius = min(projected * giRadius, vec2f(0.30));

  let rotation = interleavedGradientNoise(input.position.xy) * 2.0 * PI;
  let bias = 0.02 + 0.0015 * viewDepth;

  var aoSamples = samples;
  var giSamples = 0;
  if (giEnabled) {
    aoSamples = select(0, (samples + 1) / 2, aoEnabled);
    giSamples = samples - aoSamples;
  }

  var occlusion = 0.0;
  for (var i = 0; i < 32; i = i + 1) {
    if (i >= aoSamples) {
      break;
    }
    // Spiral over the disc; sqrt keeps the samples uniform in area.
    let radius = sqrt((f32(i) + 0.5) / f32(aoSamples));
    let angle = f32(i) * GOLDEN_ANGLE + rotation;
    let sampleUv = uv + vec2f(cos(angle), sin(angle)) * radius * aoUvRadius;
    if (sampleUv.x <= 0.0 || sampleUv.x >= 1.0 || sampleUv.y <= 0.0 || sampleUv.y >= 1.0) {
      continue;
    }
    let sampleDepth = textureLoad(sceneDepth, texelCoord(sampleUv), 0);
    if (sampleDepth >= 0.99999) {
      continue;
    }
    // Obscurance estimator (McGuire et al.): each visible point occludes by
    // how far it rises above the tangent plane, falling off with distance so
    // geometry far behind a silhouette does not create dark halos.
    let v = reconstructWorld(sampleUv, sampleDepth) - position;
    let vv = dot(v, v);
    let vn = dot(v, normal);
    let range = max(1.0 - vv / (aoRadius * aoRadius), 0.0);
    occlusion += max(vn - bias, 0.0) / (vv + 0.0001) * range * range;
  }

  var bounce = vec3f(0.0);
  for (var i = 0; i < 32; i = i + 1) {
    if (i >= giSamples) {
      break;
    }
    let radius = sqrt((f32(i) + 0.5) / f32(giSamples));
    let angle = f32(i) * GOLDEN_ANGLE + rotation + 1.7;
    let sampleUv = uv + vec2f(cos(angle), sin(angle)) * radius * giUvRadius;
    if (sampleUv.x <= 0.0 || sampleUv.x >= 1.0 || sampleUv.y <= 0.0 || sampleUv.y >= 1.0) {
      continue;
    }
    let coord = texelCoord(sampleUv);
    let sampleDepth = textureLoad(sceneDepth, coord, 0);
    let sampleSurface = textureLoad(sceneNormal, coord, 0);
    if (sampleDepth >= 0.99999 || sampleSurface.a < 0.5) {
      continue;
    }
    let v = reconstructWorld(sampleUv, sampleDepth) - position;
    let distanceToSample = length(v);
    if (distanceToSample < 0.02 || distanceToSample > pc.params.w) {
      continue;
    }
    let direction = v / distanceToSample;
    // Light leaves the sender toward the receiver and arrives above the
    // receiver's horizon; both cosines must be positive.
    let receive = max(dot(normal, direction), 0.0);
    let send = max(dot(octahedralDecode(sampleSurface.xy), -direction), 0.0);
    let falloff = 1.0 - distanceToSample / pc.params.w;
    let radiance = min(textureSampleLevel(sceneColour, colourSampler, sampleUv, 0.0).rgb, vec3f(6.0));
    bounce += radiance * (receive * send * falloff * falloff);
  }

  var ao = 1.0;
  if (aoEnabled && aoSamples > 0) {
    ao = clamp(1.0 - occlusion * (2.0 * aoRadius / f32(aoSamples)), 0.0, 1.0);
  }
  var indirect = vec3f(0.0);
  if (giSamples > 0) {
    indirect = bounce * (2.0 / f32(giSamples));
  }
  return vec4f(indirect, ao);
}
