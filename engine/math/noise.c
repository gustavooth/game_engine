#include "noise.h"

#include <math.h>

// The hash is a small integer mixer (based on the well-known "lowbias32"
// family). It gives each lattice point a stable pseudo-random gradient.
static u32 noise_hash_u32(u32 value)
{
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  value ^= value >> 16;
  return value;
}

static u32 noise_hash2(u32 seed, i32 x, i32 y)
{
  u32 hx = noise_hash_u32((u32)x + 0x9e3779b9u);
  u32 hy = noise_hash_u32((u32)y + 0x85ebca6bu);
  return noise_hash_u32(seed ^ hx ^ hy);
}

static u32 noise_hash3(u32 seed, i32 x, i32 y, i32 z)
{
  u32 hx = noise_hash_u32((u32)x + 0x9e3779b9u);
  u32 hy = noise_hash_u32((u32)y + 0x85ebca6bu);
  u32 hz = noise_hash_u32((u32)z + 0xc2b2ae35u);
  return noise_hash_u32(seed ^ hx ^ hy ^ hz);
}

static f32 noise_fade(f32 t)
{
  return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static f32 noise_grad2(u32 hash, f32 x, f32 y)
{
  switch (hash & 7u)
  {
  case 0: return x + y;
  case 1: return -x + y;
  case 2: return x - y;
  case 3: return -x - y;
  case 4: return x;
  case 5: return -x;
  case 6: return y;
  default: return -y;
  }
}

static f32 noise_grad3(u32 hash, f32 x, f32 y, f32 z)
{
  switch (hash % 12u)
  {
  case 0: return x + y;
  case 1: return -x + y;
  case 2: return x - y;
  case 3: return -x - y;
  case 4: return x + z;
  case 5: return -x + z;
  case 6: return x - z;
  case 7: return -x - z;
  case 8: return y + z;
  case 9: return -y + z;
  case 10: return y - z;
  default: return -y - z;
  }
}

static f32 noise_clamp(f32 value, f32 min_value, f32 max_value)
{
  if (value < min_value)
    return min_value;
  if (value > max_value)
    return max_value;
  return value;
}

EGNAPI f32 egn_lerp(f32 a, f32 b, f32 t)
{
  return a + (b - a) * t;
}

EGNAPI f32 egn_smoothstep(f32 edge0, f32 edge1, f32 x)
{
  if (edge0 == edge1)
    return x < edge0 ? 0.0f : 1.0f;

  f32 t = (x - edge0) / (edge1 - edge0);
  t = noise_clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

EGNAPI f32 egn_perlin2(u32 seed, f32 x, f32 y)
{
  f32 floor_x = floorf(x);
  f32 floor_y = floorf(y);
  i32 xi = (i32)floor_x;
  i32 yi = (i32)floor_y;

  f32 xf = x - floor_x;
  f32 yf = y - floor_y;

  f32 u = noise_fade(xf);
  f32 v = noise_fade(yf);

  f32 n00 = noise_grad2(noise_hash2(seed, xi, yi), xf, yf);
  f32 n10 = noise_grad2(noise_hash2(seed, xi + 1, yi), xf - 1.0f, yf);
  f32 n01 = noise_grad2(noise_hash2(seed, xi, yi + 1), xf, yf - 1.0f);
  f32 n11 = noise_grad2(noise_hash2(seed, xi + 1, yi + 1), xf - 1.0f, yf - 1.0f);

  f32 nx0 = egn_lerp(n00, n10, u);
  f32 nx1 = egn_lerp(n01, n11, u);
  return noise_clamp(egn_lerp(nx0, nx1, v) * 1.4f, -1.0f, 1.0f);
}

EGNAPI f32 egn_perlin3(u32 seed, f32 x, f32 y, f32 z)
{
  f32 floor_x = floorf(x);
  f32 floor_y = floorf(y);
  f32 floor_z = floorf(z);
  i32 xi = (i32)floor_x;
  i32 yi = (i32)floor_y;
  i32 zi = (i32)floor_z;

  f32 xf = x - floor_x;
  f32 yf = y - floor_y;
  f32 zf = z - floor_z;

  f32 u = noise_fade(xf);
  f32 v = noise_fade(yf);
  f32 w = noise_fade(zf);

  f32 n000 = noise_grad3(noise_hash3(seed, xi, yi, zi), xf, yf, zf);
  f32 n100 = noise_grad3(noise_hash3(seed, xi + 1, yi, zi), xf - 1.0f, yf, zf);
  f32 n010 = noise_grad3(noise_hash3(seed, xi, yi + 1, zi), xf, yf - 1.0f, zf);
  f32 n110 = noise_grad3(noise_hash3(seed, xi + 1, yi + 1, zi), xf - 1.0f, yf - 1.0f, zf);
  f32 n001 = noise_grad3(noise_hash3(seed, xi, yi, zi + 1), xf, yf, zf - 1.0f);
  f32 n101 = noise_grad3(noise_hash3(seed, xi + 1, yi, zi + 1), xf - 1.0f, yf, zf - 1.0f);
  f32 n011 = noise_grad3(noise_hash3(seed, xi, yi + 1, zi + 1), xf, yf - 1.0f, zf - 1.0f);
  f32 n111 = noise_grad3(noise_hash3(seed, xi + 1, yi + 1, zi + 1), xf - 1.0f, yf - 1.0f, zf - 1.0f);

  f32 nx00 = egn_lerp(n000, n100, u);
  f32 nx10 = egn_lerp(n010, n110, u);
  f32 nx01 = egn_lerp(n001, n101, u);
  f32 nx11 = egn_lerp(n011, n111, u);

  f32 nxy0 = egn_lerp(nx00, nx10, v);
  f32 nxy1 = egn_lerp(nx01, nx11, v);

  return noise_clamp(egn_lerp(nxy0, nxy1, w) * 1.25f, -1.0f, 1.0f);
}

EGNAPI f32 egn_fbm2(u32 seed, f32 x, f32 y, u32 octaves,
                    f32 lacunarity, f32 gain)
{
  if (octaves == 0)
    return 0.0f;

  f32 sum = 0.0f;
  f32 amplitude = 1.0f;
  f32 frequency = 1.0f;
  f32 norm = 0.0f;

  for (u32 i = 0; i < octaves; i++)
  {
    sum += egn_perlin2(seed + i * 1013u, x * frequency, y * frequency) * amplitude;
    norm += amplitude;
    amplitude *= gain;
    frequency *= lacunarity;
  }

  if (norm <= 0.0f)
    return 0.0f;

  return noise_clamp(sum / norm, -1.0f, 1.0f);
}

EGNAPI f32 egn_ridged2(u32 seed, f32 x, f32 y, u32 octaves,
                       f32 lacunarity, f32 gain)
{
  if (octaves == 0)
    return 0.0f;

  f32 sum = 0.0f;
  f32 amplitude = 1.0f;
  f32 frequency = 1.0f;
  f32 norm = 0.0f;

  for (u32 i = 0; i < octaves; i++)
  {
    f32 n = 1.0f - fabsf(egn_perlin2(seed + i * 1013u, x * frequency, y * frequency));
    n *= n;
    sum += n * amplitude;
    norm += amplitude;
    amplitude *= gain;
    frequency *= lacunarity;
  }

  if (norm <= 0.0f)
    return 0.0f;

  return noise_clamp(sum / norm, 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Extended procedural helpers
// ---------------------------------------------------------------------------

EGNAPI u32 egn_hash2(u32 seed, i32 x, i32 y)
{
  return noise_hash2(seed, x, y);
}

EGNAPI f32 egn_random01(u32 seed, i32 x, i32 y)
{
  // 24 bits of mantissa precision is plenty for terrain generation.
  return (f32)(noise_hash2(seed, x, y) >> 8) * (1.0f / 16777216.0f);
}

EGNAPI f32 egn_value2(u32 seed, f32 x, f32 y)
{
  f32 floor_x = floorf(x);
  f32 floor_y = floorf(y);
  i32 xi = (i32)floor_x;
  i32 yi = (i32)floor_y;

  f32 xf = x - floor_x;
  f32 yf = y - floor_y;
  f32 u = noise_fade(xf);
  f32 v = noise_fade(yf);

  f32 n00 = egn_random01(seed, xi, yi) * 2.0f - 1.0f;
  f32 n10 = egn_random01(seed, xi + 1, yi) * 2.0f - 1.0f;
  f32 n01 = egn_random01(seed, xi, yi + 1) * 2.0f - 1.0f;
  f32 n11 = egn_random01(seed, xi + 1, yi + 1) * 2.0f - 1.0f;

  f32 nx0 = egn_lerp(n00, n10, u);
  f32 nx1 = egn_lerp(n01, n11, u);
  return noise_clamp(egn_lerp(nx0, nx1, v), -1.0f, 1.0f);
}

EGNAPI f32 egn_fbm_value2(u32 seed, f32 x, f32 y, u32 octaves,
                          f32 lacunarity, f32 gain)
{
  if (octaves == 0)
    return 0.0f;

  f32 sum = 0.0f;
  f32 amplitude = 1.0f;
  f32 frequency = 1.0f;
  f32 norm = 0.0f;

  for (u32 i = 0; i < octaves; i++)
  {
    sum += egn_value2(seed + i * 1013u, x * frequency, y * frequency) * amplitude;
    norm += amplitude;
    amplitude *= gain;
    frequency *= lacunarity;
  }

  if (norm <= 0.0f)
    return 0.0f;

  return noise_clamp(sum / norm, -1.0f, 1.0f);
}

static void noise_worley_sample(u32 seed, i32 cell_x, i32 cell_y,
                                f32 x, f32 y, f32 *out_f1, f32 *out_f2)
{
  f32 fx = (f32)cell_x + egn_random01(seed, cell_x, cell_y);
  f32 fy = (f32)cell_y + egn_random01(seed + 0x9e3779b9u, cell_x, cell_y);

  f32 dx = fx - x;
  f32 dy = fy - y;
  f32 distance = sqrtf(dx * dx + dy * dy);

  if (distance < *out_f1)
  {
    *out_f2 = *out_f1;
    *out_f1 = distance;
  }
  else if (distance < *out_f2)
  {
    *out_f2 = distance;
  }
}

EGNAPI f32 egn_worley2(u32 seed, f32 x, f32 y)
{
  i32 xi = (i32)floorf(x);
  i32 yi = (i32)floorf(y);

  f32 f1 = 8.0f;
  f32 f2 = 8.0f;

  for (i32 dy = -1; dy <= 1; dy++)
    for (i32 dx = -1; dx <= 1; dx++)
      noise_worley_sample(seed, xi + dx, yi + dy, x, y, &f1, &f2);

  return noise_clamp(f1 * 0.55f, 0.0f, 1.0f);
}

EGNAPI f32 egn_worley2_edge(u32 seed, f32 x, f32 y)
{
  i32 xi = (i32)floorf(x);
  i32 yi = (i32)floorf(y);

  f32 f1 = 8.0f;
  f32 f2 = 8.0f;

  for (i32 dy = -1; dy <= 1; dy++)
    for (i32 dx = -1; dx <= 1; dx++)
      noise_worley_sample(seed, xi + dx, yi + dy, x, y, &f1, &f2);

  return noise_clamp((f2 - f1) * 1.5f, 0.0f, 1.0f);
}

EGNAPI void egn_warp2(u32 seed, f32 x, f32 y, f32 strength,
                      f32 *out_x, f32 *out_y)
{
  f32 wx = egn_fbm2(seed + 101u, x * 0.5f, y * 0.5f, 3u, 2.0f, 0.5f);
  f32 wy = egn_fbm2(seed + 211u, x * 0.5f + 5.2f, y * 0.5f - 1.7f, 3u, 2.0f, 0.5f);

  if (out_x)
    *out_x = x + wx * strength;
  if (out_y)
    *out_y = y + wy * strength;
}
