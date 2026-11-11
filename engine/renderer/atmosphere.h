#pragma once

#include "defines.h"
#include "math/math.h"

// CPU evaluation of the same single-scattering atmosphere used by the sky
// shader. The renderer samples it a few times per frame to derive lighting
// that matches the visible sky: the colour of the sun after crossing the
// atmosphere, the sky/ground ambient and the fog colour.
typedef struct AtmosphereParams
{
  vec3 sun_direction; // normalized, pointing toward the sun
  vec3 sun_color;     // colour of the sun outside the atmosphere
  f32 rayleigh;       // Rayleigh density multiplier
  f32 mie;            // Mie density multiplier
  f32 turbidity;      // haze multiplier applied to Mie scattering
  f32 mie_anisotropy; // Henyey-Greenstein g
  f32 sun_intensity;  // radiance scale of the in-scattered light
  f32 altitude_km;    // observer altitude
} AtmosphereParams;

// Radiance arriving from `direction` (sun disc excluded).
vec3 atmosphere_sky_radiance(const AtmosphereParams *params, vec3 direction,
                             u32 steps, u32 light_steps);

// Fraction of the sunlight that reaches the observer, per channel.
vec3 atmosphere_sun_transmittance(const AtmosphereParams *params, u32 steps);
