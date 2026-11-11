struct ExposureBuffer {
  exposure: f32,
  averageLuminance: f32,
  targetExposure: f32,
  padding: f32,
};

struct ExposureConstants {
  params: vec4f,  // dt, adaptation speed, min EV, max EV
  params2: vec4f, // grid size, unused, auto enabled, unused
};

@group(0) @binding(0) var<storage, read_write> exposureBuffer: ExposureBuffer;
@group(0) @binding(1) var sceneTexture: texture_2d<f32>;
@group(0) @binding(2) var<uniform> pc: ExposureConstants;

var<workgroup> histogram: array<atomic<u32>, 256>;

@compute @workgroup_size(256)
fn main(@builtin(local_invocation_id) localID: vec3u) {
  let thread = localID.x;
  if (thread < 256u) {
    atomicStore(&histogram[thread], 0u);
  }
  workgroupBarrier();

  let grid = i32(max(pc.params2.x, 8.0));
  let total = u32(grid * grid);
  let dimensions = textureDimensions(sceneTexture, 0);

  var index = thread;
  loop {
    if (index >= total) {
      break;
    }
    let x = index % u32(grid);
    let y = index / u32(grid);
    let uv = (vec2f(f32(x), f32(y)) + vec2f(0.5)) / f32(grid);
    var texel = vec2u(uv * vec2f(dimensions));
    let maxTexel = dimensions - vec2u(1u);
    texel = min(texel, maxTexel);
    let colour = textureLoad(sceneTexture, texel, 0).rgb;
    let luminance = max(dot(colour, vec3f(0.2126, 0.7152, 0.0722)), 1e-4);
    let logLuminance = log2(luminance);
    let bin = u32(clamp((logLuminance + 12.0) / 16.0 * 255.0, 0.0, 255.0));
    atomicAdd(&histogram[bin], 1u);
    index += 256u;
  }
  workgroupBarrier();

  if (thread != 0u) {
    return;
  }

  var totalSamples = 0u;
  for (var i = 0; i < 256; i = i + 1) {
    totalSamples += atomicLoad(&histogram[i]);
  }
  if (totalSamples == 0u) {
    return;
  }

  // Meter on the 15%..92% range of the histogram: the darkest pixels and the
  // brightest ones (sun, emissives, highlights) do not drive the exposure.
  let low = f32(totalSamples) * 0.15;
  let high = f32(totalSamples) * 0.92;
  var cumulative = 0.0;
  var weightedLog = 0.0;
  var count = 0.0;
  for (var i = 0; i < 256; i = i + 1) {
    let binCount = f32(atomicLoad(&histogram[i]));
    let next = cumulative + binCount;
    // Portion of this bin that falls inside the metered range.
    let inside = max(min(next, high) - max(cumulative, low), 0.0);
    if (inside > 0.0) {
      let binLog = -12.0 + (f32(i) + 0.5) / 255.0 * 16.0;
      weightedLog += binLog * inside;
      count += inside;
    }
    cumulative = next;
  }

  if (count <= 0.0) {
    return;
  }

  let averageLog = weightedLog / count;
  // Expose the metered range slightly above middle grey: outdoor scenes read
  // as sunlit instead of overcast.
  let key = 0.20;
  let targetExposure = exp2(clamp(log2(key) - averageLog, pc.params.z, pc.params.w));
  var current = exposureBuffer.exposure;
  if (!(current > 0.0) || current > 1.0e9) {
    current = targetExposure;
  }
  // The eye adapts to bright light faster than to darkness.
  let rate = select(pc.params.y, pc.params.y * 1.6, targetExposure < current);
  let speed = 1.0 - exp(-max(rate, 0.0) * max(pc.params.x, 0.0));
  exposureBuffer.exposure = mix(current, targetExposure, clamp(speed, 0.0, 1.0));
  exposureBuffer.averageLuminance = exp2(averageLog);
  exposureBuffer.targetExposure = targetExposure;
}
