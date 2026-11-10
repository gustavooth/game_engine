module egn

// Small, dependency-free math types for game code.
//
// These are pure V types: they are layout-compatible with the engine's
// vec2/vec3/vec4 (three/four contiguous f32 values) and work even when the
// engine is not running.

import math

pub struct Vec2 {
pub mut:
	x f32
	y f32
}

pub struct Vec3 {
pub mut:
	x f32
	y f32
	z f32
}

pub struct Vec4 {
pub mut:
	x f32
	y f32
	z f32
	w f32
}

// Rect is an axis-aligned rectangle in window coordinates, in pixels.
pub struct Rect {
pub mut:
	x      int
	y      int
	width  int
	height int
}

pub fn vec2(x f32, y f32) Vec2 {
	return Vec2{x, y}
}

pub fn vec3(x f32, y f32, z f32) Vec3 {
	return Vec3{x, y, z}
}

pub fn vec4(x f32, y f32, z f32, w f32) Vec4 {
	return Vec4{x, y, z, w}
}

// Vec2 ----------------------------------------------------------------------

pub fn (a Vec2) add(b Vec2) Vec2 {
	return Vec2{a.x + b.x, a.y + b.y}
}

pub fn (a Vec2) sub(b Vec2) Vec2 {
	return Vec2{a.x - b.x, a.y - b.y}
}

pub fn (a Vec2) mul(b Vec2) Vec2 {
	return Vec2{a.x * b.x, a.y * b.y}
}

pub fn (a Vec2) scale(s f32) Vec2 {
	return Vec2{a.x * s, a.y * s}
}

pub fn (a Vec2) dot(b Vec2) f32 {
	return a.x * b.x + a.y * b.y
}

pub fn (a Vec2) length_squared() f32 {
	return a.dot(a)
}

pub fn (a Vec2) length() f32 {
	return f32(math.sqrt(f64(a.length_squared())))
}

pub fn (a Vec2) distance(b Vec2) f32 {
	return a.sub(b).length()
}

pub fn (a Vec2) normalized() Vec2 {
	l := a.length()
	if l == 0 {
		return Vec2{}
	}
	return a.scale(1 / l)
}

// Vec3 ----------------------------------------------------------------------

pub fn (a Vec3) add(b Vec3) Vec3 {
	return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}
}

pub fn (a Vec3) sub(b Vec3) Vec3 {
	return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}
}

pub fn (a Vec3) mul(b Vec3) Vec3 {
	return Vec3{a.x * b.x, a.y * b.y, a.z * b.z}
}

pub fn (a Vec3) scale(s f32) Vec3 {
	return Vec3{a.x * s, a.y * s, a.z * s}
}

pub fn (a Vec3) dot(b Vec3) f32 {
	return a.x * b.x + a.y * b.y + a.z * b.z
}

pub fn (a Vec3) cross(b Vec3) Vec3 {
	return Vec3{
		x: a.y * b.z - a.z * b.y
		y: a.z * b.x - a.x * b.z
		z: a.x * b.y - a.y * b.x
	}
}

pub fn (a Vec3) length_squared() f32 {
	return a.dot(a)
}

pub fn (a Vec3) length() f32 {
	return f32(math.sqrt(f64(a.length_squared())))
}

pub fn (a Vec3) distance(b Vec3) f32 {
	return a.sub(b).length()
}

pub fn (a Vec3) normalized() Vec3 {
	l := a.length()
	if l == 0 {
		return Vec3{}
	}
	return a.scale(1 / l)
}

pub fn (a Vec3) lerp(b Vec3, t f32) Vec3 {
	return a.add(b.sub(a).scale(t))
}

// Vec4 ----------------------------------------------------------------------

pub fn (a Vec4) add(b Vec4) Vec4 {
	return Vec4{a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}
}

pub fn (a Vec4) sub(b Vec4) Vec4 {
	return Vec4{a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}
}

pub fn (a Vec4) scale(s f32) Vec4 {
	return Vec4{a.x * s, a.y * s, a.z * s, a.w * s}
}

pub fn (a Vec4) dot(b Vec4) f32 {
	return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w
}

pub fn (a Vec4) length_squared() f32 {
	return a.dot(a)
}

pub fn (a Vec4) length() f32 {
	return f32(math.sqrt(f64(a.length_squared())))
}

pub fn (a Vec4) normalized() Vec4 {
	l := a.length()
	if l == 0 {
		return Vec4{}
	}
	return a.scale(1 / l)
}

// Quat ----------------------------------------------------------------------

// Quat is a rotation quaternion (x, y, z, w).
pub struct Quat {
pub mut:
	x f32
	y f32
	z f32
	w f32
}

pub fn quat(x f32, y f32, z f32, w f32) Quat {
	return Quat{
		x: x
		y: y
		z: z
		w: w
	}
}

pub fn quat_identity() Quat {
	return Quat{
		w: 1.0
	}
}

// quat_axis_angle builds a quaternion from an axis and an angle in radians.
pub fn quat_axis_angle(axis Vec3, radians f32) Quat {
	normalized_axis := axis.normalized()
	half := radians * 0.5
	s := f32(math.sin(f64(half)))
	c := f32(math.cos(f64(half)))
	return Quat{
		x: normalized_axis.x * s
		y: normalized_axis.y * s
		z: normalized_axis.z * s
		w: c
	}
}

// quat_from_euler builds a quaternion from Euler angles in radians (X * Y * Z).
pub fn quat_from_euler(euler Vec3) Quat {
	qx := quat_axis_angle(vec3(1, 0, 0), euler.x)
	qy := quat_axis_angle(vec3(0, 1, 0), euler.y)
	qz := quat_axis_angle(vec3(0, 0, 1), euler.z)
	return qx.mul(qy).mul(qz).normalized()
}

// quat_from_euler_deg builds a quaternion from Euler angles in degrees.
pub fn quat_from_euler_deg(euler_deg Vec3) Quat {
	return quat_from_euler(vec3(deg_to_rad(euler_deg.x), deg_to_rad(euler_deg.y), deg_to_rad(euler_deg.z)))
}

pub fn (a Quat) mul(b Quat) Quat {
	return Quat{
		x: a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y
		y: a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x
		z: a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w
		w: a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
	}
}

pub fn (q Quat) length() f32 {
	return f32(math.sqrt(f64(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w)))
}

pub fn (q Quat) normalized() Quat {
	l := q.length()
	if l == 0 {
		return quat_identity()
	}
	return Quat{
		x: q.x / l
		y: q.y / l
		z: q.z / l
		w: q.w / l
	}
}

pub fn (q Quat) conjugate() Quat {
	return Quat{
		x: -q.x
		y: -q.y
		z: -q.z
		w: q.w
	}
}

pub fn deg_to_rad(degrees f32) f32 {
	return degrees * f32(math.pi) / 180.0
}

pub fn rad_to_deg(radians f32) f32 {
	return radians * 180.0 / f32(math.pi)
}
