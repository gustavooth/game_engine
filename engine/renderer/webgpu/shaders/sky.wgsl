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
};

@group(0) @binding(0) var<uniform> globals: Globals;

const PI = 3.14159265;
const PLANET_RADIUS = 6371.0;
const ATMOSPHERE_RADIUS = 6471.0;

fn raySphere(origin: vec3f, direction: vec3f, radius: f32) -> vec2f {
  let b = dot(origin, direction);
  let c = dot(origin, origin) - radius * radius;
  let discriminant = b * b - c;
  if (discriminant < 0.0) {
    return vec2f(-1.0);
  }
  let root = sqrt(discriminant);
  return vec2f(-b - root, -b + root);
}

fn atmosphere(rayDirection: vec3f) -> vec3f {
  let planetCenter = vec3f(0.0, -PLANET_RADIUS, 0.0);
  let origin = globals.cameraPosition.xyz - planetCenter;
  let atmosphereHit = raySphere(origin, rayDirection, ATMOSPHERE_RADIUS);
  if (atmosphereHit.y <= 0.0) {
    return vec3f(0.0);
  }

  let tStart = max(atmosphereHit.x, 0.0);
  let tEnd = atmosphereHit.y;
  let planetHit = raySphere(origin, rayDirection, PLANET_RADIUS);
  var end = tEnd;
  if (planetHit.x > 0.0 && planetHit.x < end) {
    end = planetHit.x;
  }

  let steps = i32(max(globals.atmosphere2.y, 2.0));
  let lightSteps = i32(max(globals.atmosphere2.z, 1.0));
  let stepSize = (end - tStart) / f32(steps);

  let betaRayleigh = vec3f(0.0058, 0.0135, 0.0331) * globals.atmosphere.x;
  let betaMie = 0.021 * globals.atmosphere.y;

  let mu = dot(rayDirection, globals.sun.direction.xyz);
  let phaseRayleigh = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
  let g = globals.atmosphere.w;
  let phaseMie = 3.0 / (8.0 * PI) *
      ((1.0 - g * g) * (1.0 + mu * mu)) /
      ((2.0 + g * g) * pow(max(1.0 + g * g - 2.0 * g * mu, 0.001), 1.5));

  var accumulatedRayleigh = vec3f(0.0);
  var accumulatedMie = vec3f(0.0);
  var opticalRayleigh = 0.0;
  var opticalMie = 0.0;

  for (var i = 0; i < 32; i = i + 1) {
    if (i >= steps) {
      break;
    }
    let distanceAlongRay = tStart + (f32(i) + 0.5) * stepSize;
    let position = origin + rayDirection * distanceAlongRay;
    let height = max(length(position) - PLANET_RADIUS, 0.0);
    let density = exp(-height / 8.0);
    opticalRayleigh += density * stepSize;
    opticalMie += density * stepSize;

    var lightOpticalRayleigh = 0.0;
    var lightOpticalMie = 0.0;
    let lightStep = 12.0 / f32(lightSteps);
    for (var j = 0; j < 8; j = j + 1) {
      if (j >= lightSteps) {
        break;
      }
      let lightPosition = position + globals.sun.direction.xyz * (f32(j) + 0.5) * lightStep;
      let lightHeight = max(length(lightPosition) - PLANET_RADIUS, 0.0);
      let lightDensity = exp(-lightHeight / 8.0);
      lightOpticalRayleigh += lightDensity * lightStep;
      lightOpticalMie += lightDensity * lightStep;
    }

    let transmittance = exp(-(betaRayleigh * (opticalRayleigh + lightOpticalRayleigh) +
                              betaMie * 1.1 * (opticalMie + lightOpticalMie)));
    accumulatedRayleigh += density * transmittance * stepSize;
    accumulatedMie += density * transmittance * stepSize;
  }

  let scattering = accumulatedRayleigh * betaRayleigh * phaseRayleigh +
                   accumulatedMie * betaMie * phaseMie;
  var colour = scattering * globals.sun.color.rgb * globals.atmosphere2.x;
  colour *= mix(0.35, 1.0, max(globals.sun.direction.y, 0.0) * 0.5 + 0.5);

  let sunDot = max(dot(rayDirection, globals.sun.direction.xyz), 0.0);
  colour += globals.sun.color.rgb * pow(sunDot, 900.0) * globals.atmosphere2.x * 2.0;
  colour += globals.sun.color.rgb * pow(sunDot, 12.0) * 0.04 *
            globals.atmosphere.y * 400.0;

  let day = clamp(globals.sun.direction.y * 3.0 + 0.35, 0.0, 1.0);
  let haze = vec3f(0.20, 0.28, 0.36) * (0.35 + 0.65 * day) +
             globals.sun.color.rgb * 0.05;
  let horizon = smoothstep(-0.16, 0.04, rayDirection.y);
  return mix(haze, colour, horizon);
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

struct FragmentOutput {
  @location(0) colour: vec4f,
  @location(1) normal: vec4f,
};

@fragment
fn fs_main(input: VertexOutput) -> FragmentOutput {
  let ndc = input.uv * 2.0 - 1.0;
  let nearPoint = globals.inverseViewProjection * vec4f(ndc, 0.0, 1.0);
  let farPoint = globals.inverseViewProjection * vec4f(ndc, 1.0, 1.0);
  let rayDirection = normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);
  return FragmentOutput(vec4f(atmosphere(rayDirection), 1.0), vec4f(0.0));
}
