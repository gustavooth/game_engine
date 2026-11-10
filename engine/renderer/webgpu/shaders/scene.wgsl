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
};

struct ObjectUniforms {
  model: mat4x4f,
  color: vec4f,
  params: vec4f, // cast shadows, metallic, roughness, map flags
  inverseModel: mat4x4f,
  emissive: vec4f,
};

@group(0) @binding(0) var<uniform> globals: Globals;
@group(1) @binding(0) var<uniform> object: ObjectUniforms;
@group(2) @binding(0) var texture0: texture_2d<f32>;
@group(2) @binding(1) var sampler0: sampler;
@group(3) @binding(0) var metallicRoughnessTexture: texture_2d<f32>;
@group(3) @binding(1) var normalTexture: texture_2d<f32>;
@group(3) @binding(2) var occlusionTexture: texture_2d<f32>;
@group(3) @binding(3) var emissiveTexture: texture_2d<f32>;
@group(3) @binding(4) var materialSampler: sampler;
@group(0) @binding(1) var shadowMap: texture_depth_2d_array;
@group(0) @binding(2) var shadowSampler: sampler_comparison;

fn toLinear(colour: vec3f) -> vec3f {
  return pow(max(colour, vec3f(0.0)), vec3f(2.2));
}

fn pbrLight(base: vec3f, metallic: f32, roughness: f32, n: vec3f, v: vec3f, l: vec3f) -> vec3f {
  let h = normalize(v + l + vec3f(0.00001));
  let nl = max(dot(n, l), 0.0);
  let nv = max(dot(n, v), 0.0001);
  let nh = max(dot(n, h), 0.0);
  let vh = max(dot(v, h), 0.0);
  let a = max(roughness * roughness, 0.0025);
  let a2 = a * a;
  let d = nh * nh * (a2 - 1.0) + 1.0;
  let distribution = a2 / max(3.14159265 * d * d, 0.000001);
  let k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
  let geometry = (nv / (nv * (1.0 - k) + k)) * (nl / max(nl * (1.0 - k) + k, 0.0001));
  let f0 = mix(vec3f(0.04), base, metallic);
  let fresnel = f0 + (vec3f(1.0) - f0) * pow(1.0 - vh, 5.0);
  let specular = distribution * geometry * fresnel / max(4.0 * nv * nl, 0.0001);
  let diffuse = (vec3f(1.0) - fresnel) * (1.0 - metallic) * base / 3.14159265;
  return (diffuse + specular) * nl;
}

fn normalFromMap(n: vec3f, worldPosition: vec3f, uv: vec2f) -> vec3f {
  let tangentNormal = textureSample(normalTexture, materialSampler, uv).xyz * 2.0 - 1.0;
  let dp1 = dpdx(worldPosition);
  let dp2 = dpdy(worldPosition);
  let duv1 = dpdx(uv);
  let duv2 = dpdy(uv);
  let dp2perp = cross(dp2, n);
  let dp1perp = cross(n, dp1);
  let tangent = dp2perp * duv1.x + dp1perp * duv2.x;
  let bitangent = dp2perp * duv1.y + dp1perp * duv2.y;
  let invmax = inverseSqrt(max(dot(tangent, tangent), dot(bitangent, bitangent)));
  let tbn = mat3x3f(tangent * invmax, bitangent * invmax, n);
  return normalize(tbn * tangentNormal);
}

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
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
  var output: VertexOutput;
  let worldPosition = object.model * vec4f(input.position, 1.0);
  output.clipPosition = globals.viewProjection * worldPosition;
  output.normal = (transpose(object.inverseModel) * vec4f(input.normal, 0.0)).xyz;
  output.uv = input.uv;
  output.worldPosition = worldPosition.xyz;
  return output;
}

struct FragmentOutput {
  @location(0) colour: vec4f,
  @location(1) normal: vec4f,
};

@fragment
fn fs_main(input: VertexOutput) -> FragmentOutput {
  let texel = textureSample(texture0, sampler0, input.uv);
  let base = texel * object.color;

  var normal = normalize(input.normal);
  let flags = i32(object.params.w + 0.5);
  let hasMetallicRoughness = (flags & 1) != 0;
  let hasNormal = (flags & 2) != 0;
  let hasOcclusion = (flags & 4) != 0;
  let hasEmissive = (flags & 8) != 0;
  let pbrEnabled = globals.pointParams.z > 0.5;

  if (hasNormal) {
    normal = normalFromMap(normal, input.worldPosition, input.uv);
  }

  var metallic = clamp(object.params.y, 0.0, 1.0);
  var roughness = clamp(object.params.z, 0.04, 1.0);
  if (hasMetallicRoughness) {
    let mr = textureSample(metallicRoughnessTexture, materialSampler, input.uv);
    roughness = clamp(object.params.z * mr.g, 0.04, 1.0);
    metallic = clamp(object.params.y * mr.b, 0.0, 1.0);
  }

  let ao = select(1.0, textureSample(occlusionTexture, materialSampler, input.uv).r, hasOcclusion);
  let albedo = toLinear(base.rgb);
  var emissive = toLinear(max(object.emissive.rgb, vec3f(0.0))) * object.emissive.a;
  if (hasEmissive) {
    emissive *= toLinear(textureSample(emissiveTexture, materialSampler, input.uv).rgb);
  }

  let sunDirection = normalize(globals.sun.direction.xyz);
  let sunNdl = max(dot(normal, sunDirection), 0.0);
  let viewDirection = normalize(globals.cameraPosition.xyz - input.worldPosition + vec3f(0.00001));

  // Keep textureSampleCompare in uniform control flow: the cascade layer is
  // part of the coordinate, not a branch around the sampling instruction.
  let viewDistance = length(input.worldPosition - globals.cameraPosition.xyz);
  var cascade = 0;
  let cascadeCount = i32(globals.shadowParams.y + 0.5);
  for (var i = 1; i < 4; i = i + 1) {
    if (i < cascadeCount && viewDistance > globals.cascadeSplits[i - 1]) {
      cascade = i;
    }
  }
  let shadowPosition = globals.shadowViewProjection[cascade] *
      vec4f(input.worldPosition, 1.0);
  let projected = shadowPosition.xyz / max(shadowPosition.w, 0.0001);
  let shadowUv = clamp(projected.xy * vec2f(0.5, -0.5) + vec2f(0.5), vec2f(0.0), vec2f(1.0));
  let texelSize = globals.shadowParams.z;
  let bias = 0.0008 + 0.0006 * f32(cascade);
  let radius = i32(max(1, i32(globals.shadowParams.w * 1.5 + 0.5)));
  var shadowSum = 0.0;
  for (var y = -radius; y <= radius; y = y + 1) {
    for (var x = -radius; x <= radius; x = x + 1) {
      let offset = vec2f(f32(x), f32(y)) * texelSize;
      shadowSum += textureSampleCompare(shadowMap, shadowSampler,
          shadowUv + offset, cascade,
          clamp(projected.z - bias, 0.0, 1.0), vec2i(0));
    }
  }

  let inShadowRange = projected.z >= 0.0 && projected.z <= 1.0 &&
      projected.xy.x >= -1.0 && projected.xy.x <= 1.0 &&
      projected.xy.y >= -1.0 && projected.xy.y <= 1.0;
  let shadowsEnabled = globals.shadowParams.x > 0.5 && sunNdl > 0.0 &&
      inShadowRange && cascade < cascadeCount;
  var shadow = 1.0;
  if (shadowsEnabled) {
    let samples = f32((radius * 2 + 1) * (radius * 2 + 1));
    shadow = mix(1.0, shadowSum / samples, globals.sun.color.a);
  }

  if (base.a < 0.01) {
    discard;
  }

  var color: vec3f;
  if (pbrEnabled) {
    color = pbrLight(albedo, metallic, roughness, normal, viewDirection, sunDirection) *
        globals.sun.color.rgb * globals.sun.direction.w * shadow;
    color += albedo * globals.ambient.rgb * globals.ambient.a * ao * (1.0 - metallic);

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
      let attenuation = pow(clamp(1.0 - distance / range, 0.0, 1.0), 2.0);
      color += pbrLight(albedo, metallic, roughness, normal, viewDirection, direction) *
          light.color.rgb * light.color.w * attenuation * ao;
    }
  } else {
    color = albedo * (globals.ambient.rgb * globals.ambient.a * ao +
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
      color += albedo * light.color.rgb * light.color.w * ndl * attenuation;
    }
  }

  color += emissive;

  if (globals.fogParams.z > 0.5) {
    let distanceToCamera = length(globals.cameraPosition.xyz - input.worldPosition);
    let density = globals.fogParams.x *
        exp(-globals.fogParams.y * max(input.worldPosition.y, 0.0));
    let fog = clamp(1.0 - exp(-density * distanceToCamera), 0.0, 1.0);
    color = mix(color, globals.fogColor.rgb, fog);
  }

  return FragmentOutput(vec4f(color, base.a), vec4f(normal, 1.0));
}
