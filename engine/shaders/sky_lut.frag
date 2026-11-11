#version 450

// Sky-view LUT: single-scattering atmosphere for every direction of the upper
// hemisphere. u = azimuth, v = sqrt(elevation) so the horizon, where the sky
// changes fastest, gets most of the texels.
//
// Keep the integration in sync with engine/renderer/atmosphere.c, which
// evaluates the same model on the CPU to derive the scene lighting.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_colour;

layout(push_constant) uniform SkyLutConstants
{
  vec4 sun_direction; // xyz = toward the sun, w = observer altitude (km)
  vec4 sun_radiance;  // rgb = sun radiance outside the atmosphere
  vec4 atmosphere;    // rayleigh, mie, turbidity, mie anisotropy
  vec4 params;        // sun intensity, steps, light steps, unused
} pc;

const float PI = 3.14159265;
const float PLANET_RADIUS = 6371.0;
const float ATMOSPHERE_RADIUS = 6471.0;
const float RAYLEIGH_HEIGHT = 8.0;
const float MIE_HEIGHT = 1.2;

vec2 ray_sphere(vec3 origin, vec3 direction, float radius)
{
  float b = dot(origin, direction);
  float c = dot(origin, origin) - radius * radius;
  float discriminant = b * b - c;
  if (discriminant < 0.0)
    return vec2(-1.0);
  float root = sqrt(discriminant);
  return vec2(-b - root, -b + root);
}

// Optical depth (x = Rayleigh, y = Mie) toward the sun; z = 0 when the planet
// blocks the sun.
vec3 light_optical_depth(vec3 position, vec3 sun, int steps)
{
  vec2 hit = ray_sphere(position, sun, ATMOSPHERE_RADIUS);
  if (hit.y <= 0.0)
    return vec3(0.0, 0.0, 1.0);
  vec2 planet = ray_sphere(position, sun, PLANET_RADIUS - 1.0);
  if (planet.x > 0.0)
    return vec3(0.0);

  vec2 depth = vec2(0.0);
  float previous = 0.0;
  for (int i = 0; i < 8; i++)
  {
    if (i >= steps)
      break;
    float u = float(i + 1) / float(steps);
    float next = hit.y * u * u;
    float mid = 0.5 * (previous + next);
    float height = max(length(position + sun * mid) - PLANET_RADIUS, 0.0);
    depth += exp(-height / vec2(RAYLEIGH_HEIGHT, MIE_HEIGHT)) * (next - previous);
    previous = next;
  }
  return vec3(depth, 1.0);
}

void main()
{
  // Single scattering has no light scattered back into very long paths, so
  // the last degrees above the horizon would turn dark and yellow. Clamping
  // the elevation keeps the bright, pale horizon of a real clear sky.
  float elevation = max(v_uv.y * v_uv.y * (0.5 * PI), 0.07);
  float azimuth = (v_uv.x - 0.5) * (2.0 * PI);
  vec3 direction = vec3(cos(elevation) * cos(azimuth), sin(elevation),
                        cos(elevation) * sin(azimuth));
  vec3 sun = normalize(pc.sun_direction.xyz);

  vec3 origin = vec3(0.0, PLANET_RADIUS + 0.002 + max(pc.sun_direction.w, 0.0), 0.0);
  vec2 hit = ray_sphere(origin, direction, ATMOSPHERE_RADIUS);
  if (hit.y <= 0.0)
  {
    out_colour = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }
  float t_start = max(hit.x, 0.0);
  float t_end = hit.y;
  vec2 planet = ray_sphere(origin, direction, PLANET_RADIUS);
  if (planet.x > 0.0 && planet.x < t_end)
    t_end = planet.x;

  int steps = int(clamp(pc.params.y, 2.0, 32.0));
  int light_steps = int(clamp(pc.params.z, 1.0, 8.0));

  vec3 beta_rayleigh = vec3(0.0058, 0.0135, 0.0331) * pc.atmosphere.x;
  float beta_mie = 0.021 * pc.atmosphere.y * max(pc.atmosphere.z, 0.0001);

  float mu = dot(direction, sun);
  float phase_rayleigh = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
  float g = pc.atmosphere.w;
  float phase_mie = 3.0 / (8.0 * PI) * ((1.0 - g * g) * (1.0 + mu * mu)) /
                    ((2.0 + g * g) * pow(max(1.0 + g * g - 2.0 * g * mu, 0.001), 1.5));

  vec3 sum_rayleigh = vec3(0.0);
  vec3 sum_mie = vec3(0.0);
  vec2 optical = vec2(0.0);
  float previous = t_start;
  float span = t_end - t_start;

  for (int i = 0; i < 32; i++)
  {
    if (i >= steps)
      break;
    // Quadratic distribution: short steps close to the camera, where the
    // air is dense, long steps toward the top of the atmosphere.
    float u = float(i + 1) / float(steps);
    float next = t_start + span * u * u;
    float mid = 0.5 * (previous + next);
    float segment = next - previous;
    previous = next;

    vec3 position = origin + direction * mid;
    float height = max(length(position) - PLANET_RADIUS, 0.0);
    vec2 density = exp(-height / vec2(RAYLEIGH_HEIGHT, MIE_HEIGHT)) * segment;
    optical += density;

    vec3 light = light_optical_depth(position, sun, light_steps);
    if (light.z < 0.5)
      continue;

    vec3 transmittance = exp(-(beta_rayleigh * (optical.x + light.x) +
                               beta_mie * 1.1 * (optical.y + light.y)));
    sum_rayleigh += density.x * transmittance;
    sum_mie += density.y * transmittance;
  }

  vec3 scattering = sum_rayleigh * beta_rayleigh * phase_rayleigh +
                    sum_mie * beta_mie * phase_mie;
  out_colour = vec4(scattering * pc.sun_radiance.rgb * pc.params.x, 1.0);
}
