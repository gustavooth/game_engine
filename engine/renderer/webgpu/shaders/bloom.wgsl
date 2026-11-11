// Mip-chain bloom: a 13-tap downsample builds progressively blurrier mips and
// a 9-tap tent upsample adds them back together. Keep in sync with
// engine/shaders/bloom.frag.

struct BloomConstants {
  params: vec4f,  // source texel size xy, mode, upsample radius
  params2: vec4f, // threshold, manual exposure, auto exposure enabled, unused
};

struct ExposureBuffer {
  exposure: f32,
  averageLuminance: f32,
  targetExposure: f32,
  padding: f32,
};

@group(0) @binding(0) var sourceTexture: texture_2d<f32>;
@group(0) @binding(1) var sourceSampler: sampler;
@group(0) @binding(2) var<uniform> pc: BloomConstants;
@group(0) @binding(3) var<storage, read> exposureBuffer: ExposureBuffer;

fn luminance(colour: vec3f) -> f32 {
  return dot(colour, vec3f(0.2126, 0.7152, 0.0722));
}

fn tap(uv: vec2f) -> vec3f {
  return textureSampleLevel(sourceTexture, sourceSampler, uv, 0.0).rgb;
}

// Soft-knee threshold: keeps the hue and fades in smoothly instead of
// clipping, so highlights do not pop when they cross the threshold.
fn prefilter(colourIn: vec3f, threshold: f32) -> vec3f {
  let colour = min(colourIn, vec3f(threshold * 48.0 + 16.0));
  let brightness = max(colour.r, max(colour.g, colour.b));
  let knee = max(threshold * 0.5, 0.0001);
  var soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
  soft = soft * soft / (4.0 * knee);
  let contribution = max(soft, brightness - threshold) / max(brightness, 0.0001);
  return colour * contribution;
}

// Weighting each 2x2 block by 1 / (1 + luma) stops a single very bright
// pixel from flickering across the whole mip chain.
fn karisBox(a: vec3f, b: vec3f, c: vec3f, d: vec3f) -> vec3f {
  let wa = 1.0 / (1.0 + luminance(a));
  let wb = 1.0 / (1.0 + luminance(b));
  let wc = 1.0 / (1.0 + luminance(c));
  let wd = 1.0 / (1.0 + luminance(d));
  return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
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
  let texel = pc.params.xy;
  let mode = i32(pc.params.z + 0.5);

  if (mode == 2) {
    // 3x3 tent filter.
    let r = texel * pc.params.w;
    var sum = tap(uv) * 4.0;
    sum += (tap(uv + vec2f(-r.x, 0.0)) + tap(uv + vec2f(r.x, 0.0)) +
            tap(uv + vec2f(0.0, -r.y)) + tap(uv + vec2f(0.0, r.y))) * 2.0;
    sum += tap(uv + vec2f(-r.x, -r.y)) + tap(uv + vec2f(r.x, -r.y)) +
           tap(uv + vec2f(-r.x, r.y)) + tap(uv + vec2f(r.x, r.y));
    return vec4f(sum / 16.0, 1.0);
  }

  var a = tap(uv + texel * vec2f(-2.0, -2.0));
  var b = tap(uv + texel * vec2f(0.0, -2.0));
  var c = tap(uv + texel * vec2f(2.0, -2.0));
  var d = tap(uv + texel * vec2f(-2.0, 0.0));
  var e = tap(uv);
  var f = tap(uv + texel * vec2f(2.0, 0.0));
  var g = tap(uv + texel * vec2f(-2.0, 2.0));
  var h = tap(uv + texel * vec2f(0.0, 2.0));
  var i = tap(uv + texel * vec2f(2.0, 2.0));
  var j = tap(uv + texel * vec2f(-1.0, -1.0));
  var k = tap(uv + texel * vec2f(1.0, -1.0));
  var l = tap(uv + texel * vec2f(-1.0, 1.0));
  var m = tap(uv + texel * vec2f(1.0, 1.0));

  var colour: vec3f;
  if (mode == 0) {
    // The threshold is authored for an exposed image (1.0 = display white),
    // so convert it to scene units with the current exposure.
    var exposure = pc.params2.y;
    if (pc.params2.z > 0.5) {
      exposure *= clamp(exposureBuffer.exposure, 0.0001, 100000.0);
    }
    let threshold = pc.params2.x / max(exposure, 0.0001);
    a = prefilter(a, threshold); b = prefilter(b, threshold); c = prefilter(c, threshold);
    d = prefilter(d, threshold); e = prefilter(e, threshold); f = prefilter(f, threshold);
    g = prefilter(g, threshold); h = prefilter(h, threshold); i = prefilter(i, threshold);
    j = prefilter(j, threshold); k = prefilter(k, threshold); l = prefilter(l, threshold);
    m = prefilter(m, threshold);
    colour = karisBox(j, k, l, m) * 0.5 +
        (karisBox(a, b, d, e) + karisBox(b, c, e, f) +
         karisBox(d, e, g, h) + karisBox(e, f, h, i)) * 0.125;
  } else {
    colour = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 +
        (j + k + l + m) * 0.125;
  }
  return vec4f(max(colour, vec3f(0.0)), 1.0);
}
