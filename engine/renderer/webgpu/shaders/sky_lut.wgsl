// Sky-view LUT: single-scattering atmosphere for every direction of the upper
// hemisphere. u = azimuth, v = sqrt(elevation) so the horizon, where the sky
// changes fastest, gets most of the texels. The sky pass and the PBR
// reflections sample it, so the scattering integral runs on 32k texels
// instead of on every screen pixel.
//
// Keep in sync with engine/shaders/sky_lut.frag and the JS/C CPU evaluation.

struct SkyLutConstants {
  sunDirection: vec4f, // xyz = toward the sun, w = observer altitude (km)
  sunRadiance: vec4f,  // rgb = sun radiance outside the atmosphere
  atmosphere: vec4f,   // rayleigh, mie, turbidity, mie anisotropy
  params: vec4f,       // sun intensity, steps, light steps, unused
};

@group(0) @binding(0) var<uniform> pc: SkyLutConstants;

const PI = 3.14159265;
const PLANET_RADIUS = 6371.0;
const ATMOSPHERE_RADIUS = 6471.0;
const SCALE_HEIGHTS = vec2f(8.0, 1.2); // Rayleigh, Mie

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

// Optical depth (x = Rayleigh, y = Mie) toward the sun; z = 0 when the planet
// blocks the sun.
fn lightOpticalDepth(position: vec3f, sun: vec3f, steps: i32) -> vec3f {
  let hit = raySphere(position, sun, ATMOSPHERE_RADIUS);
  if (hit.y <= 0.0) {
    return vec3f(0.0, 0.0, 1.0);
  }
  let planet = raySphere(position, sun, PLANET_RADIUS - 1.0);
  if (planet.x > 0.0) {
    return vec3f(0.0);
  }
  var depth = vec2f(0.0);
  var previous = 0.0;
  for (var i = 0; i < 8; i = i + 1) {
    if (i >= steps) {
      break;
    }
    let u = f32(i + 1) / f32(steps);
    let next = hit.y * u * u;
    let mid = 0.5 * (previous + next);
    let height = max(length(position + sun * mid) - PLANET_RADIUS, 0.0);
    depth += exp(-vec2f(height) / SCALE_HEIGHTS) * (next - previous);
    previous = next;
  }
  return vec3f(depth, 1.0);
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
  // Single scattering has no light scattered back into very long paths, so
  // the last degrees above the horizon would turn dark and yellow. Clamping
  // the elevation keeps the bright, pale horizon of a real clear sky.
  let elevation = max(uv.y * uv.y * (0.5 * PI), 0.07);
  let azimuth = (uv.x - 0.5) * (2.0 * PI);
  let direction = vec3f(cos(elevation) * cos(azimuth), sin(elevation),
      cos(elevation) * sin(azimuth));
  let sun = normalize(pc.sunDirection.xyz);

  let origin = vec3f(0.0, PLANET_RADIUS + 0.002 + max(pc.sunDirection.w, 0.0), 0.0);
  let hit = raySphere(origin, direction, ATMOSPHERE_RADIUS);
  if (hit.y <= 0.0) {
    return vec4f(0.0, 0.0, 0.0, 1.0);
  }
  let tStart = max(hit.x, 0.0);
  var tEnd = hit.y;
  let planet = raySphere(origin, direction, PLANET_RADIUS);
  if (planet.x > 0.0 && planet.x < tEnd) {
    tEnd = planet.x;
  }

  let steps = i32(clamp(pc.params.y, 2.0, 32.0));
  let lightSteps = i32(clamp(pc.params.z, 1.0, 8.0));

  let betaRayleigh = vec3f(0.0058, 0.0135, 0.0331) * pc.atmosphere.x;
  let betaMie = 0.021 * pc.atmosphere.y * max(pc.atmosphere.z, 0.0001);

  let mu = dot(direction, sun);
  let phaseRayleigh = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
  let g = pc.atmosphere.w;
  let phaseMie = 3.0 / (8.0 * PI) * ((1.0 - g * g) * (1.0 + mu * mu)) /
      ((2.0 + g * g) * pow(max(1.0 + g * g - 2.0 * g * mu, 0.001), 1.5));

  var sumRayleigh = vec3f(0.0);
  var sumMie = vec3f(0.0);
  var optical = vec2f(0.0);
  var previous = tStart;
  let span = tEnd - tStart;

  for (var i = 0; i < 32; i = i + 1) {
    if (i >= steps) {
      break;
    }
    // Quadratic distribution: short steps close to the camera, where the
    // air is dense, long steps toward the top of the atmosphere.
    let u = f32(i + 1) / f32(steps);
    let next = tStart + span * u * u;
    let mid = 0.5 * (previous + next);
    let segment = next - previous;
    previous = next;

    let position = origin + direction * mid;
    let height = max(length(position) - PLANET_RADIUS, 0.0);
    let density = exp(-vec2f(height) / SCALE_HEIGHTS) * segment;
    optical += density;

    let light = lightOpticalDepth(position, sun, lightSteps);
    if (light.z < 0.5) {
      continue;
    }
    let transmittance = exp(-(betaRayleigh * (optical.x + light.x) +
        vec3f(betaMie * 1.1 * (optical.y + light.y))));
    sumRayleigh += density.x * transmittance;
    sumMie += density.y * transmittance;
  }

  let scattering = sumRayleigh * betaRayleigh * phaseRayleigh + sumMie * betaMie * phaseMie;
  return vec4f(scattering * pc.sunRadiance.rgb * pc.params.x, 1.0);
}
