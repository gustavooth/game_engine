
struct Globals {
  viewProjection: mat4x4f,
};

@group(0) @binding(0) var<uniform> globals: Globals;
@group(1) @binding(0) var particleTexture: texture_2d<f32>;
@group(1) @binding(1) var particleSampler: sampler;

struct VertexInput {
  @location(0) position: vec3f,
  @location(1) uv: vec2f,
  @location(2) color: vec4f,
};

struct VertexOutput {
  @builtin(position) clipPosition: vec4f,
  @location(0) uv: vec2f,
  @location(1) color: vec4f,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
  var output: VertexOutput;
  output.clipPosition = globals.viewProjection * vec4f(input.position, 1.0);
  output.uv = input.uv;
  output.color = input.color;
  return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
  // Use the sprite as an alpha mask and keep the emitter colour saturated.
  let mask = textureSample(particleTexture, particleSampler, input.uv).a;
  let alpha = input.color.a * mask;
  if (alpha < 0.001) {
    discard;
  }
  return vec4f(input.color.rgb, alpha);
}
