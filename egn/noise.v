module egn

// Seeded procedural noise.
//
// All functions are deterministic for a given seed and coordinate, so an
// island generator can reproduce a world from a single integer seed.

fn C.egn_perlin2(seed u32, x f32, y f32) f32
fn C.egn_perlin3(seed u32, x f32, y f32, z f32) f32
fn C.egn_fbm2(seed u32, x f32, y f32, octaves u32, lacunarity f32, gain f32) f32
fn C.egn_ridged2(seed u32, x f32, y f32, octaves u32, lacunarity f32, gain f32) f32
fn C.egn_lerp(a f32, b f32, t f32) f32
fn C.egn_smoothstep(edge0 f32, edge1 f32, x f32) f32

// perlin2 returns classic 2D Perlin gradient noise in roughly [-1, 1].
pub fn perlin2(seed u32, x f32, y f32) f32 {
	return C.egn_perlin2(seed, x, y)
}

// perlin3 returns classic 3D Perlin gradient noise in roughly [-1, 1].
pub fn perlin3(seed u32, x f32, y f32, z f32) f32 {
	return C.egn_perlin3(seed, x, y, z)
}

// fbm2 combines several Perlin octaves into fractal Brownian motion.
pub fn fbm2(seed u32, x f32, y f32, octaves u32, lacunarity f32, gain f32) f32 {
	return C.egn_fbm2(seed, x, y, octaves, lacunarity, gain)
}

// ridged2 combines inverted Perlin octaves into sharp mountain ridges.
pub fn ridged2(seed u32, x f32, y f32, octaves u32, lacunarity f32, gain f32) f32 {
	return C.egn_ridged2(seed, x, y, octaves, lacunarity, gain)
}

// lerp linearly interpolates between a and b.
pub fn lerp(a f32, b f32, t f32) f32 {
	return C.egn_lerp(a, b, t)
}

// smoothstep interpolates smoothly between two edges.
pub fn smoothstep(edge0 f32, edge1 f32, x f32) f32 {
	return C.egn_smoothstep(edge0, edge1, x)
}

fn C.egn_value2(seed u32, x f32, y f32) f32
fn C.egn_fbm_value2(seed u32, x f32, y f32, octaves u32, lacunarity f32, gain f32) f32
fn C.egn_worley2(seed u32, x f32, y f32) f32
fn C.egn_worley2_edge(seed u32, x f32, y f32) f32
fn C.egn_hash2(seed u32, x i32, y i32) u32
fn C.egn_random01(seed u32, x i32, y i32) f32
fn C.egn_warp2(seed u32, x f32, y f32, strength f32, out_x &f32, out_y &f32)

// value2 returns smoothly interpolated lattice value noise in [-1, 1].
pub fn value2(seed u32, x f32, y f32) f32 {
	return C.egn_value2(seed, x, y)
}

// fbm_value2 combines several value-noise octaves.
pub fn fbm_value2(seed u32, x f32, y f32, octaves u32, lacunarity f32, gain f32) f32 {
	return C.egn_fbm_value2(seed, x, y, octaves, lacunarity, gain)
}

// worley2 returns the cellular F1 distance in roughly [0, 1].
pub fn worley2(seed u32, x f32, y f32) f32 {
	return C.egn_worley2(seed, x, y)
}

// worley2_edge returns the F2-F1 cell border distance in roughly [0, 1].
pub fn worley2_edge(seed u32, x f32, y f32) f32 {
	return C.egn_worley2_edge(seed, x, y)
}

// hash2 is a deterministic integer hash for a seed and lattice point.
pub fn hash2(seed u32, x i32, y i32) u32 {
	return C.egn_hash2(seed, x, y)
}

// random01 returns a deterministic value in [0, 1) for a seed and point.
pub fn random01(seed u32, x i32, y i32) f32 {
	return C.egn_random01(seed, x, y)
}

// warp2 domain-warps a 2D coordinate by a low-frequency noise field.
pub fn warp2(seed u32, x f32, y f32, strength f32) (f32, f32) {
	mut wx := f32(0)
	mut wy := f32(0)
	C.egn_warp2(seed, x, y, strength, &wx, &wy)
	return wx, wy
}
