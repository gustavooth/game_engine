struct BloomConstants {
  params: vec4f,  // direction xy, threshold, unused
  params2: vec4f, // texel size xy, radius, apply threshold
};

@group(0) @binding(0) var sourceTexture: texture_2d<f32>;
@group(0) @binding(1) var sourceSampler: sampler;
@group(0) @binding(2) var<uniform> pc: BloomConstants;

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
  var colour = textureSample(sourceTexture, sourceSampler, uv).rgb;
  if (pc.params2.w > 0.5) {
    colour = max(colour - pc.params.z, vec3f(0.0));
  }

  let step = pc.params.xy * pc.params2.xy;
  let radius = i32(pc.params2.z + 0.5);
  var sum = colour;
  var total = 1.0;
  for (var i = 1; i <= 8; i = i + 1) {
    if (i > radius) {
      break;
    }
    let weight = 1.0 / (1.0 + f32(i));
    sum += (textureSample(sourceTexture, sourceSampler, uv + step * f32(i)).rgb +
            textureSample(sourceTexture, sourceSampler, uv - step * f32(i)).rgb) * weight;
    total += 2.0 * weight;
  }

  return vec4f(sum / total, 1.0);
}
