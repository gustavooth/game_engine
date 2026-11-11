#include "renderer/atmosphere.h"

#include <math.h>

// Keep these constants and the integration below in sync with
// engine/shaders/sky.frag.
#define ATMOSPHERE_PLANET_RADIUS 6371.0f
#define ATMOSPHERE_RADIUS 6471.0f
#define ATMOSPHERE_RAYLEIGH_HEIGHT 8.0f
#define ATMOSPHERE_MIE_HEIGHT 1.2f
#define ATMOSPHERE_PI 3.14159265f

static const f32 beta_rayleigh_base[3] = {0.0058f, 0.0135f, 0.0331f};

static b8 ray_sphere(vec3 origin, vec3 direction, f32 radius, f32 *out_near, f32 *out_far)
{
  f32 b = vec3_dot(origin, direction);
  f32 c = vec3_dot(origin, origin) - radius * radius;
  f32 discriminant = b * b - c;
  if (discriminant < 0.0f)
    return false;
  f32 root = sqrtf(discriminant);
  *out_near = -b - root;
  *out_far = -b + root;
  return true;
}

static f32 mie_coefficient(const AtmosphereParams *params)
{
  return 0.021f * params->mie * (params->turbidity > 0.0f ? params->turbidity : 1.0f);
}

// Optical depth (Rayleigh, Mie) from `position` toward the sun until the ray
// leaves the atmosphere. Returns false when the planet blocks the sun.
static b8 light_optical_depth(vec3 position, vec3 sun_direction, u32 steps,
                              f32 *out_rayleigh, f32 *out_mie)
{
  f32 t_near, t_far;
  *out_rayleigh = 0.0f;
  *out_mie = 0.0f;
  if (!ray_sphere(position, sun_direction, ATMOSPHERE_RADIUS, &t_near, &t_far) || t_far <= 0.0f)
    return true;

  f32 planet_near, planet_far;
  if (ray_sphere(position, sun_direction, ATMOSPHERE_PLANET_RADIUS - 1.0f, &planet_near, &planet_far) &&
      planet_near > 0.0f)
    return false;

  // Quadratic step distribution: most of the density sits near the start.
  f32 previous = 0.0f;
  for (u32 i = 0; i < steps; i++)
  {
    f32 u = (f32)(i + 1) / (f32)steps;
    f32 next = t_far * u * u;
    f32 mid = 0.5f * (previous + next);
    f32 length = next - previous;
    vec3 sample = vec3_add(position, vec3_mul_scalar(sun_direction, mid));
    f32 height = vec3_length(sample) - ATMOSPHERE_PLANET_RADIUS;
    if (height < 0.0f)
      height = 0.0f;
    *out_rayleigh += expf(-height / ATMOSPHERE_RAYLEIGH_HEIGHT) * length;
    *out_mie += expf(-height / ATMOSPHERE_MIE_HEIGHT) * length;
    previous = next;
  }
  return true;
}

vec3 atmosphere_sky_radiance(const AtmosphereParams *params, vec3 direction,
                             u32 steps, u32 light_steps)
{
  if (!params || steps == 0 || light_steps == 0)
    return vec3_zero();

  vec3 origin = vec3_create(0.0f, ATMOSPHERE_PLANET_RADIUS + 0.002f +
                                      (params->altitude_km > 0.0f ? params->altitude_km : 0.0f),
                            0.0f);

  f32 t_near, t_far;
  if (!ray_sphere(origin, direction, ATMOSPHERE_RADIUS, &t_near, &t_far) || t_far <= 0.0f)
    return vec3_zero();
  f32 t_start = t_near > 0.0f ? t_near : 0.0f;
  f32 t_end = t_far;

  f32 planet_near, planet_far;
  if (ray_sphere(origin, direction, ATMOSPHERE_PLANET_RADIUS, &planet_near, &planet_far) &&
      planet_near > 0.0f && planet_near < t_end)
    t_end = planet_near;

  f32 beta_r[3];
  for (u32 c = 0; c < 3; c++)
    beta_r[c] = beta_rayleigh_base[c] * params->rayleigh;
  f32 beta_m = mie_coefficient(params);

  f32 mu = vec3_dot(direction, params->sun_direction);
  f32 phase_r = 3.0f / (16.0f * ATMOSPHERE_PI) * (1.0f + mu * mu);
  f32 g = params->mie_anisotropy;
  f32 denominator = 1.0f + g * g - 2.0f * g * mu;
  if (denominator < 0.001f)
    denominator = 0.001f;
  f32 phase_m = 3.0f / (8.0f * ATMOSPHERE_PI) * ((1.0f - g * g) * (1.0f + mu * mu)) /
                ((2.0f + g * g) * powf(denominator, 1.5f));

  f32 sum_r[3] = {0.0f, 0.0f, 0.0f};
  f32 sum_m[3] = {0.0f, 0.0f, 0.0f};
  f32 optical_r = 0.0f;
  f32 optical_m = 0.0f;

  f32 previous = t_start;
  f32 span = t_end - t_start;
  for (u32 i = 0; i < steps; i++)
  {
    f32 u = (f32)(i + 1) / (f32)steps;
    f32 next = t_start + span * u * u;
    f32 mid = 0.5f * (previous + next);
    f32 length = next - previous;
    previous = next;

    vec3 position = vec3_add(origin, vec3_mul_scalar(direction, mid));
    f32 height = vec3_length(position) - ATMOSPHERE_PLANET_RADIUS;
    if (height < 0.0f)
      height = 0.0f;
    f32 density_r = expf(-height / ATMOSPHERE_RAYLEIGH_HEIGHT) * length;
    f32 density_m = expf(-height / ATMOSPHERE_MIE_HEIGHT) * length;
    optical_r += density_r;
    optical_m += density_m;

    f32 light_r, light_m;
    if (!light_optical_depth(position, params->sun_direction, light_steps, &light_r, &light_m))
      continue;

    for (u32 c = 0; c < 3; c++)
    {
      f32 transmittance = expf(-(beta_r[c] * (optical_r + light_r) +
                                 beta_m * 1.1f * (optical_m + light_m)));
      sum_r[c] += density_r * transmittance;
      sum_m[c] += density_m * transmittance;
    }
  }

  f32 sun[3] = {params->sun_color.x, params->sun_color.y, params->sun_color.z};
  f32 out[3];
  for (u32 c = 0; c < 3; c++)
    out[c] = (sum_r[c] * beta_r[c] * phase_r + sum_m[c] * beta_m * phase_m) *
             sun[c] * params->sun_intensity;
  return vec3_create(out[0], out[1], out[2]);
}

vec3 atmosphere_sun_transmittance(const AtmosphereParams *params, u32 steps)
{
  if (!params || steps == 0)
    return vec3_one();

  vec3 origin = vec3_create(0.0f, ATMOSPHERE_PLANET_RADIUS + 0.002f +
                                      (params->altitude_km > 0.0f ? params->altitude_km : 0.0f),
                            0.0f);
  f32 optical_r, optical_m;
  if (!light_optical_depth(origin, params->sun_direction, steps, &optical_r, &optical_m))
    return vec3_zero();

  f32 beta_m = mie_coefficient(params);
  return vec3_create(
      expf(-(beta_rayleigh_base[0] * params->rayleigh * optical_r + beta_m * 1.1f * optical_m)),
      expf(-(beta_rayleigh_base[1] * params->rayleigh * optical_r + beta_m * 1.1f * optical_m)),
      expf(-(beta_rayleigh_base[2] * params->rayleigh * optical_r + beta_m * 1.1f * optical_m)));
}
