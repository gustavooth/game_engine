#pragma once

#include "defines.h"

// ---------------------------------------------------------------------------
// Seeded procedural noise
// ---------------------------------------------------------------------------
//
// These functions are deterministic for a given (seed, coordinate) pair and
// do not touch global state. They are safe to call from generation code and
// from multiple threads as long as the caller owns the coordinates.

// Classic 2D Perlin gradient noise in roughly [-1, 1].
EGNAPI f32 egn_perlin2(u32 seed, f32 x, f32 y);

// Classic 3D Perlin gradient noise in roughly [-1, 1].
EGNAPI f32 egn_perlin3(u32 seed, f32 x, f32 y, f32 z);

// Fractal Brownian motion built from perlin2. Returns roughly [-1, 1].
EGNAPI f32 egn_fbm2(u32 seed, f32 x, f32 y, u32 octaves,
                    f32 lacunarity, f32 gain);

// Ridged multifractal built from perlin2. Returns roughly [0, 1].
EGNAPI f32 egn_ridged2(u32 seed, f32 x, f32 y, u32 octaves,
                       f32 lacunarity, f32 gain);

// Classic value noise (smoothly interpolated lattice values) in [-1, 1].
EGNAPI f32 egn_value2(u32 seed, f32 x, f32 y);

// Fractal value noise in [-1, 1].
EGNAPI f32 egn_fbm_value2(u32 seed, f32 x, f32 y, u32 octaves,
                          f32 lacunarity, f32 gain);

// Cellular/Worley F1 distance in roughly [0, 1]. Good for rock, foam and
// archipelago masks.
EGNAPI f32 egn_worley2(u32 seed, f32 x, f32 y);

// Cell edge distance (F2 - F1) in roughly [0, 1]; near zero on cell borders.
EGNAPI f32 egn_worley2_edge(u32 seed, f32 x, f32 y);

// Deterministic integer hash and uniform random value for a lattice point.
EGNAPI u32 egn_hash2(u32 seed, i32 x, i32 y);
EGNAPI f32 egn_random01(u32 seed, i32 x, i32 y);

// Domain warping: offsets (x, y) by a low-frequency fbm field.
EGNAPI void egn_warp2(u32 seed, f32 x, f32 y, f32 strength,
                      f32 *out_x, f32 *out_y);

// Small helpers shared by procedural generators.
EGNAPI f32 egn_lerp(f32 a, f32 b, f32 t);
EGNAPI f32 egn_smoothstep(f32 edge0, f32 edge1, f32 x);
