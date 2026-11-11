struct Sun {
  direction: vec4f,
  color: vec4f,
};

struct PointLight {
  position: vec4f,
  color: vec4f,
};

struct Globals {
  viewProjection: mat4x4f,
  cameraPosition: vec4f,
  ambient: vec4f,
  sun: Sun,
  shadowViewProjection: array<mat4x4f, 4>,
  cascadeSplits: vec4f,
  shadowParams: vec4f,
  pointParams: vec4f,
  pointLights: array<PointLight, 8>,
  inverseViewProjection: mat4x4f,
  atmosphere: vec4f,
  atmosphere2: vec4f,
  fogParams: vec4f,
  fogColor: vec4f,
  time: vec4f,
  cascadeTexel: vec4f,
  shadowFade: vec4f,
  skyZenith: vec4f,
  skyHorizon: vec4f,
  skyGround: vec4f,
  sunDisc: vec4f, // rgb = radiance of the sun disc seen from the ground
};

@group(0) @binding(0) var<uniform> globals: Globals;
// Sky-view LUT rendered by sky_lut.wgsl.
@group(0) @binding(3) var skyLut: texture_2d<f32>;
@group(0) @binding(4) var skyLutSampler: sampler;

const PI = 3.14159265;

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

struct FragmentOutput {
  @location(0) colour: vec4f,
  @location(1) normal: vec4f,
};

@fragment
fn fs_main(input: VertexOutput) -> FragmentOutput {
  let ndc = input.uv * 2.0 - 1.0;
  let nearPoint = globals.inverseViewProjection * vec4f(ndc, 0.0, 1.0);
  let farPoint = globals.inverseViewProjection * vec4f(ndc, 1.0, 1.0);
  let direction = normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);
  let sun = normalize(globals.sun.direction.xyz);

  let elevation = asin(clamp(direction.y, 0.0, 1.0));
  let uv = vec2f(atan2(direction.z, direction.x) / (2.0 * PI) + 0.5,
      sqrt(elevation / (0.5 * PI)));
  // Night glow so the sky never collapses to pure black.
  var colour = textureSampleLevel(skyLut, skyLutSampler, uv, 0.0).rgb + vec3f(0.006, 0.009, 0.018);

  // Sun disc with a soft limb, hidden once it sinks below the horizon.
  let disc = smoothstep(0.99990, 0.99996, dot(direction, sun));
  colour += globals.sunDisc.rgb * disc * smoothstep(-0.01, 0.01, direction.y);

  // Below the horizon there is no terrain, so fade into the haze the fog
  // converges to and then into the light bounced by the ground.
  let below = smoothstep(0.0, 0.35, -direction.y);
  let lower = mix(globals.skyHorizon.rgb, globals.skyGround.rgb, below);
  colour = mix(lower, colour, smoothstep(-0.03, 0.02, direction.y));

  return FragmentOutput(vec4f(colour, 1.0), vec4f(0.0));
}
