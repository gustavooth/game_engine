struct Globals {
  viewProjection: mat4x4f,
};
struct ObjectUniforms {
  model: mat4x4f,
  color: vec4f,
  params: vec4f,
  inverseModel: mat4x4f,
};
@group(0) @binding(0) var<uniform> globals: Globals;
@group(1) @binding(0) var<uniform> object: ObjectUniforms;
@group(2) @binding(0) var tex: texture_2d<f32>;
@group(2) @binding(1) var texSampler: sampler;
struct VertexOutput { @builtin(position) position: vec4f, @location(0) uv: vec2f };
@vertex fn vs_main(@location(0) position: vec3f, @location(2) uv: vec2f) -> VertexOutput {
  var output: VertexOutput;
  output.position = globals.viewProjection * object.model * vec4f(position, 1.0);
  output.uv = uv;
  return output;
}
struct FragmentOutput {
  @location(0) colour: vec4f,
  @location(1) normal: vec4f,
};

@fragment fn fs_main(input: VertexOutput) -> FragmentOutput {
  return FragmentOutput(textureSample(tex, texSampler, input.uv) * object.color, vec4f(0.0));
}
