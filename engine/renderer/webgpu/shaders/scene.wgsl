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
  cascadeTexel: vec4f, // world size of one shadow texel per cascade
  shadowFade: vec4f,   // shadow distance, fade length, cascade blend fraction
  skyZenith: vec4f,
  skyHorizon: vec4f,
  skyGround: vec4f,
  sunDisc: vec4f,
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
@group(0) @binding(3) var skyLut: texture_2d<f32>;
@group(0) @binding(4) var skyLutSampler: sampler;

// Keep this file in sync with engine/shaders/3d.frag (the Vulkan backend).

const PI = 3.14159265;

fn toLinear(colour: vec3f) -> vec3f {
  return pow(max(colour, vec3f(0.0)), vec3f(2.2));
}

fn luminance(colour: vec3f) -> f32 {
  return dot(colour, vec3f(0.2126, 0.7152, 0.0722));
}

fn octahedralEncode(direction: vec3f) -> vec2f {
  let n = direction / (abs(direction.x) + abs(direction.y) + abs(direction.z));
  let wrapped = (vec2f(1.0) - abs(n.yx)) *
      vec2f(select(-1.0, 1.0, n.x >= 0.0), select(-1.0, 1.0, n.y >= 0.0));
  return select(wrapped, n.xy, n.z >= 0.0) * 0.5 + vec2f(0.5);
}

fn interleavedGradientNoise(pixel: vec2f) -> f32 {
  return fract(52.9829189 * fract(dot(pixel, vec2f(0.06711056, 0.00583715))));
}

// ---------------------------------------------------------------------------
// Shadows
// ---------------------------------------------------------------------------

const POISSON = array<vec2f, 16>(
    vec2f(-0.94201624, -0.39906216), vec2f(0.94558609, -0.76890725),
    vec2f(-0.09418410, -0.92938870), vec2f(0.34495938, 0.29387760),
    vec2f(-0.91588581, 0.45771432), vec2f(-0.81544232, -0.87912464),
    vec2f(-0.38277543, 0.27676845), vec2f(0.97484398, 0.75648379),
    vec2f(0.44323325, -0.97511554), vec2f(0.53742981, -0.47373420),
    vec2f(-0.26496911, -0.41893023), vec2f(0.79197514, 0.19090188),
    vec2f(-0.24188840, 0.99706507), vec2f(-0.81409955, 0.91437590),
    vec2f(0.19984126, 0.78641367), vec2f(0.14383161, -0.14100790));

// Light-space coordinates of a cascade; xy in [0, 1] inside the map.
fn cascadeCoord(cascade: i32, position: vec3f) -> vec3f {
  let clip = globals.shadowViewProjection[cascade] * vec4f(position, 1.0);
  return vec3f(clip.xy * vec2f(0.5, -0.5) + vec2f(0.5), clip.z);
}

fn filterCascade(cascade: i32, coord: vec3f, rotation: mat2x2f) -> f32 {
  if (coord.z >= 1.0) {
    return 1.0;
  }

  // Keep the penumbra the same size in world units on every cascade.
  let texel = globals.shadowParams.z;
  let worldTexel = max(globals.cascadeTexel[cascade], 0.0001);
  let softness = globals.shadowParams.w;
  let radius = clamp(softness * 0.035 / worldTexel, 1.0, 5.0) * texel;
  let depth = clamp(coord.z, 0.0, 1.0);

  // Four taps on the outside of the kernel first: when they agree the pixel
  // is fully lit or fully shadowed and the remaining taps are skipped.
  var sum = 0.0;
  for (var i = 0; i < 4; i = i + 1) {
    sum += textureSampleCompareLevel(shadowMap, shadowSampler,
        coord.xy + rotation * POISSON[i] * radius, cascade, depth);
  }
  if (sum <= 0.0 || sum >= 4.0) {
    return sum * 0.25;
  }

  var taps = 16;
  if (softness < 0.8) {
    taps = 8;
  } else if (softness < 1.2) {
    taps = 12;
  }
  for (var i = 4; i < taps; i = i + 1) {
    sum += textureSampleCompareLevel(shadowMap, shadowSampler,
        coord.xy + rotation * POISSON[i] * radius, cascade, depth);
  }
  return sum / f32(taps);
}

fn sunShadow(position: vec3f, normal: vec3f, ndl: f32, pixel: vec2f) -> f32 {
  let cascadeCount = i32(globals.shadowParams.y + 0.5);
  let viewDistance = length(position - globals.cameraPosition.xyz);
  let fadeStart = globals.shadowFade.x - globals.shadowFade.y;
  if (cascadeCount < 1 || viewDistance >= globals.shadowFade.x) {
    return 1.0;
  }

  let angle = interleavedGradientNoise(pixel) * 2.0 * PI;
  let s = sin(angle);
  let c = cos(angle);
  let rotation = mat2x2f(c, s, -s, c);

  // Pick the most detailed cascade that contains the pixel.
  let slope = 1.0 - ndl;
  var cascade = cascadeCount - 1;
  var coord = vec3f(0.0);
  var edge = 1.0;
  for (var i = 0; i < cascadeCount; i = i + 1) {
    // Normal offset: moves the lookup off the surface by about one texel,
    // more at grazing angles, which removes acne without a depth bias that
    // would detach the shadow from its caster.
    let offset = normal * globals.cascadeTexel[i] * (1.0 + 2.5 * slope);
    let candidate = cascadeCoord(i, position + offset);
    let centred = abs(candidate.xy * 2.0 - vec2f(1.0));
    let candidateEdge = max(centred.x, centred.y);
    if (candidateEdge < 1.0 || i == cascadeCount - 1) {
      cascade = i;
      coord = candidate;
      edge = candidateEdge;
      break;
    }
  }

  if (edge >= 1.0) {
    return 1.0;
  }

  var shadow = filterCascade(cascade, coord, rotation);

  // Cross-fade into the next cascade near the border to hide the seam.
  let blend = smoothstep(1.0 - globals.shadowFade.z, 1.0, edge);
  if (blend > 0.0) {
    var next = 1.0;
    if (cascade + 1 < cascadeCount) {
      let offset = normal * globals.cascadeTexel[cascade + 1] * (1.0 + 2.5 * slope);
      next = filterCascade(cascade + 1, cascadeCoord(cascade + 1, position + offset), rotation);
    }
    shadow = mix(shadow, next, blend);
  }

  shadow = mix(shadow, 1.0, smoothstep(fadeStart, globals.shadowFade.x, viewDistance));
  return mix(1.0, shadow, globals.sun.color.a);
}

// ---------------------------------------------------------------------------
// Image based ambient
// ---------------------------------------------------------------------------

// Analytic sky gradient built from the colours sampled from the atmosphere
// model on the CPU.
fn skyGradient(direction: vec3f) -> vec3f {
  let sky = mix(globals.skyHorizon.rgb, globals.skyZenith.rgb,
      sqrt(clamp(direction.y, 0.0, 1.0)));
  return mix(sky, globals.skyGround.rgb, smoothstep(0.0, 0.25, -direction.y));
}

// The rendered sky itself (sun glow and sunset gradients included).
fn skyReflection(direction: vec3f) -> vec3f {
  if (globals.fogParams.w < 0.5) {
    return skyGradient(direction);
  }
  let elevation = asin(clamp(direction.y, 0.0, 1.0));
  let uv = vec2f(atan2(direction.z, direction.x) / (2.0 * PI) + 0.5,
      sqrt(elevation / (0.5 * PI)));
  let sky = textureSampleLevel(skyLut, skyLutSampler, uv, 0.0).rgb + vec3f(0.006, 0.009, 0.018);
  return mix(sky, globals.skyGround.rgb, smoothstep(0.0, 0.25, -direction.y));
}

// Cosine-weighted irradiance of a sky hemisphere over a ground hemisphere.
fn skyIrradiance(normal: vec3f) -> vec3f {
  let sky = mix(globals.skyHorizon.rgb, globals.skyZenith.rgb, 0.65);
  return mix(globals.skyGround.rgb, sky, normal.y * 0.5 + 0.5);
}

// Split-sum environment BRDF, analytic fit (Karis).
fn environmentBrdf(f0: vec3f, roughness: f32, nv: f32) -> vec3f {
  let c0 = vec4f(-1.0, -0.0275, -0.572, 0.022);
  let c1 = vec4f(1.0, 0.0425, 1.04, -0.04);
  let r = roughness * c0 + c1;
  let a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
  let scaleBias = vec2f(-1.04, 1.04) * a004 + r.zw;
  return f0 * scaleBias.x + vec3f(scaleBias.y);
}

// ---------------------------------------------------------------------------
// Direct lighting
// ---------------------------------------------------------------------------

// Cook-Torrance with GGX, height-correlated Smith visibility and Schlick
// Fresnel. Light intensities are radiance scales (a white surface facing a
// light of intensity 1 has radiance 1), so the BRDF is premultiplied by pi.
fn pbrLight(diffuseColour: vec3f, f0: vec3f, roughness: f32, n: vec3f, v: vec3f, l: vec3f) -> vec3f {
  let nl = max(dot(n, l), 0.0);
  if (nl <= 0.0) {
    return vec3f(0.0);
  }
  let h = normalize(v + l);
  let nv = max(dot(n, v), 0.0001);
  let nh = max(dot(n, h), 0.0);
  let vh = max(dot(v, h), 0.0);

  let a = max(roughness * roughness, 0.002);
  let a2 = a * a;
  let d = (nh * a2 - nh) * nh + 1.0;
  let distribution = a2 / (d * d); // GGX * pi
  let visibility = 0.5 / max(mix(2.0 * nl * nv, nl + nv, a), 0.0001);
  let fresnel = f0 + (vec3f(1.0) - f0) * pow(1.0 - vh, 5.0);

  // The clamp keeps mirror-like highlights from turning into fireflies.
  let specular = min(distribution * visibility * fresnel, vec3f(64.0));
  let diffuse = diffuseColour * (vec3f(1.0) - fresnel);
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
  let scale = max(dot(tangent, tangent), dot(bitangent, bitangent));
  if (scale <= 1e-20) {
    return n;
  }
  let invmax = inverseSqrt(scale);
  let tbn = mat3x3f(tangent * invmax, bitangent * invmax, n);
  return normalize(tbn * tangentNormal);
}

// Exponential height fog integrated along the view ray, tinted toward the
// sun the way haze scatters light forward.
fn applyFog(colour: vec3f, position: vec3f) -> vec3f {
  if (globals.fogParams.z < 0.5) {
    return colour;
  }
  let ray = position - globals.cameraPosition.xyz;
  let distanceToCamera = length(ray);
  let falloff = globals.fogParams.y;
  let rise = clamp(falloff * ray.y, -20.0, 20.0);
  var column = 1.0;
  if (abs(rise) > 0.001) {
    column = (1.0 - exp(-rise)) / rise;
  }
  let density = globals.fogParams.x *
      exp(-falloff * max(globals.cameraPosition.y, 0.0)) * column;
  let fog = 1.0 - exp(-density * distanceToCamera);

  var fogColour = globals.fogColor.rgb;
  if (globals.fogParams.w > 0.5) {
    let towardSun = max(dot(ray / max(distanceToCamera, 0.0001),
        normalize(globals.sun.direction.xyz)), 0.0);
    fogColour += globals.sun.color.rgb * globals.sun.direction.w * (0.25 * pow(towardSun, 8.0));
  }
  return mix(colour, fogColour, clamp(fog, 0.0, 1.0));
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

  let geometricNormal = normalize(input.normal);
  var normal = geometricNormal;
  let flags = i32(object.params.w + 0.5);
  let hasMetallicRoughness = (flags & 1) != 0;
  let hasNormal = (flags & 2) != 0;
  let hasOcclusion = (flags & 4) != 0;
  let hasEmissive = (flags & 8) != 0;
  let isWater = (flags & 16) != 0;
  let pbrEnabled = globals.pointParams.z > 0.5;

  if (hasNormal) {
    normal = normalFromMap(normal, input.worldPosition, input.uv);
  }

  var metallic = clamp(object.params.y, 0.0, 1.0);
  var roughness = clamp(object.params.z, 0.045, 1.0);
  if (hasMetallicRoughness) {
    let mr = textureSample(metallicRoughnessTexture, materialSampler, input.uv);
    roughness = clamp(object.params.z * mr.g, 0.045, 1.0);
    metallic = clamp(object.params.y * mr.b, 0.0, 1.0);
  }

  let ao = select(1.0, textureSample(occlusionTexture, materialSampler, input.uv).r, hasOcclusion);
  let albedo = toLinear(base.rgb);
  var emissive = toLinear(max(object.emissive.rgb, vec3f(0.0))) * object.emissive.a;
  if (hasEmissive) {
    emissive *= toLinear(textureSample(emissiveTexture, materialSampler, input.uv).rgb);
  }

  // Every implicit-derivative sample is above this point.
  if (base.a < 0.01) {
    discard;
  }

  let sunDirection = normalize(globals.sun.direction.xyz);
  let sunRadiance = globals.sun.color.rgb * globals.sun.direction.w;
  let sunNdl = max(dot(normal, sunDirection), 0.0);
  let viewDirection = normalize(globals.cameraPosition.xyz - input.worldPosition + vec3f(0.00001));

  var shadow = 1.0;
  if (globals.shadowParams.x > 0.5 && sunNdl > 0.0) {
    shadow = sunShadow(input.worldPosition, geometricNormal,
        max(dot(geometricNormal, sunDirection), 0.0), input.clipPosition.xy);
  }

  var color: vec3f;
  if (isWater) {
    let t = globals.time.x;
    let p = input.worldPosition.xz;
    let waveA = sin(p.x * 1.3 + t * 1.7) * cos(p.y * 1.1 - t * 1.3);
    let waveB = sin(p.y * 1.7 - t * 1.1) * cos(p.x * 1.4 + t * 1.6);
    let waterNormal = normalize(vec3f(waveA * 0.10, 1.0, waveB * 0.10));
    let nv = max(dot(waterNormal, viewDirection), 0.0);
    let fresnel = 0.02 + 0.98 * pow(1.0 - nv, 5.0);

    // Body colour lit by the sky and the sun, reflection of the sky on top.
    let body = albedo * (skyIrradiance(waterNormal) +
        sunRadiance * max(dot(waterNormal, sunDirection), 0.0) * shadow);
    let reflection = skyReflection(reflect(-viewDirection, waterNormal));
    color = mix(body, reflection, fresnel);
    color += pbrLight(vec3f(0.0), vec3f(0.02), 0.06, waterNormal, viewDirection, sunDirection) *
        sunRadiance * shadow;
    return FragmentOutput(vec4f(applyFog(color, input.worldPosition), 1.0), vec4f(0.0));
  }

  var diffuseColour = albedo;
  if (pbrEnabled) {
    diffuseColour = albedo * (1.0 - metallic);
    let f0 = mix(vec3f(0.04), albedo, metallic);
    let nv = max(dot(normal, viewDirection), 0.0001);

    // Sun.
    color = pbrLight(diffuseColour, f0, roughness, normal, viewDirection, sunDirection) *
        sunRadiance * shadow;

    // Sky and ground: diffuse irradiance plus a reflection of the sky that
    // blurs into the irradiance as the surface gets rougher.
    let irradiance = skyIrradiance(normal);
    let reflected = reflect(-viewDirection, normal);
    let environment = mix(skyReflection(reflected), irradiance, roughness * roughness);
    // Reflections that would come from below the surface are occluded by it.
    let horizon = clamp(1.0 + dot(reflected, geometricNormal), 0.0, 1.0);
    let specularOcclusion = clamp(pow(nv + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao,
        0.0, 1.0) * horizon * horizon;
    color += diffuseColour * irradiance * ao;
    color += environment * environmentBrdf(f0, roughness, nv) * specularOcclusion;

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
      let falloff = clamp(1.0 - distance / range, 0.0, 1.0);
      color += pbrLight(diffuseColour, f0, roughness, normal, viewDirection, direction) *
          light.color.rgb * light.color.w * (falloff * falloff) * ao;
    }
  } else {
    color = albedo * (skyIrradiance(normal) * ao + sunRadiance * sunNdl * shadow);

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
      let falloff = clamp(1.0 - distance / range, 0.0, 1.0);
      color += albedo * light.color.rgb * light.color.w * ndl * (falloff * falloff);
    }
  }

  color += emissive;
  color = applyFog(color, input.worldPosition);

  // xy = octahedral world normal, z = albedo luminance (weights the bounced
  // light in the screen-space GI), w = 1 for lit geometry.
  return FragmentOutput(vec4f(color, base.a),
      vec4f(octahedralEncode(normal), clamp(luminance(diffuseColour), 0.0, 1.0), 1.0));
}
