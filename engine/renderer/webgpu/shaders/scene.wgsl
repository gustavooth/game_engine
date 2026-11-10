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
  shadowViewProjection: mat4x4f,
  pointParams: vec4f,
  pointLights: array<PointLight, 8>,
};

struct ObjectUniforms {
  model: mat4x4f,
  color: vec4f,
  params: vec4f,
};

@group(0) @binding(0) var<uniform> globals: Globals;
@group(1) @binding(0) var<uniform> object: ObjectUniforms;
@group(2) @binding(0) var texture0: texture_2d<f32>;
@group(2) @binding(1) var sampler0: sampler;
@group(0) @binding(1) var shadowMap: texture_depth_2d;
@group(0) @binding(2) var shadowSampler: sampler_comparison;

struct VertexInput {
  @location(0) position: vec3f,
  @location(1) normal: vec3f,
  @location(2) uv: vec2f,
};

struct VertexOutput {
  @builtin(position) clipPosition: vec4f,
  @location(0) normal: vec3f,
  @location(1) uv: vec2f,
  @location(2) worldPosition: vec3f,
  @location(3) shadowPosition: vec4f,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
  var output: VertexOutput;
  let worldPosition = object.model * vec4f(input.position, 1.0);
  output.clipPosition = globals.viewProjection * worldPosition;
  output.normal = (object.model * vec4f(input.normal, 0.0)).xyz;
  output.uv = input.uv;
  output.worldPosition = worldPosition.xyz;
  output.shadowPosition = globals.shadowViewProjection * worldPosition;
  return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
  let texel = textureSample(texture0, sampler0, input.uv);
  let base = texel * object.color;

  let normal = normalize(input.normal);
  let sunDirection = normalize(globals.sun.direction.xyz);
  let sunNdl = max(dot(normal, sunDirection), 0.0);

  // Keep textureSampleCompare in uniform control flow: compute the filtered
  // visibility for every fragment and apply it only where shadows are enabled.
  let projected = input.shadowPosition.xyz / max(input.shadowPosition.w, 0.0001);
  let shadowUv = clamp(projected.xy * vec2f(0.5, -0.5) + vec2f(0.5), vec2f(0.0), vec2f(1.0));
  let shadowDepth = clamp(projected.z - 0.0015, 0.0, 1.0);
  let texelSize = globals.pointParams.z;
  var shadowSum = 0.0;
  for (var y = -1; y <= 1; y = y + 1) {
    for (var x = -1; x <= 1; x = x + 1) {
      let offset = vec2f(f32(x), f32(y)) * texelSize;
      shadowSum += textureSampleCompare(shadowMap, shadowSampler, shadowUv + offset, shadowDepth);
    }
  }

  let inShadowRange = projected.z >= 0.0 && projected.z <= 1.0 &&
      projected.xy.x >= -1.0 && projected.xy.x <= 1.0 &&
      projected.xy.y >= -1.0 && projected.xy.y <= 1.0;
  let shadowsEnabled = globals.pointParams.y > 0.5 && sunNdl > 0.0 && inShadowRange;
  var shadow = 1.0;
  if (shadowsEnabled) {
    shadow = mix(1.0, shadowSum / 9.0, globals.sun.color.a);
  }

  if (base.a < 0.01) {
    discard;
  }

  var color = base.rgb * (globals.ambient.rgb * globals.ambient.a +
      globals.sun.color.rgb * globals.sun.direction.w * sunNdl * shadow);

  let pointCount = u32(globals.pointParams.x);
  for (var i = 0u; i < 8u; i = i + 1u) {
    if (i >= pointCount) {
      break;
    }
    let light = globals.pointLights[i];
    let toLight = light.position.xyz - input.worldPosition;
    let distance = length(toLight);
    let range = light.position.w;
    if (distance >= range) {
      continue;
    }
    let direction = toLight / max(distance, 0.0001);
    let ndl = max(dot(normal, direction), 0.0);
    let attenuation = pow(clamp(1.0 - distance / range, 0.0, 1.0), 2.0);
    color += base.rgb * light.color.rgb * light.color.w * ndl * attenuation;
  }

  return vec4f(color, base.a);
}
